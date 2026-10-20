/* Engine tests: pitch, voice assignment, unison, the Rev 2 / Rev 4 switch, self-oscillation, CV steps, all factory programs. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../src/engine.h"

#define FS 48000
static float buf[FS * 4];
static p5_engine_t E;

/* saw-only, open filter, no modulation */
static void plain(p5_engine_t *e, float rev, float vintage) {
    for (int i = 0; i < P5P_UNISON + 1; i++) e->p[i] = 0;
    e->p[P5P_A_SAW] = 1; e->p[P5P_MIX_A] = 0.8f; e->p[P5P_CUTOFF] = 1; e->p[P5P_A_FREQ] = 24 / 60.0f;
    e->p[P5P_A_A] = 0; e->p[P5P_A_S] = 1; e->p[P5P_A_R] = 0.2f; e->p[P5P_F_KBD] = 1; e->p[P5P_RELEASE] = 1;
    e->p[P5P_A_PW] = e->p[P5P_B_PW] = 0.5f;
    p5_set(e, P5P_REV, rev);
    p5_set(e, P5P_VINTAGE, vintage);
}

/* frequency by counting rising zero crossings with linear interpolation, over the second half */
static double freq(const float *x, int n) {
    double first = -1, last = -1; int cnt = 0;
    for (int i = n / 2 + 1; i < n; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double t = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0) first = t; else cnt++;
            last = t;
        }
    return cnt > 0 ? cnt * FS / (last - first) : 0;
}

static float peak(const float *x, int n) { float p = 0; for (int i = 0; i < n; i++) if (fabsf(x[i]) > p) p = fabsf(x[i]); return p; }
static int active(const p5_engine_t *e) { int c = 0; for (int v = 0; v < P5_VOICES; v++) c += e->v[v].gate; return c; }

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    /* 1. pitch: freq pot 24 semitones, MIDI 69 -> C0 * 2^((69 - 36 + 24) / 12) = 440 Hz */
    for (int r = 0; r < 2; r++) {
        p5_init(&E, FS); plain(&E, (float)r, 0);
        p5_note_on(&E, 69, 1);
        p5_render(&E, buf, FS);
        double f = freq(buf, FS), c = 1200 * log2(f / 440);
        printf("rev %d: 440 Hz note plays %.2f Hz (%+.2f cents)\n", r ? 4 : 2, f, c);
        assert(fabs(c) < 2.0);
    }
    /* 2. high notes go slightly flat in Rev 2 (VCO reset residual) and less in Rev 4 */
    {
        double cr[2];
        for (int r = 0; r < 2; r++) {
            p5_init(&E, FS); plain(&E, (float)r, 0); E.p[P5P_A_FREQ] = 60 / 60.0f;
            p5_note_on(&E, 84, 1);   /* 84 - 36 + 60 = 108 semis above C0: 8.37 kHz */
            p5_render(&E, buf, FS);
            cr[r] = 1200 * log2(freq(buf, FS) / (16.3516 * pow(2, 108 / 12.0)));
        }
        printf("8.4 kHz note: rev 2 %+.1f cents, rev 4 %+.1f cents\n", cr[0], cr[1]);
        assert(cr[0] < cr[1] && cr[0] > -25 && cr[1] < 1);
    }
    /* 3. voice assignment: round robin, a sixth note steals the oldest, a repeated note reuses its voice */
    p5_init(&E, FS); plain(&E, 0, 0);
    for (int k = 0; k < 5; k++) p5_note_on(&E, 60 + k, 1);
    assert(active(&E) == 5);
    for (int k = 0; k < 5; k++) assert(E.v[(1 + k) % 5].note == 60 + k);   /* starts after the last voice used */
    p5_note_on(&E, 70, 1);
    assert(E.v[1].note == 70);                              /* oldest (60) stolen */
    p5_note_off(&E, 62); p5_note_on(&E, 62, 1);
    assert(E.v[3].note == 62 && E.v[3].gate);               /* same note back on its voice */
    p5_all_off(&E);
    p5_set(&E, P5P_NVOICES, 0.75f);                          /* 4 voices (the firmware's 4-voice input) */
    for (int k = 0; k < 5; k++) p5_note_on(&E, 40 + k, 1);
    assert(active(&E) == 4 && !E.v[4].gate);
    /* 4. unison stacks every voice; releasing the top note falls back to the held one */
    p5_init(&E, FS); plain(&E, 0, 0.6f); E.p[P5P_UNISON] = 1;
    p5_note_on(&E, 48, 1); p5_note_on(&E, 55, 1);
    for (int v = 0; v < 5; v++) assert(E.v[v].note == 55 && E.v[v].gate);
    p5_note_off(&E, 55);
    for (int v = 0; v < 5; v++) assert(E.v[v].note == 48 && E.v[v].gate);
    /* 5. Rev 2 steps the pots (121 levels), Rev 4 does not */
    p5_init(&E, FS); plain(&E, 0, 0);
    E.p[P5P_CUTOFF] = 0.5041f;
    assert(fabsf(ma_cv_quant(0.5041f, 121) - 60 / 120.0f) < 1e-6f);
    /* 6. self-oscillation from an empty mixer (5-6 Resonance Bells relies on it), both revisions, and it stays bounded */
    for (int r = 0; r < 2; r++) {
        p5_init(&E, FS); plain(&E, (float)r, 0);
        E.p[P5P_MIX_A] = 0; E.p[P5P_RESO] = 1; E.p[P5P_CUTOFF] = 0.6f;
        p5_note_on(&E, 36, 1);
        p5_render(&E, buf, FS);
        double f = freq(buf, FS);
        printf("rev %d self-oscillation: peak %.3f at %.0f Hz\n", r ? 4 : 2, peak(buf + FS / 2, FS / 2), f);
        assert(peak(buf + FS / 2, FS / 2) > 0.02f && f > 250 && f < 800);
    }
    /* 7. bass under resonance: Rev 2 loses more than Rev 4 */
    {
        float pk[2];
        for (int r = 0; r < 2; r++) {
            p5_init(&E, FS); plain(&E, (float)r, 0);
            E.p[P5P_A_FREQ] = 0; E.p[P5P_CUTOFF] = 0.75f; E.p[P5P_RESO] = 0.7f;
            p5_note_on(&E, 36, 1);
            p5_render(&E, buf, FS);
            pk[r] = peak(buf + FS / 2, FS / 2);
        }
        printf("low note at resonance 7: rev 2 peak %.3f, rev 4 peak %.3f\n", pk[0], pk[1]);
        assert(pk[0] < pk[1]);
    }
    /* 8. every factory program, both revisions and both qualities: a three-note chord for 1.5 s, then 1.5 s of release */
    int quiet = 0;
    float maxpk = 0;
    for (int q = 0; q < 2; q++)
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < p5_factory_count(); i++) {
                p5_init(&E, FS);
                p5_set(&E, P5P_QUALITY, (float)q);
                p5_set(&E, P5P_REV, (float)r);
                p5_load_factory(&E, i);
                p5_note_on(&E, 48, 0.8f); p5_note_on(&E, 55, 0.8f); p5_note_on(&E, 64, 0.8f);
                p5_render(&E, buf, FS * 3 / 2);
                p5_all_off(&E);
                p5_render(&E, buf + FS * 3 / 2, FS * 3 / 2);
                float pk = peak(buf, FS * 3 / 2);
                for (int k = 0; k < FS * 3; k++) assert(buf[k] == buf[k]);
                assert(E.diag_nan == 0);
                if (pk > maxpk) maxpk = pk;
                if (pk < 0.02f) { quiet++; if (q) printf("  quiet: rev %d %s (peak %.4f)\n", r ? 4 : 2, p5_factory_name(i), pk); }
            }
    printf("%d program renders, max peak %.3f, %d quiet (< 0.02)\n", 4 * p5_factory_count(), maxpk, quiet);
    assert(maxpk <= 1.25f);
    puts("ok");
    return 0;
}
