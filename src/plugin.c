/* Profit-5 for mpc-vst-plugins' VST2 wrapper (wrapper/engine.h): 44100 Hz, interleaved int16 stereo, string parameters.
 *
 * Keys and ranges are src/plugin_keys.h (tools/gen_params.py); each maps linearly onto the engine's 0..1. "preset" loads a
 * program slot: 0..37 are the factory sheets, then one slot per Repro-5 .h2p patch or Prophet-5 Rev 4 .syx program found in <plugin dir>/banks (src/p5_import.c;
 * scanned once at create), the rest "Init". MPC's PRESET menu lists all P5_SLOTS through vst.json "programs" (names come from
 * "preset:<n>"). "user" is the 0.2.0 import knob, kept so old saves load. "state" saves and restores every value, and
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
#include "engine.h"      /* mpc_engine_t: src/engine.h, vendored from mpc-vst-plugins wrapper/engine.h */
#include "p5_engine.h"
#include "p5_import.h"
#include "plugin_keys.h"

#define FS 44100
#define BLOCK 128
#define P5P_COUNT_V1 58           /* values in a state saved before 0.2.0 */
#define P5_SLOTS 128               /* programs in the MPC menu = MIDI program numbers: the factory sheets, then imported patches, then Init */
#define MAX_USER 90                /* imported patches in the PRESET menu: the first MAX_USER found in <plugin dir>/banks (P5_SLOTS - 38 factory) */
#define MAX_ALL 2048               /* imported patches in all: every one is on the BANKS page */
#define MAX_BANKS 256              /* BANKS page: Factory 1978, then one bank per file (a .syx of more than BANK_SPLIT programs is split) */
#define BANK_SPLIT 40              /* the Rev 4's bank size */
#define BANK_SLOTS 11              /* bank tiles in the list */
#define PROG_SLOTS 45              /* program tiles a page: three columns of 15 */
#define MAX_H2P (256u << 10)       /* one patch file; a bank zip may be up to MAX_ZIP */
#define MAX_ZIP (32u << 20)
#define MAX_SYX (1u << 20)         /* a .syx bank: 200 programs are 32 KB */

typedef struct { char name[40]; int first, count, loose; } p5_bank_t;   /* bank 0 is the factory sheets (first -1) */

typedef struct {
    p5_engine_t e;
    int preset;                    /* the current slot (a later edit leaves it) */
    int nuser;
    p5_import_t *imp;              /* MAX_ALL, filled once at create; the first MAX_USER are PRESET slots U01-U90 */
    p5_bank_t banks[MAX_BANKS];
    int nbanks, browse_bank, browse_page, cur_bank, cur_prog;
    int off_slot;                  /* the sound is a BANKS program beyond the PRESET slots: its name is loaded_name */
    char loaded_name[28];
    uint32_t rnd;
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
    if (s->nuser >= MAX_ALL) return;
    p5_import_t *o = &s->imp[s->nuser];
    if (!p5_import_h2p((const char *)data, len, o)) return;
    snprintf(o->name, sizeof o->name, "%s", name);
    s->nuser++;
}
static const char *base_noext(const char *path, char *b, size_t n) {
    const char *f = strrchr(path, '/');
    f = f ? f + 1 : path;
    int hex = 0;                                   /* an upload's "1a2b3c4d-" prefix is not part of the name */
    while (hex < 8 && isxdigit((unsigned char)f[hex])) hex++;
    if (hex == 8 && f[8] == '-' && f[9] && f[9] != '.') f += 9;
    snprintf(b, n, "%s", f);
    char *dot = strrchr(b, '.');
    if (dot && dot != b) *dot = 0;
    return b;
}
static int zip_entry(const char *name, const uint8_t *data, size_t len, void *user) {
    char b[28];
    add_user((p5_plugin_t *)user, base_noext(name, b, sizeof b), data, len);
    return ((p5_plugin_t *)user)->nuser >= MAX_ALL;
}
/* a Prophet-5 Rev 4 .syx: every program in it, in file order, named as in the dump */
static int syx_patch(const p5_import_t *patch, void *user) {
    p5_plugin_t *s = (p5_plugin_t *)user;
    if (s->nuser >= MAX_ALL) return 1;
    s->imp[s->nuser++] = *patch;
    return s->nuser >= MAX_ALL;
}
static int zip_syx(const char *name, const uint8_t *data, size_t len, void *user) {
    (void)name;
    p5_import_syx(data, len, syx_patch, user);
    return ((p5_plugin_t *)user)->nuser >= MAX_ALL;
}
/* a bank for the patches of one file: added after them, split into banks of BANK_SPLIT when larger; loose .h2p files share one */
static void close_bank(p5_plugin_t *s, const char *file, int first, int loose) {
    int n = s->nuser - first;
    if (n <= 0) return;
    if (loose && s->nbanks > 1 && s->banks[s->nbanks - 1].loose && s->banks[s->nbanks - 1].first + s->banks[s->nbanks - 1].count == first) {
        s->banks[s->nbanks - 1].count += n;
        return;
    }
    char nm[40];
    snprintf(nm, sizeof nm, "%s", loose ? "Repro-5" : file);
    for (char *c = nm; *c; c++) if (*c == '_') *c = ' ';
    const int parts = loose || n <= BANK_SPLIT ? 1 : (n + BANK_SPLIT - 1) / BANK_SPLIT;
    for (int k = 0; k < parts && s->nbanks < MAX_BANKS; k++) {
        p5_bank_t *b = &s->banks[s->nbanks++];
        b->first = first + k * BANK_SPLIT;
        b->count = parts == 1 ? n : (k == parts - 1 ? n - k * BANK_SPLIT : BANK_SPLIT);
        b->loose = loose;
        if (parts == 1) snprintf(b->name, sizeof b->name, "%s", nm);
        else snprintf(b->name, sizeof b->name, "%.24s %d", nm, (k + 1) % 1000);   /* short enough that the part number shows on a tile */
    }
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
    uint8_t *d = (uint8_t *)malloc((size_t)st.st_size);
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
        if (e->d_name[0] == '.' || !(has_ext(e->d_name, ".h2p") || has_ext(e->d_name, ".syx") || has_ext(e->d_name, ".zip"))) continue;
        if (n == cap) {
            char **t = (char **)realloc(names, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *names);
            if (!t) break;
            names = t;
        }
        if ((names[n] = strdup(e->d_name))) n++;
    }
    closedir(d);
    if (n > 1) qsort(names, (size_t)n, sizeof *names, by_name);
    for (int i = 0; i < n && s->nuser < MAX_ALL; i++) {
        char path[1024], base[40];
        size_t len = 0;
        const int first = s->nuser;
        if (snprintf(path, sizeof path, "%s/%s", dir, names[i]) >= (int)sizeof path) continue;
        base_noext(names[i], base, sizeof base);
        if (has_ext(names[i], ".h2p")) {
            uint8_t *data = slurp(path, MAX_H2P, &len);
            if (data) { char b[28]; add_user(s, base_noext(names[i], b, sizeof b), data, len); free(data); }
        } else if (has_ext(names[i], ".syx")) {
            uint8_t *data = slurp(path, MAX_SYX, &len);
            if (data) { p5_import_syx(data, len, syx_patch, s); free(data); }
        } else {
            uint8_t *data = slurp(path, MAX_ZIP, &len), *scratch = data ? (uint8_t *)malloc(MAX_SYX) : NULL;
            if (scratch) p5_zip_walk(data, len, ".h2p", scratch, MAX_H2P, zip_entry, s);
            if (scratch && s->nuser < MAX_ALL) p5_zip_walk(data, len, ".syx", scratch, MAX_SYX, zip_syx, s);
            free(scratch);
            free(data);
        }
        close_bank(s, base, first, has_ext(names[i], ".h2p"));
    }
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
}

static void *p5_create(const char *dir) {
    p5_plugin_t *s = (p5_plugin_t *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->imp = (p5_import_t *)calloc(MAX_ALL, sizeof *s->imp);
    p5_init(&s->e, FS);
    snprintf(s->banks[0].name, sizeof s->banks[0].name, "Factory 1978");
    s->banks[0].first = -1;
    s->banks[0].count = p5_factory_count();
    s->nbanks = 1;
    s->rnd = 0x9E3779B9u ^ (uint32_t)(uintptr_t)s;
    if (s->imp) scan_banks(s, dir);
    return s;
}
static void p5_destroy(void *h) {
    p5_plugin_t *s = (p5_plugin_t *)h;
    if (s) free(s->imp);
    free(s);
}

static int nfactory(void) { return p5_factory_count(); }

/* the name of a slot, as the PRESET menu lists it: factory sheet, "U01 <file>" for an imported patch, "U07 Init" for a free slot */
static int slot_name(const p5_plugin_t *s, int slot, char *b, int n) {
    if (slot < 0 || slot >= P5_SLOTS) return 0;
    if (slot < nfactory()) return snprintf(b, (size_t)n, "%s", p5_factory_name(slot)) + 1;
    int u = slot - nfactory();
    return snprintf(b, (size_t)n, "U%02d %s", u + 1, u < s->nuser && u < MAX_USER ? s->imp[u].name : "Init") + 1;
}

/* the bank and program an imported patch is in, for the BANKS page to follow a PRESET pick or a program change */
static void follow_import(p5_plugin_t *s, int g) {
    for (int b = 1; b < s->nbanks; b++)
        if (g >= s->banks[b].first && g < s->banks[b].first + s->banks[b].count) { s->cur_bank = b; s->cur_prog = g - s->banks[b].first; return; }
    s->cur_bank = s->cur_prog = -1;
}
static void browse_follow(p5_plugin_t *s) {
    if (s->cur_bank < 0) return;
    s->browse_bank = s->cur_bank;
    s->browse_page = s->cur_prog / PROG_SLOTS;
}

/* a factory sheet, an imported patch (sets the panel values it carries so the engine's side effects run; master tune, volume,
 * voices and quality stay) or, for a free slot, the neutral Init patch */
static void load_slot(p5_plugin_t *s, int slot) {
    if (slot < 0 || slot >= P5_SLOTS) return;
    if (slot < nfactory()) p5_load_factory(&s->e, slot);
    else {
        const int u = slot - nfactory();
        p5_import_t init, *o = u < s->nuser && u < MAX_USER ? &s->imp[u] : (p5_import_init(&init), &init);
        for (int i = 0; i < P5P_COUNT; i++) if (o->mask >> i & 1) p5_set(&s->e, i, o->v[i]);
    }
    s->preset = slot;
    s->off_slot = 0;
    if (slot < nfactory()) { s->cur_bank = 0; s->cur_prog = slot; }
    else follow_import(s, slot - nfactory());
    browse_follow(s);
    s->display_rev++;
}

/* BANKS: load program p of bank b. One in a PRESET slot loads through it (the PRESET menu follows); beyond them the sound is set
 * the same way and keeps its own name (off_slot), the PRESET slot staying as it was */
static void load_bank_program(p5_plugin_t *s, int b, int p) {
    if (b < 0 || b >= s->nbanks || p < 0 || p >= s->banks[b].count) return;
    if (b == 0) { load_slot(s, p); return; }
    const int g = s->banks[b].first + p;
    if (g < MAX_USER) { load_slot(s, nfactory() + g); return; }
    const p5_import_t *o = &s->imp[g];
    for (int i = 0; i < P5P_COUNT; i++) if (o->mask >> i & 1) p5_set(&s->e, i, o->v[i]);
    s->off_slot = 1;
    snprintf(s->loaded_name, sizeof s->loaded_name, "%s", o->name);
    s->cur_bank = b;
    s->cur_prog = p;
    browse_follow(s);
    s->display_rev++;
}
static int bank_pages(const p5_plugin_t *s, int b) { int c = s->banks[b].count; return c > PROG_SLOTS ? (c + PROG_SLOTS - 1) / PROG_SLOTS : 1; }
static void prog_label(const p5_plugin_t *s, int b, int p, char *out, size_t n) {
    if (b == 0) snprintf(out, n, "%s", p5_factory_name(p));
    else snprintf(out, n, "%03d %s", p + 1, s->imp[s->banks[b].first + p].name);
}

static void p5_midi(void *h, const uint8_t *m, int len) {
    p5_plugin_t *s = (p5_plugin_t *)h;
    if (len < 1) return;
    int st = m[0] & 0xF0, d1 = len > 1 ? m[1] & 127 : 0, d2 = len > 2 ? m[2] & 127 : 0;
    switch (st) {
    case 0x90: if (d2) { s->deferred[d1] = 0; p5_note_on(&s->e, d1, d2 / 127.0f); break; } /* fall through */
    case 0x80: if (s->sustain) s->deferred[d1] = 1; else p5_note_off(&s->e, d1); break;
    case 0xA0: p5_pressure(&s->e, d1, d2 / 127.0f); break;
    case 0xD0: p5_pressure(&s->e, -1, d1 / 127.0f); break;
    case 0xE0: p5_bend(&s->e, ((d1 | (d2 << 7)) - 8192) / 8192.0f); break;
    case 0xC0: load_slot(s, d1); break;
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
    p5_plugin_t *s = (p5_plugin_t *)h;
    if (!strcmp(k, "preset")) { load_slot(s, atoi(val)); return; }
    /* BANKS page (src/Profit5Plugin.cpp: momentary keys fire "1"; tiles light from "<key>_on") */
    if (!strcmp(k, "browse_bank_index")) { int x = atoi(val); if (x >= 0 && x < s->nbanks && x != s->browse_bank) { s->browse_bank = x; s->browse_page = 0; s->display_rev++; } return; }
    if (!strcmp(k, "next_browse_bank") || !strcmp(k, "prev_browse_bank")) {
        if (atoi(val)) { s->browse_bank = (s->browse_bank + (k[0] == 'n' ? 1 : s->nbanks - 1)) % s->nbanks; s->browse_page = 0; s->display_rev++; }
        return;
    }
    if (!strncmp(k, "bank_slot_", 10)) {
        int b = (s->browse_bank / BANK_SLOTS) * BANK_SLOTS + atoi(k + 10) - 1;
        if (atoi(val) && b >= 0 && b < s->nbanks) { s->browse_bank = b; s->browse_page = 0; s->display_rev++; }
        return;
    }
    if (!strncmp(k, "patch_slot_", 11)) { if (atoi(val)) load_bank_program(s, s->browse_bank, s->browse_page * PROG_SLOTS + atoi(k + 11) - 1); return; }
    if (!strcmp(k, "patch_page_next") || !strcmp(k, "patch_page_prev")) {
        const int np = bank_pages(s, s->browse_bank);
        if (atoi(val)) { s->browse_page = (s->browse_page + (k[11] == 'n' ? 1 : np - 1)) % np; s->display_rev++; }
        return;
    }
    if (!strcmp(k, "bank_prog")) {   /* the data wheel / Q-Link on PROGRAM: a program of the current bank (the browsed one if none) */
        int b = s->cur_bank >= 0 ? s->cur_bank : s->browse_bank, p = atoi(val);
        if (p >= s->banks[b].count) p = s->banks[b].count - 1;
        if (p != s->cur_prog || b != s->cur_bank) load_bank_program(s, b, p);
        return;
    }
    if (!strcmp(k, "bank_prog_next") || !strcmp(k, "bank_prog_prev")) {
        if (!atoi(val)) return;
        int b = s->cur_bank >= 0 ? s->cur_bank : s->browse_bank, c = s->banks[b].count;
        int p = s->cur_bank >= 0 ? s->cur_prog + (k[10] == 'n' ? 1 : -1) : 0;
        load_bank_program(s, b, (p % c + c) % c);
        return;
    }
    if (!strcmp(k, "random_patch")) {   /* a random program from every imported bank (the factory sheets when nothing is imported) */
        if (!atoi(val)) return;
        s->rnd ^= s->rnd << 13; s->rnd ^= s->rnd >> 17; s->rnd ^= s->rnd << 5;
        if (s->nuser > 0) { int g = (int)(s->rnd % (uint32_t)s->nuser); follow_import(s, g); if (s->cur_bank > 0) load_bank_program(s, s->cur_bank, s->cur_prog); }
        else load_bank_program(s, 0, (int)(s->rnd % (uint32_t)nfactory()));
        return;
    }
    if (!strcmp(k, "user")) { if (atoi(val) >= 1) load_slot(s, nfactory() + atoi(val) - 1); return; }   /* 0.2.0's knob; 0 left the sound as it was */
    if (!strcmp(k, "state")) {
        /* "P5A <preset> v0 v1 ... v<P5P_COUNT-1> [U<n>] [m<n>]" (each value 0..1; U: 0.2.0 import; m: model version, absent = 0) */
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
        int model = 0, named = 0;               /* a state without "m<n>" was saved by 2.0.0 or earlier: keep the 2.0.0 laws */
        char nm[28] = "";
        for (;;) {
            while (*p == ' ') p++;
            if (*p == 'U') user = atoi(p + 1);
            else if (*p == 'm') model = atoi(p + 1);
            else if (*p == 'N') {   /* a BANKS program beyond the PRESET slots: its name, spaces and '%' as %XX */
                int o = 0;
                for (const char *c = p + 1; *c && *c != ' ' && o < (int)sizeof nm - 1; c++) {
                    if (c[0] == '%' && c[1] && c[2]) { char hx[3] = { c[1], c[2], 0 }; nm[o++] = (char)strtol(hx, NULL, 16); c += 2; }
                    else nm[o++] = *c;
                }
                nm[o] = 0;
                named = 1;
            }
            else break;
            while (*p && *p != ' ') p++;
        }
        s->e.model = model < 0 ? 0 : model > P5_MODEL_LATEST ? P5_MODEL_LATEST : model;
        for (int i = 0; i < got; i++) p5_set(&s->e, i, v[i]);
        p5_set(&s->e, P5P_REV, s->e.p[P5P_REV]);   /* recompute what depends on the model (envelope coefficients) */
        if (user > 0) pre = nfactory() + user - 1;       /* a 0.2.0 state: factory program + imported patch number */
        s->preset = pre >= 0 && pre < P5_SLOTS ? pre : 0;
        s->off_slot = named;
        if (named) { snprintf(s->loaded_name, sizeof s->loaded_name, "%s", nm); s->cur_bank = s->cur_prog = -1; }
        else if (s->preset < nfactory()) { s->cur_bank = 0; s->cur_prog = s->preset; }
        else follow_import(s, s->preset - nfactory());
        browse_follow(s);
        s->display_rev++;
        return;
    }
    int i = find(k);
    if (i >= 0) p5_set(&s->e, i, to_norm(i, (float)atof(val)));
}

static int p5_get_param(void *h, const char *k, char *b, int n) {
    p5_plugin_t *s = (p5_plugin_t *)h;
    if (!strcmp(k, "display_rev")) return snprintf(b, (size_t)n, "%u", s->display_rev) + 1;
    if (!strcmp(k, "preset")) return snprintf(b, (size_t)n, "%d", s->preset) + 1;
    if (!strcmp(k, "user")) return snprintf(b, (size_t)n, "%d", s->preset >= nfactory() && s->preset - nfactory() < s->nuser ? s->preset - nfactory() + 1 : 0) + 1;
    if (!strncmp(k, "preset:", 7)) return slot_name(s, atoi(k + 7), b, n);
    if (!strcmp(k, "program_name")) return s->off_slot ? snprintf(b, (size_t)n, "%s", s->loaded_name) + 1 : slot_name(s, s->preset, b, n);
    if (!strcmp(k, "import_status")) {
        if (!s->nuser) return snprintf(b, (size_t)n, "No files in banks/") + 1;
        if (s->nuser > MAX_USER) return snprintf(b, (size_t)n, "%d imported (U01-U%02d, all on BANKS)", s->nuser, MAX_USER) + 1;
        return snprintf(b, (size_t)n, "%d imported (U01-U%02d)", s->nuser, s->nuser) + 1;
    }
    /* BANKS page */
    if (!strcmp(k, "browse_bank_index")) return snprintf(b, (size_t)n, "%d", s->browse_bank) + 1;
    if (!strcmp(k, "browse_bank_index_text")) return snprintf(b, (size_t)n, "%s", s->banks[s->browse_bank].name) + 1;
    if (!strcmp(k, "bank_prog")) return snprintf(b, (size_t)n, "%d", s->cur_prog >= 0 ? s->cur_prog : 0) + 1;
    if (!strcmp(k, "bank_prog_text")) {
        if (s->cur_bank < 0) return snprintf(b, (size_t)n, "%s", s->off_slot ? s->loaded_name : "Edited") + 1;
        char t[48];
        prog_label(s, s->cur_bank, s->cur_prog, t, sizeof t);
        return snprintf(b, (size_t)n, "%s", t) + 1;
    }
    if (!strcmp(k, "patch_page_text")) return snprintf(b, (size_t)n, "PAGE %d/%d", s->browse_page + 1, bank_pages(s, s->browse_bank)) + 1;
    {
        size_t kl = strlen(k);
        const int on = kl > 3 && !strcmp(k + kl - 3, "_on");
        if (!strncmp(k, "bank_slot_", 10)) {
            int bk = (s->browse_bank / BANK_SLOTS) * BANK_SLOTS + atoi(k + 10) - 1;
            if (on) return snprintf(b, (size_t)n, "%d", bk == s->browse_bank) + 1;
            return snprintf(b, (size_t)n, "%s", bk >= 0 && bk < s->nbanks ? s->banks[bk].name : "") + 1;
        }
        if (!strncmp(k, "patch_slot_", 11)) {
            int p = s->browse_page * PROG_SLOTS + atoi(k + 11) - 1;
            if (on) return snprintf(b, (size_t)n, "%d", s->browse_bank == s->cur_bank && p == s->cur_prog) + 1;
            char t[48] = "";
            if (p >= 0 && p < s->banks[s->browse_bank].count) prog_label(s, s->browse_bank, p, t, sizeof t);
            return snprintf(b, (size_t)n, "%s", t) + 1;
        }
        if (!strcmp(k, "next_browse_bank") || !strcmp(k, "prev_browse_bank") || !strcmp(k, "patch_page_next") || !strcmp(k, "patch_page_prev") ||
            !strcmp(k, "bank_prog_next") || !strcmp(k, "bank_prog_prev") || !strcmp(k, "random_patch")) return snprintf(b, (size_t)n, "0") + 1;
    }
    if (!strcmp(k, "state")) {
        int w = snprintf(b, (size_t)n, "P5A %d", s->preset);
        for (int i = 0; i < P5P_COUNT && w < n; i++) w += snprintf(b + w, (size_t)(n - w), " %.5g", s->e.p[i]);
        if (s->e.model > 0 && w < n) w += snprintf(b + w, (size_t)(n - w), " m%d", s->e.model);   /* absent = 0; 2.0.0 stops at the first non-number */
        if (s->off_slot && w < n - 4) {
            w += snprintf(b + w, (size_t)(n - w), " N");
            for (const char *c = s->loaded_name; *c && w < n - 4; c++)
                w += (*c > ' ' && *c != '%' && (unsigned char)*c < 127) ? snprintf(b + w, (size_t)(n - w), "%c", *c) : snprintf(b + w, (size_t)(n - w), "%%%02X", (unsigned char)*c);
        }
        return w < n ? w + 1 : 0;
    }
    int i = find(k);
    if (i < 0) return 0;
    float u = to_units(i, s->e.p[i]);
    if (P5_KEYS[i].nopts) return snprintf(b, (size_t)n, "%d", (int)lroundf(u)) + 1;
    return snprintf(b, (size_t)n, "%g", u) + 1;
}

static void p5_render_block(void *h, int16_t *out, int frames) {
    p5_plugin_t *s = (p5_plugin_t *)h;
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
