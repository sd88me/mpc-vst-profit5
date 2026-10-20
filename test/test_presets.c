/* Checks the generated factory table: ranges, and the printed "OSC A/B: up N" notes of the sheets. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/presets_factory.h"
static const p5_sheet_t *find(const char *id) {
    for (unsigned i = 0; i < sizeof P5_FACTORY / sizeof *P5_FACTORY; i++)
        if (!strncmp(P5_FACTORY[i].name, id, strlen(id))) return &P5_FACTORY[i];
    return 0;
}
int main(void) {
    unsigned n = sizeof P5_FACTORY / sizeof *P5_FACTORY, silent = 0;
    assert(n == 38);
    for (unsigned i = 0; i < n; i++) {
        const p5_sheet_t *p = &P5_FACTORY[i];
        assert(p->a_semis >= 0 && p->a_semis <= 60 && p->b_semis >= 0 && p->b_semis <= 60);
        assert(p->cutoff >= 0 && p->cutoff <= 1 && p->reso >= 0 && p->reso <= 1 && p->a_s <= 1);
        if (!(p->sw & (P5S_A_SAW | P5S_A_PULSE | P5S_B_SAW | P5S_B_TRI | P5S_B_PULSE)) && p->mix_noise < 0.2f) silent++;
    }
    /* semitones from the sheets' printed notes */
    struct { const char *id; int a, b; } t[] = { { "3-7", 48, 12 }, { "3-8", 36, 36 }, { "4-8", 30, 23 }, { "5-1", 0, 60 },
                                                 { "5-6", 0, 24 }, { "3-5", 12, 24 }, { "4-5", 24, 0 } };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        const p5_sheet_t *p = find(t[i].id);
        assert(p && p->a_semis == t[i].a && p->b_semis == t[i].b);
    }
    /* 5-1 Alien and 5-6 Resonance Bells rely on a self-oscillating filter (no oscillator shape on, or very high resonance) */
    assert(find("5-6")->reso > 0.9f);
    printf("%u programs, %u with no oscillator shape and no noise\n", n, silent);
    puts("ok");
    return 0;
}
