#define _USE_MATH_DEFINES
// Headless engine tests + real-time load benchmark. No audio hardware needed.
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include "airdaw/engine.h"
#include "airdaw/session.h"
#include "airdaw/work_pool.h"
#include <filesystem>
#include <fstream>

using namespace airdaw;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double SR = 96000.0;
static const int N = 64;

struct Rig {
  Engine e{SR};
  std::vector<std::vector<float>> in, out;
  Rig() : in(8, std::vector<float>(N)), out(2, std::vector<float>(N)) {}
  void sine(int ch, float amp, int offset = 0) {
    for (int i = 0; i < N; ++i) in[ch][i] = amp * std::sin(2 * M_PI * 1000.0 * (offset + i) / SR);
  }
  // run `blocks` blocks of sine on input 0 and return last block peak L,R
  void run(int blocks, float amp, float& pl, float& pr) {
    const float* ip[8]; float* op[2] = {out[0].data(), out[1].data()};
    for (int c = 0; c < 8; ++c) ip[c] = in[c].data();
    for (int b = 0; b < blocks; ++b) { sine(0, amp, b * N); e.process(ip, 8, op, 2, N); }
    pl = pr = 0;
    for (int i = 0; i < N; ++i) { pl = std::max(pl, std::fabs(out[0][i])); pr = std::max(pr, std::fabs(out[1][i])); }
  }
};

// ---- recording helpers ----
static float sigAt(long k) { return 0.5f * std::sin(2 * M_PI * 1000.0 * k / SR); }
// Feeds sigAt(k) on input 0 for `frames` frames in odd-sized blocks; returns left output.
static std::vector<float> stream(Engine& e, long& k, int frames, int blk = 100) {
  std::vector<float> outL; std::vector<float> in(blk), l(blk), r(blk);
  for (int done = 0; done < frames; done += blk) {
    int n = std::min(blk, frames - done);
    for (int i = 0; i < n; ++i) in[i] = sigAt(k + i);
    const float* ip[1] = {in.data()}; float* op[2] = {l.data(), r.data()};
    e.process(ip, 1, op, 2, n); k += n;
    outL.insert(outL.end(), l.begin(), l.begin() + n);
  }
  return outL;
}
static std::vector<std::string> g_tmpDirs;
static std::string tmpDir() {
  static int n = 0;
  auto p = std::filesystem::temp_directory_path() /
           ("airdaw_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(n++));
  std::filesystem::create_directories(p); g_tmpDirs.push_back(p.string()); return p.string();
}
static const bool kQuick = std::getenv("AIRDAW_QUICK") != nullptr;  // short runs (for sanitizers)
static float db(float x) { return 20 * std::log10(std::max(x, 1e-9f)); }

// Own function (own stack frame): MSVC gives every block-scoped Engine in main() its own stack slot.
static void testMetronome() {
  { const int beat = 48000;  // 120 BPM at 96 kHz
    const int clickLen = (int)(SR * 0.03);
    auto peakIn = [](const std::vector<float>& v, size_t a, size_t b) { float p = 0; for (size_t i = a; i < b && i < v.size(); ++i) p = std::max(p, std::fabs(v[i])); return p; };
    auto zeroCross = [](const std::vector<float>& v, size_t a, size_t b) { int z = 0; for (size_t i = a + 1; i < b; ++i) z += (v[i - 1] < 0) != (v[i] < 0); return z; };
    { Engine e(SR); auto& m = e.metronome(); m.enabled = true; m.bpm = 120; m.beatsPerBar = 4; m.levelDb = 0;
      e.transport().play(); long k = 0; auto out = stream(e, k, 5 * beat, 64);
      bool grid = true;
      for (int b = 0; b < 5; ++b) {
        grid &= peakIn(out, (size_t)b * beat, (size_t)b * beat + clickLen) > 0.3f;               // click present
        grid &= peakIn(out, (size_t)b * beat + clickLen, (size_t)(b + 1) * beat) == 0.f;         // silence between
      }
      CHECK(grid, "metronome clicks not on the beat grid");
      const int zAccent = zeroCross(out, 0, clickLen), zBeat = zeroCross(out, beat, beat + clickLen), zBar = zeroCross(out, 4 * beat, 4 * beat + clickLen);
      CHECK(zAccent > zBeat && zBar == zAccent, "accent on beat 1 (zero crossings %d vs %d, next bar %d)", zAccent, zBeat, zBar);
      e.transport().stop(); auto silent = stream(e, k, 2000, 64);
      CHECK(peakIn(silent, 0, silent.size()) == 0.f, "metronome must be silent when stopped"); }
    { Engine e(SR); auto dir = tmpDir(); e.track(0).inputL = 0; e.track(0).armed = true;  // click is not in the take
      e.metronome().enabled = true; e.metronome().levelDb = 0;
      long k = 0; e.startRecording(dir); stream(e, k, 20000); e.stopRecording(); stream(e, k, 200); e.flushRecording();
      auto f = e.pollFinishedTakes(); bool exact = f.size() == 1;
      if (exact) { WavData w; readWav(f[0].path, w); exact = w.left.size() == 20000; for (size_t i = 0; exact && i < w.left.size(); ++i) exact = w.left[i] == sigAt((long)i); }
      CHECK(exact, "take recorded with metronome on must be bit-exact (no click)"); }
    { Engine e(SR); auto dir = tmpDir(); e.track(0).inputL = 0; e.track(0).armed = true;  // 1-bar count-in
      e.track(0).mute = true;  // keep live monitoring off the output so only the click is measured (recording is pre-fader)
      auto& m = e.metronome(); m.countInBars = 1; m.beatsPerBar = 4; m.bpm = 120; m.levelDb = 0;  // enabled stays off
      long k = 0; e.startRecording(dir);
      CHECK(e.transport().countingIn(), "count-in should be active");
      auto ci = stream(e, k, 4 * beat + 20000, 77);  // 77-sample blocks: the count-in ends mid-block
      CHECK(!e.transport().countingIn(), "count-in should be over");
      bool clicks = true; for (int b = 0; b < 4; ++b) clicks &= peakIn(ci, (size_t)b * beat, (size_t)b * beat + clickLen) > 0.3f;
      CHECK(clicks, "count-in clicks missing");
      CHECK(peakIn(ci, 4 * beat, ci.size()) == 0.f, "no click after count-in when the metronome is off");
      CHECK(e.transport().pos.load() == 20000, "pos after count-in=%lld", (long long)e.transport().pos.load());
      e.stopRecording(); stream(e, k, 200); e.flushRecording(); e.transport().stop();
      auto f = e.pollFinishedTakes(); bool ok = f.size() == 1;
      if (ok) { WavData w; readWav(f[0].path, w); ok = f[0].start == 0 && w.left.size() == 20000;
        for (size_t i = 0; ok && i < w.left.size(); ++i) ok = w.left[i] == sigAt(4L * beat + (long)i); }
      CHECK(ok, "count-in take must start exactly when the count-in ends");
      // Stop during a count-in: nothing recorded, nothing moves.
      e.metronome().countInBars = 2; e.startRecording(dir); stream(e, k, beat); e.transport().stop(); stream(e, k, 2000); e.flushRecording();
      CHECK(e.pollFinishedTakes().empty() && e.transport().pos.load() == 20200 && !e.transport().countingIn(), "stop during count-in");
      std::printf("metronome: clicks on grid, accent on beat 1, not recorded, 1-bar count-in starts the take sample-exact\n"); }
    { Engine a(SR); auto& m = a.metronome(); m.enabled = true; m.bpm = 93.5f; m.beatsPerBar = 7; m.levelDb = -12; m.countInBars = 2;
      auto p = tmpDir() + "/metro.airdaw"; CHECK(saveSession(a, p).ok, "metro save");
      Engine b(SR); CHECK(loadSession(b, p).ok, "metro load"); auto& n = b.metronome();
      CHECK(n.enabled && n.bpm == 93.5f && n.beatsPerBar == 7 && n.levelDb == -12.f && n.countInBars == 2, "metronome settings round trip"); } }
}

int main() {
  // 1. Registry
  auto fx = listEffects();
  std::printf("Airwindows effects in registry: %zu\n", fx.size());
  CHECK(fx.size() > 400, "registry too small");
  CHECK(effectExists("ToTape8") && effectExists("Console7Channel") && effectExists("Monitoring3"), "key plugins missing");
  { auto t = FxInstance::create("ToTape8", SR);
    CHECK(t && t->numParams() > 4, "ToTape8 should expose more than 4 knobs");
    if (t) { std::printf("ToTape8 params (%d):", t->numParams()); for (auto& p : t->params()) std::printf(" %s", p.name.c_str()); std::printf("\n"); } }

  // 2. Unity: mono track at centre pan -> -3 dB each side
  { Rig r; auto& t = r.e.track(0); t.inputL = 0; t.stereo = false; t.pan = 0.f;
    float pl, pr; r.run(30, 0.5f, pl, pr);
    CHECK(std::fabs(db(pl) - (db(0.5f) - 3.01f)) < 0.1f, "centre pan law L=%f", db(pl));
    CHECK(std::fabs(pl - pr) < 1e-5f, "centre symmetric"); }
  // 3. Hard pan
  { Rig r; auto& t = r.e.track(0); t.inputL = 0; t.pan = -1.f;
    float pl, pr; r.run(30, 0.5f, pl, pr);
    CHECK(pl > 0.49f && pr < 1e-4f, "hard left L=%f R=%f", pl, pr); }
  // 4. Fader -6 dB
  { Rig r; auto& t = r.e.track(0); t.inputL = 0; t.pan = -1.f; t.gainDb = -6.f;
    float pl, pr; r.run(30, 0.5f, pl, pr);
    CHECK(std::fabs(db(pl) - db(0.25f)) < 0.2f, "fader -6 got %f", db(pl)); }
  // 5. Mute & solo
  { Rig r; r.e.track(0).inputL = 0; r.e.track(0).mute = true; float pl, pr; r.run(30, 0.5f, pl, pr);
    CHECK(pl < 1e-6f, "mute"); }
  { Rig r; r.e.track(0).inputL = 0; r.e.track(1).inputL = 0; r.e.track(1).solo = true; r.e.track(0).pan = -1; r.e.track(1).pan = 1;
    float pl, pr; r.run(30, 0.5f, pl, pr);
    CHECK(pl < 1e-5f && pr > 0.49f, "solo isolates track 2 (L=%f R=%f)", pl, pr); }
  // 6. Bus routing: track -> bus1 with bus fader -6, master unity
  { Rig r; auto& t = r.e.track(0); t.inputL = 0; t.pan = -1; t.route = 1; r.e.bus(0).gainDb = -6.f;
    float pl, pr; r.run(30, 0.5f, pl, pr);
    CHECK(std::fabs(db(pl) - db(0.25f)) < 0.2f, "bus -6 got %f", db(pl));
    CHECK(r.e.bus(0).meter.peakL > 0.2f, "bus meter"); }
  // 7. Hot-insert Airwindows plugins while running; output must stay finite
  { Rig r; auto& t = r.e.track(0); t.inputL = 0;
    bool a = r.e.setInsert(0, 0, "Console7Channel"), b = r.e.setInsert(0, 1, "ToTape8"), c = r.e.setInsert(200, 0, "Console7Buss");
    CHECK(a && b && c, "insert create");
    CHECK(!r.e.setInsert(0, 2, "NotARealPlugin"), "unknown plugin rejected");
    float pl, pr; r.run(200, 0.5f, pl, pr);
    CHECK(std::isfinite(pl) && std::isfinite(pr) && pl > 0.05f, "chain output L=%f", pl);
    // swap + remove while running, then collect garbage
    CHECK(r.e.setInsert(0, 0, "Density2"), "swap");
    r.run(5, 0.5f, pl, pr);
    CHECK(r.e.setInsert(0, 1, ""), "remove");
    r.run(5, 0.5f, pl, pr);
    r.e.collectGarbage();
    CHECK(std::isfinite(pl), "after swap"); }
  // 8. Parameter change is applied
  { Rig r; r.e.track(0).inputL = 0; r.e.setInsert(0, 0, "ToTape8");
    float pl, pr; r.run(100, 0.5f, pl, pr); float before = pl;
    auto* fxp = r.e.insertAt(0, 0); fxp->setParam(0, 1.0f); fxp->setBypass(false);
    r.run(100, 0.5f, pl, pr);
    CHECK(std::fabs(pl - before) > 1e-4f, "param change had no effect"); }


  // 11. Recording: plain take, bit-exact
  { Engine e(SR); auto dir = tmpDir(); e.track(0).inputL = 0; e.track(0).armed = true; e.track(0).name = "Kick";
    long k = 0; e.startRecording(dir); stream(e, k, 20000); e.stopRecording(); stream(e, k, 200); e.flushRecording();
    auto f = e.pollFinishedTakes();
    CHECK(f.size() == 1, "expected 1 take, got %zu", f.size());
    if (f.size() == 1) {
      WavData w; CHECK(readWav(f[0].path, w), "read wav"); CHECK(w.channels == 1 && w.sampleRate == 96000, "wav format");
      CHECK(w.left.size() == 20000 && f[0].start == 0, "len=%zu start=%lld", w.left.size(), (long long)f[0].start);
      bool exact = w.left.size() == 20000; for (size_t i = 0; exact && i < w.left.size(); ++i) exact = w.left[i] == sigAt((long)i);
      CHECK(exact, "recorded audio is not bit-exact");
      std::printf("record: %s (%zu frames, 32-bit float, bit-exact)\n", f[0].path.c_str(), w.left.size()); } }
  // 12. Punch in/out lands on exact samples
  { Engine e(SR); auto dir = tmpDir(); e.track(0).inputL = 0; e.track(0).armed = true;
    auto& t = e.transport(); t.punchOn = true; t.punchIn = 1000; t.punchOut = 3000;
    long k = 0; e.startRecording(dir); stream(e, k, 5000); e.stopRecording(); stream(e, k, 100); e.flushRecording();
    auto f = e.pollFinishedTakes(); CHECK(f.size() == 1, "punch: takes=%zu", f.size());
    if (f.size() == 1) { WavData w; readWav(f[0].path, w);
      CHECK(f[0].start == 1000 && w.left.size() == 2000, "punch start=%lld len=%zu", (long long)f[0].start, w.left.size());
      bool ex = w.left.size() == 2000; for (size_t i = 0; ex && i < 2000; ++i) ex = w.left[i] == sigAt(1000 + (long)i);
      CHECK(ex, "punch content"); } }
  // 13. Loop recording: each pass is a separate take; pick one and play it back in the loop
  { Engine e(SR); auto dir = tmpDir(); auto& tk = e.track(0); tk.inputL = 0; tk.armed = true; tk.pan = -1.f;
    auto& t = e.transport(); t.loopOn = true; t.loopIn = 1000; t.loopOut = 3000; t.seek(1000);
    long k = 0; stream(e, k, 0);  // apply seek
    e.startRecording(dir); stream(e, k, 6000); e.stopRecording(); stream(e, k, 100); e.flushRecording();
    auto f = e.pollFinishedTakes(); CHECK(f.size() == 3, "loop takes=%zu", f.size());
    std::sort(f.begin(), f.end(), [](auto& a, auto& b) { return a.path < b.path; });
    bool ok = f.size() == 3;
    for (size_t j = 0; ok && j < 3; ++j) { WavData w; readWav(f[j].path, w); ok = w.left.size() == 2000 && f[j].start == 1000;
      for (size_t i = 0; ok && i < 2000; ++i) ok = w.left[i] == sigAt((long)(j * 2000 + i)); }
    CHECK(ok, "loop takes content/start");
    if (f.size() == 3) {
      for (auto& x : f) e.addTake(0, x.path, x.start);
      e.setActiveTake(0, 1);                // second pass
      tk.armed = false; t.punchOn = false; t.seek(1000);
      long k2 = 0; auto out = stream(e, k2, 5000);
      bool pb = true; for (size_t i = 0; pb && i < out.size(); ++i) pb = std::fabs(out[i] - sigAt(2000 + (long)(i % 2000))) < 1e-6f;
      CHECK(pb, "take playback through loop"); std::printf("loop record: 3 takes, pass 2 plays back sample-exact across loop wraps\n"); } }

  // 13b. Metronome: clicks on the beat grid, accent on beat 1, output only (never recorded), count-in
  testMetronome();

  // 14. Session save/load round trip, including plugin parameters, takes, loop/punch
  { auto dir = tmpDir(); std::string sess = dir + "/song.airdaw";
    Engine a(SR); a.track(2).name = "Snare"; a.track(2).gainDb = -4.5f; a.track(2).pan = 0.25f; a.track(2).stereo = true; a.track(2).route = 2; a.track(2).mute = true;
    a.track(2).inputL = 3; a.track(2).inputR = 4; a.bus(1).gainDb = -2.f; a.master().gainDb = -0.5f;
    a.setInsert(2, 0, "ToTape8"); a.setInsert(2, 1, "Density2"); a.setInsert(200, 0, "Console7Buss");
    a.insertAt(2, 0)->setParam(3, 0.321f); a.insertAt(2, 1)->setBypass(true);
    a.transport().loopOn = true; a.transport().loopIn = 500; a.transport().loopOut = 4500; a.transport().punchIn = 10; a.transport().punchOut = 90;
    // a take on disk
    { Engine r(SR); r.track(0).inputL = 0; r.track(0).armed = true; r.track(0).name = "Vox"; long k = 0; r.startRecording(dir); stream(r, k, 3000); r.stopRecording(); stream(r, k, 100); r.flushRecording();
      auto f = r.pollFinishedTakes(); a.track(5).name = "Vox2"; if (!f.empty()) { a.addTake(5, f[0].path, 777); a.setActiveTake(5, 0); } }
    auto sr = saveSession(a, sess); CHECK(sr.ok, "save: %s", sr.error.c_str());
    Engine b(SR); auto lr = loadSession(b, sess); CHECK(lr.ok, "load: %s", lr.error.c_str());
    for (auto& w : lr.warnings) std::printf("  warning: %s\n", w.c_str());
    CHECK(b.track(2).name == "Snare" && std::fabs(b.track(2).gainDb - (-4.5f)) < 1e-6f && std::fabs(b.track(2).pan - 0.25f) < 1e-6f, "channel fields");
    CHECK(b.track(2).stereo && b.track(2).route == 2 && b.track(2).mute && b.track(2).inputL == 3 && b.track(2).inputR == 4, "channel flags");
    CHECK(std::fabs(b.bus(1).gainDb - (-2.f)) < 1e-6f && std::fabs(b.master().gainDb - (-0.5f)) < 1e-6f, "bus/master");
    CHECK(b.insertAt(2, 0) && b.insertAt(2, 0)->name() == "ToTape8" && std::fabs(b.insertAt(2, 0)->getParam(3) - 0.321f) < 1e-6f, "insert param");
    CHECK(b.insertAt(2, 1) && b.insertAt(2, 1)->bypassed() && b.insertAt(200, 0), "bypass/master insert");
    CHECK(b.transport().loopOn && b.transport().loopIn == 500 && b.transport().loopOut == 4500 && b.transport().punchOut == 90, "transport");
    CHECK(b.numTakes(5) == 1 && b.takeAt(5, 0)->start == 777 && b.track(5).take.load() == b.takeAt(5, 0), "takes restored");
    CHECK(sessionToJson(a, dir) == sessionToJson(b, dir), "re-serialised session is identical");
    // corrupt file must fail cleanly, not crash
    { std::ofstream bad(dir + "/bad.airdaw"); bad << "{ not json"; } Engine c(SR); CHECK(!loadSession(c, dir + "/bad.airdaw").ok, "corrupt rejected");
    // missing plugin + missing take -> warnings, still loads
    { auto txt = sessionToJson(a, dir); size_t p = txt.find("Density2"); txt.replace(p, 8, "GoneFX999"); std::ofstream o(dir + "/old.airdaw"); o << txt; }
    Engine d(SR); auto lr2 = loadSession(d, dir + "/old.airdaw"); CHECK(lr2.ok && !lr2.warnings.empty(), "missing plugin should warn, not fail");
    std::printf("session: save/load round trip OK (%zu bytes)\n", sessionToJson(a, dir).size()); }
  // 15. Auto-save: interval, skip-while-recording, skip-if-unchanged, rotation, recovery
  { auto dir = tmpDir(); Engine e(SR); AutoSaver as(e); as.setLocation(dir + "/autosave", "Song");
    as.configure({true, 30, 3});
    CHECK(as.tick(0).empty(), "first tick only starts the timer");
    CHECK(as.tick(29).empty(), "before interval");
    std::string p1 = as.tick(31); CHECK(!p1.empty() && std::filesystem::exists(p1), "saves when due");
    CHECK(as.tick(70).empty(), "unchanged -> no new file");
    e.track(0).gainDb = -3.f; e.transport().recording = true;
    CHECK(as.tick(100).empty(), "never saves while recording");
    e.transport().recording = false;
    std::string p2 = as.tick(131); CHECK(!p2.empty() && p2 != p1, "saves after change once recording stops");
    for (int i = 0; i < 4; ++i) { e.track(0).gainDb = -10.f - i; std::this_thread::sleep_for(std::chrono::milliseconds(1100)); as.tick(200 + 40 * i); }
    int count = 0; for (auto& f : std::filesystem::directory_iterator(dir + "/autosave")) count += f.path().extension() == ".airdaw";
    CHECK(count == 3, "rotation keeps newest 3, found %d", count);
    auto latest = AutoSaver::latestAutosave(dir + "/autosave", "Song"); Engine r(SR);
    CHECK(!latest.empty() && loadSession(r, latest).ok && std::fabs(r.track(0).gainDb - (-13.f)) < 1e-6f, "recover latest autosave");
    as.configure({false, 30, 3}); e.track(0).gainDb = -1.f; CHECK(as.tick(1000).empty() && as.tick(2000).empty(), "disabled saves nothing");
    std::printf("autosave: interval, change detection, recording guard, rotation, recovery OK\n"); }

  // 16. Non-Airwindows inserts (stand-in for hosted VST3): process, session round trip via factory
  { struct Gain2 : InsertProcessor {  // doubles the signal; state blob = its own setting
      std::string n = "TestGain"; std::vector<FxParamInfo> ps{{"Amount", "", 0.5f}}; std::atomic<float> v{0.5f}; bool bp = false; std::string st;
      const std::string& name() const override { return n; }
      std::string sessionId() const override { return "test:gain"; }
      int numParams() const override { return 1; } const std::vector<FxParamInfo>& params() const override { return ps; }
      void setParam(int, float x) override { v = x; } float getParam(int) const override { return v; }
      std::string paramDisplay(int) override { return "x"; } void setBypass(bool b) override { bp = b; } bool bypassed() const override { return bp; }
      std::string saveState() override { return "state-" + std::to_string(int(v * 100)); }
      void loadState(const std::string& s) override { st = s; v = std::stoi(s.substr(6)) / 100.f; }
      void process(float* l, float* r, int n) override { for (int i = 0; i < n; ++i) { l[i] *= 2 * v; r[i] *= 2 * v; } } };
    auto dir = tmpDir(); Engine a(SR); a.setInsertFactory([](const std::string& id, double) -> std::unique_ptr<InsertProcessor> { return id == "test:gain" ? std::make_unique<Gain2>() : nullptr; });
    a.track(0).inputL = 0; a.track(0).pan = -1.f; auto g = std::make_unique<Gain2>(); g->v = 0.25f; a.setInsertProcessor(0, 0, std::move(g));
    long k = 0; auto out = stream(a, k, 400); CHECK(std::fabs(out[300] - sigAt(300) * 0.5f) < 1e-6f, "hosted insert processes audio");
    CHECK(saveSession(a, dir + "/h.airdaw").ok, "save hosted");
    Engine b(SR); b.setInsertFactory(a.insertFactory()); auto lr = loadSession(b, dir + "/h.airdaw");
    CHECK(lr.ok && lr.warnings.empty() && b.insertAt(0, 0) && std::fabs(b.insertAt(0, 0)->getParam(0) - 0.25f) < 1e-6f, "hosted insert restored from state blob");
    Engine c(SR); auto lr2 = loadSession(c, dir + "/h.airdaw"); CHECK(lr2.ok && !lr2.warnings.empty(), "no factory -> warning only");
    std::printf("hosted-insert interface: processing, state blob, factory restore OK\n"); }

  // 17. Multi-core: output bit-identical for any thread count, plus speed-up, pool idle/wake, and recording under load
  { auto build = [&](Engine& e) {
      const char* chain[4] = {"Console7Channel", "Density2", "ToTape8", "Pressure5"};
      for (int t = 0; t < 24; ++t) { e.track(t).inputL = t % 8; e.track(t).stereo = (t % 2); e.track(t).route = t % 5; e.track(t).pan = (t % 7 - 3) / 3.f;
        for (int s2 = 0; s2 < 4; ++s2) e.setInsert(t, s2, chain[s2]); }
      for (int b = 0; b < 4; ++b) { e.setInsert(100 + b, 0, "Console7Buss"); e.setInsert(100 + b, 1, "Density2"); }
      e.setInsert(200, 0, "Console7Buss"); e.setInsert(200, 1, "ADClip8"); };
    auto render = [&](Engine& e, int blocks, std::vector<float>* keep, double* avgMs, double* worstMs) {
      std::vector<std::vector<float>> in(8, std::vector<float>(N)), out(2, std::vector<float>(N));
      const float* ip[8]; float* op[2] = {out[0].data(), out[1].data()};
      for (int c = 0; c < 8; ++c) ip[c] = in[c].data();
      double total = 0, worst = 0;
      for (int b = 0; b < blocks; ++b) {
        for (int c = 0; c < 8; ++c) for (int i = 0; i < N; ++i) in[c][i] = 0.3f * std::sin(2 * M_PI * (200 + 150 * c) * (b * N + i) / SR);
        auto a = std::chrono::steady_clock::now(); e.process(ip, 8, op, 2, N);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); total += ms; worst = std::max(worst, ms);
        if (keep) { keep->insert(keep->end(), out[0].begin(), out[0].end()); keep->insert(keep->end(), out[1].begin(), out[1].end()); }
      }
      if (avgMs) *avgMs = total / blocks; if (worstMs) *worstMs = worst; };
    std::vector<float> ref, par;
    // Several Airwindows effects seed their dither from rand() at construction, so seed it identically per run.
    { std::srand(1234); Engine e(SR); build(e); render(e, 300, &ref, nullptr, nullptr); }
    { std::srand(1234); Engine e(SR); build(e); e.setWorkerThreads(3); render(e, 300, &par, nullptr, nullptr); }
    CHECK(ref.size() == par.size() && std::memcmp(ref.data(), par.data(), ref.size() * sizeof(float)) == 0, "multi-thread output differs from single-thread");
    if (ref.size() == par.size() && std::memcmp(ref.data(), par.data(), ref.size() * sizeof(float)) == 0) std::printf("multicore: output bit-identical with 0 and 3 workers over %zu samples\n", ref.size());
    for (int w : {0, 1, -1}) {
      Engine e(SR); build(e); e.setWorkerThreads(w); double avg, worst; render(e, kQuick ? 5 : 200, nullptr, nullptr, nullptr); render(e, kQuick ? 20 : 2000, nullptr, &avg, &worst);
      std::printf("  workers=%d (threads incl. audio: %d): avg %.3f ms/block, worst %.3f ms, budget %.3f ms -> %.0f%% of budget\n",
                  w, e.workerThreads() + 1, avg, worst, 1000.0 * N / SR, 100 * avg / (1000.0 * N / SR)); }
    // idle -> park -> wake: workers must resume after a long pause
    { Engine e(SR); build(e); e.setWorkerThreads(2); std::vector<float> a1, a2; render(e, 20, nullptr, nullptr, nullptr);
      std::this_thread::sleep_for(std::chrono::milliseconds(600)); double avg; render(e, 50, nullptr, &avg, nullptr); CHECK(avg < 5.0, "pool did not wake after idle (avg %.2f ms)", avg); }
    // recording from many threads at once
    { Engine e(SR); auto dir = tmpDir(); for (int t = 0; t < 8; ++t) { e.track(t).inputL = t; e.track(t).armed = true; e.setInsert(t, 0, "Density2"); e.track(t).name = "T" + std::to_string(t); }
      e.setWorkerThreads(2); e.startRecording(dir);
      std::vector<std::vector<float>> in(8, std::vector<float>(N)), out(2, std::vector<float>(N)); const float* ip[8]; float* op[2] = {out[0].data(), out[1].data()};
      for (int c = 0; c < 8; ++c) ip[c] = in[c].data();
      for (int b = 0; b < (kQuick ? 300 : 1500); ++b) { for (int c = 0; c < 8; ++c) for (int i = 0; i < N; ++i) in[c][i] = 0.1f * (c + 1) * std::sin(2 * M_PI * 440.0 * (b * N + i) / SR); e.process(ip, 8, op, 2, N); }
      e.stopRecording(); e.process(ip, 8, op, 2, N); e.flushRecording(); auto f = e.pollFinishedTakes();
      CHECK(f.size() == 8, "recorded takes=%zu", f.size()); bool ok = f.size() == 8;
      for (auto& x : f) { WavData w; ok &= readWav(x.path, w) && w.left.size() == (kQuick ? 300u : 1500u) * N; int c = x.track;
        for (size_t i = 0; ok && i < w.left.size(); ++i) { float want = 0.1f * (c + 1) * std::sin(2 * M_PI * 440.0 * (double)i / SR); if (std::fabs(w.left[i] - want) > 1e-6f) { ok = false; } } }
      CHECK(ok, "parallel recording content"); std::printf("multicore: 8 tracks recorded in parallel, all bit-accurate\n"); } }

  // 18. Work pool stress: jobs of alternating sizes, 1..3 workers, every result checked
  { long bad = 0; int iters = kQuick ? 1500 : 20000;
    for (int workers : {1, 2, 3}) { WorkPool p; p.start(workers); std::vector<int> in(24), out(24);
      struct Job { int base; std::vector<int>* in; std::vector<int>* out; };
      for (int it = 0; it < iters; ++it) { int n = (it % 3 == 0) ? 4 : 24; for (int i = 0; i < 24; ++i) in[i] = it * 100 + i;
        Job j{it, &in, &out}; p.run(n, [](void* c, int i) { auto* jj = (Job*)c; (*jj->out)[i] = (*jj->in)[i] + jj->base; }, &j);
        for (int i = 0; i < n; ++i) bad += (out[i] != in[i] + it); } }
    CHECK(bad == 0, "work pool produced %ld wrong results", bad); if (!bad) std::printf("work pool: %d jobs x 3 configurations, no wrong results\n", iters * 3); }

  // 9. Load benchmark: 24 tracks x 4 inserts + 4 buses x 2 + master x 2, 96 kHz, 64-sample blocks
  { Engine e(SR);
    const char* chain[4] = {"Console7Channel", "Density2", "ToTape8", "Pressure5"};
    for (int t = 0; t < 24; ++t) { e.track(t).inputL = t % 8; e.track(t).stereo = (t % 2); e.track(t).route = 1 + t % 4;
      for (int s = 0; s < 4; ++s) e.setInsert(t, s, chain[s]); }
    for (int b = 0; b < 4; ++b) { e.setInsert(100 + b, 0, "Console7Buss"); e.setInsert(100 + b, 1, "Density2"); }
    e.setInsert(200, 0, "Console7Buss"); e.setInsert(200, 1, "ADClip8");
    std::vector<std::vector<float>> in(8, std::vector<float>(N)), out(2, std::vector<float>(N));
    const float* ip[8]; float* op[2] = {out[0].data(), out[1].data()};
    for (int c = 0; c < 8; ++c) ip[c] = in[c].data();
    int blocks = kQuick ? 60 : 3000;
    auto t0 = std::chrono::steady_clock::now(); double worst = 0;
    for (int b = 0; b < blocks; ++b) {
      for (int c = 0; c < 8; ++c) for (int i = 0; i < N; ++i) in[c][i] = 0.3f * std::sin(2 * M_PI * (200 + 150 * c) * (b * N + i) / SR);
      auto a = std::chrono::steady_clock::now();
      e.process(ip, 8, op, 2, N);
      worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count());
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    double budget = 1000.0 * N / SR;
    std::printf("FULL LOAD (24 trk x4 inserts + 4 bus x2 + master x2 = 108 inserts): avg %.3f ms/block, worst %.3f ms, budget %.3f ms -> avg DSP %.1f%%\n",
                ms / blocks, worst, budget, 100.0 * (ms / blocks) / budget);
    CHECK(std::isfinite(out[0][0]), "bench finite"); }

  // 10. Per-effect cost (one stereo instance, 96 kHz, 64-sample blocks) for the benchmark chain
  for (const char* nm : {"Console7Channel", "Density2", "ToTape8", "Pressure5", "Console7Buss", "ADClip8"}) {
    auto f = FxInstance::create(nm, SR); std::vector<float> l(N, 0.2f), r(N, 0.2f);
    for (int i = 0; i < N; ++i) l[i] = r[i] = 0.3f * std::sin(i * 0.1f);
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < (kQuick ? 50 : 5000); ++b) f->process(l.data(), r.data(), N);
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (kQuick ? 50 : 5000);
    std::printf("  %-16s %6.2f us/block  (%.2f%% of one core)\n", nm, us, 100 * us / (1e6 * N / SR));
  }
  for (auto& d : g_tmpDirs) std::filesystem::remove_all(d);  // test recordings are large; never leave them behind
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
