/* Proposed additions to mpc_analog.h for the Profit-5 port (draft, offline only, not yet in morpho-PE's master copy).
 * Checked against the Rev 1 service manual schematics (board 4: SSM 2030 VCO, SSM 2040 VCF, SSM 2050 EG, SSM 2020 VCA).
 * Header only, no allocation, no globals; same conventions as mpc_analog.h (include it first: uses ma_tanh). */
#pragma once
#include <math.h>
#include <stdint.h>

/* ---- control-voltage path: 7-bit ADC/DAC, multiplexed sample-and-hold ----
 * The CPU reads each pot with a 7-bit ADC and writes the value back through the CV DAC into a per-parameter S&H, refreshed
 * once per main-loop pass. Service manual 3.3: every S&H moves in 1/12 V steps; a pot at 10 reads about 10 V (121 steps),
 * the oscillator frequency pots are halved by the firmware to 5 V = 60 semitone steps. Each step is rounded by the S&H RC.
 * q: steps (121 for authentic pots, 61 for frequency, 0 = continuous); tau_s: S&H/filter RC in seconds. */
typedef struct { float y, a; } ma_cv_t;
static inline void ma_cv_init(ma_cv_t *c, float tau_s, float fs) { c->y = 0; c->a = 1 - expf(-1 / (tau_s * fs)); }
static inline float ma_cv_quant(float x01, int q) { return q > 1 ? floorf(x01 * (q - 1) + 0.5f) / (q - 1) : x01; }
static inline float ma_cv_tick(ma_cv_t *c, float target) { c->y += (target - c->y) * c->a; return c->y; }

/* ---- analog drift: per-voice slow random walk (about 1/f over 0.05-5 Hz) plus a fixed tolerance ----
 * Three one-pole-filtered noise sources at octave-spaced corners, summed: cheap pink-ish wander. Call at a control rate
 * (rate_hz = update rate, for example fs/16); returns about -1..1 (unit std), scale outside (cents or octaves). */
typedef struct { float s[3], c[3]; uint32_t rng; } ma_drift_t;
static inline float ma_drift_rnd(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return (float)(int32_t)*r * (1.0f / 2147483648.0f); }
static inline void ma_drift_init(ma_drift_t *d, uint32_t seed, float rate_hz) {
    static const float hz[3] = { 0.08f, 0.6f, 4.0f };
    d->rng = seed * 2654435761u + 1;
    for (int i = 0; i < 3; i++) { d->s[i] = 0; d->c[i] = 1 - expf(-6.2831853f * hz[i] / rate_hz); }
}
static inline float ma_drift_tick(ma_drift_t *d) {
    static const float w[3] = { 1.0f, 0.5f, 0.25f };   /* 1/f-like weights */
    float y = 0;
    for (int i = 0; i < 3; i++) {
        d->s[i] += d->c[i] * (ma_drift_rnd(&d->rng) - d->s[i]);
        /* a one-pole filtered uniform has std 0.577 sqrt(c / (2 - c)): normalise each band to unit std */
        y += w[i] * d->s[i] / (0.577f * sqrtf(d->c[i] / (2 - d->c[i])));
    }
    return y * (1.0f / 1.1456f);                        /* sqrt(1 + 0.25 + 0.0625) */
}

/* ---- RC envelope generator (SSM 2050 style ADSR; board 4 ICs 1-10, 0.1 uF timing cap, per-voice RC trim) ----
 * Each stage is a capacitor charging toward a target through a CV-controlled current: attack aims past the top (over,
 * assumed 1.3 until measured) and switches to decay at 1, so attack is the concave-down start of an RC curve; decay and
 * release are true exponentials toward sustain / 0. Times are the 0 -> 1 (attack) or 1 -> 1/e^k (k below) times in seconds. */
enum { MA_ENV_IDLE, MA_ENV_A, MA_ENV_D, MA_ENV_R };
typedef struct { float y; int st; } ma_env_t;
typedef struct { float ca, cd, cr, s, over; } ma_env_k_t;
static inline float ma_env_coef(float t_s, float fs, float span) {   /* one-pole coefficient so that 'span' time constants take t_s */
    return 1 - expf(-span / (t_s * fs + 1e-9f));
}
static inline void ma_env_set(ma_env_k_t *k, float a_s, float d_s, float s, float r_s, float over, float fs) {
    k->over = over; k->s = s;
    k->ca = ma_env_coef(a_s, fs, -logf(1 - 1 / over));    /* reaches 1 (of target over) in a_s */
    k->cd = ma_env_coef(d_s, fs, 4.6f);                   /* -40 dB toward sustain in d_s */
    k->cr = ma_env_coef(r_s, fs, 4.6f);
}
static inline void ma_env_gate(ma_env_t *e, int on) { e->st = on ? MA_ENV_A : (e->st == MA_ENV_IDLE ? MA_ENV_IDLE : MA_ENV_R); }
static inline float ma_env_tick(ma_env_t *e, const ma_env_k_t *k) {
    switch (e->st) {
    case MA_ENV_A: e->y += (k->over - e->y) * k->ca; if (e->y >= 1) { e->y = 1; e->st = MA_ENV_D; } break;
    case MA_ENV_D: e->y += (k->s - e->y) * k->cd; break;
    case MA_ENV_R: e->y -= e->y * k->cr; if (e->y < 1e-5f) { e->y = 0; e->st = MA_ENV_IDLE; } break;
    default: break;
    }
    return e->y;
}

/* ---- SSM 2040 lowpass (Rev 1/2 voice, board 4 IC 78) ----
 * Four OTA integrator stages, each fed through a 10k/200 attenuator (about -34 dB, so the differential pair limits at a
 * signal of about 1), resonance through an SSM 2020 VCA from the output back to the input summer. No passband compensation:
 * the bass drops by 1 / (1 + k) as resonance rises, as on the instrument. Same structure as ma_ota_lpf (zero-delay feedback,
 * an OTA that limits lowers its own cutoff) with a harder input knee and harder stage limiting, and no input gain.
 * G0 from ma_ota_G; k: 0..~4.3 (self-oscillates from 4). st[4], nl[4]: states. */
static inline float ma_ssm2040_lpf(float *st, float *nl, float G0, float k, float in) {
    float Gs[4], a = 1, b = 0;
    for (int j = 0; j < 4; j++) {
        float n = nl[j];
        Gs[j] = G0 * (1 - n) * (1 + n * G0 * (1 + n * G0));
        b = Gs[j] * b + (1 - Gs[j]) * st[j];
        a *= Gs[j];
    }
    float prev = 1.4f * ma_tanh((in - k * b) / (1 + k * a) * (1.0f / 1.4f));
    for (int j = 0; j < 4; j++) {
        float vv = (prev - st[j]) * Gs[j], yy = vv + st[j];
        st[j] = yy + vv;
        if (fabsf(st[j]) < 1e-20f) st[j] = 0;
        float u = prev - yy;
        nl[j] = u * u / (3 + u * u);
        prev = yy;
    }
    return prev;
}

/* ---- self-oscillation seed: a real filter starts singing from its own noise; an ideal one fed silence never does ----
 * Add to the filter input when k is above the onset (about -70 dBFS). */
static inline float ma_selfosc_seed(uint32_t *rng, float k, float onset) {
    return k > onset ? 3e-4f * ma_drift_rnd(rng) : 0.0f;
}

/* ---- exponential glide (portamento as an RC on the pitch CV, constant time per interval) ---- */
static inline float ma_glide(float cur, float target, float coef) { return cur + (target - cur) * coef; }

/* ---- static per-voice component tolerance: a repeatable -1..1 value for (voice, part) ----
 * The instrument trims every voice by hand (manual 3.4: envelope RC, filter V/oct and initial frequency, oscillator initial
 * frequency, V/oct, high V/oct) and autotune then corrects only the oscillators' offset. Use for the residuals. */
static inline float ma_tol(unsigned voice, unsigned part) {
    uint32_t h = (voice * 0x9E3779B1u) ^ (part * 0x85EBCA77u);
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return (float)(int32_t)h * (1.0f / 2147483648.0f);
}
