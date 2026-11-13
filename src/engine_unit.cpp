// The C engine (src/engine.c, p5_import.c and plugin.c's mpc_engine()) compiled as C++ for the native build, which builds only
// top-level .cpp files in src/. The same sources still build as C for the 1.x wrapper build (vst/vst.json). mpc_engine() keeps C
// linkage (engine.h).
#include "engine.c"
#define clampf p5_import_clampf   // both .c files have a static clampf: one unit here
#include "p5_import.c"
#undef clampf
#include "plugin.c"
