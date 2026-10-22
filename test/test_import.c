/* Repro-5 .h2p import: the parser, the mapping onto Profit-5's panel, and the zip + DEFLATE reader (synthetic patches, test/fixture_import.h). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/p5_import.h"
#include "fixture_import.h"

static int near(float a, float b) { return fabsf(a - b) < 1e-4f; }
#define V(o, id) ((o).v[id])
#define HAS(o, id) (((o).mask >> (id)) & 1)

static char found[8][40];
static size_t flen[8];
static int nfound;
static int collect(const char *name, const uint8_t *d, size_t len, void *u) {
    (void)u;
    snprintf(found[nfound], sizeof found[0], "%s", name);
    flen[nfound++] = len;
    p5_import_t o;
    printf("  zip entry %s (%zu bytes) -> %s\n", name, len, p5_import_h2p((const char *)d, len, &o) ? "patch" : "not a Repro-5 patch");
    return 0;
}

static p5_import_t inv;
static int inv_ok;
static int pick_inverted(const char *name, const uint8_t *d, size_t len, void *u) {
    (void)u;
    if (strstr(name, "Inverted")) inv_ok = p5_import_h2p((const char *)d, len, &inv);
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    p5_import_t o;
    assert(p5_import_h2p(FIX_BRASS, strlen(FIX_BRASS), &o));
    /* filter, envelopes, glide: dial / 100 */
    assert(near(V(o, P5P_CUTOFF), 0.45f) && near(V(o, P5P_RESO), 0.30f) && near(V(o, P5P_ENV_AMT), 0.60f));
    assert(near(V(o, P5P_F_A), 0.10f) && near(V(o, P5P_F_D), 0.40f) && near(V(o, P5P_F_S), 0.30f) && near(V(o, P5P_F_R), 0.20f));
    assert(near(V(o, P5P_A_A), 0.05f) && near(V(o, P5P_A_S), 0.80f) && near(V(o, P5P_GLIDE), 0.15f));
    /* key amount 50: on, half */
    assert(V(o, P5P_F_KBD) == 1.f && V(o, P5P_KBD_HALF) == 1.f);
    /* oscillators: A at the key (0 semitones), B an octave plus 7 plus half a semitone = 19.5 -> 19 on the pot, 0.5 on the fine pot */
    assert(near(V(o, P5P_A_FREQ), 0.f) && V(o, P5P_A_SAW) == 1.f && V(o, P5P_A_PULSE) == 0.f);
    assert(near(V(o, P5P_B_FREQ), 19.f / 60.f) && near(V(o, P5P_B_FINE), 0.5f / 6.f));
    assert(V(o, P5P_B_SAW) == 1.f && V(o, P5P_B_TRI) == 0.f && V(o, P5P_B_PULSE) == 1.f && V(o, P5P_B_KBD) == 1.f && V(o, P5P_B_LO) == 0.f);
    assert(near(V(o, P5P_MIX_A), 0.8f) && near(V(o, P5P_MIX_B), 0.6f) && near(V(o, P5P_MIX_NOISE), 0.1f));
    /* poly-mod: sources, and the filter as its only destination */
    assert(near(V(o, P5P_PM_FENV), 0.25f) && near(V(o, P5P_PM_OSCB), 0.40f));
    assert(V(o, P5P_PM_DEST_FILT) == 1.f && V(o, P5P_PM_DEST_FA) == 0.f && V(o, P5P_PM_DEST_PWA) == 0.f);
    /* Chip 3 (Poly) is the CEM voicing: Rev 4, with the velocity depths and voice detune as vintage */
    assert(V(o, P5P_REV) == 1.f && near(V(o, P5P_VEL_FILT), 0.7f) && near(V(o, P5P_VEL_AMP), 0.6f) && near(V(o, P5P_VINTAGE), 0.35f));
    /* wheel-mod and LFO are reset, and master tune, volume, voices, quality and aftertouch are not part of a patch */
    /* LFO and wheel-mod: rate, shapes (an inverted saw is a saw), source mix, INIT AMT from the lower limit, destinations */
    assert(near(V(o, P5P_LFO_FREQ), 0.40f) && V(o, P5P_LFO_TRI) == 1.f && V(o, P5P_LFO_SAW) == 1.f && V(o, P5P_LFO_SQR) == 0.f);
    assert(near(V(o, P5P_WM_MIX), 0.25f) && near(V(o, P5P_LFO_INIT), 0.12f));
    assert(V(o, P5P_WM_FA) == 1.f && V(o, P5P_WM_FB) == 0.f && V(o, P5P_WM_PWA) == 0.f && V(o, P5P_WM_PWB) == 1.f && V(o, P5P_WM_FILT) == 1.f);
    {   p5_import_t z; p5_import_init(&z);
        assert(!strcmp(z.name, "Init") && V(z, P5P_LFO_INIT) == 0.f && V(z, P5P_CUTOFF) == 1.f && V(z, P5P_WM_FA) == 0.f); }
    assert(!HAS(o, P5P_TUNE) && !HAS(o, P5P_VOLUME) && !HAS(o, P5P_NVOICES) && !HAS(o, P5P_QUALITY) && !HAS(o, P5P_AT_FILT) && !HAS(o, P5P_AT_LFO));
    for (int i = 0; i < P5P_COUNT; i++) assert(V(o, i) >= 0.f && V(o, i) <= 1.f);

    /* not Repro-5, empty, binary garbage and a truncated number */
    assert(!p5_import_h2p("#AM=Diva\n#cm=Filter\nCutoff=50\n", 31, &o));
    assert(!p5_import_h2p("", 0, &o) && !p5_import_h2p("\x01\x02\x03", 3, &o));
    assert(p5_import_h2p("#cm=Filter\nCutoff=12x\nReso=40\n", 30, &o) && near(V(o, P5P_RESO), 0.4f) && near(V(o, P5P_CUTOFF), 1.f));

    /* the zip: a deflated entry, a stored one, a foreign patch and a text file that the suffix filter skips */
    {
        static uint8_t scratch[1 << 16];
        int n = p5_zip_walk(FIX_ZIP, sizeof FIX_ZIP, ".h2p", scratch, sizeof scratch, collect, NULL);
        assert(n == 3 && nfound == 3);
        assert(!strcmp(found[0], "bank/Test Brass.h2p") && flen[0] == sizeof FIX_BRASS - 1);
        assert(!strcmp(found[1], "bank/Inverted Env.h2p"));
        assert(p5_zip_walk(FIX_ZIP, sizeof FIX_ZIP - 5, ".h2p", scratch, sizeof scratch, collect, NULL) < 0);   /* no end record */
        assert(p5_zip_walk(FIX_ZIP, sizeof FIX_ZIP, ".h2p", scratch, 100, collect, NULL) == 0);                  /* too big for the scratch: skipped */
        uint8_t bad[sizeof FIX_ZIP];
        memcpy(bad, FIX_ZIP, sizeof bad);
        for (size_t i = 40; i < 90; i++) bad[i] ^= 0x5a;                                                          /* corrupt the first deflate stream */
        nfound = 0;
        n = p5_zip_walk(bad, sizeof bad, ".h2p", scratch, sizeof scratch, collect, NULL);
        assert(n >= 0 && n <= 2);                                                                                 /* never crashes, drops what it cannot read */
    }
    /* inverted filter envelope: off (no negative envelope in Profit-5); Chip 1 (Rounded): Rev 2 with no velocity; key amount 100: full */
    {
        static uint8_t scratch[1 << 16];
        memset(&inv, 0, sizeof inv);
        p5_zip_walk(FIX_ZIP, sizeof FIX_ZIP, ".h2p", scratch, sizeof scratch, pick_inverted, NULL);
        assert(inv_ok && V(inv, P5P_ENV_AMT) == 0.f && V(inv, P5P_REV) == 0.f && V(inv, P5P_VEL_FILT) == 0.f);
        assert(V(inv, P5P_F_KBD) == 1.f && V(inv, P5P_KBD_HALF) == 0.f);
    }
    puts("ok");
    return 0;
}
