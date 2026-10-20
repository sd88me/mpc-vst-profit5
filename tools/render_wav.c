/* Render a factory program to a 16-bit mono WAV for listening offline:
 *   render_wav <program 0-37> <rev 2|4> <out.wav> [quality 0|1]
 * Plays a C major chord (1.5 s held, 1.5 s release), then a rising line of single notes. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../src/engine.h"

#define FS 48000
static float buf[FS * 8];

static void w32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void w16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <program 0-%d> <rev 2|4> <out.wav> [quality 0|1]\n", argv[0], p5_factory_count() - 1); return 2; }
    static p5_engine_t e;
    p5_init(&e, FS);
    p5_set(&e, P5P_QUALITY, argc > 4 ? (float)atof(argv[4]) : 1.0f);
    p5_set(&e, P5P_REV, atoi(argv[2]) == 4 ? 1.0f : 0.0f);
    if (p5_load_factory(&e, atoi(argv[1]))) { fprintf(stderr, "no program %s\n", argv[1]); return 2; }
    int n = 0;
    p5_note_on(&e, 48, 0.8f); p5_note_on(&e, 52, 0.8f); p5_note_on(&e, 55, 0.8f);
    p5_render(&e, buf + n, FS * 3 / 2); n += FS * 3 / 2;
    p5_all_off(&e);
    p5_render(&e, buf + n, FS * 3 / 2); n += FS * 3 / 2;
    static const int line[8] = { 48, 50, 52, 55, 57, 60, 64, 67 };
    for (int k = 0; k < 8; k++) {
        p5_note_on(&e, line[k], 0.5f + 0.06f * k);
        p5_render(&e, buf + n, FS / 4); n += FS / 4;
        p5_note_off(&e, line[k]);
        p5_render(&e, buf + n, FS / 8); n += FS / 8;
    }
    p5_render(&e, buf + n, FS); n += FS;
    FILE *f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 1; }
    fwrite("RIFF", 1, 4, f); w32(f, 36 + 2 * n); fwrite("WAVEfmt ", 1, 8, f);
    w32(f, 16); w16(f, 1); w16(f, 1); w32(f, FS); w32(f, 2 * FS); w16(f, 2); w16(f, 16);
    fwrite("data", 1, 4, f); w32(f, 2 * n);
    for (int i = 0; i < n; i++) { float x = buf[i] < -1 ? -1 : buf[i] > 1 ? 1 : buf[i]; w16(f, (uint16_t)(int16_t)(x * 32767)); }
    fclose(f);
    printf("%s (%s, rev %s) -> %s\n", p5_factory_name(atoi(argv[1])), argc > 4 && atof(argv[4]) < 0.5 ? "eco" : "high", argv[2], argv[3]);
    return 0;
}
