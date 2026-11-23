/* Golden renders (docs/improvements.md, check set): every factory program x Rev 2/4 x Eco/High, two workloads each.
 *   golden write DIR [model]   render and save DIR/<prog>-r<rev>-q<q>-<workload>.f32 (float32, 48 kHz, mono)
 *   golden check DIR [model]   render again and report the largest difference against DIR, in dB re full scale
 * model: the engine's model version to render with (default: the latest). "check DIR 0" after a sound-changing round must report
 * every render identical: old projects keep their sound.
 * gcc -O2 -std=c11 tools/golden.c src/engine.c -lm -o build/golden */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/p5_engine.h"

#define FS 48000
#define LEN (FS * 5 / 2)
static float buf[LEN], ref[LEN];
static p5_engine_t E;

static int render(int prog, int rev, int q, int wl, int model) {
    p5_init(&E, FS);
#ifdef P5_MODEL_LATEST
    E.model = model < 0 ? P5_MODEL_LATEST : model;
#else
    (void)model;
#endif
    p5_load_factory(&E, prog);
    p5_set(&E, P5P_REV, (float)rev);
    p5_set(&E, P5P_QUALITY, (float)q);
    static const int chord[4] = { 48, 55, 60, 64 };
    int n = 0, on = wl == 0 ? FS * 3 / 2 : FS;
    if (wl == 0) for (int k = 0; k < 4; k++) p5_note_on(&E, chord[k], 0.8f);
    else p5_note_on(&E, 36, 0.8f);
    while (n < LEN) {
        int m = LEN - n < 128 ? LEN - n : 128;
        if (n <= on && n + m > on) m = on - n > 0 ? on - n : m;
        p5_render(&E, buf + n, m);
        n += m;
        if (n == on) { if (wl == 0) for (int k = 0; k < 4; k++) p5_note_off(&E, chord[k]); else p5_note_off(&E, 36); }
    }
    return LEN;
}

int main(int argc, char **argv) {
    if (argc < 3 || (strcmp(argv[1], "write") && strcmp(argv[1], "check"))) { fprintf(stderr, "golden write|check DIR [model]\n"); return 2; }
    int wr = !strcmp(argv[1], "write"), model = argc > 3 ? atoi(argv[3]) : -1;
    int total = 0, same = 0, missing = 0;
    double worst = -1e9; char worst_name[96] = "";
    for (int p = 0; p < p5_factory_count(); p++)
        for (int r = 0; r < 2; r++)
            for (int q = 0; q < 2; q++)
                for (int wl = 0; wl < 2; wl++) {
                    char path[512];
                    snprintf(path, sizeof path, "%s/%02d-r%d-q%d-%s.f32", argv[2], p, r, q, wl ? "bass" : "chord");
                    int n = render(p, r, q, wl, model);
                    total++;
                    if (wr) {
                        FILE *f = fopen(path, "wb");
                        if (!f || fwrite(buf, sizeof(float), (size_t)n, f) != (size_t)n) { perror(path); return 1; }
                        fclose(f);
                        continue;
                    }
                    FILE *f = fopen(path, "rb");
                    if (!f || fread(ref, sizeof(float), (size_t)n, f) != (size_t)n) { missing++; if (f) fclose(f); continue; }
                    fclose(f);
                    double d = 0;
                    for (int i = 0; i < n; i++) { double x = fabs((double)buf[i] - ref[i]); if (x > d) d = x; }
                    if (d == 0) { same++; continue; }
                    double db = 20 * log10(d);
                    printf("%-40s max diff %7.1f dB\n", path + strlen(argv[2]) + 1, db);
                    if (db > worst) { worst = db; snprintf(worst_name, sizeof worst_name, "%s", path + strlen(argv[2]) + 1); }
                }
    if (wr) { printf("%d renders written to %s\n", total, argv[2]); return 0; }
    printf("%d renders: %d identical, %d differ, %d missing", total, same, total - same - missing, missing);
    if (total - same - missing) printf("; worst %s at %.1f dB", worst_name, worst);
    printf("\n");
    return missing ? 1 : 0;
}
