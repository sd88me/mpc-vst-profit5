// Profit-5 for MPC / Force: the native front end (framework/mpc_plugin.h) around the unchanged Profit-5 engine
// (src/engine.c + src/p5_import.c + src/plugin.c's string-key mpc_engine(), via engine_unit.cpp: five voices, Rev 2 / Rev 4, the
// 38 factory programs, Repro-5 .h2p patches imported from <plugin folder>/banks).
//
// Signal flow: host MIDI (sample accurate) -> engine midi -> engine render (int16 stereo, up to 128 frames) -> float outputs.
//
// Parameters: the 0.2.1 wrapper build's 64 in the same order (vst/params.json -> tools/gen_native_params.py -> params_gen.h).
//   * value params (kPolled): a host change goes to the engine as its own set_param string, in engine units (pots 0..10, semitones,
//     options by index). Whole numbers and options settle toward the direction of a Q-Link move, so a small turn moves one step
//     (the 1.x rule). A program load, a state load or a received CC changes many values at once: the engine bumps "display_rev",
//     and the worker reads every value back and tells the host.
//   * momentary verbs (kMomentary): PROGRAM - / + fire on 0 -> 1, step "preset" (wrapping) and drop back to 0.
//   * text readouts (kText): the value text is the engine's text; the value is a counter bumped when the text changes, because the
//     host only re-reads a param's text when its value moves.
//   * programs: the MPC's PRESET menu lists the engine's 128 slots ("preset:<n>": factory sheets, then imported patches, then Init),
//     as 0.2.1 did. Picking one sets "preset"; a change of "preset" (knob, arrows, MIDI program change) moves the menu's pick.
//   * favourites (kLocal, appended after the 0.2.1 table): 32 more PRESET entries after the slots. +FAV saves the current sound with its
//     edits (the engine state) in the first free one, -FAV deletes the favourite that is picked. They are kept in
//     <plugin folder>/profit5-favourites.dat, shared by every project. While a favourite is picked the menu stays on it until the
//     program changes. +FAV first opens the drawn keyboard (kb_open = 1, the skin shows it) with the program's name to edit; SAVE stores
//     the sound as it was when +FAV was pressed, under the typed name. The MPC can report one tap on a drawn key as two presses
//     (mpc-vst-plugins NOTES.md): a key's repeat within 250 ms is ignored.
// Parameter 0 is PM FILT ENV. The host nudges parameter 0 on every prepare: a change applies only after it has held for ~40 ms.
// State: the engine's own "state" string ("P5A <program> <58 values>") is the truth; it travels as the MPC chunk's extra state, and
// nothing is written to the engine while the MPC chunk restores parameter values. A project saved by the 1.x wrapper build stored
// only that string (with its NUL) as its chunk: it is still read (effSetChunk is wrapped below), in all its 0.1.x / 0.2.x forms.

#include "mpc_plugin.h"

#include "engine.h"
#include "params_gen.h"

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef P5_NAME
#define P5_NAME "Profit-5"
#endif
#ifndef P5_UID
#define P5_UID 'P', 'f', 't', '5'
#endif
#define P5_FOURCC(...) MPC_FOURCC(__VA_ARGS__)   // expands P5_UID before MPC_FOURCC counts its arguments
#ifndef P5_VERSION
#define P5_VERSION 2000
#endif

namespace {

const int kChunk = 128;     // the engine renders at most 128 frames a call (wrapper/engine.h)
const int kStateMax = 4096;

mpc::PluginInfo makeInfo()
{
    mpc::PluginInfo i;
    i.name = P5_NAME;
    i.vendor = "sd88me";
    i.uniqueId = P5_FOURCC(P5_UID);
    i.version = P5_VERSION;
    i.isInstrument = true;
    i.numInputs = 0;
    i.numOutputs = 2;
    return i;
}

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint32_t fnv(const char* s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

bool isState(const std::string& s) { return s.compare(0, 4, "P5A ") == 0; }

thread_local int t_pushing = -1;   // the parameter the worker is telling the host about right now (its echo is ignored)

} // namespace

class Profit5Plugin : public mpc::Plugin
{
public:
    Profit5Plugin() : mpc::Plugin(makeInfo())
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const PSpec& s = g_pspecs[i];
            mpc::Param p = (s.flags & kChoice) ? mpc::choiceParam(s.key, s.name, std::vector<std::string>(s.choices, s.choices + s.nchoices), (int)s.def)
                         : (s.flags & kFloat)  ? mpc::floatParam(s.key, s.name, s.min, s.max, s.def, "", 2)
                                               : mpc::intParam(s.key, s.name, (int)s.min, (int)s.max, (int)s.def);
            p.automatable = !(s.flags & kNoAuto);
            addParam(p);
        }
        const size_t n = (size_t)kNumParams;
        held_ = new std::atomic<bool>[n];
        release_ = new std::atomic<bool>[n];
        hostMoved_ = new std::atomic<bool>[n];
        hostAt_ = new std::atomic<long long>[n];
        told_ = new std::atomic<float>[n];
        for (size_t i = 0; i < n; ++i)
        {
            held_[i].store(false); release_[i].store(false); hostMoved_[i].store(false);
            hostAt_[i].store(0); told_[i].store(-1.0e9f);
        }
        lastPos_.assign(n, -1.0f);
        textHash_.assign(n, 0u);

        pthread_mutexattr_t ma;   // recursive + priority-inheriting: audio waiting on a short worker call lifts it
        pthread_mutexattr_init(&ma);
        pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
        pthread_mutex_init(&lock_, &ma);
        pthread_mutexattr_destroy(&ma);

        // The engine scans the folder it is given for .h2p / .zip patches. 1.x gave it <plugin folder>/banks (MODULE_SUBDIR "banks"):
        // keep that, so imported patches and the projects that picked them stay where they were.
        // (Not mpc::contentPath(): a function-local static std::string segfaults at dlclose.)
        const std::string folder = mpc::detail::computePluginFolder();
        banksDir_ = folder + "/banks";
        eng_ = mpc_engine();
        inst_ = eng_->create(banksDir_.c_str());
        char b[64];
        for (int i = 0; i < kPresetSlots; ++i)
        {
            std::snprintf(b, sizeof b, "preset:%d", i);
            char name[64];
            addProgram(engineGet(b, name, sizeof name) ? name : "Init", {});
        }
        for (int i = 0; i < kFavSlots; ++i) addProgram("", {});
        favFile_ = folder + "/profit5-favourites.dat";
        loadFavs();
        worker_ = std::thread(&Profit5Plugin::workerMain, this);
    }

    ~Profit5Plugin() override
    {
        quit_.store(true);
        if (worker_.joinable()) worker_.join();
        if (inst_) eng_->destroy(inst_);
        pthread_mutex_destroy(&lock_);
        delete[] held_;
        delete[] release_;
        delete[] hostMoved_;
        delete[] hostAt_;
        delete[] told_;
    }

    void prepare(double, int) override {}

    // ---- audio ----------------------------------------------------------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        if (!inst_) return;
        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        int frame = 0, mi = 0;
        while (frame < ctx.numFrames)
        {
            while (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset <= frame)
            {
                const mpc::MidiEvent& e = ctx.midi[mi++];
                Lock l(this);
                eng_->midi(inst_, e.data, e.size);
            }
            int end = ctx.numFrames;
            if (mi < ctx.numMidiEvents) end = std::min(std::max(ctx.midi[mi].sampleOffset, frame + 1), ctx.numFrames);
            while (frame < end)
            {
                const int n = std::min(end - frame, kChunk);
                { Lock l(this); eng_->render(inst_, pcm_, n); }
                for (int i = 0; i < n; ++i)
                {
                    L[frame + i] = pcm_[2 * i] * (1.0f / 32768.0f);
                    if (R) R[frame + i] = pcm_[2 * i + 1] * (1.0f / 32768.0f);
                }
                frame += n;
            }
        }
    }

    // ---- parameters -----------------------------------------------------------------------------------------------------------
    void parameterChanged(int index, float plain) override
    {
        if (index < 0 || index >= kNumParams || !inst_) return;
        if (t_pushing == index) return;          // the host echoing what the worker just told it
        const PSpec& s = g_pspecs[index];
        if (s.flags & kLocal) { localChanged(index, plain); return; }
        hostAt_[index].store(nowMs());
        if (loading_.load()) return;             // an MPC chunk is restoring values: the engine state follows as extra state
        if (s.flags & kText) return;             // a readout's counter: nothing to send
        if (s.flags & kMomentary)
        {
            if (plain < 0.5f) { held_[index].store(false); return; }
            if (held_[index].exchange(true)) return;
            if (s.flags & kStep) stepTarget(index);
            else engineSet(s.key, "1");
            release_[index].store(true);
            return;
        }
        char buf[32];
        float v = plain;
        if (s.flags & kFloat)
        {
            v = std::min(std::max(v, s.min), s.max);
            std::snprintf(buf, sizeof buf, "%.6g", v);
        }
        else   // whole numbers and options: settle toward the move, then send
        {
            const float span = s.max - s.min;
            const float pos = getNormalised(index) * span;
            const float cur = told_[index] > -1.0e8f ? told_[index] - s.min : pos;
            float steps = settle(pos, cur, lastPos_[(size_t)index]);
            // The data wheel on the PROGRAM readout moves the host value by more than one step per click on a long range: a small
            // off-step move there is one step in its direction (a drag or Q-Link still lands where it points).
            if ((s.flags & kPreset) && std::fabs(pos - std::round(pos)) > 0.001f && std::fabs(pos - cur) < 3.0f && pos != cur)
                steps = cur + (pos > cur ? 1.0f : -1.0f);
            lastPos_[(size_t)index] = pos;
            steps = std::min(std::max(steps, 0.0f), span);
            v = s.min + steps;
            // Where it landed reaches the host from the worker ~10 ms later: the host's own value is left as it set it until then.
            if (std::fabs(getNormalised(index) * span - steps) > 0.001f) hostMoved_[index].store(true);
            std::snprintf(buf, sizeof buf, "%d", (int)std::lround(v));
        }
        if (index == 0)   // debounced (see the header comment)
        {
            pend0Value_.store(v);
            pend0At_.store(nowMs());
            return;
        }
        told_[index] = v;
        engineSet(s.key, buf);
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        if (index < 0 || index >= kNumParams) return false;
        const PSpec& s = g_pspecs[index];
        char buf[128];
        if (index == kFavName) { std::lock_guard<std::mutex> g(favMu_); out = kbName_ + "_"; return true; }
        if (s.flags & kText) { out = engineGet(s.key, buf, sizeof buf) ? buf : ""; return true; }
        if (s.flags & kMomentary) { out = ""; return true; }
        if (s.flags & kFloat) { std::snprintf(buf, sizeof buf, "%.1f", plain); out = buf; return true; }   // 0.0 .. 10.0, as the panel
        if (s.flags & kPreset)   // the slot's name, as the PRESET menu shows it
        {
            char k[24];
            std::snprintf(k, sizeof k, "preset:%d", (int)std::lround(plain));
            if (engineGet(k, buf, sizeof buf)) { out = buf; return true; }
        }
        return false;
    }

    // ---- state ----------------------------------------------------------------------------------------------------------------
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const std::string s = engineState();
        out.assign(s.begin(), s.end());
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        if (!data || !size) return;
        ignoreProgUntil_.store(nowMs() + 2000);   // the host re-picks the saved program after a project load: the project's sound wins
        favActive_.store(-1);
        kbOpen_.store(false);
        kbDirty_.store(true);   // a project saved with the keyboard open restored kb_open = 1: push it closed
        applyState(std::string(reinterpret_cast<const char*>(data), strnlen(reinterpret_cast<const char*>(data), size)));
    }

    // A project saved by the 1.x wrapper build: its chunk is the engine's state string, NUL-terminated. Called from the wrapped effSetChunk.
    bool loadLegacy(const char* data, size_t size)
    {
        if (!data || !size) return false;
        const std::string s(data, strnlen(data, size));
        if (!isState(s)) return false;
        ignoreProgUntil_.store(nowMs() + 2000);
        favActive_.store(-1);
        applyState(s);
        return true;
    }

    // The MPC picked a program in its PRESET menu (the wrapped effSetProgram; the framework has already stored the pick).
    void programPicked(int prog)
    {
        if (prog < 0 || prog >= kPresetSlots + kFavSlots || nowMs() < ignoreProgUntil_.load()) return;
        if (prog >= kPresetSlots) { loadFav(prog - kPresetSlots); return; }
        favActive_.store(-1);
        char b[24];
        if (engineGet("preset", b, sizeof b) && std::atoi(b) == prog) return;
        std::snprintf(b, sizeof b, "%d", prog);
        engineSet("preset", b);
        hostMoved_[kPresetParam].store(true);
    }

    std::atomic<bool> loading_{false};   // set around an MPC chunk load (wrapped effSetChunk)
    void requestResync() { resync_.store(true); }

private:
    struct Lock
    {
        explicit Lock(Profit5Plugin* p) : p_(p) { pthread_mutex_lock(&p_->lock_); }
        ~Lock() { pthread_mutex_unlock(&p_->lock_); }
        Profit5Plugin* p_;
    };

    void engineSet(const char* key, const char* val)
    {
        Lock l(this);
        eng_->set_param(inst_, key, val);
    }

    bool engineGet(const char* key, char* buf, int n)
    {
        int r;
        { Lock l(this); r = eng_->get_param(inst_, key, buf, n); }
        if (r <= 0) { buf[0] = 0; return false; }
        buf[r < n ? r : n - 1] = 0;
        return true;
    }

    std::string engineState()
    {
        std::vector<char> buf((size_t)kStateMax);
        return inst_ && engineGet("state", buf.data(), kStateMax) ? std::string(buf.data()) : std::string();
    }

    void applyState(const std::string& s)
    {
        if (inst_ && isState(s)) engineSet("state", s.c_str());
        resync_.store(true);
    }

    // Set a parameter's value without calling parameterChanged (so it is not written back to the engine).
    void store(int i, float plain) { values_[(size_t)i]->store(param(i).toNormalised(plain), std::memory_order_relaxed); }

    // Where a whole-number / option param lands, in steps from its minimum (the 1.x wrapper's rule): a value on a step is a click,
    // a preset or automation; between steps it is a Q-Link / wheel / drag move, rounded toward the way it moves.
    static float settle(float pos, float cur, float last)
    {
        if (std::fabs(pos - std::round(pos)) <= 0.001f) return std::round(pos);
        const float dir = (last >= 0 && std::fabs(pos - last) < 0.5f) ? pos - last : pos - cur;
        if (dir == 0) return std::round(cur);
        return dir > 0 ? std::ceil(pos - 0.001f) : std::floor(pos + 0.001f);
    }

    void stepTarget(int index)   // PROGRAM - / +: step "preset" from the engine's current value, wrapping
    {
        const PSpec& s = g_pspecs[index];
        const PSpec& t = g_pspecs[s.stepTarget];
        char buf[32];
        if (!engineGet(t.key, buf, sizeof buf)) return;
        int v = std::atoi(buf) + (int)s.stepDelta;
        const int lo = (int)t.min, hi = (int)t.max;
        v = v < lo ? hi : v > hi ? lo : v;
        std::snprintf(buf, sizeof buf, "%d", v);
        engineSet(t.key, buf);
        hostMoved_[s.stepTarget].store(true);
    }

    // ---- worker: debounced parameter 0, momentary releases, everything the engine changes by itself -----------------------------
    void pushValue(int i, float plain)
    {
        told_[i] = plain;
        store(i, plain);
        t_pushing = i;
        notifyHostParameterChanged(i);
        t_pushing = -1;
    }

    void workerMain()
    {
        while (!quit_.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            tickOnce(false);
        }
    }

    // One worker pass. force = the offline test: apply a pending parameter 0 now and read back everything.
    void tickOnce(bool force)
    {
        if (!inst_) return;
        const long long now = nowMs();
        ++tick_;
        const long long p0 = pend0At_.load();
        if (p0 && (force || now - p0 >= 40))
        {
            pend0At_.store(0);
            char buf[24];
            std::snprintf(buf, sizeof buf, "%.6g", pend0Value_.load());
            engineSet(g_pspecs[0].key, buf);
            told_[0] = -1.0e9f;   // read back below
            hostMoved_[0].store(true);
        }
        for (int i = 0; i < kNumParams; ++i)
            if (release_[i].exchange(false)) { held_[i].store(false); pushValue(i, 0.0f); }
        if (resync_.exchange(false))
        {
            for (int i = 0; i < kNumParams; ++i) { told_[i].store(-1.0e9f); hostMoved_[i].store(true); }
            std::fill(textHash_.begin(), textHash_.end(), 0u);
        }

        // display_rev moves on anything that changes many values (program, state, a received CC): then every value is read back
        char buf[128];
        bool full = force;
        if (engineGet("display_rev", buf, sizeof buf))
        {
            const uint32_t r = fnv(buf) | 1u;
            if (r != lastRev_) { lastRev_ = r; full = true; }
        }
        int changed = 0;
        for (int i = 0; i < kNumEngineParams; ++i)
        {
            const PSpec& s = g_pspecs[i];
            const bool moved = hostMoved_[i].exchange(false);
            if (!(moved || full) || !(s.flags & kPolled)) continue;
            if (i == 0 && pend0At_.load()) { if (moved) hostMoved_[0].store(true); continue; }
            // while the host is still moving it, don't send the engine's value back under the finger; read it back once it has rested
            const long long at = hostAt_[i].load();
            if (!force && now - at < 300) { hostMoved_[i].store(true); continue; }
            if (!engineGet(s.key, buf, sizeof buf)) continue;
            float v = (float)std::atof(buf);
            v = std::min(std::max(v, s.min), s.max);
            // A host write between the check above and here would be overwritten by the value just read: look again (host_sim's sweep
            // caught this: set 0.43, read back the 1.0 of the program loaded before)
            if (hostAt_[i].load() != at) { hostMoved_[i].store(true); continue; }
            if (moved || std::fabs(v - told_[i]) > 1e-4f) { pushValue(i, v); if (!moved) ++changed; }
        }

        // the PRESET menu follows the engine's program (knob, arrows, MIDI program change, a loaded state); a picked favourite stays
        // picked until the program changes
        if (engineGet("preset", buf, sizeof buf))
        {
            int prog = std::atoi(buf);
            const int fav = favActive_.load();
            if (fav >= 0 && prog == favBase_.load()) prog = kPresetSlots + fav;
            else if (fav >= 0) favActive_.store(-1);
            if (prog >= 0 && prog < kPresetSlots + kFavSlots && prog != currentProgram_) { currentProgram_ = prog; wantDisplay_ = true; }
        }
        if (favDirty_.exchange(false)) { saveFavs(); wantDisplay_ = true; }   // new names in the PRESET menu
        if (kbDirty_.exchange(false))   // the keyboard opened or closed, or the typed name changed
        {
            pushValue(kKbOpen, kbOpen_.load() ? 1.0f : 0.0f);
            counter_ = (counter_ + 1) % 1024;
            pushValue(kFavName, (float)counter_);
            wantDisplay_ = true;
        }

        // text readouts: every 100 ms; a changed text bumps the counter (the host re-reads text when the value moves)
        bool textChanged = false;
        if (force || full || tick_ % 10 == 0)
            for (int i = 0; i < kNumEngineParams; ++i)
            {
                const PSpec& s = g_pspecs[i];
                if (!(s.flags & kText)) continue;
                const uint32_t h = engineGet(s.key, buf, sizeof buf) ? fnv(buf) | 1u : 1u;
                if (h == textHash_[(size_t)i]) continue;
                textHash_[(size_t)i] = h;
                textChanged = true;
                counter_ = (counter_ + 1) % 1024;
                pushValue(i, (float)counter_);
            }
        // Many values at once or new text: one display update, at most every 100 ms. Never periodically: it re-lists programs and
        // closes the PRESET menu.
        if (textChanged || changed > 8) wantDisplay_ = true;
        if (wantDisplay_ && (force || now - lastDisplay_ >= 100))
        {
            wantDisplay_ = false;
            lastDisplay_ = now;
            requestDisplayUpdate();
        }
    }

    // ---- favourites (kLocal) ----------------------------------------------------------------------------------------------------
    struct Fav { bool used = false; std::string name, state; };

    // "P5F1", slot count, then per slot: used (1 byte), name length + name, state length + state (u32 little endian lengths)
    void loadFavs()
    {
        std::lock_guard<std::mutex> g(favMu_);
        favs_.assign(kFavSlots, Fav());
        std::ifstream f(favFile_, std::ios::binary);
        char magic[4] = {};
        uint32_t n = 0;
        if (f.read(magic, 4) && !std::memcmp(magic, "P5F1", 4) && f.read(reinterpret_cast<char*>(&n), 4))
            for (uint32_t i = 0; i < n && i < (uint32_t)kFavSlots; ++i)
            {
                char used = 0;
                uint32_t ln = 0, ls = 0;
                if (!f.read(&used, 1) || !f.read(reinterpret_cast<char*>(&ln), 4) || ln > 256) break;
                std::string name(ln, '\0');
                if (ln && !f.read(&name[0], ln)) break;
                if (!f.read(reinterpret_cast<char*>(&ls), 4) || ls > (uint32_t)kStateMax) break;
                std::string st(ls, '\0');
                if (ls && !f.read(&st[0], ls)) break;
                favs_[i].used = used != 0 && isState(st);
                favs_[i].name = name;
                favs_[i].state = st;
            }
        for (int i = 0; i < kFavSlots; ++i) nameProgram(i);
    }

    void saveFavs()   // worker thread: a snapshot under the lock, then the file (.tmp + rename)
    {
        std::vector<Fav> snap;
        { std::lock_guard<std::mutex> g(favMu_); snap = favs_; }
        const std::string tmp = favFile_ + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return;
            const uint32_t n = (uint32_t)snap.size();
            f.write("P5F1", 4);
            f.write(reinterpret_cast<const char*>(&n), 4);
            for (const Fav& v : snap)
            {
                const char used = v.used ? 1 : 0;
                const uint32_t ln = (uint32_t)v.name.size(), ls = (uint32_t)v.state.size();
                f.write(&used, 1);
                f.write(reinterpret_cast<const char*>(&ln), 4);
                f.write(v.name.data(), ln);
                f.write(reinterpret_cast<const char*>(&ls), 4);
                f.write(v.state.data(), ls);
            }
        }
        std::rename(tmp.c_str(), favFile_.c_str());
    }

    void nameProgram(int i)   // favMu_ held
    {
        char b[16];
        std::snprintf(b, sizeof b, "F%02d ", i + 1);
        programs_[(size_t)(kPresetSlots + i)].name = b + (favs_[(size_t)i].used ? favs_[(size_t)i].name : std::string("--"));
    }

    void loadFav(int slot)
    {
        std::string st;
        { std::lock_guard<std::mutex> g(favMu_); if (slot >= 0 && slot < kFavSlots && favs_[(size_t)slot].used) st = favs_[(size_t)slot].state; }
        if (st.empty()) return;
        favBase_.store(std::atoi(st.c_str() + 4));   // "P5A <program> ...": the program the favourite was made from
        favActive_.store(slot);
        applyState(st);
    }

    void closeKeyboard()
    {
        if (kbOpen_.exchange(false)) kbDirty_.store(true);
    }

    void typeKey(int k)   // favMu_ held; k = 0 .. kKbCount - 1
    {
        const int nchars = (int)sizeof kKbChars - 1;
        if (k < nchars || k == nchars)   // a character or space
        {
            if ((int)kbName_.size() >= kFavNameMax) return;
            char c = k < nchars ? kKbChars[k] : ' ';
            const bool cap = kbName_.empty() || kbName_.back() == ' ';
            if (c >= 'A' && c <= 'Z')
            {
                if (cap == kbShift_) c = (char)(c - 'A' + 'a');   // capital at the start and after a space; SHIFT flips the next letter
                kbShift_ = false;
            }
            kbName_ += c;
        }
        else if (k == nchars + 1) { if (!kbName_.empty()) kbName_.pop_back(); }
        else if (k == nchars + 2) kbShift_ = !kbShift_;
    }

    void localChanged(int index, float plain)
    {
        if (!(g_pspecs[index].flags & kMomentary)) return;   // kb_open / fav_name: set by the plugin only
        if (plain < 0.5f) { held_[index].store(false); return; }
        if (held_[index].exchange(true)) return;
        release_[index].store(true);   // a momentary: back to 0 on the next worker pass
        const long long now = nowMs();
        if (now - lastFire_[index - kFavAdd] < 250) return;   // the second report of one tap
        lastFire_[index - kFavAdd] = now;
        if (index >= kKbFirst && index < kKbFirst + kKbCount)
        {
            if (!kbOpen_.load()) return;
            const int k = index - kKbFirst, nchars = (int)sizeof kKbChars - 1;
            if (k == nchars + 3) { closeKeyboard(); return; }   // CANCEL
            if (k == nchars + 4) { saveTyped(); return; }       // SAVE
            { std::lock_guard<std::mutex> g(favMu_); typeKey(k); }
            kbDirty_.store(true);
            return;
        }
        if (index == kFavAdd)   // open the keyboard on the program's name; the sound is taken now
        {
            const std::string st = engineState();
            if (!isState(st)) return;
            char b[64];
            std::string name = engineGet("program_name", b, sizeof b) ? b : "";
            if (name.size() > (size_t)kFavNameMax) name.resize((size_t)kFavNameMax);
            {
                std::lock_guard<std::mutex> g(favMu_);
                kbState_ = st;
                kbName_ = name;
                kbShift_ = false;
            }
            kbOpen_.store(true);
            kbDirty_.store(true);
            return;
        }
        localVerb(index);
    }

    void saveTyped()
    {
        Fav v;
        {
            std::lock_guard<std::mutex> g(favMu_);
            v.state = kbState_;
            v.name = kbName_;
        }
        while (!v.name.empty() && v.name.back() == ' ') v.name.pop_back();
        if (v.name.empty()) v.name = "Favourite";
        closeKeyboard();
        storeFav(v);
    }

    void storeFav(Fav v)
    {
        {
            if (!isState(v.state)) return;
            v.used = true;
            int slot = -1;
            {
                std::lock_guard<std::mutex> g(favMu_);
                for (int i = 0; i < kFavSlots && slot < 0; ++i) if (!favs_[(size_t)i].used) slot = i;
                if (slot >= 0) { favs_[(size_t)slot] = v; nameProgram(slot); }
            }
            if (slot >= 0)
            {
                favBase_.store(std::atoi(v.state.c_str() + 4));   // the new favourite is now the picked one
                favActive_.store(slot);
                favDirty_.store(true);
            }
        }
    }

    void localVerb(int index)
    {
        if (index == kFavDel)   // the favourite that is picked now
        {
            const int fav = favActive_.load();
            if (fav < 0) return;
            bool had;
            {
                std::lock_guard<std::mutex> g(favMu_);
                had = favs_[(size_t)fav].used;
                if (had) { favs_[(size_t)fav] = Fav(); nameProgram(fav); }
            }
            favActive_.store(-1);
            if (had) favDirty_.store(true);
        }
    }

public:
#ifdef P5_TEST
    void dbgTick() { tickOnce(true); }
    std::string dbgEngine(const char* key)
    {
        char b[kStateMax];
        return engineGet(key, b, sizeof b) ? std::string(b) : std::string();
    }
    void dbgEngineSet(const char* key, const char* v) { engineSet(key, v); }
    void dbgNoIgnore() { ignoreProgUntil_.store(0); }
    void dbgNoRepeatGuard() { std::fill(std::begin(lastFire_), std::end(lastFire_), 0LL); }
    int dbgFavCount()
    {
        std::lock_guard<std::mutex> g(favMu_);
        int n = 0;
        for (const Fav& v : favs_) n += v.used ? 1 : 0;
        return n;
    }
#endif

private:
    const mpc_engine_t* eng_ = nullptr;
    void* inst_ = nullptr;
    std::string banksDir_, favFile_;
    std::mutex favMu_;
    std::vector<Fav> favs_;
    std::atomic<bool> favDirty_{false};
    std::atomic<int> favActive_{-1};   // the favourite picked now (-1 = none)
    std::atomic<int> favBase_{-1};     // the engine program it was made from
    std::atomic<bool> kbOpen_{false}, kbDirty_{false};
    std::string kbName_, kbState_;     // favMu_: the name being typed, the sound +FAV took
    bool kbShift_ = false;
    long long lastFire_[kNumParams] = {0};   // per local momentary (from kFavAdd): when it last fired
    pthread_mutex_t lock_;   // one engine call at a time; held for microseconds
    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> resync_{true};
    std::atomic<long long> pend0At_{0};
    std::atomic<float> pend0Value_{0.0f};
    std::atomic<bool>* held_ = nullptr;
    std::atomic<bool>* release_ = nullptr;
    std::atomic<bool>* hostMoved_ = nullptr;   // read this one back and tell the host on the next pass
    std::atomic<long long>* hostAt_ = nullptr; // when the host last moved it (ms), per parameter
    std::atomic<float>* told_ = nullptr;       // the value the host was last told (or sent), per parameter
    std::vector<float> lastPos_;               // the host's last position, for settle()
    std::vector<uint32_t> textHash_;
    std::atomic<long long> ignoreProgUntil_{0};
    int16_t pcm_[kChunk * 2] = {0};
    uint32_t lastRev_ = 0;
    int counter_ = 0;
    long tick_ = 0;
    bool wantDisplay_ = false;
    long long lastDisplay_ = 0;
};

// The same entry as MPC_PLUGIN_ENTRY, plus a wrapped effSetChunk and effSetProgram: an "MPCP" chunk loads with engine writes held
// back (the engine state in its extra part is the truth); a 1.x chunk (the engine's "P5A ..." string) goes to the engine as before.
// A program pick in the PRESET menu loads that slot.
namespace {
VstIntPtr (*g_origDispatcher)(AEffect*, VstInt32, VstInt32, VstIntPtr, void*, float) = nullptr;

VstIntPtr p5Dispatcher(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    Profit5Plugin* p = static_cast<Profit5Plugin*>(e->object);
    if (opcode == effSetChunk && p && ptr && value > 0)
    {
        if (value >= 4 && std::memcmp(ptr, "MPCP", 4) == 0)
        {
            p->loading_.store(true);
            const VstIntPtr r = g_origDispatcher(e, opcode, index, value, ptr, opt);
            p->loading_.store(false);
            p->requestResync();
            return r;
        }
        return p->loadLegacy(static_cast<const char*>(ptr), (size_t)value) ? 1 : 0;
    }
    if (opcode == effSetProgram && p)
    {
        const VstIntPtr r = g_origDispatcher(e, opcode, index, value, ptr, opt);
        p->programPicked((int)value);
        return r;
    }
    return g_origDispatcher(e, opcode, index, value, ptr, opt);
}
} // namespace

#ifdef P5_TEST   // offline test hooks (tests/host_test.cpp); absent from the release build
extern "C" MPC_EXPORT void p5_dbg_tick(AEffect* e) { static_cast<Profit5Plugin*>(e->object)->dbgTick(); }
extern "C" MPC_EXPORT int p5_dbg_engine(AEffect* e, const char* key, char* out, int n)
{
    const std::string s = static_cast<Profit5Plugin*>(e->object)->dbgEngine(key);
    std::snprintf(out, (size_t)n, "%s", s.c_str());
    return (int)s.size();
}
extern "C" MPC_EXPORT void p5_dbg_engine_set(AEffect* e, const char* key, const char* v) { static_cast<Profit5Plugin*>(e->object)->dbgEngineSet(key, v); }
extern "C" MPC_EXPORT int p5_dbg_favs(AEffect* e) { return static_cast<Profit5Plugin*>(e->object)->dbgFavCount(); }
extern "C" MPC_EXPORT void p5_dbg_no_repeat_guard(AEffect* e) { static_cast<Profit5Plugin*>(e->object)->dbgNoRepeatGuard(); }
extern "C" MPC_EXPORT void p5_dbg_no_ignore(AEffect* e) { static_cast<Profit5Plugin*>(e->object)->dbgNoIgnore(); }
#endif

extern "C" MPC_EXPORT AEffect* VSTPluginMain(audioMasterCallback host)
{
    if (host != nullptr && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0) return nullptr;   // host is VST1 only
    AEffect* e = mpc::detail::createEffect(new Profit5Plugin(), host);
    if (e && !g_origDispatcher) g_origDispatcher = e->dispatcher;
    if (e) e->dispatcher = p5Dispatcher;
    return e;
}
