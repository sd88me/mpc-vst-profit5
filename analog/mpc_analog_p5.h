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

/* ---- SSM 2020 control port wired as a diode (control collector and both bases tied, emitter grounded) fed through R from a CV ----
 * Solves CV = I R + V_T ln(I / I_s) for I (A) by Newton from the larger of the two asymptotes. ASSUMED: I_s = 2.5e-15 A, so the
 * transistor drops 0.65 V at 240 uA (a typical small NPN; the datasheet gives none). Below about 0.5 V the current falls off
 * exponentially instead of stopping. */
static inline float ma_2020_ctl(float cv, float r) {
    const float vt = 0.025693f, is = 2.5e-15f;
    float i = cv > 0.7f ? (cv - 0.65f) / r : is * expf(fminf(cv, 0.7f) / vt) * 0.5f;
    if (i < 1e-15f) i = 1e-15f;
    for (int k = 0; k < 6; k++) {                   /* f(i) = i r + vt ln(i / is) - cv, f' = r + vt / i */
        float fx = i * r + vt * logf(i / is) - cv, d = fx / (r + vt / i);
        i -= d;
        if (i < 1e-15f) i = 1e-15f;
        if (fabsf(d) < 1e-4f * i) break;
    }
    return i;
}

/* ---- SSM 2040 filter section as a circuit (Rev 2 voice SD411; docs/CIRCUIT.md "Filter", circuits/vcf2040.cir) ----
 * Four 2040 lowpass real-pole cells (datasheet Fig. 1): an OTA I = I_ABC tanh(V_IN / 2 V_T) into 1000 pF, a unity buffer, IN on a
 * 10k / 10k / 200 ohm network (stage 1: no R1; its input is a current). The swing is limited to +-1 V at CAP (datasheet: OUT swings
 * +-1 V; where the chip limits is assumed). OUT4 drives U479 (x20, clipped at +-13.5 V) = F; F through C413 0.1 uF / R471 100k
 * (15.9 Hz) is H, which feeds the final VCA and the RES FDBK VCA (SSM 2020: I_c H / 14 V into IN1).
 * Voltages are the circuit's. Inputs per sample: w0 = the cells' small-signal pole (2 pi f, rad/s; the 2040's I_ABC scales it),
 * kres = 10k I_c / 14 V (the loop gain is 20 kres: 4.45 at RES 10), x = the mixer's current into IN1 times 10k (volts).
 * Each sample is solved implicitly: the tanh of each cell is linearised (tanh z / z) at the last estimate, the linear zero-delay
 * loop is solved exactly, and that repeats until no cell's gain moves by 1e-5 (at most MA_VCF_ITER passes; *unconv counts the
 * rest). Returns H (volts). s[0..3]: the cells' integrator states, s[4]: the high-pass state. */
#ifndef MA_VCF_ITER
#define MA_VCF_ITER 12
#endif
typedef struct { float s[5], q[4], o4; } ma_vcf2040_t;   /* q: each cell's tanh(z) / z at its last operating point; o4: OUT4 (probe) */
static inline float ma_tanhz_z(float z) {                 /* tanh(z) / z: Lambert's (7,6) continued fraction, capped at 1 / |z| where it
                                                            would pass 1 (|z| ~ 5). Continuous, within 1e-4 of tanh; no exp, one division */
    float z2 = z * z;
    float r = (135135.0f + z2 * (17325.0f + z2 * (378.0f + z2))) / (135135.0f + z2 * (62370.0f + z2 * (3150.0f + 28.0f * z2)));
    float a = fabsf(z);
    return a * r > 1.0f ? 1.0f / a : r;
}
static inline float ma_vcf2040(ma_vcf2040_t *f, float g0, float gh, float kres, float x, uint32_t *unconv) {
    /* g0 = tan(w0 T / 2) (prewarped at the small-signal pole), gh = tan(w_hp T / 2); the cells' u / 2 V_T scale per volt of
     * (in + out): r / 10k / (2 V_T) with r = 192.3 ohm (stages 2-4) and 196.1 ohm (stage 1) */
    const float kz = 192.31f / 10000.0f / (2 * 0.025693f), kz1 = 196.08f / 10000.0f / (2 * 0.025693f);
    const float w1 = 196.08f / 192.31f;                  /* stage 1's pole against the others' (its IN node lacks R1) */
    if (f->q[0] == 0) f->q[0] = f->q[1] = f->q[2] = f->q[3] = 1;   /* a cleared state: the cells at rest */
    float Gh = gh / (1 + gh), y[4], xin = x, F = 0, H = 0, G[4];
    int it;
    for (it = 0; it < MA_VCF_ITER; it++) {
        float S[4], A = 1, B = 0;
        for (int i = 0; i < 4; i++) {
            float g = g0 * (i ? 1.0f : w1) * f->q[i];
            G[i] = g / (1 + g);
            S[i] = (1 - G[i]) * f->s[i];
            A *= G[i];
            B = -G[i] * B + S[i];                         /* y_i = -G_i y_(i-1) + S_i: B is y4 with the input at 0 */
        }
        /* x1 = x + kres H, H = (1 - Gh) (F - s_h), F = -20 y4 (unless clipped); y4 = A x1 + B */
        float c = kres * (1 - Gh);
        float n4 = (A * (x - c * f->s[4]) + B) / (1 + 20 * c * A);
        F = -20 * n4;
        if (F > 13.5f || F < -13.5f) F = F > 0 ? 13.5f : -13.5f;
        H = (1 - Gh) * (F - f->s[4]);
        xin = x + kres * H;
        /* the cells' operating points this solution gives; converged when none of their gains moves (another pass would
         * return the same solution) */
        float z[4], q[4], dq = 0;
        y[0] = -G[0] * xin + S[0];
        z[0] = kz1 * (xin + y[0]);
        for (int i = 1; i < 4; i++) { y[i] = -G[i] * y[i - 1] + S[i]; z[i] = kz * (y[i - 1] + y[i]); }
        for (int i = 0; i < 4; i++) q[i] = ma_tanhz_z(z[i]);
        for (int i = 0; i < 4; i++) { float d = fabsf(q[i] - f->q[i]); if (d > dq) dq = d; f->q[i] = q[i]; }
        if (dq < 1e-4f) break;
    }
#ifdef MA_VCF_COUNT
    extern unsigned long ma_vcf_passes, ma_vcf_samples; ma_vcf_passes += it + (it < MA_VCF_ITER); ma_vcf_samples++;
#endif
    if (it == MA_VCF_ITER) ++*unconv;
    /* commit with the gains the solution used: the cells' states (CAP limited to +-1 V), the high-pass state */
    float prev = xin;
    for (int i = 0; i < 4; i++) {
        float v = (-prev - f->s[i]) * G[i], yy = v + f->s[i];
        if (yy > 1) yy = 1; else if (yy < -1) yy = -1;
        f->s[i] = yy + v;
        if (f->s[i] > 1) f->s[i] = 1; else if (f->s[i] < -1) f->s[i] = -1;
        prev = yy;
    }
    f->o4 = prev;
    float vh = (F - f->s[4]) * Gh;
    f->s[4] = vh + (vh + f->s[4]);
    return H;
}

/* ---- CEM 3320 filter section as a circuit (Rev 3 voice SD432, TM1000D; the Rev 4 reissue's "Rev 3" filter; circuits/vcf3320.cir) ----
 * Four 3320 sections (V3320 datasheet, the CEM 3320's pin-compatible clone): a current-in, current-out gain cell into its pole cap and
 * a buffer; I_out = (I_ref - I_in) A e^(-Vc/V_T), I_ref = 63 uA. Rev 3's network: 100k feedback (90.9k with the cell's reflected 1M),
 * 91k coupling (stage gain -0.999), the inputs' DC balanced by 240k from -15 V; stage 1's input is a current (the mixer, the Q VCA).
 * ASSUMED: each cell's input current limits softly at +-I_ref (I_ref tanh(i / I_ref)); the buffers' swing +-6 V around 6.9 V (Vcc - 3 V
 * peak to peak). Resonance: OUT D through 2.2 uF / 68k (1.06 Hz) into U475 (x3.4, +-13.5 V), then 51k into Q IN (3.6k || 3k: x0.0311);
 * the Q VCA gives -I_cr tanh(v / 0.1 V) (gm 1 mmho at 100 uA) into stage 1. Voltages are the circuit's, against the quiescent point.
 * Inputs per sample: g0 = tan(w0 T / 2) (the stages' small-signal pole), gh = tan(w_hp T / 2) (the 1.06 Hz coupling), icr = the Q VCA's
 * control current (A), x = the mixer's current into stage 1 times 90.9k (volts), kc = 1 - exp(-T / (10 uF x 3k)) (C4146's step, updated a
 * sample late: its corner is 2.5 Hz). Returns U475's output (volts). Solved as ma_vcf2040. */
typedef struct { float s[5], q[5], o4, vc; } ma_vcf3320_t;   /* q[0..3]: cells' tanh(z) / z; q[4]: the Q VCA's; vc: C4146 */
static inline float ma_vcf3320(ma_vcf3320_t *f, float g0, float gh, float icr, float x, float kc, uint32_t *unconv) {
    /* Q IN node: vb through 51k, 3.6k to ground, 3k to C4146: vq = qdiv vb + qoff, with qoff from C4146's voltage */
    const float rf = 90.9e3f, iref = 63e-6f, kz = 1.0f / (rf * iref), alpha = 90.9f / 91.0f, rpar = 1.0f / (1 / 51e3f + 1 / 3.6e3f + 1 / 3e3f),
                qdiv = rpar / 51e3f, qoff = f->vc * rpar / 3e3f, vq0 = 0.1f;
    if (f->q[0] == 0) f->q[0] = f->q[1] = f->q[2] = f->q[3] = f->q[4] = 1;
    float Gh = gh / (1 + gh), y[4], xin = x, H = 0, G[4];
    int it;
    for (it = 0; it < MA_VCF_ITER; it++) {
        float S[4], A = 1, B = 0;
        for (int i = 0; i < 4; i++) {
            float g = g0 * f->q[i];
            G[i] = g / (1 + g);
            S[i] = (1 - G[i]) * f->s[i];
            float a = i ? alpha * G[i] : G[i];
            A *= a;
            B = -a * B + S[i];                         /* y_i = -G_i (alpha) y_(i-1) + S_i */
        }
        /* x1 = x - rf Kq vb, vb = 3.4 (1 - Gh) (y4 - s_h) (U475, clipped), Kq = I_cr / 0.1 V x tanh(u)/u x 0.0311 */
        float kq = rf * icr / vq0 * f->q[4] * qdiv, c = kq * 3.4f * (1 - Gh);
        float n4 = (A * (x - rf * icr / vq0 * f->q[4] * qoff + c * f->s[4]) + B) / (1 + A * c);
        float vb = 3.4f * (1 - Gh) * (n4 - f->s[4]);
        if (vb > 13.5f || vb < -13.5f) vb = vb > 0 ? 13.5f : -13.5f;
        H = vb;
        float u = (qdiv * vb + qoff) / vq0;
        xin = x - rf * icr / vq0 * f->q[4] * (qdiv * vb + qoff);
        float z[5], q[5], dq = 0;
        y[0] = -G[0] * xin + S[0];
        z[0] = kz * (xin + y[0]);
        for (int i = 1; i < 4; i++) { y[i] = -G[i] * alpha * y[i - 1] + S[i]; z[i] = kz * (alpha * y[i - 1] + y[i]); }
        z[4] = u;
        for (int i = 0; i < 5; i++) q[i] = ma_tanhz_z(z[i]);
        for (int i = 0; i < 5; i++) { float d = fabsf(q[i] - f->q[i]); if (d > dq) dq = d; f->q[i] = q[i]; }
        if (dq < 1e-4f) break;
    }
#ifdef MA_VCF_COUNT
    extern unsigned long ma_vcf_passes, ma_vcf_samples; ma_vcf_passes += it + (it < MA_VCF_ITER); ma_vcf_samples++;
#endif
    if (it == MA_VCF_ITER) ++*unconv;
    float prev = xin;
    for (int i = 0; i < 4; i++) {
        float in = i ? alpha * prev : prev;
        float v = (-in - f->s[i]) * G[i], yy = v + f->s[i];
        if (yy > 6) yy = 6; else if (yy < -6) yy = -6;
        f->s[i] = yy + v;
        if (f->s[i] > 6) f->s[i] = 6; else if (f->s[i] < -6) f->s[i] = -6;
        prev = yy;
    }
    f->o4 = prev;
    float vh = (prev - f->s[4]) * Gh;
    f->s[4] = vh + (vh + f->s[4]);
    f->vc += kc * (qdiv * H + qoff - f->vc);
    return H;
}
