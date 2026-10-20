#include <stdio.h>
#include <assert.h>
#ifndef MA_MASTER_HDR
#define MA_MASTER_HDR "../analog/mpc_analog.h"
#endif
#include MA_MASTER_HDR  /* synced copy of morpho-PE's master (analog/sync.sh) */
#include "../analog/mpc_analog_p5.h"
int main(void) {
    const float fs = 48000;
    /* envelope: attack reaches 1 in about a_s, decays to sustain, releases to idle */
    ma_env_t e = {0}; ma_env_k_t k; ma_env_set(&k, 0.010f, 0.2f, 0.5f, 0.3f, 1.3f, fs);
    ma_env_gate(&e, 1); int n = 0; while (e.st == MA_ENV_A) { ma_env_tick(&e, &k); n++; }
    printf("attack %.2f ms\n", n / fs * 1e3); assert(n > 0.009 * fs && n < 0.011 * fs);
    for (int i = 0; i < fs; i++) ma_env_tick(&e, &k);
    assert(fabsf(e.y - 0.5f) < 1e-3f);
    ma_env_gate(&e, 0); for (int i = 0; i < fs; i++) ma_env_tick(&e, &k);
    assert(e.st == MA_ENV_IDLE);
    /* ssm2040: self-oscillates at k 4.1 from the seed, stays bounded, rings near the cutoff */
    float st[4] = {0}, nl[4] = {0}, G = ma_ota_G(1000, fs), last = 0, peak = 0; uint32_t r = 1; int zc = 0;
    for (int i = 0; i < 2 * fs; i++) {
        float y = ma_ssm2040_lpf(st, nl, G, 4.1f, ma_selfosc_seed(&r, 4.1f, 3.6f));
        if (i > fs) { if (fabsf(y) > peak) peak = fabsf(y); if (last < 0 && y >= 0) zc++; }
        last = y; assert(y == y && fabsf(y) < 4);
    }
    printf("ssm2040 self-osc peak %.3f, %d Hz\n", peak, zc); fflush(stdout); assert(peak > 0.03f && zc > 800 && zc < 1200);
    /* drift: unit-ish std, bounded */
    ma_drift_t d; ma_drift_init(&d, 3, fs / 16); double s2 = 0; int N = 200000;
    for (int i = 0; i < N; i++) { float x = ma_drift_tick(&d); s2 += x * x; }
    printf("drift std %.2f\n", sqrt(s2 / N)); assert(sqrt(s2 / N) > 0.5 && sqrt(s2 / N) < 1.5);
    /* CV: 128 steps */
    assert(ma_cv_quant(0.5f, 128) == 64.0f / 127);
    /* SSM 2040: bass drops with resonance (no compensation): 100 Hz sine through a 2 kHz filter, k 0 vs k 3 */
    for (int kk = 0; kk < 2; kk++) {
        float s4[4] = {0}, n4[4] = {0}, Gb = ma_ota_G(2000, fs), pk = 0;
        for (int i = 0; i < fs; i++) { float y = ma_ssm2040_lpf(s4, n4, Gb, kk ? 3.0f : 0.0f, 0.1f * sinf(6.2831853f * 100 * i / fs)); if (i > fs / 2 && fabsf(y) > pk) pk = fabsf(y); }
        printf("ssm2040 100 Hz gain at k=%d: %.3f\n", kk ? 3 : 0, pk / 0.1f);
        if (kk) assert(pk / 0.1f < 0.3f); else assert(pk / 0.1f > 0.95f);
    }
    assert(ma_tol(1, 2) == ma_tol(1, 2) && ma_tol(1, 2) != ma_tol(2, 2) && fabsf(ma_tol(3, 4)) <= 1);
    puts("ok"); return 0;
}
