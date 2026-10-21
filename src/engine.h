/* Profit-5 engine: five-voice analog model of the early SSM-chip instrument, with a Rev 2 / Rev 4 voicing switch.
 * Plain C, no allocation after p5_init, no I/O, no locks. Parameters are 0..1 (VST convention); switches are on above 0.5. */
#pragma once
#include <stdint.h>
#include "../analog/mpc_analog.h"
#include "../analog/mpc_analog_p5.h"

#define P5_VOICES 5

/* Panel order: the 24 pots, then the 22 program switches, then the performance and global controls (append-only once released). */
enum {
    /* pots */
    P5P_PM_FENV, P5P_PM_OSCB, P5P_A_FREQ, P5P_A_PW, P5P_LFO_FREQ, P5P_WM_MIX, P5P_B_FREQ, P5P_B_FINE, P5P_B_PW, P5P_GLIDE,
    P5P_MIX_A, P5P_MIX_B, P5P_MIX_NOISE, P5P_CUTOFF, P5P_RESO, P5P_ENV_AMT, P5P_F_A, P5P_F_D, P5P_F_S, P5P_F_R,
    P5P_A_A, P5P_A_D, P5P_A_S, P5P_A_R,
    /* switches (same order as the P5S_* bits of presets_factory.h) */
    P5P_PM_DEST_FA, P5P_PM_DEST_PWA, P5P_PM_DEST_FILT, P5P_A_SAW, P5P_A_PULSE, P5P_A_SYNC, P5P_LFO_SAW, P5P_LFO_TRI, P5P_LFO_SQR,
    P5P_WM_FA, P5P_WM_FB, P5P_WM_PWA, P5P_WM_PWB, P5P_WM_FILT, P5P_B_SAW, P5P_B_TRI, P5P_B_PULSE, P5P_B_LO, P5P_B_KBD,
    P5P_F_KBD, P5P_RELEASE, P5P_UNISON,
    /* performance / global */
    P5P_REV,        /* < 0.5: Rev 2 (SSM 2030 / 2040 / 2050, 7-bit stepped pots); >= 0.5: Rev 4 (CEM-style, smooth pots, velocity, pressure) */
    P5P_VINTAGE,    /* 0..1: drift, component tolerance, tuning residuals */
    P5P_VEL_FILT, P5P_VEL_AMP, P5P_AT_FILT, P5P_AT_LFO,   /* Rev 4 only (ignored in Rev 2, which has no velocity or pressure) */
    P5P_TUNE,       /* master tune, 0.5 = A440, +-1 semitone */
    P5P_VOLUME,
    P5P_NVOICES,    /* 1..5 voices (0..1 -> 1..5) */
    P5P_QUALITY,    /* < 0.5: Eco 1x; >= 0.5: High 2x oversampled */
    P5P_COUNT
};

typedef struct {
    float ph[2], sync_corr, out_b;      /* oscillator phases (A, B), pending sync correction for A, last osc B sample */
    float semis, semis_target;          /* glided key pitch, semitones above the lowest key */
    int note, gate, age;
    float vel, press;
    ma_env_t fenv, aenv;
    float st[4], nl[4];                 /* filter */
    ma_drift_t drift[3];                /* osc A, osc B, cutoff */
    float dr[3];
    float tol[8];                       /* static per-voice tolerances (-1..1) */
    float tri_sym;
    int kick;
} p5_voice_t;

typedef struct {
    float fs, fsi;                      /* host rate, internal rate */
    int os;
    float p[P5P_COUNT];
    p5_voice_t v[P5_VOICES];
    int rr, clock;                      /* round-robin pointer, age counter */
    int held[16], nheld;                /* note stack for unison */
    float wheel, bend;                  /* 0..1, -1..1 */
    float lfo_ph, lfo_sh;
    uint32_t rng;
    ma_cv_t cv[10];
    ma_env_k_t fk, ak;
    float hb[MA_HB_B_M], hb_a[MA_HB_A_M];
    ma_hb_t dec;
    int ctl;                            /* control-rate counter */
    float pink, pink_c;
    uint32_t diag_nan;
} p5_engine_t;

void p5_init(p5_engine_t *e, float fs);
void p5_set(p5_engine_t *e, int id, float v);
void p5_note_on(p5_engine_t *e, int note, float vel);
void p5_note_off(p5_engine_t *e, int note);
void p5_all_off(p5_engine_t *e);
void p5_wheel(p5_engine_t *e, float v);
void p5_bend(p5_engine_t *e, float v);
void p5_pressure(p5_engine_t *e, int note, float v);   /* note < 0: channel pressure */
void p5_render(p5_engine_t *e, float *out, int n);    /* mono, adds nothing: writes n samples */
int  p5_load_factory(p5_engine_t *e, int index);       /* 0..37; returns 0 on success */
const char *p5_factory_name(int index);
int  p5_factory_count(void);
