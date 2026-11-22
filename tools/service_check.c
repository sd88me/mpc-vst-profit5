/* The service manual's checks as pass/fail (TM1000C Section 4; docs/SOURCES.md). Each case starts from BASIC TRIM SETUP (Table 4-0) as
 * the manual says, with Vintage 0 (a nominal, freshly trimmed unit). Pot positions are the manual's DVM readings over the DAC's 10 V.
 * gcc -O2 -std=c11 tools/service_check.c src/engine.c -lm -o build/service_check && build/service_check [model] */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/p5_engine.h"

#define FS 48000
static float buf[FS * 2];
static p5_engine_t E;
static int model = -1, bad;

/* Table 4-0: everything off or 0, except osc A saw on, MIX OSC A 10, CUTOFF 10, KEYBOARD on, AMP SUSTAIN 10, master tune centred */
static void table40(int rev) {
    p5_init(&E, FS);
#ifdef P5_MODEL_LATEST
    if (model >= 0) E.model = model;
#endif
    for (int i = 0; i < P5P_COUNT; i++) E.p[i] = 0;
    E.p[P5P_A_SAW] = 1; E.p[P5P_MIX_A] = 1; E.p[P5P_CUTOFF] = 1; E.p[P5P_F_KBD] = 1; E.p[P5P_A_S] = 1;
    E.p[P5P_TUNE] = 0.5f; E.p[P5P_VOLUME] = 0.8f; E.p[P5P_NVOICES] = 1; E.p[P5P_QUALITY] = 1; E.p[P5P_A_PW] = E.p[P5P_B_PW] = 0.5f;
    p5_set(&E, P5P_REV, (float)rev);
    p5_set(&E, P5P_VINTAGE, 0);
}

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

static void check(const char *para, const char *what, double got, double lo, double hi, const char *unit) {
    int ok = got >= lo && got <= hi;
    bad += !ok;
    printf("%-6s %-58s %10.3f %-4s [%g .. %g]  %s\n", para, what, got, unit, lo, hi, ok ? "PASS" : "FAIL");
}

/* 4-17 FILTER TUNING: osc A saw off, RESONANCE 10, cutoff CV 3.083 V (37 steps); C3 rings at A-440, C5 two octaves higher */
static double ring(int rev, float cutoff, int midi) {
    table40(rev);
    E.p[P5P_A_SAW] = 0; E.p[P5P_MIX_A] = 0; E.p[P5P_RESO] = 1; E.p[P5P_CUTOFF] = cutoff;
    p5_note_on(&E, midi, 1);
    p5_render(&E, buf, FS * 2);
    return freq(buf, FS * 2);
}

/* 4-10 / 4-11 ENVELOPE TIME CONSTANT: attack CV 6.250 V; the ATK period (gate to the end of the attack) is 0.9 s */
static double attack(int rev, int filt) {
    table40(rev);
    E.p[filt ? P5P_F_A : P5P_A_A] = 75 / 120.0f;    /* 6.250 V */
    E.p[filt ? P5P_F_S : P5P_A_S] = 0;
    p5_set(&E, P5P_A_A, E.p[P5P_A_A]);             /* recompute the envelope coefficients */
    p5_note_on(&E, 48, 1);
    int v = 0;
    while (v < P5_VOICES - 1 && !E.v[v].gate) v++;
    const ma_env_t *env = filt ? &E.v[v].fenv : &E.v[v].aenv;
    for (int n = 0; n < FS * 200; n += 16) {
        p5_render(&E, buf, 16);
        if (env->st != MA_ENV_A) return (n + 16) / (double)FS;
    }
    return 999;
}

int main(int argc, char **argv) {
    if (argc > 1) model = atoi(argv[1]);
    setvbuf(stdout, 0, _IONBF, 0);
    for (int rev = 0; rev < 1; rev++) {     /* the manual describes Rev 1/2; Rev 4 is not held to it */
        check("4-17", "filter rings at C3, cutoff 3.083 V, resonance 10 (Hz)", ring(rev, 37 / 120.0f, 72), 440 * 0.986, 440 * 1.014, "Hz");
        check("4-17", "filter rings at C5, two octaves up (Hz)", ring(rev, 37 / 120.0f, 96), 1760 * 0.986, 1760 * 1.014, "Hz");
        check("3.4", "filter rings 400-500 Hz at cutoff 6, lowest key (Hz)", ring(rev, 72 / 120.0f, 36), 400, 500, "Hz");
        check("4-10", "amp attack period at 6.250 V (s)", attack(rev, 0), 0.81, 0.99, "s");
        check("4-11", "filter attack period at 6.250 V (s)", attack(rev, 1), 0.81, 0.99, "s");
    }
    /* the Rev 4 setting's filter is the Rev 3 one: Rev 3 manual (TM1000D) 4-20, cutoff CV 2.000 V, A3 rings at 440 Hz, A4 at 880 Hz */
    check("D 4-20", "Rev 4 (3320): filter rings at A3, cutoff 2.000 V (Hz)", ring(1, 0.2f, 81), 440 * 0.986, 440 * 1.014, "Hz");
    check("D 4-20", "Rev 4 (3320): filter rings at A4 (Hz)", ring(1, 0.2f, 93), 880 * 0.986, 880 * 1.014, "Hz");
    printf("%d failed\n", bad);
    return bad != 0;
}
