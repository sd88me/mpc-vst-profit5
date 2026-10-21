/* Profit-5 for mpc-vst-plugins' VST2 wrapper (wrapper/engine.h): 44100 Hz, interleaved int16 stereo, string parameters.
 *
 * Keys and ranges are src/plugin_keys.h (tools/gen_params.py); each maps linearly onto the engine's 0..1. "preset" loads a
 * factory program (MPC's PRESET menu lists them through vst.json "programs"), "user" loads an imported Repro-5 .h2p patch from
 * <plugin dir>/banks (src/p5_import.c; scanned once at create), "state" saves and restores every value, and
 * "display_rev" tells the wrapper (HAS_DISPLAY_REV) when the engine changed values by itself: a program load or MIDI.
 * No I/O, no allocation after create; nothing here blocks (mpc-vst-plugins PORTING.md, wrapper limits). */
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "engine.h"      /* mpc-vst-plugins wrapper/engine.h (mpc_engine_t) */
#include "p5_engine.h"
#include "p5_import.h"
#include "plugin_keys.h"

#define FS 44100
#define BLOCK 128
#define P5P_COUNT_V1 58           /* values in a state saved before 0.2.0 */
#define MAX_USER 128               /* imported patches: the first MAX_USER found in <plugin dir>/banks, sorted by file name */
#define MAX_H2P (256u << 10)       /* one patch file; a bank zip may be up to MAX_ZIP */
#define MAX_ZIP (32u << 20)

typedef struct {
    p5_engine_t e;
    int preset;
    int user;                      /* 0: a factory program (or an edit); n: imported patch n (1-based) */
    int nuser;
    p5_import_t *imp;              /* MAX_USER, filled once at create */
    unsigned display_rev;
    unsigned char sustain, deferred[128];
    float buf[BLOCK];
} p5_plugin_t;

static int find(const char *k) {
    for (int i = 0; i < P5P_COUNT; i++) if (!strcmp(P5_KEYS[i].key, k)) return i;
    return -1;
}
static float to_units(int i, float norm) { return P5_KEYS[i].lo + norm * (P5_KEYS[i].hi - P5_KEYS[i].lo); }
static float to_norm(int i, float v) {
    float d = P5_KEYS[i].hi - P5_KEYS[i].lo;
    return d > 0 ? (v - P5_KEYS[i].lo) / d : 0;
}

/* ---- imported patches: scanned once at create, never again (no I/O while playing) ---- */
static void add_user(p5_plugin_t *s, const char *name, const uint8_t *data, size_t len) {
    if (s->nuser >= MAX_USER) return;
    p5_import_t *o = &s->imp[s->nuser];
    if (!p5_import_h2p((const char *)data, len, o)) return;
    snprintf(o->name, sizeof o->name, "%s", name);
    s->nuser++;
}
static const char *base_noext(const char *path, char *b, size_t n) {
    const char *f = strrchr(path, '/');
    f = f ? f + 1 : path;
    snprintf(b, n, "%s", f);
    char *dot = strrchr(b, '.');
    if (dot && dot != b) *dot = 0;
    return b;
}
static int zip_entry(const char *name, const uint8_t *data, size_t len, void *user) {
    char b[28];
    add_user(user, base_noext(name, b, sizeof b), data, len);
    return ((p5_plugin_t *)user)->nuser >= MAX_USER;
}
static int has_ext(const char *f, const char *ext) {
    size_t a = strlen(f), b = strlen(ext);
    return a > b && !strcasecmp(f + a - b, ext);
}
static int by_name(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }
static uint8_t *slurp(const char *path, size_t max, size_t *len) {
    struct stat st;
    if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (size_t)st.st_size > max) return NULL;   /* regular files only: no symlinks */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *d = malloc((size_t)st.st_size);
    if (d && fread(d, 1, (size_t)st.st_size, f) != (size_t)st.st_size) { free(d); d = NULL; }
    fclose(f);
    if (d) *len = (size_t)st.st_size;
    return d;
}
static void scan_banks(p5_plugin_t *s, const char *dir) {
    DIR *d = dir ? opendir(dir) : NULL;
    if (!d) return;
    char **names = NULL;
    int n = 0, cap = 0;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.' || !(has_ext(e->d_name, ".h2p") || has_ext(e->d_name, ".zip"))) continue;
        if (n == cap) {
            char **t = realloc(names, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *names);
            if (!t) break;
            names = t;
        }
        if ((names[n] = strdup(e->d_name))) n++;
    }
    closedir(d);
    if (n > 1) qsort(names, (size_t)n, sizeof *names, by_name);
    for (int i = 0; i < n && s->nuser < MAX_USER; i++) {
        char path[1024];
        size_t len = 0;
        if (snprintf(path, sizeof path, "%s/%s", dir, names[i]) >= (int)sizeof path) continue;
        if (has_ext(names[i], ".h2p")) {
            uint8_t *data = slurp(path, MAX_H2P, &len);
            if (data) { char b[28]; add_user(s, base_noext(names[i], b, sizeof b), data, len); free(data); }
        } else {
            uint8_t *data = slurp(path, MAX_ZIP, &len), *scratch = data ? malloc(MAX_H2P) : NULL;
            if (scratch) p5_zip_walk(data, len, ".h2p", scratch, MAX_H2P, zip_entry, s);
            free(scratch);
            free(data);
        }
    }
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
}

static void *p5_create(const char *dir) {
    p5_plugin_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->imp = calloc(MAX_USER, sizeof *s->imp);
    p5_init(&s->e, FS);
    if (s->imp) scan_banks(s, dir);
    return s;
}
static void p5_destroy(void *h) {
    p5_plugin_t *s = h;
    if (s) free(s->imp);
    free(s);
}

static void load_preset(p5_plugin_t *s, int n) {
    if (n < 0 || n >= p5_factory_count()) return;
    p5_load_factory(&s->e, n);
    s->preset = n;
    s->user = 0;
    s->display_rev++;
}

/* an imported patch: sets the panel values it carries (so the engine's side effects run), leaves master tune, volume, voices and quality alone */
static void load_user(p5_plugin_t *s, int n) {
    if (n < 1 || n > s->nuser) return;
    const p5_import_t *o = &s->imp[n - 1];
    for (int i = 0; i < P5P_COUNT; i++) if (o->mask >> i & 1) p5_set(&s->e, i, o->v[i]);
    s->user = n;
    s->display_rev++;
}

static void p5_midi(void *h, const uint8_t *m, int len) {
    p5_plugin_t *s = h;
    if (len < 1) return;
    int st = m[0] & 0xF0, d1 = len > 1 ? m[1] & 127 : 0, d2 = len > 2 ? m[2] & 127 : 0;
    switch (st) {
    case 0x90: if (d2) { s->deferred[d1] = 0; p5_note_on(&s->e, d1, d2 / 127.0f); break; } /* fall through */
    case 0x80: if (s->sustain) s->deferred[d1] = 1; else p5_note_off(&s->e, d1); break;
    case 0xA0: p5_pressure(&s->e, d1, d2 / 127.0f); break;
    case 0xD0: p5_pressure(&s->e, -1, d1 / 127.0f); break;
    case 0xE0: p5_bend(&s->e, ((d1 | (d2 << 7)) - 8192) / 8192.0f); break;
    case 0xC0: if (d1 < p5_factory_count()) load_preset(s, d1); else load_user(s, d1 - p5_factory_count() + 1); break;
    case 0xB0:
        if (d1 == 1) p5_wheel(&s->e, d2 / 127.0f);
        else if (d1 == 64) {
            s->sustain = d2 >= 64;
            if (!s->sustain) for (int n = 0; n < 128; n++) if (s->deferred[n]) { s->deferred[n] = 0; p5_note_off(&s->e, n); }
        } else if (d1 == 120 || d1 == 123) { memset(s->deferred, 0, sizeof s->deferred); p5_all_off(&s->e); }
        break;
    }
}

static void p5_set_param(void *h, const char *k, const char *val) {
    p5_plugin_t *s = h;
    if (!strcmp(k, "preset")) { load_preset(s, atoi(val)); return; }
    if (!strcmp(k, "user")) { load_user(s, atoi(val)); return; }     /* 0 leaves the sound as it is */
    if (!strcmp(k, "state")) {
        /* "P5A <preset> v0 v1 ... v<P5P_COUNT-1>" (each value 0..1) */
        const char *p = val;
        int n = 0, pre = 0;
        if (sscanf(p, "P5A %d%n", &pre, &n) < 1) return;
        p += n;
        float v[P5P_COUNT];
        int got = 0, user = 0;
        for (; got < P5P_COUNT; got++) {
            char *end;
            v[got] = strtof(p, &end);
            if (end == p) break;
            p = end;
        }
        if (got < P5P_COUNT_V1) return;         /* a short or damaged state changes nothing; a 0.1.x state has P5P_COUNT_V1 values */
        while (*p == ' ') p++;
        if (*p == 'U') user = atoi(p + 1);
        for (int i = 0; i < got; i++) p5_set(&s->e, i, v[i]);
        s->preset = pre >= 0 && pre < p5_factory_count() ? pre : 0;
        s->user = user >= 0 && user <= s->nuser ? user : 0;
        s->display_rev++;
        return;
    }
    int i = find(k);
    if (i >= 0) p5_set(&s->e, i, to_norm(i, (float)atof(val)));
}

static int p5_get_param(void *h, const char *k, char *b, int n) {
    p5_plugin_t *s = h;
    if (!strcmp(k, "display_rev")) return snprintf(b, (size_t)n, "%u", s->display_rev) + 1;
    if (!strcmp(k, "preset")) return snprintf(b, (size_t)n, "%d", s->preset) + 1;
    if (!strcmp(k, "user")) return snprintf(b, (size_t)n, "%d", s->user) + 1;
    if (!strcmp(k, "user_display"))
        return snprintf(b, (size_t)n, "%s", s->user >= 1 && s->user <= s->nuser ? s->imp[s->user - 1].name : s->nuser ? "-" : "No files") + 1;
    if (!strncmp(k, "preset:", 7)) {
        int i = atoi(k + 7);
        if (i < 0 || i >= p5_factory_count()) return 0;
        return snprintf(b, (size_t)n, "%s", p5_factory_name(i)) + 1;
    }
    if (!strcmp(k, "state")) {
        int w = snprintf(b, (size_t)n, "P5A %d", s->preset);
        for (int i = 0; i < P5P_COUNT && w < n; i++) w += snprintf(b + w, (size_t)(n - w), " %.5g", s->e.p[i]);
        if (s->user) w += snprintf(b + w, (size_t)(n - w), " U%d", s->user);
        return w < n ? w + 1 : 0;
    }
    int i = find(k);
    if (i < 0) return 0;
    float u = to_units(i, s->e.p[i]);
    if (P5_KEYS[i].nopts) return snprintf(b, (size_t)n, "%d", (int)lroundf(u)) + 1;
    return snprintf(b, (size_t)n, "%g", u) + 1;
}

static void p5_render_block(void *h, int16_t *out, int frames) {
    p5_plugin_t *s = h;
    for (int f = 0; f < frames; f += BLOCK) {
        int m = frames - f < BLOCK ? frames - f : BLOCK;
        p5_render(&s->e, s->buf, m);
        for (int i = 0; i < m; i++) {
            int16_t x = (int16_t)lrintf(s->buf[i] * 32767);
            out[2 * (f + i)] = out[2 * (f + i) + 1] = x;
        }
    }
}

static const mpc_engine_t ENGINE = {p5_create, p5_destroy, p5_midi, p5_set_param, p5_get_param, p5_render_block, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
