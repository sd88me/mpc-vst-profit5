// Offline host test for the native front end (src/Profit5Plugin.cpp) with the real engine, under ASan/UBSan (tests/run.sh).
// A fake VST2 host drives it: identity and the frozen order, the PRESET menu (factory + an imported patch), sound, pot / choice /
// whole-number / momentary / text params, the parameter-0 nudge, PRESET picks both ways, a state round trip into a second instance,
// the 0.1.1 / 0.2.0 / 0.2.1 project chunks (tests/data/legacy-*.chunk, made by the released engines themselves), bad chunks.
// The plugin folder holds banks/Test Brass.h2p (a synthetic patch, tests/run.sh). Prints OK at the end, or the failures and exits 1.
#include "mpc_vst2_abi.h"
#include "params_gen.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

extern "C" AEffect* VSTPluginMain(audioMasterCallback);
extern "C" void p5_dbg_tick(AEffect*);
extern "C" int p5_dbg_engine(AEffect*, const char*, char*, int);
extern "C" void p5_dbg_engine_set(AEffect*, const char*, const char*);
extern "C" void p5_dbg_no_ignore(AEffect*);
extern "C" int p5_dbg_favs(AEffect*);

static std::atomic<int> g_automate[kNumParams];
static std::atomic<int> g_updateDisplay{0};
static int g_fail = 0;

static VstIntPtr host(AEffect*, VstInt32 op, VstInt32 idx, VstIntPtr, void*, float)
{
    if (op == audioMasterAutomate && idx >= 0 && idx < kNumParams) g_automate[idx]++;
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterUpdateDisplay) g_updateDisplay++;
    return 0;
}

#define CHECK(c, ...) do { if (c) { std::printf("  ok   "); } else { std::printf("  FAIL "); g_fail++; } std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

static int idx(const char* key)
{
    for (int i = 0; i < kNumParams; i++) if (!std::strcmp(g_pspecs[i].key, key)) return i;
    std::printf("no param %s\n", key); std::exit(2);
}
static float norm(int i, float plain) { const PSpec& s = g_pspecs[i]; return s.max > s.min ? (plain - s.min) / (s.max - s.min) : 0.f; }
static float plainOf(AEffect* e, int i) { const PSpec& s = g_pspecs[i]; return s.min + e->getParameter(e, i) * (s.max - s.min); }
static std::string eng(AEffect* e, const char* k) { static char b[8192]; p5_dbg_engine(e, k, b, sizeof b); return b; }
static int engi(AEffect* e, const char* k) { return std::atoi(eng(e, k).c_str()); }
static float engf(AEffect* e, const char* k) { return (float)std::atof(eng(e, k).c_str()); }
static std::string disp(AEffect* e, int i) { char b[128] = {0}; e->dispatcher(e, effGetParamDisplay, i, 0, b, 0); return b; }
static std::string progName(AEffect* e, int i) { char b[64] = {0}; e->dispatcher(e, effGetProgramNameIndexed, i, 0, b, 0); return b; }
static bool near(float a, float b) { return std::fabs(a - b) < 0.011f; }

static double g_rms;
static void run(AEffect* e, int blocks, const std::vector<std::vector<uint8_t>>& midi = {})
{
    float L[128], R[128], *out[2] = {L, R};
    double acc = 0; long n = 0;
    for (int b = 0; b < blocks; b++)
    {
        if (b == 0 && !midi.empty())
        {
            std::vector<VstMidiEvent> ev(midi.size());
            std::vector<char> buf(sizeof(VstEvents) + sizeof(VstEvent*) * midi.size());
            VstEvents* evs = reinterpret_cast<VstEvents*>(buf.data());
            evs->numEvents = (VstInt32)midi.size();
            for (size_t k = 0; k < midi.size(); k++)
            {
                std::memset(&ev[k], 0, sizeof ev[k]);
                ev[k].type = kVstMidiType; ev[k].byteSize = sizeof(VstMidiEvent);
                ev[k].deltaFrames = (VstInt32)(k * 17 % 128);   // sample-accurate split
                for (size_t j = 0; j < 3 && j < midi[k].size(); j++) ev[k].midiData[j] = (char)midi[k][j];
                evs->events[k] = reinterpret_cast<VstEvent*>(&ev[k]);
            }
            e->dispatcher(e, effProcessEvents, 0, 0, evs, 0);
        }
        e->processReplacing(e, nullptr, out, 128);
        for (int i = 0; i < 128; i++) { acc += (double)L[i] * L[i] + (double)R[i] * R[i]; n += 2; }
    }
    g_rms = n ? std::sqrt(acc / n) : 0;
}
static void settle(AEffect* e, int blocks = 4) { run(e, blocks); p5_dbg_tick(e); }

static AEffect* open(const char* what)
{
    AEffect* e = VSTPluginMain(host);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);
    settle(e);
    std::printf("%s: opened\n", what);
    return e;
}
static void close(AEffect* e) { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0); e->dispatcher(e, effClose, 0, 0, nullptr, 0); }
static std::vector<uint8_t> chunk(AEffect* e)
{
    void* p = nullptr;
    const VstIntPtr n = e->dispatcher(e, effGetChunk, 0, 0, &p, 0);
    return n > 0 && p ? std::vector<uint8_t>((uint8_t*)p, (uint8_t*)p + n) : std::vector<uint8_t>();
}
static VstIntPtr setChunk(AEffect* e, std::vector<uint8_t> c) { return e->dispatcher(e, effSetChunk, 0, (VstIntPtr)c.size(), c.data(), 0); }
static std::vector<uint8_t> readFile(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv)
{
    const std::string data = argc > 1 ? argv[1] : "tests/data";
    const int PMF = idx("pm_fenv"), CUT = idx("cutoff"), RESO = idx("reso"), AFREQ = idx("a_freq"), SAW = idx("a_saw"), SYNC = idx("a_sync"),
              REV = idx("rev"), PRE = idx("preset"), PNEXT = idx("preset_next"), PPREV = idx("preset_prev"), PNAME = idx("program_name"),
              ISTAT = idx("import_status"), AREL = idx("a_release"), USER = idx("user");

    std::printf("== identity\n");
    AEffect* a = open("A");
    char name[64] = {0};
    a->dispatcher(a, effGetEffectName, 0, 0, name, 0);
    CHECK(a->uniqueID == (VstInt32)P5_EXPECT_UID, "uid %08x", (unsigned)a->uniqueID);
    CHECK(!std::strcmp(name, P5_EXPECT_NAME), "name \"%s\"", name);
    CHECK(a->numParams == 66, "66 params: the 64 of 0.2.1 + +FAV / -FAV (%d)", a->numParams);
    CHECK(a->dispatcher(a, effGetVendorVersion, 0, 0, nullptr, 0) == 2000, "version 2000");
    {
        int bad = 0, i = 0;
        std::ifstream f("vst/params.lock");
        std::string line;
        while (std::getline(f, line)) if (!line.empty() && line[0] != '#' && line != g_pspecs[i++].key) bad++;
        CHECK(i == 66 && bad == 0, "order = vst/params.lock (%d keys, %d differ)", i, bad);
        char pn[64] = {0};
        a->dispatcher(a, effGetParamName, PMF, 0, pn, 0);
        CHECK(PMF == 0 && PRE == 58 && ISTAT == 63, "param 0 is PM Filt Env (\"%s\"), preset 58, import_status 63", pn);
    }

    std::printf("== PRESET menu\n");
    CHECK(a->numPrograms == 160, "160 programs: 128 slots + 32 favourites (%d)", a->numPrograms);
    CHECK(kFavAdd == 64 && kFavDel == 65 && progName(a, 128) == "F01 --", "favourites after the slots (\"%s\")", progName(a, 128).c_str());
    CHECK(progName(a, 0) == "1-1 Brass" || progName(a, 0).compare(0, 4, "1-1 ") == 0, "program 0 \"%s\"", progName(a, 0).c_str());
    CHECK(progName(a, 38) == "U01 Test Brass", "program 38 = the imported patch (\"%s\")", progName(a, 38).c_str());
    CHECK(progName(a, 39) == "U02 Init", "program 39 = a free slot (\"%s\")", progName(a, 39).c_str());
    CHECK(disp(a, ISTAT) == "1 imported (U01-U01)", "import status \"%s\"", disp(a, ISTAT).c_str());

    std::printf("== sound\n");
    run(a, 100, {{0x90, 60, 100}, {0x90, 64, 100}, {0x90, 67, 100}});
    const double on = g_rms;
    run(a, 1500, {{0x80, 60, 0}, {0x80, 64, 0}, {0x80, 67, 0}});
    run(a, 50);
    const double off = g_rms;
    CHECK(on > 1e-3, "notes are audible (rms %.4f)", on);
    CHECK(off < on * 0.05, "note off releases (rms %.5f)", off);

    std::printf("== pots, switches, whole numbers\n");
    a->setParameter(a, CUT, norm(CUT, 3.7f));
    settle(a);
    CHECK(near(engf(a, "cutoff"), 3.7f), "CUTOFF 3.7 -> engine %s", eng(a, "cutoff").c_str());
    CHECK(near(plainOf(a, CUT), 3.7f) && disp(a, CUT) == "3.7", "CUTOFF reads back 3.7, shows \"%s\"", disp(a, CUT).c_str());
    a->setParameter(a, SAW, 0.0f);
    a->setParameter(a, SYNC, 1.0f);
    a->setParameter(a, REV, 1.0f);
    settle(a);
    CHECK(engi(a, "a_saw") == 0 && engi(a, "a_sync") == 1, "A SAW off, SYNC on");
    CHECK(engi(a, "rev") == 1 && disp(a, REV) == "Rev 4", "REV -> Rev 4 (\"%s\")", disp(a, REV).c_str());
    a->setParameter(a, REV, 0.0f);
    a->setParameter(a, AFREQ, norm(AFREQ, 20));
    settle(a);
    a->setParameter(a, AFREQ, norm(AFREQ, 20.3f));
    settle(a);
    CHECK(engi(a, "a_freq") == 21, "a small turn up moves A FREQUENCY one semitone (engine %d)", engi(a, "a_freq"));

    std::printf("== parameter 0 nudge\n");
    a->setParameter(a, PMF, norm(PMF, 2.0f));
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    run(a, 2);
    CHECK(near(engf(a, "pm_fenv"), 2.0f), "a held change applies (%s)", eng(a, "pm_fenv").c_str());
    a->setParameter(a, PMF, 1.0f);
    a->setParameter(a, PMF, norm(PMF, 2.0f));
    run(a, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    run(a, 2);
    CHECK(near(engf(a, "pm_fenv"), 2.0f), "a nudge to the end and back leaves it at 2.0 (%s)", eng(a, "pm_fenv").c_str());

    std::printf("== programs: arrows, knob, PRESET pick\n");
    a->setParameter(a, PRE, norm(PRE, 0));
    settle(a);
    a->setParameter(a, PNEXT, 1.0f);
    settle(a);
    CHECK(engi(a, "preset") == 1, "PROGRAM + steps 0 -> %d", engi(a, "preset"));
    CHECK(plainOf(a, PNEXT) < 0.5f, "PROGRAM + drops back to 0");
    CHECK(a->dispatcher(a, effGetProgram, 0, 0, nullptr, 0) == 1, "the PRESET menu follows (program %d)", (int)a->dispatcher(a, effGetProgram, 0, 0, nullptr, 0));
    a->setParameter(a, PNEXT, 0.0f);
    a->setParameter(a, PRE, norm(PRE, 0));
    settle(a);
    a->setParameter(a, PPREV, 1.0f);
    settle(a);
    a->setParameter(a, PPREV, 0.0f);
    CHECK(engi(a, "preset") == 127, "PROGRAM - wraps 0 -> 127 (%d)", engi(a, "preset"));
    CHECK(disp(a, PNAME) == progName(a, 127), "NAME shows the engine's text (\"%s\")", disp(a, PNAME).c_str());
    p5_dbg_no_ignore(a);
    g_updateDisplay = 0;
    a->dispatcher(a, effSetProgram, 0, 38, nullptr, 0);
    settle(a);
    CHECK(engi(a, "preset") == 38 && disp(a, PNAME) == "U01 Test Brass", "a PRESET pick loads the imported patch (\"%s\")", disp(a, PNAME).c_str());
    CHECK(std::lround(plainOf(a, PRE)) == 38 && disp(a, PRE) == "U01 Test Brass", "the Program control shows it (\"%s\")", disp(a, PRE).c_str());
    CHECK(engi(a, "user") == 1 && std::lround(plainOf(a, USER)) == 1, "Imported reads 1");
    CHECK(g_updateDisplay > 0, "one display update for the new program (%d)", g_updateDisplay.load());
    a->setParameter(a, PRE, norm(PRE, 6));
    settle(a);
    CHECK(engi(a, "preset") == 6 && near(plainOf(a, CUT), engf(a, "cutoff")), "the Program knob loads 6, the controls follow (cutoff %.2f)", plainOf(a, CUT));

    std::printf("== state round trip\n");
    a->setParameter(a, RESO, norm(RESO, 7.5f));
    a->setParameter(a, AREL, norm(AREL, 2.25f));
    settle(a);
    const std::string stA = eng(a, "state");
    std::vector<uint8_t> ca = chunk(a);
    CHECK(ca.size() > 4 && !std::memcmp(ca.data(), "MPCP", 4), "MPC chunk (%zu bytes)", ca.size());
    AEffect* b = open("B");
    CHECK(setChunk(b, ca) == 1, "B takes A's chunk");
    settle(b);
    CHECK(eng(b, "state") == stA, "B's engine state = A's");
    CHECK(near(plainOf(b, RESO), 7.5f) && near(plainOf(b, AREL), 2.25f) && std::lround(plainOf(b, PRE)) == 6,
          "B's controls show RESO 7.5, A RELEASE 2.25, program 6");
    CHECK(b->dispatcher(b, effGetProgram, 0, 0, nullptr, 0) == 6, "B's PRESET menu is on 6");
    b->dispatcher(b, effSetProgram, 0, 6, nullptr, 0);   // the host re-picks the saved program right after a load: no reload
    settle(b);
    CHECK(eng(b, "state") == stA, "the post-load re-pick keeps the project's edits");

    std::printf("== 1.x project chunks\n");
    struct L { const char* file; int preset; const char* name; };
    const L legacy[] = {{"legacy-0.1.1.chunk", 5, nullptr}, {"legacy-0.2.0.chunk", 5, nullptr}, {"legacy-0.2.1.chunk", 5, nullptr},
                        {"legacy-0.2.0-user.chunk", 38, "U01 Test Brass"}, {"legacy-0.2.1-user.chunk", 38, "U01 Test Brass"}};
    for (const L& l : legacy)
    {
        const std::vector<uint8_t> c = readFile(data + "/" + l.file);
        AEffect* e = open(l.file);
        CHECK(c.size() > 100 && setChunk(e, c) == 1, "takes %s (%zu bytes)", l.file, c.size());
        settle(e);
        const std::string want(reinterpret_cast<const char*>(c.data()));
        CHECK(eng(e, "state") == want, "  the engine state is the saved one");
        CHECK(engi(e, "preset") == l.preset && std::lround(plainOf(e, PRE)) == l.preset, "  program %d", engi(e, "preset"));
        if (l.name) CHECK(disp(e, PNAME) == l.name, "  the imported patch \"%s\"", disp(e, PNAME).c_str());
        CHECK(near(plainOf(e, CUT), 3.7f) && near(plainOf(e, RESO), 6.0f) && near(plainOf(e, AREL), 8.0f) && std::lround(plainOf(e, SYNC)) == 1,
              "  the saved edits show: CUTOFF %.2f, RESO %.2f, A RELEASE %.2f, SYNC on", plainOf(e, CUT), plainOf(e, RESO), plainOf(e, AREL));
        std::vector<uint8_t> again = chunk(e);   // saved again by this build, then reloaded
        AEffect* f = open("  re-saved");
        setChunk(f, again);
        settle(f);
        CHECK(eng(f, "state") == want, "  re-saved by this build and reloaded: unchanged");
        close(f);
        close(e);
    }

    std::printf("== bad chunks\n");
    {
        const std::string before = eng(b, "state");
        CHECK(setChunk(b, {'x', 'y', 'z', 0}) == 0, "garbage is refused");
        CHECK(setChunk(b, {'P', '5', 'A', ' ', '3', ' ', '0', '.', '5', 0}) == 1, "a short P5A state is taken but changes nothing");
        settle(b);
        CHECK(eng(b, "state") == before, "  unchanged");
        std::vector<uint8_t> trunc(ca.begin(), ca.begin() + ca.size() / 2);
        setChunk(b, trunc);
        settle(b);
        CHECK(eng(b, "state").compare(0, 4, "P5A ") == 0, "a truncated MPC chunk: no crash, still a valid program");
    }

    std::printf("== favourites\n");
    {
        const int n0 = p5_dbg_favs(a);
        p5_dbg_no_ignore(a);
        a->dispatcher(a, effSetProgram, 0, 3, nullptr, 0);
        settle(a);
        a->setParameter(a, CUT, norm(CUT, 1.25f));
        settle(a);
        const std::string saved = eng(a, "state");
        const std::string base = disp(a, PNAME);
        a->setParameter(a, kFavAdd, 1.0f);
        settle(a);
        settle(a);
        CHECK(p5_dbg_favs(a) == n0 + 1 && plainOf(a, kFavAdd) < 0.5f, "+FAV saves one and drops back (%d)", p5_dbg_favs(a));
        a->setParameter(a, kFavAdd, 0.0f);
        const int slot = 128 + n0;
        {   // the host's program names are 24 characters at most
            const std::string want = ("F" + std::string(n0 + 1 < 10 ? "0" : "") + std::to_string(n0 + 1) + " " + base).substr(0, 24);
            CHECK(progName(a, slot) == want, "PRESET menu: \"%s\"", progName(a, slot).c_str());
        }
        CHECK(a->dispatcher(a, effGetProgram, 0, 0, nullptr, 0) == slot, "the new favourite is the picked entry");
        a->dispatcher(a, effSetProgram, 0, 10, nullptr, 0);
        settle(a);
        CHECK(engi(a, "preset") == 10, "another program in between");
        a->dispatcher(a, effSetProgram, 0, slot, nullptr, 0);
        settle(a); settle(a);
        CHECK(eng(a, "state") == saved && near(plainOf(a, CUT), 1.25f), "picking it restores it (CUTOFF %.2f)", plainOf(a, CUT));
        CHECK(a->dispatcher(a, effGetProgram, 0, 0, nullptr, 0) == slot, "the menu stays on the favourite (%d)", (int)a->dispatcher(a, effGetProgram, 0, 0, nullptr, 0));
        AEffect* e2 = open("E (reads the favourites file)");
        CHECK(p5_dbg_favs(e2) == n0 + 1 && progName(e2, slot) == progName(a, slot), "a new instance reads it from profit5-favourites.dat");
        close(e2);
        a->setParameter(a, kFavDel, 1.0f);
        settle(a); settle(a);
        a->setParameter(a, kFavDel, 0.0f);
        CHECK(p5_dbg_favs(a) == n0 && progName(a, slot).compare(4, 2, "--") == 0, "-FAV deletes the picked one (\"%s\")", progName(a, slot).c_str());
        a->setParameter(a, PNEXT, 1.0f);
        settle(a);
        a->setParameter(a, PNEXT, 0.0f);
        CHECK(a->dispatcher(a, effGetProgram, 0, 0, nullptr, 0) == engi(a, "preset"), "after that the menu follows the program again");
    }

    close(b);
    close(a);
    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("OK\n");
    return 0;
}
