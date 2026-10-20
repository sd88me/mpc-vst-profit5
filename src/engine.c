/* Profit-5 engine. See docs/ENGINE.md for every law and where it comes from (manual, firmware, or an unmeasured assumption).
 *
 * One voice: osc A (saw + pulse, hard sync to B) and osc B (saw + triangle + pulse, LO FREQ, KBD) -> mixer (A, B, white noise)
 * -> 4-pole lowpass (Rev 2: SSM 2040 OTA cascade, bass drops with resonance; Rev 4: CEM 3320 style, part of the bass given back)
 * -> VCA (amp envelope). Poly-Mod (filter env + osc B) to freq A / PW A / cutoff; Wheel-Mod (LFO <-> noise) to freq A / B, PW A / B,
 * cutoff. Every pot runs through the CV path: 121 steps in Rev 2 (the 7-bit DAC in 1/12 V steps), smooth in Rev 4, then an S&H RC. */
#include <math.h>
#include <string.h>
#include "engine.h"
#include "presets_factory.h"

#define C0_HZ 16.3516f      /* frequency 0 puts the lowest key on C0 (manual 4.1-4.3) */
#define KEY_LOW 36          /* MIDI note of the lowest key */
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
} p5_rev_t;

static const p5_rev_t REV[2] = {
    /* Rev 2: SSM 2030 / 2040 / 2050. Bias step: "a little more than a semitone" over the firmware's 0..100 counts. */
    { 3.0e-6f, 0.20f, 0.03f, 0.04f, 0.06f, 0.7f, 3.0f, 0.012f, 0.06f, 0.08f, 1.0f, 1.0f, 1, 1, 0 },
    /* Rev 4: CEM-style VCO and filter, faster envelopes, smooth pots, velocity and pressure */
    { 1.0e-6f, 0.10f, 0.0f, 0.0f, 0.0f, 0.2f, 1.5f, 0.006f, 0.03f, 0.03f, 0.6f, 0.7f, 0, 0, 1 },
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

/* pot -> CV: Rev 2 quantises to the DAC's 121 steps (manual 3.3: 1/12 V steps, 10 V full scale) */
static inline float pot(const p5_engine_t *e, int id) { return rev(e)->cv_steps ? ma_cv_quant(e->p[id], 121) : e->p[id]; }
/* oscillator frequency pots: 61 semitone steps in both revisions (the firmware halves them; the Rev 4 panel is also stepped) */
static inline float semis_pot(const p5_engine_t *e, int id) { return floorf(e->p[id] * 60 + 0.5f); }

/* envelope time law: 1.5 ms .. 12 s, exponential in the pot (assumption until measured) */
static inline float env_time(float d) { return 0.0015f * powf(10.0f, 3.9f * d); }

static void env_coefs(p5_engine_t *e) {
    const p5_rev_t *r = rev(e);
    float rel = sw(e, P5P_RELEASE);
    ma_env_set(&e->fk, env_time(pot(e, P5P_F_A)) * r->atk_scale, env_time(pot(e, P5P_F_D)) * r->dec_scale, pot(e, P5P_F_S),
               rel ? env_time(pot(e, P5P_F_R)) * r->dec_scale : 0.005f, 1.3f, e->fsi);
    ma_env_set(&e->ak, env_time(pot(e, P5P_A_A)) * r->atk_scale, env_time(pot(e, P5P_A_D)) * r->dec_scale, pot(e, P5P_A_S),
               rel ? env_time(pot(e, P5P_A_R)) * r->dec_scale : 0.005f, 1.3f, e->fsi);
}

static void set_rate(p5_engine_t *e) {
    e->os = sw(e, P5P_QUALITY) ? 2 : 1;
    e->fsi = e->fs * e->os;
    for (int i = 0; i < 10; i++) ma_cv_init(&e->cv[i], 0.002f, e->fsi);    /* S&H + CV filter RC, about 2 ms */
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
    e->p[P5P_QUALITY] = 1.0f;
    e->p[P5P_RELEASE] = 1.0f;
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
    const float b_fine = pot(e, P5P_B_FINE);                                  /* 0..+1 semitone */
    const int a_saw = sw(e, P5P_A_SAW), a_pul = sw(e, P5P_A_PULSE), sync = sw(e, P5P_A_SYNC);
    const int b_saw = sw(e, P5P_B_SAW), b_tri = sw(e, P5P_B_TRI), b_pul = sw(e, P5P_B_PULSE), b_lo = sw(e, P5P_B_LO), b_kbd = sw(e, P5P_B_KBD);
    const int pm_fa = sw(e, P5P_PM_DEST_FA), pm_pw = sw(e, P5P_PM_DEST_PWA), pm_fi = sw(e, P5P_PM_DEST_FILT);
    const int wm_fa = sw(e, P5P_WM_FA), wm_fb = sw(e, P5P_WM_FB), wm_pwa = sw(e, P5P_WM_PWA), wm_pwb = sw(e, P5P_WM_PWB), wm_fi = sw(e, P5P_WM_FILT);
    const int l_saw = sw(e, P5P_LFO_SAW), l_tri = sw(e, P5P_LFO_TRI), l_sqr = sw(e, P5P_LFO_SQR);
    const float lfo_hz = 0.1f * fexp2(8 * pot(e, P5P_LFO_FREQ));             /* 0.1 .. 25.6 Hz (assumption) */
    const float wm_mix = pot(e, P5P_WM_MIX);
    const float glide = pot(e, P5P_GLIDE);
    const float glide_c = glide < 0.03f ? 1.0f : 1 - expf(-1 / (0.002f * powf(10.0f, 3 * glide) * fsi));  /* RC, tau 2 ms .. 2 s */
    const int fkbd = sw(e, P5P_F_KBD);
    const float vel_f = touch ? e->p[P5P_VEL_FILT] : 0, vel_a = touch ? e->p[P5P_VEL_AMP] : 0;
    const float at_f = touch ? e->p[P5P_AT_FILT] : 0, at_l = touch ? e->p[P5P_AT_LFO] : 0;
    const float master = 0.5f * e->p[P5P_VOLUME] * e->p[P5P_VOLUME] * 2.2f;
    float targets[10] = { pot(e, P5P_CUTOFF), pot(e, P5P_RESO), pot(e, P5P_ENV_AMT), pot(e, P5P_MIX_A), pot(e, P5P_MIX_B),
                          pot(e, P5P_MIX_NOISE), pot(e, P5P_A_PW), pot(e, P5P_B_PW), pot(e, P5P_PM_FENV), pot(e, P5P_PM_OSCB) };
    float maxp = 0;
    for (int v = 0; v < nv; v++) if (e->v[v].press > maxp) maxp = e->v[v].press;

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
            e->pink += 0.02f * (white - e->pink);
            float wm_src = lfo * (1 - wm_mix) + e->pink * 4 * wm_mix;
            float wm = wm_src * clampf(e->wheel + at_l * maxp, 0, 1);
            int ctl = (e->ctl++ & CTL_MASK) == 0;
            const float k = 4.4f * c[1];
            float sum = 0;
            for (int v = 0; v < nv; v++) {
                p5_voice_t *x = &e->v[v];
                if (x->aenv.st == MA_ENV_IDLE && !x->gate) continue;
                if (ctl) for (int d = 0; d < 3; d++) x->dr[d] = ma_drift_tick(&x->drift[d]);
                x->semis += (x->semis_target - x->semis) * glide_c;
                float fe = ma_env_tick(&x->fenv, &e->fk) * (1 - vel_f * (1 - x->vel));
                float ae = ma_env_tick(&x->aenv, &e->ak) * (1 - vel_a * (1 - x->vel));
                /* pitch, semitones above C0 */
                float cents_a = vint * (r->drift_cents * x->dr[0] + r->bias_cents * x->tol[0]);
                float cents_b = vint * (r->drift_cents * x->dr[1] + r->bias_cents * x->tol[1]);
                float pm_src = c[8] * fe;                                           /* filter-env part of poly-mod */
                float pm_b = c[9] * x->out_b;                                       /* osc B part (audio rate) */
                float sa = x->semis + a_semis + tune + cents_a * 0.01f + (pm_fa ? 48 * pm_src + 24 * pm_b : 0) + (wm_fa ? 4 * wm : 0);
                float sb = (b_kbd ? x->semis : 0) + b_semis + 0.01f * 100 * b_fine + tune + cents_b * 0.01f + (wm_fb ? 4 * wm : 0);
                float hz_a = vco_hz(r, C0_HZ * fexp2(sa * (1.0f / 12)), x->tol[2]);
                float hz_b = C0_HZ * fexp2(sb * (1.0f / 12)) * (b_lo ? (1.0f / 128) : 1.0f);
                if (!b_lo) hz_b = vco_hz(r, hz_b, x->tol[3]);
                float inc_a = clampf(hz_a * inv, 0, 0.45f), inc_b = clampf(hz_b * inv, 0, 0.45f);
                float pw_a = clampf(c[6] + (pm_pw ? 0.5f * (pm_src + pm_b) : 0) + (wm_pwa ? 0.4f * wm : 0), 0, 1);
                float pw_b = clampf(c[7] + (wm_pwb ? 0.4f * wm : 0), 0, 1);
                /* osc B (master) */
                float pb = x->ph[1] + inc_b, ob = 0;
                int wrap = pb >= 1;
                if (wrap) pb -= 1;
                x->ph[1] = pb;
                if (b_saw) ob += saw_bow(ma_osc_saw(pb, inc_b), r->saw_bow);
                if (b_tri) ob += tri_b(pb, inc_b, r->tri_sym * x->tol[4], r->tri_glitch);
                if (b_pul) ob += ma_osc(MA_PULSE, pb, inc_b, pw_b);
                x->out_b = ob;
                /* osc A (slave), shapes summed; sync resets it when B wraps */
                float pa = x->ph[0] + inc_a;
                if (pa >= 1) pa -= 1;
                float oa = x->sync_corr;
                x->sync_corr = 0;
                if (a_saw) oa += saw_bow(ma_osc_saw(pa, inc_a), r->saw_bow);
                if (a_pul) oa += ma_osc(MA_PULSE, pa, inc_a, pw_a);
                if (sync && wrap && (a_saw || a_pul)) {
                    float d = inc_b > 0 ? pb / inc_b : 0, now, next;
                    if (d > 1) d = 1;
                    if (a_saw) { ma_sync(MA_SAW, 0, pa, inc_a, d, &now, &next); oa += now; x->sync_corr += next; }
                    if (a_pul) { ma_sync(MA_PULSE, pw_a, pa, inc_a, d, &now, &next); oa += now; x->sync_corr += next; }
                    pa = inc_a * d;
                }
                x->ph[0] = pa;
                /* mixer (2020 VCAs) */
                float in = 0.45f * (c[3] * oa + c[4] * ob + c[5] * white * 0.8f);
                /* cutoff: 450 Hz at pot 6 on the lowest key with KBD on (manual 3.4), 10.5 octaves over the pot (assumption) */
                float oct = 10.5f * (c[0] - 0.6f) + (fkbd ? x->semis * (1.0f / 12) : 0) + 8 * c[2] * fe
                          + (pm_fi ? 6 * pm_src + 5 * pm_b : 0) + (wm_fi ? 3 * wm : 0) + at_f * 3 * x->press
                          + vint * (r->drift_cut_oct * x->dr[2] + r->tol_cut_oct * x->tol[5]);
                float hz = clampf(450.0f * fexp2(oct), 8, 0.4f * fsi);
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
        out[i] = 1.25f * ma_tanh(y * master * 0.8f);
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
