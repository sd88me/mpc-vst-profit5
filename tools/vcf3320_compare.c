/* The Rev 3 filter kernel (ma_vcf3320, analog/mpc_analog_p5.h) against ngspice of the same circuit (circuits/vcf3320*.cir): the same
 * cases as the decks. gcc -O2 -std=c11 -Ianalog tools/vcf3320_compare.c -lm -o build/vcf3320_compare && build/vcf3320_compare [fs] */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "mpc_analog.h"
#include "mpc_analog_p5.h"

static double FS = 96000;
static const double W0 = 0.0857 / (90909.09 * 150e-12);   /* the deck's cells: gain a / (R_eq C), rad/s */
static const double WH = 1 / (2.2e-6 * 68e3);              /* C4165 / R4468 */
static uint32_t unconv;
static float KC(void) { return (float)(1 - exp(-1 / (FS * 10e-6 * 3e3))); }
static float gt(double w) { return (float)tan(w / (2 * FS)); }

static double gain(double icr, double f) {
    ma_vcf3320_t v = {0};
    const double a = 1e-5 * 90.9e3 * 1e-3;                  /* a small input: 1e-8 A x 90.9k */
    int n = (int)(1.5 * FS), cyc = (int)(0.25 * f), m0 = n - (cyc > 0 ? (int)lround(cyc * FS / f) : n / 4);
    double re = 0, im = 0;
    for (int i = 0; i < n; i++) {
        double ph = 2 * M_PI * f * i / FS;
        ma_vcf3320(&v, gt(W0), gt(WH), (float)icr, (float)(a * sin(ph)), KC(), &unconv);
        if (i >= m0) { re += v.o4 * sin(ph); im += v.o4 * cos(ph); }
    }
    return 2 * sqrt(re * re + im * im) / (n - m0) / a;
}

int main(int argc, char **argv) {
    if (argc > 1) FS = atof(argv[1]);
    static const double ics[] = { 0, 10e-6, 20e-6, 25e-6 };   /* below the oscillation onset (about 28 uA) */
    for (int k = 0; k < 4; k++) {
        double f3 = 0;
        for (double f = 300; f < 600 && ics[k] == 0; f += 0.1) if (gain(0, f) < 0.7071) { f3 = f; break; }
        printf("ROW %g %.5f %.5f %.5f %.1f\n", ics[k], gain(ics[k], 20), gain(ics[k], 100), gain(ics[k], 900), f3);
    }
    static const double osc[] = { 50e-6, 45e-6 };
    for (int k = 0; k < 2; k++) {
        ma_vcf3320_t v = {0};
        int n = (int)(0.6 * FS), m0 = (int)(0.5 * FS), cnt = 0;
        double first = -1, last = -1, pk4 = 0, pkb = 0, prev = 0;
        for (int i = 0; i < n; i++) {
            double t = i / FS, x = (t >= 1e-3 && t < 1.1e-3) ? 1e-6 * 90.9e3 : 0;
            float h = ma_vcf3320(&v, gt(W0), gt(WH), (float)osc[k], (float)x, KC(), &unconv);
            if (i >= m0) {
                if (prev < 0 && v.o4 >= 0) { double tc = i - 1 + prev / (prev - v.o4); if (first < 0) first = tc; else cnt++; last = tc; }
                if (v.o4 > pk4) pk4 = v.o4;
                if (h > pkb) pkb = h;
            }
            prev = v.o4;
        }
        printf("OSC %g %.2f %.4f %.3f\n", osc[k], cnt ? cnt * FS / (last - first) : 0, pk4, pkb);
    }
    printf("unconverged samples: %u\n", unconv);
    return 0;
}
