/* Profit-5 for mpc-vst-plugins' VST2 wrapper (wrapper/engine.h): 44100 Hz, interleaved int16 stereo, string parameters.
 *
 * Keys and ranges are src/plugin_keys.h (tools/gen_params.py); each maps linearly onto the engine's 0..1. "preset" loads a
 * factory program (MPC's PRESET menu lists them through vst.json "programs"), "state" saves and restores every value, and
 * "display_rev" tells the wrapper (HAS_DISPLAY_REV) when the engine changed values by itself: a program load or MIDI.
 * No I/O, no allocation after create; nothing here blocks (mpc-vst-plugins PORTING.md, wrapper limits). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"      /* mpc-vst-plugins wrapper/engine.h (mpc_engine_t) */
#include "p5_engine.h"
#include "plugin_keys.h"

#define FS 44100
#define BLOCK 128

typedef struct {
    p5_engine_t e;
    int preset;
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

static void *p5_create(const char *dir) {
    (void)dir;
    p5_plugin_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    p5_init(&s->e, FS);
    return s;
}
static void p5_destroy(void *h) { free(h); }

static void load_preset(p5_plugin_t *s, int n) {
    if (n < 0 || n >= p5_factory_count()) return;
    p5_load_factory(&s->e, n);
    s->preset = n;
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
    case 0xC0: load_preset(s, d1); break;
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
    if (!strcmp(k, "state")) {
        /* "P5A <preset> v0 v1 ... v<P5P_COUNT-1>" (each value 0..1) */
        const char *p = val;
        int n = 0, pre = 0;
        if (sscanf(p, "P5A %d%n", &pre, &n) < 1) return;
        p += n;
        float v[P5P_COUNT];
        for (int i = 0; i < P5P_COUNT; i++) {
            char *end;
            v[i] = strtof(p, &end);
            if (end == p) return;               /* a short or damaged state changes nothing */
            p = end;
        }
        for (int i = 0; i < P5P_COUNT; i++) p5_set(&s->e, i, v[i]);
        s->preset = pre >= 0 && pre < p5_factory_count() ? pre : 0;
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
    if (!strncmp(k, "preset:", 7)) {
        int i = atoi(k + 7);
        if (i < 0 || i >= p5_factory_count()) return 0;
        return snprintf(b, (size_t)n, "%s", p5_factory_name(i)) + 1;
    }
    if (!strcmp(k, "state")) {
        int w = snprintf(b, (size_t)n, "P5A %d", s->preset);
        for (int i = 0; i < P5P_COUNT && w < n; i++) w += snprintf(b + w, (size_t)(n - w), " %.5g", s->e.p[i]);
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
