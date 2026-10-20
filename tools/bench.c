/* x86 CPU estimate: five voices held, 10 s of audio per setting; prints the real-time factor. */
#include <stdio.h>
#include <time.h>
#include "../src/engine.h"
static float buf[480];
int main(void) {
    static p5_engine_t e;
    for (int q = 0; q < 2; q++) for (int r = 0; r < 2; r++) {
        p5_init(&e, 48000); p5_set(&e, P5P_QUALITY, (float)q); p5_set(&e, P5P_REV, (float)r);
        p5_load_factory(&e, 6);                                /* 1-7 Sync I: sync, poly-mod, both oscillators */
        for (int k = 0; k < 5; k++) p5_note_on(&e, 48 + 4 * k, 1);
        clock_t t0 = clock();
        for (int b = 0; b < 1000; b++) p5_render(&e, buf, 480);
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("%s rev %d: %.1f%% of real time (5 voices)\n", q ? "high" : "eco ", r ? 4 : 2, 100 * s / 10);
    }
    return 0;
}
