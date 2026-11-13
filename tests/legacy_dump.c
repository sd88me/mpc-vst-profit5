/* Builds against a RELEASED engine (tests/make_legacy_chunks.sh) and prints the chunk that build's 1.x wrapper saved: the engine's
 * "state" string (vst2_wrap.c effGetChunk = eng_get("state")). argv[1] = the banks folder (the 1.x wrapper passed <plugin folder>/banks, MODULE_SUBDIR), argv[2] = the program
 * slot to pick before the edits. The edits are fixed so the host test knows what to expect. */
#include <stdio.h>
#include <stdlib.h>
#include "engine.h"
int main(int argc, char **argv) {
    const mpc_engine_t *e = mpc_engine();
    void *s = e->create(argc > 1 ? argv[1] : ".");
    if (argc > 2) e->set_param(s, "preset", argv[2]);
    e->set_param(s, "cutoff", "3.7");
    e->set_param(s, "reso", "6");
    e->set_param(s, "a_release", "8");
    e->set_param(s, "a_sync", "1");
    static char buf[8192];
    if (getenv("P5_DUMP_NAME") && e->get_param(s, "program_name", buf, sizeof buf) > 0) fprintf(stderr, "program: %s\n", buf);
    if (e->get_param(s, "state", buf, sizeof buf) <= 0) return 1;
    fputs(buf, stdout);
    e->destroy(s);
    return 0;
}
