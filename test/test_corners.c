/* Corners (docs/improvements.md, check set): every control at its extremes and random settings, both revisions and qualities, keys at
 * both ends of MIDI, jumps and sweeps of cutoff and resonance while playing. Fails on a non-finite or unbounded sample, or on a filter
 * reset (diag_nan) or an unconverged filter solve (diag_unconv). gcc -O2 -std=c11 -fsanitize=address,undefined test/test_corners.c src/engine.c -lm && ./a.out */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "../src/p5_engine.h"

#define FS 48000
static float buf[FS];
static p5_engine_t E;
static uint32_t rs = 12345;
static float rnd01(void) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) * (1.0f / 16777216.0f); }

static int bad;
static void run(const char *what, int blocks, int sweep) {
    for (int b = 0; b < blocks; b++) {
        if (sweep) {   /* jumps and sweeps while playing */
            p5_set(&E, P5P_CUTOFF, (b & 1) ? 1.0f : rnd01());
            p5_set(&E, P5P_RESO, (b % 3) ? 1.0f : rnd01());
            p5_set(&E, P5P_ENV_AMT, rnd01());
        }
        p5_render(&E, buf, 4800);
        for (int i = 0; i < 4800; i++)
            if (!isfinite(buf[i]) || fabsf(buf[i]) > 1.0001f) { printf("FAIL %s: sample %g\n", what, buf[i]); bad++; return; }
    }
    if (E.diag_nan) { printf("FAIL %s: %u filter resets\n", what, E.diag_nan); bad++; }
    if (E.diag_unconv) { printf("FAIL %s: %u unconverged filter solves\n", what, E.diag_unconv); bad++; }
}

static void keys(void) {
    static const int k[] = { 0, 36, 60, 96, 127 };
    for (int i = 0; i < 5; i++) p5_note_on(&E, k[i], 1);
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    int cases = 0;
    for (int r = 0; r < 2; r++)
        for (int q = 0; q < 2; q++) {
            /* every value at 0, every value at 1 */
            for (int ext = 0; ext < 2; ext++) {
                p5_init(&E, FS);
                for (int i = 0; i < P5P_COUNT; i++) p5_set(&E, i, (float)ext);
                p5_set(&E, P5P_REV, (float)r); p5_set(&E, P5P_QUALITY, (float)q); p5_set(&E, P5P_VOLUME, 1);
                keys(); p5_wheel(&E, (float)ext); p5_bend(&E, ext ? 1.0f : -1.0f);
                run(ext ? "all at 1" : "all at 0", 20, 0); cases++;
            }
            /* random settings, sweeps and jumps of the filter */
            for (int t = 0; t < 60; t++) {
                p5_init(&E, FS);
                for (int i = 0; i < P5P_COUNT; i++) p5_set(&E, i, rnd01() < 0.25f ? (rnd01() < 0.5f ? 0.0f : 1.0f) : rnd01());
                p5_set(&E, P5P_REV, (float)r); p5_set(&E, P5P_QUALITY, (float)q); p5_set(&E, P5P_VOLUME, 1);
                keys(); p5_wheel(&E, rnd01()); p5_bend(&E, 2 * rnd01() - 1);
                run("random", 10, t & 1); cases++;
                p5_all_off(&E);
                run("random, released", 4, 0);
            }
            /* full resonance, no oscillators, top and bottom of the cutoff: self-oscillation stays bounded */
            for (int c = 0; c < 2; c++) {
                p5_init(&E, FS);
                p5_set(&E, P5P_REV, (float)r); p5_set(&E, P5P_QUALITY, (float)q);
                p5_set(&E, P5P_MIX_A, 0); p5_set(&E, P5P_MIX_B, 0); p5_set(&E, P5P_MIX_NOISE, 1);
                p5_set(&E, P5P_RESO, 1); p5_set(&E, P5P_CUTOFF, (float)c); p5_set(&E, P5P_F_KBD, 1); p5_set(&E, P5P_VOLUME, 1);
                keys();
                run("full resonance", 20, 0); cases++;
            }
        }
    printf("corners: %d cases, %d failed\n", cases, bad);
    return bad != 0;
}
