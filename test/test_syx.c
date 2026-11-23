/* Prophet-5 Rev 4 .syx import (src/p5_import.c p5_import_syx, the banks scan in src/plugin.c), on synthetic dumps built here from the
 * layout in Sequential's "Prophet-5 MIDI Implementation" 1.4: no real bank is used or committed.
 * gcc -O1 -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined test/test_syx.c src/engine.c src/p5_import.c -lm && ./a.out */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/plugin.c"

/* 128 parameter bytes -> 152 bytes of "packed MS bit" (a top-bits byte, LSB first, then seven bytes) */
static size_t pack(const uint8_t *u, uint8_t *out) {
    size_t o = 0;
    for (size_t k = 0; k < 128; k += 7) {
        uint8_t hi = 0;
        size_t n = 128 - k < 7 ? 128 - k : 7;
        for (size_t b = 0; b < n; b++) hi |= (uint8_t)(((u[k + b] >> 7) & 1) << b);
        out[o++] = hi;
        for (size_t b = 0; b < n; b++) out[o++] = u[k + b] & 0x7F;
    }
    return o;
}
static size_t message(uint8_t *m, int id, int edit, const uint8_t *u) {
    size_t n = 0;
    m[n++] = 0xF0; m[n++] = 0x01; m[n++] = (uint8_t)id;
    if (edit) m[n++] = 0x03; else { m[n++] = 0x02; m[n++] = 5; m[n++] = 7; }
    n += pack(u, m + n);
    m[n++] = 0xF7;
    return n;
}
static void params(uint8_t *u, const char *name, int rev) {
    memset(u, 0, 128);
    u[0] = 24; u[1] = 120; u[2] = 127;          /* osc A 12 semitones, osc B 60, fine full */
    u[3] = 1; u[7] = 1; u[8] = 64; u[13] = 127; /* A saw, B square, A PW centre, glide full */
    u[14] = 127; u[17] = 100; u[18] = 200;      /* mix A full, cutoff, resonance 200: a byte with its top bit set (clamped to 1) */
    u[19] = 1; u[20] = (uint8_t)rev;            /* filter keyboard half, filter rev */
    u[32] = 127; u[34] = 1;                     /* Poly-Mod filter env to freq A */
    u[40] = 127; u[44] = 60; u[51] = 1;         /* env amount, amp attack, RELEASE on */
    u[52] = 1; u[53] = 3;                       /* unison on three voices */
    for (int i = 0; i < 20; i++) u[65 + i] = ' ';
    memcpy(u + 65, name, strlen(name));
}

static int n_cb;
static p5_import_t got[8];
static int keep(const p5_import_t *p, void *user) { (void)user; got[n_cb++] = *p; return 0; }

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    uint8_t u[128], buf[2048];
    size_t n = 0;
    /* junk first: another maker's SysEx, a Prophet-5 message of another kind, a truncated dump (a status byte interrupts it) */
    const uint8_t other[] = { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00, 0xF7 }, global[] = { 0xF0, 0x01, 0x32, 0x0F, 0x00, 0xF7 };
    memcpy(buf + n, other, sizeof other); n += sizeof other;
    memcpy(buf + n, global, sizeof global); n += sizeof global;
    params(u, "Cut Short", 0); n += message(buf + n, 0x32, 0, u) - 40; buf[n++] = 0x90;
    params(u, "Velo Res Lead 2", 1); n += message(buf + n, 0x32, 0, u);   /* a program dump, the factory banks' id */
    params(u, "", 0); n += message(buf + n, 0x31, 1, u);                  /* an edit buffer, the document's id, no name */

    int c = p5_import_syx(buf, n, keep, NULL);
    printf("%d patches: \"%s\", \"%s\"\n", c, got[0].name, got[1].name);
    assert(c == 2 && n_cb == 2);
    const p5_import_t *p = &got[0];
    assert(!strcmp(p->name, "Velo Res Lead 2") && !strcmp(got[1].name, "Program 2"));
    assert(fabsf(p->v[P5P_A_FREQ] - 24 / 120.f) < 1e-6f && fabsf(p->v[P5P_B_FREQ] - 1) < 1e-6f && p->v[P5P_B_FINE] == 1);
    assert(p->v[P5P_A_SAW] == 1 && p->v[P5P_A_PULSE] == 0 && p->v[P5P_B_PULSE] == 1 && fabsf(p->v[P5P_A_PW] - 64 / 127.f) < 1e-6f);
    assert(p->v[P5P_RESO] == 1 && fabsf(p->v[P5P_CUTOFF] - 100 / 127.f) < 1e-6f);
    assert(p->v[P5P_F_KBD] == 1 && p->v[P5P_KBD_HALF] == 1 && p->v[P5P_REV] == 1 && got[1].v[P5P_REV] == 0);
    assert(p->v[P5P_PM_FENV] == 1 && p->v[P5P_PM_DEST_FA] == 1 && p->v[P5P_ENV_AMT] == 1 && p->v[P5P_RELEASE] == 1);
    assert(p->v[P5P_UNISON] == 1 && fabsf(p->v[P5P_NVOICES] - 0.5f) < 1e-6f);
    assert((p->mask >> P5P_CUTOFF) & 1 && !((p->mask >> P5P_VOLUME) & 1) && !((p->mask >> P5P_TUNE) & 1));
    for (int i = 0; i < P5P_COUNT; i++) assert(p->v[i] >= 0 && p->v[i] <= 1);
    assert(p5_import_syx(buf, 3, keep, NULL) == 0 && p5_import_syx(NULL, 0, keep, NULL) == 0);

    /* the plugin: a .syx in <dir>/banks fills user slots in file order, after a .h2p that sorts first */
    char dir[] = "/tmp/p5syxXXXXXX";
    assert(mkdtemp(dir));
    char path[600];
    snprintf(path, sizeof path, "%s/bank.syx", dir);
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(buf, 1, n, f) == n);
    fclose(f);
    const mpc_engine_t *E = mpc_engine();
    void *h = E->create(dir);
    char b[64];
    assert(h && E->get_param(h, "preset:38", b, sizeof b) > 0);
    printf("preset 38 \"%s\"\n", b);
    assert(!strcmp(b, "U01 Velo Res Lead 2"));
    assert(E->get_param(h, "preset:39", b, sizeof b) > 0 && !strcmp(b, "U02 Program 2"));
    assert(E->get_param(h, "import_status", b, sizeof b) > 0 && strstr(b, "2 imported"));
    E->set_param(h, "preset", "38");
    assert(E->get_param(h, "reso", b, sizeof b) > 0);
    printf("loaded: resonance %s\n", b);
    E->destroy(h);
    unlink(path);

    /* a bank of 100 programs: three BANKS entries of 40 / 40 / 20; U01-U90 are the first 90; program 95 loads from BANKS and keeps
     * its name through a saved state */
    {
        static uint8_t big[100 * 200];
        size_t bn = 0;
        for (int i = 0; i < 100; i++) { char nm[24]; snprintf(nm, sizeof nm, "Big %03d", i + 1); params(u, nm, i & 1); u[17] = (uint8_t)i; bn += message(big + bn, 0x32, 0, u); }
        snprintf(path, sizeof path, "%s/Big_Bank.syx", dir);
        f = fopen(path, "wb");
        assert(f && fwrite(big, 1, bn, f) == bn);
        fclose(f);
        h = E->create(dir);
        assert(E->get_param(h, "bank_slot_2", b, sizeof b) > 0 && !strcmp(b, "Big Bank 1"));
        assert(E->get_param(h, "bank_slot_4", b, sizeof b) > 0 && !strcmp(b, "Big Bank 3"));
        assert(E->get_param(h, "bank_slot_5", b, sizeof b) > 0 && !strcmp(b, ""));
        assert(E->get_param(h, "preset:127", b, sizeof b) > 0 && !strcmp(b, "U90 Big 090"));
        assert(E->get_param(h, "import_status", b, sizeof b) > 0 && strstr(b, "100 imported") && strstr(b, "BANKS"));
        printf("big bank: %s\n", b);
        E->set_param(h, "bank_slot_4", "1");                     /* Big Bank 3: programs 81-100 */
        assert(E->get_param(h, "patch_slot_15", b, sizeof b) > 0 && !strcmp(b, "015 Big 095"));
        E->set_param(h, "patch_slot_15", "1");                   /* Big 095: beyond the PRESET slots */
        assert(E->get_param(h, "program_name", b, sizeof b) > 0 && !strcmp(b, "Big 095"));
        assert(E->get_param(h, "patch_slot_15_on", b, sizeof b) > 0 && atoi(b) == 1);
        assert(E->get_param(h, "cutoff", b, sizeof b) > 0);
        printf("program 95 loaded: cutoff %s, name \"Big 095\"\n", b);
        static char st[8192];
        assert(E->get_param(h, "state", st, sizeof st) > 0 && strstr(st, " NBig%20095"));
        void *h2 = E->create(dir);
        E->set_param(h2, "state", st);
        assert(E->get_param(h2, "program_name", b, sizeof b) > 0 && !strcmp(b, "Big 095"));
        printf("a saved state keeps it: \"%s\"\n", b);
        E->set_param(h2, "preset", "0");
        assert(E->get_param(h2, "program_name", b, sizeof b) > 0 && !strcmp(b, "1-1 Brass"));
        E->destroy(h2);
        E->destroy(h);
        unlink(path);
    }
    rmdir(dir);
    puts("ok");
    return 0;
}
