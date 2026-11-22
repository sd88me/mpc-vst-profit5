/* The circuit filter kernel (ma_vcf2040, analog/mpc_analog_p5.h) against ngspice of the same circuit (circuits/vcf2040*.cir).
 * Prints the same cases as the decks: small-signal gain of OUT4 (1 V equivalent input), and self-oscillation at RES 10.
 * gcc -O2 -std=c11 -Ianalog tools/vcf_compare.c -lm -o build/vcf_compare && build/vcf_compare [fs] */
#define _USE_MATH_DEFINES
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
static const double W0 = 10.28e-6 * 192.31 / (2 * 0.025693 * 1e4 * 1e-9);   /* I_ABC 10.28 uA: the cells' pole, rad/s */
static uint32_t unconv;

static float gt(double w) { return (float)tan(w / (2 * FS)); }
static double kres_of(double ic) { return 1e4 * ic / 14; }

/* small-signal gain of OUT4 at f, input amplitude a (volts at 10k), by lock-in over the last 0.25 s of 1 s */
static double gain(double ic, double f, double a) {
    ma_vcf2040_t v = {0};
    int n = (int)FS, cyc = (int)(0.25 * f), m0 = n - (cyc > 0 ? (int)lround(cyc * FS / f) : n / 4);   /* whole cycles */
    double re = 0, im = 0;
    for (int i = 0; i < n; i++) {
        double ph = 2 * M_PI * f * i / FS;
        ma_vcf2040(&v, gt(W0), gt(2 * M_PI * 15.915), (float)kres_of(ic), (float)(a * sin(ph)), &unconv);
        if (i >= m0) { re += v.o4 * sin(ph); im += v.o4 * cos(ph); }
    }
    return 2 * sqrt(re * re + im * im) / (n - m0) / a;
}

int main(int argc, char **argv) {
    if (argc > 1) FS = atof(argv[1]);
    printf("fs %.0f Hz, cells' pole %.1f Hz\n", FS, W0 / (2 * M_PI));
    static const double ics[] = { 0, 100e-6, 200e-6, 250e-6, 280e-6 };
    for (int k = 0; k < 5; k++) {
        double f3 = 0;
        for (double f = 200; f < 1000 && ics[k] == 0; f += 0.1) if (gain(0, f, 1e-3) < 0.7071) { f3 = f; break; }
        printf("ROW %g %.5f %.5f %.5f %.1f\n", ics[k], gain(ics[k], 20, 1e-3), gain(ics[k], 30, 1e-3), gain(ics[k], 566, 1e-3), f3);
    }
    /* self-oscillation at RES 10 and 290 uA: a 1 uA, 0.1 ms kick (0.01 V at 10k), 400 ms, measured over the last 100 ms */
    static const double osc[] = { 311.7e-6, 290e-6 };
    for (int k = 0; k < 2; k++) {
        ma_vcf2040_t v = {0};
        int n = (int)(0.4 * FS), m0 = (int)(0.3 * FS), cnt = 0;
        double first = -1, last = -1, pk4 = 0, pkf = 0, prev = 0;
        for (int i = 0; i < n; i++) {
            double t = i / FS, x = (t >= 1e-3 && t < 1.1e-3) ? 0.01 : 0;
            float h = ma_vcf2040(&v, gt(W0), gt(2 * M_PI * 15.915), (float)kres_of(osc[k]), (float)x, &unconv);
            if (i >= m0) {
                if (prev < 0 && v.o4 >= 0) { double tc = i - 1 + prev / (prev - v.o4); if (first < 0) first = tc; else cnt++; last = tc; }
                if (v.o4 > pk4) pk4 = v.o4;
                if (h > pkf) pkf = h;
            }
            prev = v.o4;
        }
        printf("OSC %g %.2f %.4f %.3f\n", osc[k], cnt ? cnt * FS / (last - first) : 0, pk4, pkf);
    }
    printf("unconverged samples: %u\n", unconv);
    return 0;
}
