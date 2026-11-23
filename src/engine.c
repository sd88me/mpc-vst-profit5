/* Profit-5 engine. See docs/ENGINE.md for every law and where it comes from (manual, firmware, or an unmeasured assumption).
 *
 * One voice: osc A (saw + pulse, hard sync to B) and osc B (saw + triangle + pulse, LO FREQ, KBD) -> mixer (A, B, white noise)
 * -> 4-pole lowpass (Rev 2: SSM 2040 OTA cascade, bass drops with resonance; Rev 4: CEM 3320 style, part of the bass given back)
 * -> VCA (amp envelope). Poly-Mod (filter env + osc B) to freq A / PW A / cutoff; Wheel-Mod (LFO <-> noise) to freq A / B, PW A / B,
 * cutoff. Every pot runs through the CV path: 121 steps in Rev 2 (the 7-bit DAC in 1/12 V steps), smooth in Rev 4, then an S&H RC. */
#include <math.h>
#include <string.h>
#include "p5_engine.h"
#include "presets_factory.h"

#define C0_HZ 16.3516f      /* frequency 0 puts the lowest key on C0 (manual 4.1-4.3) */
#define KEY_LOW 36          /* MIDI note of the lowest key */
/* model 1 filter trims (manual 4-17): the Rev 2 filter rings at 440 Hz on C3 and 1760 Hz on C5 at cutoff 3.083 V and full
 * resonance. Set by tools/service_trim.py (2026-11-22, with ma_vcf2040). */
#define CUT_TRIM -0.02460f   /* octaves (R407 INIT FREQ) */
#define CUT_KV 1.00679f     /* x the cutoff CV (R406 V/OCT; its range is 1.000-1.098) */
/* model 1: H (volts) to the engine's level: 0.45 (2.0.0's open filter, one saw at mix 10) / (20 x 0.272 V) */
#define VCF_OUT (0.45f / (20 * 0.02906f * 9.35f))
/* model 1, Rev 4 (3320) filter trims (Rev 3 manual 4-20: cutoff CV 2.000 V, A3 rings at 440 Hz and A4 at 880 Hz); tools/service_trim.py */
#define CUT4_TRIM 0.05953f
#define CUT4_KV 0.99835f
/* model 1, Rev 4 (ESTIMATES, set halfway between the 3.0 beta and a black-box render of GForce's Prophet 5, filter "3", 2026-11-23): the RES CV's resistor into the
 * Q VCA (the Rev 3 schematic's 200k put self-oscillation at RES 6; GForce has it at 6.5-7) and the Q compensation's exponent (0.68 restored
 * most of the bass; GForce loses about 5 dB at RES 5 with its Q comp on or off) */
#ifndef RES4_R
#define RES4_R 212e3f
#endif
#ifndef QCOMP4_EXP
#define QCOMP4_EXP 0.48f
#endif
/* model 1, Rev 4: one osc at 10 puts 0.25 x I_ref (63 uA) into stage 1 (ASSUMED: the Rev 3 CA3280 and Rev 4 SSI2164 mixer gains are not
 * read); x 90.9k gives the kernel's volts. Output: U475 to the engine's level, one saw at mix 10 through an open filter as loud as 2.0.0 */
#define VCF3_IN (0.25f * 63e-6f * 90.9e3f)
#define VCF3_OUT (0.45f / (3.4f * VCF3_IN))
#define CTL_MASK 15         /* control-rate work every 16 internal samples */

/* ---- voicing constants per revision (docs/ENGINE.md, "Rev 2 / Rev 4") ---- */
typedef struct {
    float t_reset;          /* VCO reset (flyback) time, s: the period grows by this, so pitch goes flat at the top */
    float hi_resid;         /* part of that error the HI V/OCT trim leaves */
    float saw_bow;          /* extra 2nd-harmonic bow of the saw (leaky charging) on top of ma_osc_saw's */
    float tri_sym, tri_glitch;  /* osc B triangle: symmetry error scale, peak glitch */
    float bias_cents;       /* autotune bias resolution residual, cents (+-half a step) */
    float drift_cents, drift_cut_oct, tol_cut_oct, tol_env;
    float atk_scale, dec_scale;
    int ssm_filter, cv_steps, touch;
    float pulse_amp;        /* pulse level against the saw: SSM 2030 7.5 Vpp against 10 Vpp (datasheet) */
} p5_rev_t;

static const p5_rev_t REV[2] = {
    /* Rev 2: SSM 2030 / 2040 / 2050. Bias step: "a little more than a semitone" over the firmware's 0..100 counts.
     * Reset: the 2030's saw fall time, 500 ns (datasheet). */
    { 0.5e-6f, 0.30f, 0.03f, 0.04f, 0.06f, 0.7f, 3.0f, 0.012f, 0.06f, 0.08f, 1.0f, 1.0f, 1, 1, 0, 0.75f },
    /* Rev 4: CEM-style VCO and filter, faster envelopes, smooth pots, velocity and pressure */
    { 0.2e-6f, 0.10f, 0.0f, 0.0f, 0.0f, 0.2f, 1.5f, 0.006f, 0.03f, 0.03f, 0.6f, 0.7f, 0, 0, 1, 1.0f },
};

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float fexp2(float x) {                  /* 2^x, about 1e-4 relative */
    float fi = floorf(x), f = x - fi;
    float p = 1 + f * (0.6951786f + f * (0.2261557f + f * 0.0782086f));
    return ldexpf(p, (int)fi);
}
static inline float ftan(float x) { float x2 = x * x; return x * (15 - x2) / (15 - 6 * x2); }   /* tan, Pade, x <= 1.26 */
static inline float rnd(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return (float)(int32_t)*r * (1.0f / 2147483648.0f); }

static inline int sw(const p5_engine_t *e, int id) { return e->p[id] >= 0.5f; }
static inline const p5_rev_t *rev(const p5_engine_t *e) { return &REV[sw(e, P5P_REV)]; }
/* model 1 laws, derived from the Rev 2 schematics and the service manual (docs/CIRCUIT.md); Rev 4 keeps the 2.0.0 laws for now */
static inline int circ(const p5_engine_t *e) { return e->model >= 1 && !sw(e, P5P_REV); }
/* model 1, Rev 4: the reissue's Rev 3 filter (CEM 3320) as a circuit; the rest of Rev 4 keeps the 2.0.0 laws */
static inline int circ4(const p5_engine_t *e) { return e->model >= 1 && sw(e, P5P_REV); }

/* pot -> CV: Rev 2 quantises to the DAC's 121 steps (manual 3.3: 1/12 V steps, 10 V full scale) */
static inline float pot(const p5_engine_t *e, int id) { return rev(e)->cv_steps ? ma_cv_quant(e->p[id], 121) : e->p[id]; }
/* oscillator frequency pots: 61 semitone steps in both revisions (the firmware halves them; the Rev 4 panel is also stepped) */
static inline float semis_pot(const p5_engine_t *e, int id) { return floorf(e->p[id] * 60 + 0.5f); }

/* SSM 2050 time constant (datasheet): 2 V per decade through the 100k input, positive = longer. The pot's CV spans 0..10.67 V
 * (the DAC), so 5.33 decades. Anchor (pot 0 = 0.5 ms) assumed; capped at 30 s. Attack approaches +13 V and the comparator ends it
 * at +10 V (target 1.3, time to the top 1.466 tau); decay and release are exponentials to sustain and to 0. */
static inline float env_tau(float d) { float t = 0.0005f * powf(10.0f, 5.333f * d); return t < 30.0f ? t : 30.0f; }
#define ATK(d) (1.4663f * env_tau(d))
#define DEC(d) (4.6f * env_tau(d))

/* Model 1 (Rev 2): SSM 2050 with C = 0.1 uF, 2 V per decade through 100k (datasheet), the time CVs x1.1 (SD313 "ENV GEN S/H GAIN
 * MODIF", S/N 590 and up) and the TC trim set so an attack CV of 6.250 V gives a 0.9 s attack (manual 4-10, 4-11). So the attack
 * period is 0.9 s x 10^(0.55 (V - 6.25)), V = pot volts; tau = period / 1.4663, limited to the 2050's typical 1 ms .. 100 s. */
static inline float env_tau_c(float d) {
    float t = (0.9f / 1.4663f) * powf(10.0f, 0.55f * (10 * d - 6.25f));
    return t < 0.001f / 1.4663f ? 0.001f / 1.4663f : t > 100.0f / 1.4663f ? 100.0f / 1.4663f : t;
}

static void env_coefs(p5_engine_t *e) {
    const p5_rev_t *r = rev(e);
    float rel = sw(e, P5P_RELEASE);
    if (circ(e)) {
#define ATKC(d) (1.4663f * env_tau_c(d))
#define DECC(d) (4.6f * env_tau_c(d))
        /* RELEASE off: the firmware writes 0x14 (20 steps, 1.667 V) to both release S&Hs instead of the pots (v7.0c 0x0380, pot
         * indices 3 and 7, switch bit 0x103A.6 or the footswitch) */
        const float roff = DECC(20 / 120.0f);
        ma_env_set(&e->fk, ATKC(pot(e, P5P_F_A)), DECC(pot(e, P5P_F_D)), pot(e, P5P_F_S), rel ? DECC(pot(e, P5P_F_R)) : roff, 1.3f, e->fsi);
        ma_env_set(&e->ak, ATKC(pot(e, P5P_A_A)), DECC(pot(e, P5P_A_D)), pot(e, P5P_A_S), rel ? DECC(pot(e, P5P_A_R)) : roff, 1.3f, e->fsi);
        return;
    }
    /* model 1, Rev 4 (ESTIMATE, 2026-11-24): decay and release x0.25 instead of x0.7. A black-box render of GForce's Prophet 5 showed
     * decay/release 0.27..0.43 of the 2.0.0 law's times at knobs 5, 8 and 10 (4.1 s against 15.1 s at 8; 19.7 against 51 at 10), attack within 25% */
    const float ds = circ4(e) ? 0.25f : r->dec_scale;
    ma_env_set(&e->fk, ATK(pot(e, P5P_F_A)) * r->atk_scale, DEC(pot(e, P5P_F_D)) * ds, pot(e, P5P_F_S),
               rel ? DEC(pot(e, P5P_F_R)) * ds : 0.005f, 1.3f, e->fsi);
    ma_env_set(&e->ak, ATK(pot(e, P5P_A_A)) * r->atk_scale, DEC(pot(e, P5P_A_D)) * ds, pot(e, P5P_A_S),
               rel ? DEC(pot(e, P5P_A_R)) * ds : 0.005f, 1.3f, e->fsi);
}

static void set_rate(p5_engine_t *e) {
    e->os = sw(e, P5P_QUALITY) ? 2 : 1;
    e->fsi = e->fs * e->os;
    e->pink_c = 1 - expf(-6.2831853f * 159.0f / e->fsi);
    e->hp16 = ftan(3.14159265f * 15.915f / e->fsi);           /* C413 / R471 */
    e->hp8 = 1 - expf(-6.2831853f * 7.96f / e->fsi);           /* C409 / R481, C408 / R439: the mixer inputs' coupling */
    e->hp1 = ftan(3.14159265f * 1.0638f / e->fsi);            /* Rev 3: C4165 / R4468 */
    e->kc3 = 1 - expf(-1 / (e->fsi * 10e-6f * 3e3f));         /* Rev 3: C4146 / R4424 */
    for (int i = 0; i < 10; i++) ma_cv_init(&e->cv[i], 0.002f, e->fsi);    /* S&H + CV filter RC, about 2 ms */
    e->cv_a2 = e->cv[0].a;
    e->cv_a4 = 1 - expf(-1 / (0.025f * e->fsi));   /* model 1, Rev 4: the reissue's pots are read finely; the MPC sends a knob turn as jumps
                                                   * tens of ms apart, which 2 ms leaves audible as steps: 25 ms joins them up */
    for (int v = 0; v < P5_VOICES; v++)
        for (int d = 0; d < 3; d++) ma_drift_init(&e->v[v].drift[d], 7 * v + d + 1, e->fsi / (CTL_MASK + 1));
    env_coefs(e);
}

void p5_init(p5_engine_t *e, float fs) {
    memset(e, 0, sizeof *e);
    e->fs = fs;
    e->rng = 0x1234567u;
    ma_hb_design_standard(e->hb_a, e->hb);
    for (int v = 0; v < P5_VOICES; v++) {
        p5_voice_t *x = &e->v[v];
        x->note = -1;
        for (int t = 0; t < 8; t++) x->tol[t] = ma_tol(v, t);
        x->ph[0] = 0.13f * v; x->ph[1] = 0.29f * v;   /* free-running oscillators: phases differ per voice */
    }
    e->p[P5P_VINTAGE] = 0.6f;
    e->p[P5P_TUNE] = 0.5f;
    e->p[P5P_VOLUME] = 0.8f;
    e->p[P5P_NVOICES] = 1.0f;
    e->p[P5P_QUALITY] = P5_DEFAULT_QUALITY ? 1.0f : 0.0f;
    e->p[P5P_RELEASE] = 1.0f;
    e->model = P5_MODEL_LATEST;
    e->qcomp4 = 1;
    for (int i = 0; i <= 256; i++) e->fvca[i] = ma_2020_ctl(10.0f * i / 256, 15e3f) / ma_2020_ctl(10, 15e3f);   /* the final VCA's law */
    p5_load_factory(e, 0);
    set_rate(e);
}

void p5_set(p5_engine_t *e, int id, float v) {
    if (id < 0 || id >= P5P_COUNT) return;
    int was_q = sw(e, P5P_QUALITY);
    e->p[id] = clampf(v, 0, 1);
    if (id == P5P_QUALITY && sw(e, P5P_QUALITY) != was_q) set_rate(e);
    else if ((id >= P5P_F_A && id <= P5P_A_R) || id == P5P_RELEASE || id == P5P_REV) env_coefs(e);
}

/* ---- voice assignment (firmware 0x045C-0x0557): reuse a voice already on this note, else the next free voice after the last one
 * used (round robin), else steal the oldest. Unison stacks every voice on the newest held note. ---- */
static int nvoices(const p5_engine_t *e) { return 1 + (int)floorf(e->p[P5P_NVOICES] * 4 + 0.5f); }

static void voice_start(p5_engine_t *e, p5_voice_t *x, int note, float vel, int legato_glide) {
    float target = (float)(note - KEY_LOW);
    if (x->note < 0 || !legato_glide) x->semis = target;    /* first note of a voice starts in tune */
    x->semis_target = target;
    if (x->note < 0) x->semis = target;
    x->note = note;
    x->vel = vel;
    x->gate = 1;
    x->age = ++e->clock;
    x->kick = 1;
    ma_env_gate(&x->fenv, 1);
    ma_env_gate(&x->aenv, 1);
}

void p5_note_on(p5_engine_t *e, int note, float vel) {
    if (note < 0 || note > 127) return;
    int n = nvoices(e);
    /* note stack */
    for (int i = 0; i < e->nheld; i++)
        if (e->held[i] == note) { memmove(&e->held[i], &e->held[i + 1], (e->nheld - i - 1) * sizeof(int)); e->nheld--; break; }
    if (e->nheld == 16) { memmove(&e->held[0], &e->held[1], 15 * sizeof(int)); e->nheld--; }
    e->held[e->nheld++] = note;
    if (sw(e, P5P_UNISON)) {
        for (int v = 0; v < n; v++) voice_start(e, &e->v[v], note, vel, 1);
        return;
    }
    int pick = -1;
    for (int v = 0; v < n; v++) if (e->v[v].note == note) { pick = v; break; }
    if (pick < 0)
        for (int k = 1; k <= n; k++) { int v = (e->rr + k) % n; if (!e->v[v].gate) { pick = v; break; } }
    if (pick < 0) {
        int oldest = 0;
        for (int v = 1; v < n; v++) if (e->v[v].age < e->v[oldest].age) oldest = v;
        pick = oldest;
    }
    e->rr = pick;
    voice_start(e, &e->v[pick], note, vel, 1);
}

void p5_note_off(p5_engine_t *e, int note) {
    for (int i = 0; i < e->nheld; i++)
        if (e->held[i] == note) { memmove(&e->held[i], &e->held[i + 1], (e->nheld - i - 1) * sizeof(int)); e->nheld--; break; }
    int n = nvoices(e);
    if (sw(e, P5P_UNISON)) {
        if (e->nheld > 0) {                  /* fall back to the newest held note without retriggering */
            for (int v = 0; v < n; v++) { e->v[v].semis_target = (float)(e->held[e->nheld - 1] - KEY_LOW); e->v[v].note = e->held[e->nheld - 1]; }
            return;
        }
        for (int v = 0; v < n; v++) if (e->v[v].gate) { e->v[v].gate = 0; ma_env_gate(&e->v[v].fenv, 0); ma_env_gate(&e->v[v].aenv, 0); }
        return;
    }
    for (int v = 0; v < P5_VOICES; v++)
        if (e->v[v].note == note && e->v[v].gate) { e->v[v].gate = 0; ma_env_gate(&e->v[v].fenv, 0); ma_env_gate(&e->v[v].aenv, 0); }
}

void p5_all_off(p5_engine_t *e) {
    e->nheld = 0;
    for (int v = 0; v < P5_VOICES; v++) { e->v[v].gate = 0; ma_env_gate(&e->v[v].fenv, 0); ma_env_gate(&e->v[v].aenv, 0); }
}
void p5_wheel(p5_engine_t *e, float v) { e->wheel = clampf(v, 0, 1); }
void p5_bend(p5_engine_t *e, float v) { e->bend = clampf(v, -1, 1); }
void p5_pressure(p5_engine_t *e, int note, float v) {
    for (int i = 0; i < P5_VOICES; i++) if (note < 0 || e->v[i].note == note) e->v[i].press = clampf(v, 0, 1);
}

/* ---- oscillators ---- */
/* pitch error of a real exponential VCO: the flyback adds t_reset to each period; HI V/OCT trims most of it out */
static inline float vco_hz(const p5_rev_t *r, float hz, float tol) {
    float tr = r->t_reset * r->hi_resid * (1 + 0.5f * tol);
    return hz / (1 + hz * tr);
}

/* osc B triangle: the 2030's triangle through the symmetry-trimmed buffer (IC 57). Rev 2 keeps a small symmetry error and the
 * glitch at the peak the manual calls normal; Rev 4 is the clean ma_osc_tri. */
static inline float tri_b(float ph, float inc, float sym, float glitch) {
    if (sym == 0 && glitch == 0) return ma_osc_tri(ph, inc);
    float h = 0.5f + sym;                       /* peak position */
    float t = ph < h ? -1 + 2 * ph / h : 1 - 2 * (ph - h) / (1 - h);
    /* slope changes at 0 and h, band-limited (coefficients are half the slope change, see ma_polyblamp) */
    float ds = 2 / h + 2 / (1 - h);
    float t2 = ph - h; if (t2 < 0) t2 += 1;
    t += 0.5f * ds * (ma_polyblamp(ph, inc) - ma_polyblamp(t2, inc));
    float g = ph - h; if (g < 0) g += 1;
    if (g < 0.01f) t += glitch * (1 - g * 100);   /* a short spike after the peak */
    return t;
}

static inline float saw_bow(float s, float bow) { return s + bow * (1 - s * s) - bow * (2.0f / 3); }

/* ---- render ---- */
void p5_render(p5_engine_t *e, float *out, int n) {
    const p5_rev_t *r = rev(e);
    const int nv = nvoices(e), touch = r->touch;
    const float fsi = e->fsi, inv = 1.0f / fsi;
    /* block-rate values */
    const float vint = e->p[P5P_VINTAGE];
    const float tune = (e->p[P5P_TUNE] - 0.5f) * 2 + e->bend * 2;           /* +-1 semitone tune, +-2 bend (assumption) */
    const float a_semis = semis_pot(e, P5P_A_FREQ), b_semis = semis_pot(e, P5P_B_FREQ);
    const int cm = circ(e), c4 = circ4(e);
    /* fine: model 1 = R3107 10M into 100k, 0.01 x 10 V = 1.2 semitones (SD312); model 0 = 1 semitone (user report 2026-10-28) */
    const float b_fine = pot(e, P5P_B_FINE) * (cm ? 1.2f : 1.0f);                                  /* 0..+1 semitone (user report 2026-10-28: the original spans about a semitone) */
    const int a_saw = sw(e, P5P_A_SAW), a_pul = sw(e, P5P_A_PULSE), sync = sw(e, P5P_A_SYNC);
    const int b_saw = sw(e, P5P_B_SAW), b_tri = sw(e, P5P_B_TRI), b_pul = sw(e, P5P_B_PULSE), b_lo = sw(e, P5P_B_LO), b_kbd = sw(e, P5P_B_KBD);
    const int pm_fa = sw(e, P5P_PM_DEST_FA), pm_pw = sw(e, P5P_PM_DEST_PWA), pm_fi = sw(e, P5P_PM_DEST_FILT);
    const int wm_fa = sw(e, P5P_WM_FA), wm_fb = sw(e, P5P_WM_FB), wm_pwa = sw(e, P5P_WM_PWA), wm_pwb = sw(e, P5P_WM_PWB), wm_fi = sw(e, P5P_WM_FILT);
    const int l_saw = sw(e, P5P_LFO_SAW), l_tri = sw(e, P5P_LFO_TRI), l_sqr = sw(e, P5P_LFO_SQR);
    const float lfo_hz = 0.5f * fexp2(5.333f * pot(e, P5P_LFO_FREQ));         /* the LFO is a 2030 at 1 V/oct whose CV is halved (100k/100k,
                                                                                 board 3): 5.33 octaves; 0.5 Hz anchor assumed, 20 Hz top */
    const float wm_mix = pot(e, P5P_WM_MIX);
    const float glide = pot(e, P5P_GLIDE);
    /* Glide is a CA3080 OTA charging 0.1 uF (board 3 p3): a constant slew rate, its current set exponentially by the glide CV
     * (AD820 pair), with the OTA's tanh softening the last half semitone. Rate 3000 .. 10 semitones/s over the pot (range assumed). */
    const float glide_rate = 3000.0f * powf(10.0f, -2.48f * glide) * inv;
    /* model 1 (Rev 2): "The GLIDE circuit, active only in UNISON mode" (manual 2, SD312 U379): no glide in poly mode */
    const int glide_on = glide >= 0.03f && !(circ(e) && !sw(e, P5P_UNISON));
    const int fkbd = sw(e, P5P_F_KBD), kbd_half = sw(e, P5P_KBD_HALF);
    const float vel_f = touch ? e->p[P5P_VEL_FILT] : 0, vel_a = touch ? e->p[P5P_VEL_AMP] : 0;
    const float at_f = touch ? e->p[P5P_AT_FILT] : 0, at_l = touch ? e->p[P5P_AT_LFO] : 0;
    const float master = 0.5f * e->p[P5P_VOLUME] * e->p[P5P_VOLUME] * 2.2f;
    float targets[10] = { pot(e, P5P_CUTOFF), pot(e, P5P_RESO), pot(e, P5P_ENV_AMT), pot(e, P5P_MIX_A), pot(e, P5P_MIX_B),
                          pot(e, P5P_MIX_NOISE), pot(e, P5P_A_PW), pot(e, P5P_B_PW), pot(e, P5P_PM_FENV), pot(e, P5P_PM_OSCB) };
    float maxp = 0;
    for (int v = 0; v < nv; v++) if (e->v[v].press > maxp) maxp = e->v[v].press;

    {
        const float a = e->model >= 1 && sw(e, P5P_REV) ? e->cv_a4 : e->cv_a2;
        for (int k = 0; k < 10; k++) e->cv[k].a = a;
    }
    for (int i = 0; i < n; i++) {
        float acc[2] = { 0, 0 };
        for (int o = 0; o < e->os; o++) {
            float c[10];
            for (int k = 0; k < 10; k++) c[k] = ma_cv_tick(&e->cv[k], targets[k]);
            /* LFO (shapes summed, as on the panel) and the wheel-mod source */
            e->lfo_ph += lfo_hz * inv;
            if (e->lfo_ph >= 1) e->lfo_ph -= 1;
            float lfo = (l_saw ? 2 * e->lfo_ph - 1 : 0) + (l_tri ? (e->lfo_ph < 0.5f ? 4 * e->lfo_ph - 1 : 3 - 4 * e->lfo_ph) : 0)
                      + (l_sqr ? (e->lfo_ph < 0.5f ? 1.0f : -1.0f) : 0);
            float white = rnd(&e->rng);
            e->pink += e->pink_c * (white - e->pink);                     /* one pole at 159 Hz: 100k, 0.01 uF (board 3 p3) */
            float wm_src = lfo * (1 - wm_mix) + e->pink * 6 * wm_mix;   /* 2020 crossfade on SOURCE MIX, then the wheel pot */
            float wm = wm_src * clampf(e->wheel + e->p[P5P_LFO_INIT] + at_l * maxp, 0, 1);
            /* model 1 (Rev 2), ESTIMATE from SD312: the W-MOD bus in volts. The LFO's shapes average through 10k into 200k (saw and
             * triangle +-5 V, the square 0-7.5 V: 0.75 of them), the SOURCE MIX 2020s' control currents through 10k (LFO: 10 V - SRC MIX;
             * noise: SRC MIX) against their full 0.935 mA, the bus (R364 100k || R360 4.7k + R374 4.7k || the wheel's 100k: 8.4k) and the
             * wheel network (x0.489): 1.31 V peak for one shape, the LFO VCA full and the wheel at 10. The pink noise keeps 2.0.0's level
             * against the LFO. Destinations from the master summers: freq x0.549 (182k), PW A x7.52, PW B x6.67, cutoff x7.52 oct per V. */
            float wmv = 0;
            if (circ(e)) {
                int nl = l_saw + l_tri + l_sqr;
                float lfo_v = nl ? ((l_saw ? 2 * e->lfo_ph - 1 : 0) + (l_tri ? (e->lfo_ph < 0.5f ? 4 * e->lfo_ph - 1 : 3 - 4 * e->lfo_ph) : 0)
                                    + (l_sqr ? (e->lfo_ph < 0.5f ? 0.75f : -0.75f) : 0)) * (1.05f / (nl + 0.05f)) : 0;
                wmv = 1.31f * (lfo_v * e->vca[7] + e->pink * 6 * e->vca[8]) * clampf(e->wheel + e->p[P5P_LFO_INIT] + at_l * maxp, 0, 1);
            }
            int ctl = (e->ctl++ & CTL_MASK) == 0;
            if (c4 && ctl) {   /* Rev 4 Q compensation (ESTIMATE, below): k = 90.9k x gm(I_cr) x 0.0311 x 3.4, gm the 3320 sheet's Fig. 6 */
                float icr = 10 * c[1] / RES4_R;
                e->qcomp4 = powf(1 + 9611 * 2.24e-3f * icr / (icr + 122.5e-6f), QCOMP4_EXP);
            }
            if (cm && ctl) {   /* model 1: the 2020s' control currents (mixer 39k, resonance 30k), at the control rate */
                for (int k = 0; k < 3; k++) e->vca[k] = ma_2020_ctl(10 * c[3 + k], 39e3f) * (39e3f * 0.02906f);
                e->vca[3] = ma_2020_ctl(10 * c[1], 30e3f) * (1e4f / 14);
                /* Poly-Mod (SD411 voice 3/4: P-MOD OSC B AMT VCA, control 75k; P-MOD FILT ENV AMT VCA, control 39k; their currents into
                 * R432 47k): volts on the P-MOD bus per volt of osc B node X and per volt of filter envelope. FILT ENV AMT VCA (control
                 * 62k): its current into the cutoff summer's virtual ground, in octaves (x 100k) per envelope volt. */
                e->vca[4] = ma_2020_ctl(10 * c[9], 75e3f) * (47e3f / 14);
                e->vca[5] = ma_2020_ctl(10 * c[8], 39e3f) * (47e3f / 14);
                e->vca[6] = ma_2020_ctl(10 * c[2], 62e3f) * (1e5f / 14);
                e->vca[7] = ma_2020_ctl(10 * (1 - wm_mix), 10e3f) / ma_2020_ctl(10, 10e3f);   /* SOURCE MIX: the LFO VCA (ESTIMATE, above) */
                e->vca[8] = ma_2020_ctl(10 * wm_mix, 10e3f) / ma_2020_ctl(10, 10e3f);         /* and the noise VCA */
            }
            const float k = 4.4f * c[1];
            float sum = 0;
            for (int v = 0; v < nv; v++) {
                p5_voice_t *x = &e->v[v];
                if (x->aenv.st == MA_ENV_IDLE && !x->gate) continue;
                if (ctl) for (int d = 0; d < 3; d++) x->dr[d] = ma_drift_tick(&x->drift[d]);
                if (glide_on) x->semis += glide_rate * ma_tanh((x->semis_target - x->semis) * 2); else x->semis = x->semis_target;
                float fe = ma_env_tick(&x->fenv, &e->fk) * (1 - vel_f * (1 - x->vel));
                float ae = ma_env_tick(&x->aenv, &e->ak) * (1 - vel_a * (1 - x->vel));
                /* pitch, semitones above C0 */
                float cents_a = vint * (r->drift_cents * x->dr[0] + r->bias_cents * x->tol[0]);
                float cents_b = vint * (r->drift_cents * x->dr[1] + r->bias_cents * x->tol[1]);
                float pm_src = c[8] * fe;                                           /* filter-env part of poly-mod */
                float pm_b = c[9] * x->out_b;                                       /* osc B part (audio rate) */
                /* model 1: the P-MOD bus (U427 follower of R432, +-13.5 V), from osc B's node X of the last sample (DC included) */
                const float vpm = cm ? clampf(e->vca[4] * x->pmx + e->vca[5] * 10 * fe, -13.5f, 13.5f) : 0;
                float sa = x->semis + a_semis + tune + cents_a * 0.01f + (pm_fa ? (cm ? 4 * vpm : 48 * pm_src + 24 * pm_b) : 0) + (wm_fa ? (cm ? 6.59f * wmv : 4 * wm) : 0);
                float sb = (b_kbd ? x->semis : 0) + b_semis + b_fine + tune + cents_b * 0.01f + (wm_fb ? (cm ? 6.59f * wmv : 4 * wm) : 0);
                float hz_a = vco_hz(r, C0_HZ * fexp2(sa * (1.0f / 12)), x->tol[2]);
                float hz_b = C0_HZ * fexp2(sb * (1.0f / 12)) * (b_lo ? (cm ? 1.0f / 32 : 1.0f / 128) : 1.0f);   /* LO: model 1 = 5 octaves (SD312 divider), 0 = 7 */
                if (!b_lo) hz_b = vco_hz(r, hz_b, x->tol[3]);
                float inc_a = clampf(hz_a * inv, 0, 0.45f), inc_b = clampf(hz_b * inv, 0, 0.45f);
                float pw_a = clampf(c[6] + (pm_pw ? (cm ? 0.332f * vpm : 0.5f * (pm_src + pm_b)) : 0) + (wm_pwa ? (cm ? 0.752f * wmv : 0.4f * wm) : 0), 0, 1);
                float pw_b = clampf(c[7] + (wm_pwb ? (cm ? 0.667f * wmv : 0.4f * wm) : 0), 0, 1);
                /* osc B (master) */
                float pb = x->ph[1] + inc_b, ob = 0;
                int wrap = pb >= 1;
                if (wrap) pb -= 1;
                x->ph[1] = pb;
                float bs = b_saw ? saw_bow(ma_osc_saw(pb, inc_b), r->saw_bow) : 0;
                float bt = b_tri ? tri_b(pb, inc_b, r->tri_sym * x->tol[4], r->tri_glitch) : 0;
                float bp = b_pul ? r->pulse_amp * ma_osc(MA_PULSE, pb, inc_b, pw_b) : 0;
                if (b_saw) ob += bs;
                if (b_tri) ob += bt;
                if (b_pul) ob += bp;
                if (cm) {
                    /* model 1, osc B's output network (SD411, voices 3/4): saw (0..10 V) and pulse (0..7.5 V, manual 2-23) through 10k each into
                     * R434 200k and the U442 buffer, then R438 10k to node X; the triangle (+-5 V) into X through R433 3.3k. X feeds the
                     * P-MOD OSC B VCA (DC coupled) and, through C408 / 200k (8 Hz), the mixer VCA */
                    int m = b_saw + b_pul;
                    float yb = m ? (5 * (bs + bp) + 5.0f * b_saw + 3.75f * b_pul) / (m + 0.05f) : 0;
                    float xb = b_tri ? 0.2481f * yb + 0.7519f * 5 * bt : yb;
                    x->pmx = xb;
                    x->hpb += e->hp8 * (xb - x->hpb);
                    x->mixb = (xb - x->hpb) * (b_tri ? 0.988f : 0.952f);
                }
                x->out_b = ob;
                /* osc A (slave), shapes summed; sync resets it when B wraps */
                float pa = x->ph[0] + inc_a;
                if (pa >= 1) pa -= 1;
                float oa = x->sync_corr;
                x->sync_corr = 0;
                if (a_saw) oa += saw_bow(ma_osc_saw(pa, inc_a), r->saw_bow);
                if (a_pul) oa += r->pulse_amp * ma_osc(MA_PULSE, pa, inc_a, pw_a);
                if (sync && wrap && (a_saw || a_pul)) {
                    float d = inc_b > 0 ? pb / inc_b : 0, now, next;
                    if (d > 1) d = 1;
                    if (a_saw) { ma_sync(MA_SAW, 0, pa, inc_a, d, &now, &next); oa += now; x->sync_corr += next; }
                    if (a_pul) { ma_sync(MA_PULSE, pw_a, pa, inc_a, d, &now, &next); oa += r->pulse_amp * now; x->sync_corr += r->pulse_amp * next; }
                    pa = inc_a * d;
                }
                x->ph[0] = pa;
                if (cm) {
                    /* model 1, osc A's output network: saw and pulse through 10k each (R456, R457) into C409 / R481 200k (8 Hz): the
                     * shapes average (1 / (n + 0.05) each) rather than add */
                    int na = a_saw + a_pul;
                    float va = na ? (5 * oa + 5.0f * a_saw + 3.75f * a_pul) / (na + 0.05f) : 0;
                    x->hpa += e->hp8 * (va - x->hpa);
                    x->mixa = va - x->hpa;
                }
                /* mixer (2020 VCAs) */
                float in = 0.45f * (c[3] * oa + c[4] * ob + c[5] * white * 0.8f);
                /* cutoff: 450 Hz at pot 6 on the lowest key with KBD on (manual 3.4), 10.5 octaves over the pot (assumption) */
                /* model 1: 10 octaves over the pot (FILT SUM CV 0-10 V at ~1 oct/V, SD312 / SD411) and trimmed as manual 4-17: cutoff
                 * CV 3.083 V on C3 (36 semitones up) rings at 440 Hz; CUT_TRIM is the 2040 model's ring / cutoff ratio at full resonance */
                float oct = (cm ? 10.0f * (c[0] - 37 / 120.0f) - 3 - 0.03242f : c4 ? 10.0f * (c[0] - 0.2f) - 3.75f - 0.03242f : 10.5f * (c[0] - 0.6f)) + (fkbd ? x->semis * (kbd_half ? 1.0f / 24 : 1.0f / 12) : 0) + (cm ? e->vca[6] * 10 * fe : 8 * c[2] * fe)
                          + (pm_fi ? (cm ? 1.821f * vpm : 6 * pm_src + 5 * pm_b) : 0) + (wm_fi ? (cm ? 7.52f * wmv : 3 * wm) : 0) + at_f * 3 * x->press
                          + vint * (r->drift_cut_oct * x->dr[2] + r->tol_cut_oct * x->tol[5]);
                if (cm) oct = CUT_KV * oct + CUT_TRIM; else if (c4) oct = CUT4_KV * oct + CUT4_TRIM;    /* R406 V/OCT and R407 INIT FREQ, trimmed as manual 4-17 */
                float hz = clampf(450.0f * fexp2(oct), 8, 0.4f * fsi);
                if (cm) {
                    /* model 1: the filter section as a circuit (ma_vcf2040; docs/CIRCUIT.md, circuits/vcf2040.cir). The mixer's current
                     * into IN1 (x 10k, volts): each 2020's I_c from its CV through 39k and its diode-wired control port (ma_2020_ctl, ~(CV -
                     * 0.65) / 39k above 1 V), the osc (+-5 V saw) x0.952 into its input, the bus (100k || 200k) into R476: 0.02906 V per
                     * 39k x I_c per unit of osc (0.272 V for one osc at 10). Noise keeps its 2.0.0 level against the oscillators (its
                     * source level is not read yet). The resonance VCA: kres = 10k I_c / 14, I_c through 30k. Circuit noise (about 10 uV)
                     * starts the self-oscillation. e->vca[] is updated at the control rate. */
                    float xin = (e->vca[0] * x->mixa + e->vca[1] * x->mixb) * (1 / 4.76f) + e->vca[2] * white * 0.8f + 1e-5f * rnd(&e->rng);
                    float kr = e->vca[3];
                    float h = ma_vcf2040(&x->vcf, ftan(3.14159265f * hz * inv), e->hp16, kr, xin, &e->diag_unconv);
                    if (!(h == h) || fabsf(h) > 20) { memset(&x->vcf, 0, sizeof x->vcf); e->diag_nan++; h = 0; }
                    /* the final VCA (ESTIMATE): its 2020 controlled through R474 15k from the amp envelope (0-10 V) with the diode law, so
                     * the gain falls away exponentially below about 0.6 V instead of linearly; one saw at mix 10, open, as loud as 2.0.0 */
                    float ai = ae * 256; int ak = ai >= 256 ? 255 : ai <= 0 ? 0 : (int)ai; float af = clampf(ai - ak, 0, 1);
                    sum += h * VCF_OUT * (e->fvca[ak] + af * (e->fvca[ak + 1] - e->fvca[ak]));
                    continue;
                }
                if (c4) {
                    /* model 1, Rev 4: the Rev 3 filter section (ma_vcf3320; docs/CIRCUIT.md "Rev 3 filter"). Q VCA control: RES CV / 200k */
                    float xin = VCF3_IN * (c[3] * oa + c[4] * ob + c[5] * white * 0.8f) + 1e-5f * rnd(&e->rng);
                    float h = ma_vcf3320(&x->vcf3, ftan(3.14159265f * hz * inv), e->hp1, 10 * c[1] / RES4_R, xin, e->kc3, &e->diag_unconv);
                    if (!(h == h) || fabsf(h) > 20) { memset(&x->vcf3, 0, sizeof x->vcf3); e->diag_nan++; h = 0; }
                    /* ESTIMATE: the reissue's Q compensation (maffez.com: an SSI2164 cell on the Q CV adds level at the 3320's output; R274
                     * 16.2k, "more" at 10-12k) restores 0.68 (11 / 16.2) of the passband's loss in dB: (1 + k)^0.68, k the loop gain.
                     * It restores the passband, so it is applied to the output's bass (a one-pole lowpass two octaves under the cutoff),
                     * not to the whole output: the resonant peak and self-oscillation keep the 3320's own level (3.0 beta scaled
                     * everything, up to +11.7 dB, which made a jump and overdrive once the filter rang, above RES 6) */
                    float gq = ftan(3.14159265f * 0.25f * hz * inv), lq = (h - x->qlp) * (gq / (1 + gq));
                    float bass = x->qlp + lq;
                    x->qlp = bass + lq;
                    if (!(x->qlp == x->qlp)) x->qlp = 0;
                    sum += (h + (e->qcomp4 - 1) * bass) * VCF3_OUT * ae;
                    continue;
                }
                float g = ftan(3.14159265f * hz * inv), G0 = g / (1 + g);
                if (k > 3.6f) { in += ma_selfosc_seed(&e->rng, k, 3.6f); if (x->kick) in += 0.01f; }
                x->kick = 0;
                float y = r->ssm_filter ? ma_ssm2040_lpf(x->st, x->nl, G0, k, in) : ma_ota_lpf(x->st, x->nl, G0, k, 1, in);
                if (!(y == y) || fabsf(y) > 8) { memset(x->st, 0, sizeof x->st); memset(x->nl, 0, sizeof x->nl); e->diag_nan++; y = 0; }
                sum += y * ae;
            }
            acc[o] = sum;
        }
        float y = e->os == 2 ? ma_hb_dec(&e->dec, e->hb, MA_HB_B_M, acc[0], acc[1]) : acc[0];
        out[i] = ma_tanh(y * master);                        /* soft limiter, ceiling 1.0 */
    }
}

/* ---- factory programs (tools/factory_sheets.txt) ---- */
int p5_factory_count(void) { return (int)(sizeof P5_FACTORY / sizeof *P5_FACTORY); }
const char *p5_factory_name(int i) { return i >= 0 && i < p5_factory_count() ? P5_FACTORY[i].name : ""; }

int p5_load_factory(p5_engine_t *e, int i) {
    if (i < 0 || i >= p5_factory_count()) return -1;
    const p5_sheet_t *s = &P5_FACTORY[i];
    const float pots[24] = { s->pm_fenv, s->pm_oscb, s->a_semis / 60.0f, s->a_pw, s->lfo_freq, s->wm_mix, s->b_semis / 60.0f, s->b_fine,
                             s->b_pw, s->glide, s->mix_a, s->mix_b, s->mix_noise, s->cutoff, s->reso, s->env_amt, s->f_a, s->f_d,
                             s->f_s, s->f_r, s->a_a, s->a_d, s->a_s, s->a_r };
    for (int k = 0; k < 24; k++) e->p[P5P_PM_FENV + k] = pots[k];
    for (int b = 0; b < 22; b++) e->p[P5P_PM_DEST_FA + b] = (s->sw >> b) & 1 ? 1.0f : 0.0f;
    env_coefs(e);
    return 0;
}
