/* The plugin's use of imported patches: the banks folder is scanned at create, "user" loads one, the state keeps it, an old
 * (0.1.x) state still loads, and an unreadable folder or file changes nothing. Includes src/plugin.c to reach its statics. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/plugin.c"
#include "fixture_import.h"

static void put_file(const char *dir, const char *name, const void *d, size_t n) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    assert(f && fwrite(d, 1, n, f) == n);
    fclose(f);
}
static float getf(const mpc_engine_t *E, void *h, const char *k) {
    char b[64];
    assert(E->get_param(h, k, b, sizeof b) > 0);
    return (float)atof(b);
}

int main(void) {
    const mpc_engine_t *E = mpc_engine();
    char dir[] = "/tmp/p5banksXXXXXX";
    assert(mkdtemp(dir));
    put_file(dir, "zz Brass.h2p", FIX_BRASS, sizeof FIX_BRASS - 1);   /* a loose patch, sorted after the zip */
    put_file(dir, "bank.ZIP", FIX_ZIP, sizeof FIX_ZIP);               /* two Repro-5 patches inside (one foreign, one text file skipped) */
    put_file(dir, "notes.txt", "x", 1);
    put_file(dir, "empty.h2p", "", 0);
    put_file(dir, "junk.h2p", "\001\002\003 not a patch", 16);
    put_file(dir, "truncated.zip", FIX_ZIP, 60);

    void *h = E->create(dir);
    assert(h);
    char b[128];
    {   char nb[40];
        assert(!strcmp(base_noext("x/bd590f2d-Pete_Standing_2.h2p", nb, sizeof nb), "Pete_Standing_2"));
        assert(!strcmp(base_noext("bd590f2d-.h2p", nb, sizeof nb), "bd590f2d-") && !strcmp(base_noext("Brass-1234.h2p", nb, sizeof nb), "Brass-1234"));
        assert(!strcmp(base_noext("dir/abcdefgh-X.h2p", nb, sizeof nb), "abcdefgh-X")); }
    const int NF = p5_factory_count();
    /* the program list: factory sheets, then imported patches sorted by file name (bank.ZIP: Test Brass, Inverted Env; then "zz Brass"), then Init */
    assert(E->get_param(h, "preset:0", b, sizeof b) > 0 && !strcmp(b, p5_factory_name(0)));
    assert(E->get_param(h, "preset:38", b, sizeof b) > 0 && !strcmp(b, "U01 Test Brass") && NF == 38);
    assert(E->get_param(h, "preset:40", b, sizeof b) > 0 && !strcmp(b, "U03 zz Brass"));
    assert(E->get_param(h, "preset:41", b, sizeof b) > 0 && !strcmp(b, "U04 Init"));
    assert(E->get_param(h, "preset:127", b, sizeof b) > 0 && !strcmp(b, "U90 Init"));
    assert(E->get_param(h, "preset:128", b, sizeof b) == 0 && E->get_param(h, "preset:-1", b, sizeof b) == 0);
    assert(E->get_param(h, "import_status", b, sizeof b) > 0 && strstr(b, "3 imported"));
    unsigned rev0 = (unsigned)getf(E, h, "display_rev");
    E->set_param(h, "preset", "38");
    assert(getf(E, h, "preset") == 38 && E->get_param(h, "program_name", b, sizeof b) > 0 && !strcmp(b, "U01 Test Brass"));
    assert((unsigned)getf(E, h, "display_rev") == rev0 + 1);
    assert(getf(E, h, "cutoff") > 4.4f && getf(E, h, "cutoff") < 4.6f);        /* dial 4.5 of 10 */
    assert(getf(E, h, "rev") == 1 && getf(E, h, "mix_b") > 5.9f);
    assert(getf(E, h, "wm_filter") == 1 && getf(E, h, "wm_pw_b") == 1 && getf(E, h, "lfo_init") > 1.1f);
    E->set_param(h, "preset", "39");
    assert(E->get_param(h, "program_name", b, sizeof b) > 0 && !strcmp(b, "U02 Inverted Env") && getf(E, h, "env_amt") == 0 && getf(E, h, "rev") == 0);
    E->set_param(h, "preset", "41");                                          /* a free slot: the neutral Init patch */
    assert(!strcmp((E->get_param(h, "program_name", b, sizeof b), b), "U04 Init") && getf(E, h, "wm_filter") == 0 && getf(E, h, "mix_b") == 0);
    E->set_param(h, "preset", "128");                                         /* past the end: ignored */
    assert(getf(E, h, "preset") == 41);
    E->set_param(h, "user", "3");                                             /* 0.2.0's knob still loads the third imported patch */
    assert(getf(E, h, "preset") == 40 && getf(E, h, "user") == 3);
    E->set_param(h, "preset", "5");
    assert(getf(E, h, "user") == 0);

    /* master tune, volume and voices are the player's, not the patch's */
    E->set_param(h, "volume", "3"); E->set_param(h, "voices", "2"); E->set_param(h, "tune", "20");
    E->set_param(h, "user", "1");
    assert(getf(E, h, "volume") > 2.9f && getf(E, h, "volume") < 3.1f && getf(E, h, "voices") == 2 && getf(E, h, "tune") > 19.f);

    /* MIDI program change = the slot */
    E->set_param(h, "preset", "0");
    uint8_t pc[2] = { 0xC0, 39 };
    E->midi(h, pc, 2);
    assert(getf(E, h, "preset") == 39);
    pc[1] = 5; E->midi(h, pc, 2);
    assert(getf(E, h, "preset") == 5);

    /* state round trip keeps the slot and the values */
    E->set_param(h, "preset", "38");
    static char st[2048];
    assert(E->get_param(h, "state", st, sizeof st) > 0 && !strstr(st, " U"));
    void *h2 = E->create(dir);
    E->set_param(h2, "state", st);
    assert(getf(E, h2, "preset") == 38 && getf(E, h2, "cutoff") == getf(E, h, "cutoff"));
    /* a 0.2.0 state ("P5A <factory> <58 values> U<n>") and a 0.1.x state (no U) */
    char old[2048];
    strcpy(old, st); strcat(old, " U2");
    E->set_param(h2, "state", old);
    assert(getf(E, h2, "preset") == 39);
    E->set_param(h2, "state", "P5A 3 0.5 0.5");                               /* a damaged one changes nothing */
    assert(getf(E, h2, "preset") == 39);
    E->destroy(h2);
    E->destroy(h);

    /* no folder, an empty folder, an unreadable name: nothing found, "No files", and the plugin still plays */
    h = E->create("/nonexistent/banks");
    assert(h && E->get_param(h, "import_status", b, sizeof b) > 0 && strstr(b, "No files"));
    E->set_param(h, "preset", "38");                                          /* an empty slot is Init */
    assert(getf(E, h, "preset") == 38 && E->get_param(h, "program_name", b, sizeof b) > 0 && !strcmp(b, "U01 Init"));
    static int16_t out[256];
    uint8_t on[3] = { 0x90, 60, 100 };
    E->midi(h, on, 3);
    E->render(h, out, 128);
    E->destroy(h);
    h = E->create(NULL);
    assert(h);
    E->destroy(h);

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    assert(system(cmd) == 0);
    puts("ok");
    return 0;
}
