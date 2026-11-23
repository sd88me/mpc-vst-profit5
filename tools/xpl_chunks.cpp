// Prints the plugin's own state chunk for every factory program and for every program in the banks given (.syx, .h2p, .zip of
// either; no 90-slot limit), one line each: <folder> TAB <name> TAB <chunk as hex>. The native plugin (src/Profit5Plugin.cpp with the
// P5_TEST hooks) makes each chunk exactly as the MPC would save it. Driven by tools/make_xpl.py, which wraps them into .xpl presets.
#include "mpc_vst2_abi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
extern "C" {
#include "p5_engine.h"
#include "p5_import.h"
}

extern "C" AEffect* VSTPluginMain(audioMasterCallback host);
extern "C" int p5_dbg_engine(AEffect*, const char*, char*, int);
extern "C" void p5_dbg_engine_set(AEffect*, const char*, const char*);
extern "C" void p5_dbg_tick(AEffect*);

static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { return op == audioMasterVersion ? 2400 : 0; }

static std::vector<uint8_t> chunk(AEffect* e)
{
    void* p = nullptr;
    const VstIntPtr n = e->dispatcher(e, effGetChunk, 0, 0, &p, 0);
    return n > 0 && p ? std::vector<uint8_t>((uint8_t*)p, (uint8_t*)p + n) : std::vector<uint8_t>();
}
static void run(AEffect* e, int blocks)
{
    static float l[128], r[128];
    float* out[2] = {l, r};
    for (int b = 0; b < blocks; b++) e->processReplacing(e, nullptr, out, 128);
}
// "P5A <slot> v0 .. vN m1": the engine's state string (src/plugin.c), model 1 so the preset plays the circuit model
static std::string state(const p5_engine_t& en, int slot)
{
    std::string s = "P5A " + std::to_string(slot);
    char b[32];
    for (int i = 0; i < P5P_COUNT; i++) { std::snprintf(b, sizeof b, " %.5g", en.p[i]); s += b; }
    return s + " m" + std::to_string(P5_MODEL_LATEST);
}
static void emit(AEffect* e, const std::string& folder, const std::string& name, const std::string& st)
{
    p5_dbg_engine_set(e, "state", st.c_str());
    run(e, 2);
    p5_dbg_tick(e);
    const std::vector<uint8_t> c = chunk(e);
    if (c.empty()) { std::fprintf(stderr, "no chunk for %s\n", name.c_str()); std::exit(1); }
    std::printf("%s\t%s\t", folder.c_str(), name.c_str());
    for (uint8_t x : c) std::printf("%02x", x);
    std::printf("\n");
}

struct Ctx { AEffect* e; std::string folder; int n; };
static void apply(Ctx* c, const p5_import_t* o)
{
    static p5_engine_t en;
    p5_init(&en, 44100);
    for (int i = 0; i < P5P_COUNT; i++) if (o->mask >> i & 1) p5_set(&en, i, o->v[i]);
    char nm[48];
    std::snprintf(nm, sizeof nm, "%03d %s", ++c->n, o->name);
    emit(c->e, c->folder, nm, state(en, 127));   // slot 127: an Init slot (the preset's own name is the file's)
}
static int on_syx(const p5_import_t* o, void* u) { apply((Ctx*)u, o); return 0; }
static int on_zip_syx(const char*, const uint8_t* d, size_t n, void* u) { p5_import_syx(d, n, on_syx, u); return 0; }
static int on_zip_h2p(const char* name, const uint8_t* d, size_t n, void* u)
{
    p5_import_t o;
    if (!p5_import_h2p((const char*)d, n, &o)) return 0;
    const char* f = std::strrchr(name, '/');
    std::snprintf(o.name, sizeof o.name, "%s", f ? f + 1 : name);
    if (char* dot = std::strrchr(o.name, '.')) *dot = 0;
    apply((Ctx*)u, &o);
    return 0;
}
static bool ends(const std::string& s, const char* x) { size_t n = std::strlen(x); return s.size() > n && strcasecmp(s.c_str() + s.size() - n, x) == 0; }

int main(int argc, char** argv)
{
    AEffect* e = VSTPluginMain(host);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);
    run(e, 4);
    { const int id = e->uniqueID; char ver[64] = {0}; e->dispatcher(e, effGetProductString, 0, 0, ver, 0);
      std::fprintf(stderr, "PLUGIN fxid %c%c%c%c version %d programs %d product \"%s\"\n", (id >> 24) & 255, (id >> 16) & 255, (id >> 8) & 255, id & 255,
                   e->version, e->numPrograms, ver); }
    // the factory sheets, as their slots load them
    static p5_engine_t en;
    for (int i = 0; i < p5_factory_count(); i++) {
        p5_init(&en, 44100);
        p5_load_factory(&en, i);
        char nm[48];
        std::snprintf(nm, sizeof nm, "%s", p5_factory_name(i));
        emit(e, "Factory 1978", nm, state(en, i));
    }
    // every program in each bank file given, in file order, in a folder named after the file
    for (int a = 1; a < argc; a++) {
        std::ifstream f(argv[a], std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string base = argv[a];
        if (size_t sl = base.find_last_of('/'); sl != std::string::npos) base = base.substr(sl + 1);
        if (size_t dot = base.find_last_of('.'); dot != std::string::npos) base = base.substr(0, dot);
        Ctx c{e, base, 0};
        if (ends(argv[a], ".syx")) p5_import_syx(d.data(), d.size(), on_syx, &c);
        else if (ends(argv[a], ".h2p")) {
            p5_import_t o;
            if (p5_import_h2p((const char*)d.data(), d.size(), &o)) { std::snprintf(o.name, sizeof o.name, "%s", base.c_str()); c.folder = "Repro-5"; apply(&c, &o); }
        } else if (ends(argv[a], ".zip")) {
            std::vector<uint8_t> scratch(1u << 20);
            p5_zip_walk(d.data(), d.size(), ".syx", scratch.data(), scratch.size(), on_zip_syx, &c);
            p5_zip_walk(d.data(), d.size(), ".h2p", scratch.data(), scratch.size(), on_zip_h2p, &c);
        }
        std::fprintf(stderr, "%s: %d programs\n", argv[a], c.n);
    }
    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0);
    e->dispatcher(e, effClose, 0, 0, nullptr, 0);
    return 0;
}
