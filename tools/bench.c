/* CPU estimate: five voices held, 10 s of audio per setting; prints the real-time factor. Optional argument: the model version
 * (0 = the 2.0.0 laws; default the latest). Cross-built for the devices as in docs/improvements.md. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../src/p5_engine.h"
static float buf[480];
int main(int argc, char **argv) {
    static p5_engine_t e;
    int model = argc > 1 ? atoi(argv[1]) : P5_MODEL_LATEST;
    for (int q = 0; q < 2; q++) for (int r = 0; r < 2; r++) {
        p5_init(&e, 48000); e.model = model; p5_set(&e, P5P_QUALITY, (float)q); p5_set(&e, P5P_REV, (float)r);
        p5_load_factory(&e, 6);                                /* 1-7 Sync I: sync, poly-mod, both oscillators */
        for (int k = 0; k < 5; k++) p5_note_on(&e, 48 + 4 * k, 1);
        clock_t t0 = clock();
        for (int b = 0; b < 1000; b++) p5_render(&e, buf, 480);
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("model %d %s rev %d: %.1f%% of real time (5 voices)\n", model, q ? "high" : "eco ", r ? 4 : 2, 100 * s / 10);
    }
    return 0;
}
