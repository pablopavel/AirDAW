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
#include "airdaw/render.h"
#include "airdaw/analysis.h"
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
    // Live input is only heard on armed (monitoring) tracks, so arm every track that has an input.
    for (int t = 0; t < kMaxTracks; ++t) if (e.track(t).inputL.load() >= 0) e.track(t).armed = true;
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

// Metronome: clicks on the beat grid, accent on beat 1, output only (never recorded), count-in.
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

// Two inputs: input 0 = sigAt(k), input 1 = a 300 Hz tone. Returns left/right output.
static float sig2At(long k) { return 0.3f * std::sin(2 * M_PI * 300.0 * k / SR); }
static void stream2(Engine& e, long& k, int frames, std::vector<float>& L, std::vector<float>& R, int blk = 64) {
  std::vector<float> a(blk), b(blk), l(blk), r(blk);
  for (int done = 0; done < frames; done += blk) {
    const int n = std::min(blk, frames - done);
    for (int i = 0; i < n; ++i) { a[i] = sigAt(k + i); b[i] = sig2At(k + i); }
    const float* ip[2] = {a.data(), b.data()}; float* op[2] = {l.data(), r.data()};
    e.process(ip, 2, op, 2, n); k += n;
    L.insert(L.end(), l.begin(), l.begin() + n); R.insert(R.end(), r.begin(), r.begin() + n);
  }
}
static float peakOf(const std::vector<float>& v, size_t from = 0) { float p = 0; for (size_t i = from; i < v.size(); ++i) p = std::max(p, std::fabs(v[i])); return p; }

// Live input is heard only when armed + monitoring (or auditioning); recording does not depend on monitoring.
// Layering: a second track records while the first plays its take back.
static void testMonitoringAndLayering() {
  { Engine e(SR); auto& t = e.track(0); t.inputL = 0; t.pan = -1.f;
    long k = 0; std::vector<float> L, R;
    stream2(e, k, 2000, L, R);
    CHECK(peakOf(L) == 0.f && t.meter.peakL.load() == 0.f, "unarmed track must not pass live input (peak %f)", peakOf(L));
    t.armed = true; L.clear(); R.clear(); stream2(e, k, 2000, L, R);
    CHECK(std::fabs(peakOf(L, 500) - 0.5f) < 0.01f, "armed + monitoring must pass input (peak %f)", peakOf(L, 500));
    t.monitor = false; L.clear(); R.clear(); stream2(e, k, 2000, L, R);
    CHECK(peakOf(L, 500) == 0.f, "monitoring off must silence the input (peak %f)", peakOf(L, 500));
    CHECK(t.meter.peakL.load() > 0.4f, "armed track meter must show the input level even when not monitoring");
    t.armed = false; t.audition = true; L.clear(); R.clear(); stream2(e, k, 2000, L, R);
    CHECK(peakOf(L, 500) > 0.4f, "audition must pass input on an unarmed track"); }
  { Engine e(SR); auto dir = tmpDir(); auto& t = e.track(0); t.inputL = 0; t.armed = true; t.monitor = false;
    long k = 0; std::vector<float> L, R;
    e.startRecording(dir); stream2(e, k, 5000, L, R); e.transport().stop(); stream2(e, k, 64, L, R); e.flushRecording();
    auto f = e.pollFinishedTakes(); bool ok = f.size() == 1;
    if (ok) { WavData w; readWav(f[0].path, w); ok = w.left.size() == 5000; for (size_t i = 0; ok && i < 5000; ++i) ok = w.left[i] == sigAt((long)i); }
    CHECK(ok && peakOf(L) == 0.f, "monitoring off: take must still be bit-exact and nothing audible"); }
  { Engine e(SR); auto dir = tmpDir();
    auto& a = e.track(0); a.inputL = 0; a.armed = true; a.pan = -1.f;
    long k = 0; std::vector<float> L, R;
    e.startRecording(dir); stream2(e, k, 20000, L, R); e.transport().stop(); stream2(e, k, 64, L, R); e.flushRecording();
    auto f1 = e.pollFinishedTakes();
    CHECK(f1.size() == 1, "layering: first take"); if (f1.size() != 1) return;
    e.setActiveTake(0, e.addTake(0, f1[0].path, f1[0].start));
    a.armed = false;
    auto& b = e.track(1); b.inputL = 1; b.armed = true; b.pan = 1.f;  // second take from input 2, hard right
    e.transport().seek(0); long k2 = 0; std::vector<float> L2, R2;
    e.startRecording(dir); stream2(e, k2, 20000, L2, R2); e.transport().stop(); stream2(e, k2, 64, L2, R2); e.flushRecording();
    auto f2 = e.pollFinishedTakes();
    bool ok = f2.size() == 1 && f2[0].track == 1;
    if (ok) { WavData w; readWav(f2[0].path, w); ok = w.left.size() == 20000; for (size_t i = 0; ok && i < 20000; ++i) ok = w.left[i] == sig2At((long)i); }
    CHECK(ok, "layering: track 2 take must hold input 2, bit-exact");
    bool pb = true; for (size_t i = 0; pb && i < 20000; ++i) pb = std::fabs(L2[i] - sigAt((long)i)) < 1e-6f;
    CHECK(pb, "layering: track 1 must play its take back while track 2 records");
    std::printf("monitoring: heard only when armed + monitoring; layering: track 2 overdubs while track 1 plays back\n"); }
}

// Takes bar: loop passes as takes, keep one, remove the others (also the one playing), keep survives save/load.
static void testKeepAndRemoveTakes() {
  Engine e(SR); auto dir = tmpDir(); auto& tk = e.track(0); tk.inputL = 0; tk.armed = true; tk.pan = -1.f;
  auto& t = e.transport(); t.loopOn = true; t.loopIn = 1000; t.loopOut = 3000; t.seek(1000);
  long k = 0; stream(e, k, 0);
  e.startRecording(dir); stream(e, k, 6000); e.transport().stop(); stream(e, k, 100); e.flushRecording();
  auto f = e.pollFinishedTakes(); std::sort(f.begin(), f.end(), [](auto& a, auto& b) { return a.path < b.path; });
  CHECK(f.size() == 3, "3 loop passes, got %zu", f.size()); if (f.size() != 3) return;
  for (auto& x : f) e.setActiveTake(0, e.addTake(0, x.path, x.start));  // like the app: the newest pass is active
  const Take* playing = e.takeAt(0, 2);
  e.setTakeKeep(0, 1, true);
  for (int i = e.numTakes(0) - 1; i >= 0; --i) if (!e.takeAt(0, i)->keep) e.removeTake(0, i);
  CHECK(e.numTakes(0) == 1 && e.takeAt(0, 0)->keep && e.takeAt(0, 0)->path == f[1].path, "only the kept take remains");
  CHECK(e.track(0).take.load() == nullptr, "removing the playing take must deactivate it");
  CHECK(playing->l.size() == 2000, "a removed take's audio stays valid in memory");
  e.setActiveTake(0, 0); tk.armed = false;
  CHECK(saveSession(e, dir + "/k.airdaw").ok, "save kept");
  Engine b(SR); auto r = loadSession(b, dir + "/k.airdaw");
  CHECK(r.ok && b.numTakes(0) == 1 && b.takeAt(0, 0)->keep && b.track(0).take.load() == b.takeAt(0, 0), "keep flag and active take survive save/load");
  std::printf("takes: keep / remove unkept / save and reload OK\n");
}

// Mix export: sample-exact against the take, 24-bit within rounding, ranges, no click/live input, cancel.
static void testRenderMix() {
  auto dir = tmpDir();
  { WavWriter w; w.open(dir + "/src.wav", 1, (int)SR); std::vector<float> s(20000); for (size_t i = 0; i < s.size(); ++i) s[i] = sigAt((long)i);
    w.write(s.data(), s.size()); w.close(); }
  Engine e(SR); auto& t = e.track(0); t.pan = -1.f;
  e.setActiveTake(0, e.addTake(0, dir + "/src.wav", 1000));
  t.inputL = 0; t.armed = true; e.metronome().enabled = true; e.metronome().levelDb = 0;  // must not end up in the export
  CHECK(songEnd(e) == 21000, "song end %lld", (long long)songEnd(e));
  RenderOptions o; o.start = 0; o.end = songEnd(e); o.tailSeconds = 0.01; o.bits = 32; o.blockSize = 333;
  auto r = renderMix(e, dir + "/mix32.wav", o);
  WavData w; bool ok = r.ok && readWav(dir + "/mix32.wav", w) && w.channels == 2 && (int64_t)w.left.size() == 21000 + 960;
  for (size_t i = 0; ok && i < w.left.size(); ++i) {
    const float want = (i >= 1000 && i < 21000) ? sigAt((long)i - 1000) : 0.f;
    ok = w.left[i] == want && w.right[i] == 0.f;
  }
  CHECK(ok, "32-bit export must equal the take sample for sample (ok=%d frames=%zu err=%s)", (int)r.ok, w.left.size(), r.error.c_str());
  CHECK(std::fabs(r.peak - 0.5f) < 0.001f, "export peak %f", r.peak);
  o.bits = 24; r = renderMix(e, dir + "/mix24.wav", o);
  WavData w24; ok = r.ok && readWav(dir + "/mix24.wav", w24) && w24.left.size() == w.left.size();
  for (size_t i = 0; ok && i < w24.left.size(); ++i) ok = std::fabs(w24.left[i] - w.left[i]) <= 1.0f / 8388608.f;
  CHECK(ok, "24-bit export within one LSB");
  o.start = 5000; o.end = 8000; o.tailSeconds = 0; o.bits = 32; r = renderMix(e, dir + "/part.wav", o);
  WavData wp; ok = r.ok && readWav(dir + "/part.wav", wp) && wp.left.size() == 3000;
  for (size_t i = 0; ok && i < 3000; ++i) ok = wp.left[i] == sigAt(4000 + (long)i);
  CHECK(ok, "range export");
  int calls = 0; o.start = 0; o.end = songEnd(e);
  r = renderMix(e, dir + "/cancel.wav", o, [&](double) { return ++calls < 3; });
  CHECK(!r.ok && r.error == "cancelled" && !std::filesystem::exists(dir + "/cancel.wav"), "cancel removes the partial file");
  std::printf("export: 32-bit sample-exact, 24-bit within 1 LSB, ranges, no click/live input, cancel cleans up\n");
}

// Loudness (EBU Tech 3341/3342 reference cases), true peak, correlation, and the engine's master tap.
static void testLoudness() {
  auto feed = [](LoudnessMeter& m, double seconds, auto gen) {  // gen(i) -> {l, r}
    const size_t n = (size_t)(seconds * m.sampleRate()); std::vector<float> buf(2 * 4096);
    for (size_t done = 0; done < n;) {
      const size_t k = std::min<size_t>(4096, n - done);
      for (size_t i = 0; i < k; ++i) { auto [l, r] = gen(done + i); buf[2 * i] = l; buf[2 * i + 1] = r; }
      m.process(buf.data(), k); done += k;
    }
  };
  auto sine = [](double sr, double dbfs, double f = 997.0) {
    const double a = std::pow(10.0, dbfs / 20.0);
    return [=](size_t i) { const float v = (float)(a * std::sin(2 * M_PI * f * (double)i / sr)); return std::pair<float, float>{v, v}; };
  };
  { LoudnessMeter m(48000); feed(m, 20, sine(48000, -23)); auto r = m.read();  // EBU 3341 case 1
    CHECK(std::fabs(r.momentary + 23) < 0.1f && std::fabs(r.shortTerm + 23) < 0.1f && std::fabs(r.integrated + 23) < 0.1f,
          "-23 dBFS stereo 997 Hz: M %.2f S %.2f I %.2f (want -23.0)", r.momentary, r.shortTerm, r.integrated);
    CHECK(r.lra < 0.5f, "steady tone LRA %.2f", r.lra); }
  { LoudnessMeter m(96000); auto a = sine(96000, -36), b = sine(96000, -23);  // gating: -36/-23/-36 -> -23
    feed(m, 10, a); feed(m, 20, b); feed(m, 10, a); auto r = m.read();
    CHECK(std::fabs(r.integrated + 23) < 0.1f, "gated integrated %.2f (want -23.0)", r.integrated); }
  { LoudnessMeter m(48000); feed(m, 5, [](size_t i) { return std::pair<float, float>{(float)std::sin(2 * M_PI * 997.0 * (double)i / 48000), 0.f}; });
    auto r = m.read(); CHECK(std::fabs(r.integrated + 3.01f) < 0.1f, "0 dBFS sine on one channel %.2f (want -3.01)", r.integrated); }
  { LoudnessMeter m(48000); feed(m, 20, sine(48000, -20, 1000)); feed(m, 20, sine(48000, -30, 1000));  // EBU 3342 case 1
    auto r = m.read(); CHECK(std::fabs(r.lra - 10.f) < 1.f, "LRA %.2f (want 10 +- 1)", r.lra); }
  { LoudnessMeter m(48000);  // samples land at +-0.354 but the waveform peaks at 0.5
    feed(m, 1, [](size_t i) { const float v = 0.5f * (float)std::sin(2 * M_PI * (double)i / 4.0 + M_PI / 4); return std::pair<float, float>{v, v}; });
    auto r = m.read();
    CHECK(std::fabs(r.samplePeak + 9.03f) < 0.1f && std::fabs(r.truePeak + 6.02f) < 0.4f, "sample peak %.2f (want -9.03), true peak %.2f (want -6.02)", r.samplePeak, r.truePeak); }
  { LoudnessMeter m(48000); feed(m, 1, sine(48000, -12)); CHECK(m.read().correlation > 0.99f, "identical channels correlate +1");
    LoudnessMeter n(48000); auto s = sine(48000, -12); feed(n, 1, [&](size_t i) { auto p = s(i); return std::pair<float, float>{p.first, -p.second}; });
    CHECK(n.read().correlation < -0.99f, "inverted channels correlate -1");
    LoudnessMeter u(48000); uint32_t x = 1, y = 7;
    feed(u, 2, [&](size_t) { x = x * 1664525u + 1013904223u; y = y * 22695477u + 1u;
                             return std::pair<float, float>{(float)((x >> 8) / 16777216.0 - 0.5), (float)((y >> 8) / 16777216.0 - 0.5)}; });
    CHECK(std::fabs(u.read().correlation) < 0.1f, "unrelated noise correlation %.2f", u.read().correlation); }
  { Engine e(SR); e.setMasterTapEnabled(true); auto& t = e.track(0); t.inputL = 0; t.armed = true; t.pan = -1.f;
    e.metronome().enabled = true; e.metronome().levelDb = 0; e.transport().play();
    long k = 0; std::vector<float> L, R; stream2(e, k, 4000, L, R);
    std::vector<float> tap(2 * 8000); const size_t got = e.readMasterTap(tap.data(), 8000);
    bool ok = got == 4000;
    for (size_t i = 0; ok && i < got; ++i) ok = tap[2 * i] == sigAt((long)i) && tap[2 * i + 1] == 0.f;
    CHECK(ok, "master tap must carry the mix exactly (frames %zu)", got);
    CHECK(peakOf(L) > 0.9f, "the output does contain the click (peak %f)", peakOf(L)); }
  std::printf("loudness: EBU 3341/3342 cases, true peak, correlation; master tap exact and click-free\n");
}

// Insert reordering: same instances, new order, each insert processed exactly once per block.
struct Affine : InsertProcessor {
  float a, b; std::string nm; std::vector<FxParamInfo> ps; long calls = 0;
  Affine(float a_, float b_, std::string n) : a(a_), b(b_), nm(std::move(n)) {}
  const std::string& name() const override { return nm; }
  int numParams() const override { return 0; } const std::vector<FxParamInfo>& params() const override { return ps; }
  void setParam(int, float) override {} float getParam(int) const override { return 0; }
  std::string paramDisplay(int) override { return {}; } void setBypass(bool) override {} bool bypassed() const override { return false; }
  void process(float* l, float* r, int n) override { ++calls; for (int i = 0; i < n; ++i) { l[i] = l[i] * a + b; r[i] = r[i] * a + b; } }
};
static void testInsertReorder() {
  Engine e(SR); auto& t = e.track(0); t.inputL = 0; t.armed = true; t.pan = -1.f;
  auto A = std::make_unique<Affine>(2.f, 0.f, "A"), B = std::make_unique<Affine>(1.f, 0.1f, "B"), C = std::make_unique<Affine>(0.5f, 0.f, "C");
  Affine *pa = A.get(), *pb = B.get(), *pc = C.get();
  e.setInsertProcessor(0, 0, std::move(A)); e.setInsertProcessor(0, 1, std::move(B)); e.setInsertProcessor(0, 3, std::move(C));
  std::vector<float> in(N, 0.25f), l(N), r(N); const float* ip[1] = {in.data()}; float* op[2] = {l.data(), r.data()};
  auto run = [&](int blocks) { for (int i = 0; i < blocks; ++i) e.process(ip, 1, op, 2, N); return l[N - 1]; };
  float y = run(4);
  CHECK(std::fabs(y - ((0.25f * 2.f + 0.1f) * 0.5f)) < 1e-6f, "chain A,B,-,C gives %f", y);
  CHECK(e.moveInsert(0, 0, 3), "move A to the end");
  CHECK(e.insertAt(0, 0) == pb && e.insertAt(0, 1) == nullptr && e.insertAt(0, 2) == pc && e.insertAt(0, 3) == pa, "UI view after move");
  const long ca = pa->calls, cb = pb->calls, cc = pc->calls;
  y = run(10);
  CHECK(std::fabs(y - (0.25f + 0.1f) * 0.5f * 2.f) < 1e-6f, "chain B,-,C,A gives %f", y);
  CHECK(pa->calls - ca == 10 && pb->calls - cb == 10 && pc->calls - cc == 10, "each insert must run exactly once per block (A %ld B %ld C %ld)",
        pa->calls - ca, pb->calls - cb, pc->calls - cc);
  CHECK(e.moveInsert(0, 3, 0) && e.insertAt(0, 0) == pa && e.insertAt(0, 1) == pb, "move back to the front");
  CHECK(!e.moveInsert(0, 2, 0), "moving an empty slot is refused");
  run(2); e.setInsert(0, 0, ""); run(2); e.collectGarbage();  // replacing after a reorder still frees the right instance
  CHECK(e.insertAt(0, 0) == nullptr && e.insertAt(0, 1) == pb, "remove after reorder");
  std::printf("insert reorder: same instances, new order, each insert runs once per block\n");
}

// Project folders with spaces and non-ASCII letters (the "My Song1" overdub bug: the space-to-underscore
// rule was applied to the whole path, so takes went to a folder that did not exist and were silently lost).
static void testRecordingPaths() {
  const std::string proj = tmpDir() + "/My Song 1 Caf\xc3\xa9", audio = proj + "/audio";
  std::filesystem::create_directories(std::filesystem::u8path(audio));
  Engine e(SR); auto& t = e.track(1); t.inputL = 0; t.armed = true; t.name = "Lead Vox: take/1?";
  long k = 0; e.startRecording(audio); stream(e, k, 3000); e.transport().stop(); stream(e, k, 64); e.flushRecording();
  auto f = e.pollFinishedTakes(); auto errs = e.pollRecordingErrors();
  CHECK(f.size() == 1 && errs.empty(), "take in a folder with spaces/accents: takes=%zu errors=%zu %s", f.size(), errs.size(), errs.empty() ? "" : errs[0].c_str());
  if (f.size() != 1) return;
  const auto file = std::filesystem::u8path(f[0].path);
  CHECK(std::filesystem::exists(file) && file.parent_path() == std::filesystem::u8path(audio), "take must be in the project's own audio folder: %s", f[0].path.c_str());
  CHECK(file.filename().u8string() == "Lead_Vox__take_1_take001.wav", "file-safe take name, got %s", file.filename().u8string().c_str());
  e.setActiveTake(1, e.addTake(1, f[0].path, f[0].start));
  CHECK(e.numTakes(1) == 1, "take with a non-ASCII path must load");
  CHECK(saveSession(e, proj + "/My Song 1 Caf\xc3\xa9.airdaw").ok, "save into non-ASCII folder");
  Engine b(SR); auto r = loadSession(b, proj + "/My Song 1 Caf\xc3\xa9.airdaw");
  CHECK(r.ok && r.warnings.empty() && b.numTakes(1) == 1 && b.takeAt(1, 0)->l.size() == 3000, "reload from non-ASCII folder (warnings=%zu)", r.warnings.size());
  // A folder that does not exist: the take is lost, but it must be reported, not silent.
  Engine c(SR); c.track(0).inputL = 0; c.track(0).armed = true;
  long k2 = 0; c.startRecording(proj + "/no such folder"); stream(c, k2, 1000); c.transport().stop(); stream(c, k2, 64); c.flushRecording();
  CHECK(c.pollFinishedTakes().empty() && c.pollRecordingErrors().size() == 1, "an unwritable take must be reported");
  std::printf("recording paths: spaces, accents and odd track names work; unwritable takes are reported\n");
}

// Loading a session (or New) replaces the take list instead of appending to it, so takes never leak
// from one project into the next. Moved take files keep working via setTakePath.
static void testTakeLists() {
  auto dir = tmpDir();
  Engine e(SR); e.track(0).inputL = 0; e.track(0).armed = true;
  long k = 0; e.startRecording(dir); stream(e, k, 5000); e.transport().stop(); stream(e, k, 100); e.flushRecording();
  auto f = e.pollFinishedTakes(); CHECK(f.size() == 1, "take lists: recorded %zu takes", f.size());
  if (f.size() != 1) return;
  e.track(0).armed = false;
  CHECK(e.addTake(0, f[0].path, f[0].start) == 0, "first take index"); e.setActiveTake(0, 0);
  const Take* first = e.takeAt(0, 0);
  CHECK(saveSession(e, dir + "/a.airdaw").ok, "save a");
  { Engine blank(SR); CHECK(saveSession(blank, dir + "/blank.airdaw").ok, "save blank"); }
  CHECK(loadSession(e, dir + "/blank.airdaw").ok, "load blank");
  CHECK(e.numTakes(0) == 0 && e.track(0).take.load() == nullptr, "new session must start with no takes (numTakes=%d)", e.numTakes(0));
  CHECK(first->l.size() == 5000, "earlier take must stay in memory after the list is cleared");
  CHECK(loadSession(e, dir + "/a.airdaw").ok && loadSession(e, dir + "/a.airdaw").ok, "load a twice");
  CHECK(e.numTakes(0) == 1 && e.track(0).take.load() == e.takeAt(0, 0), "reloading must not duplicate takes (numTakes=%d)", e.numTakes(0));
  e.transport().seek(0); e.transport().play(); e.track(0).pan = -1.f;
  long k2 = 0; auto out = stream(e, k2, 5000); e.transport().stop();
  bool pb = true; for (size_t i = 0; pb && i < out.size(); ++i) pb = std::fabs(out[i] - sigAt((long)i)) < 1e-6f;
  CHECK(pb, "take plays back after reload");
  // Move the file (as Save As does) and point the take at it: the next save must reference the new place.
  auto moved = dir + "/audio_moved.wav"; std::filesystem::rename(f[0].path, moved);
  e.setTakePath(0, 0, moved); CHECK(saveSession(e, dir + "/b.airdaw").ok, "save b");
  Engine e2(SR); auto r = loadSession(e2, dir + "/b.airdaw");
  CHECK(r.ok && r.warnings.empty() && e2.numTakes(0) == 1 && e2.takeAt(0, 0)->l.size() == 5000, "moved take reloads (warnings=%zu)", r.warnings.size());
  std::printf("take lists: replaced on load/new, no duplicates, playback after reload, moved files follow\n");
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
  testTakeLists();
  testMonitoringAndLayering();
  testRecordingPaths();
  testInsertReorder();
  testKeepAndRemoveTakes();
  testRenderMix();
  testLoudness();

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
    a.track(0).inputL = 0; a.track(0).armed = true; a.track(0).pan = -1.f; auto g = std::make_unique<Gain2>(); g->v = 0.25f; a.setInsertProcessor(0, 0, std::move(g));
    long k = 0; auto out = stream(a, k, 400); CHECK(std::fabs(out[300] - sigAt(300) * 0.5f) < 1e-6f, "hosted insert processes audio");
    CHECK(saveSession(a, dir + "/h.airdaw").ok, "save hosted");
    Engine b(SR); b.setInsertFactory(a.insertFactory()); auto lr = loadSession(b, dir + "/h.airdaw");
    CHECK(lr.ok && lr.warnings.empty() && b.insertAt(0, 0) && std::fabs(b.insertAt(0, 0)->getParam(0) - 0.25f) < 1e-6f, "hosted insert restored from state blob");
    Engine c(SR); auto lr2 = loadSession(c, dir + "/h.airdaw"); CHECK(lr2.ok && !lr2.warnings.empty(), "no factory -> warning only");
    std::printf("hosted-insert interface: processing, state blob, factory restore OK\n"); }

  // 17. Multi-core: output bit-identical for any thread count, plus speed-up, pool idle/wake, and recording under load
  { auto build = [&](Engine& e) {
      const char* chain[4] = {"Console7Channel", "Density2", "ToTape8", "Pressure5"};
      for (int t = 0; t < 24; ++t) { e.track(t).inputL = t % 8; e.track(t).armed = true; e.track(t).stereo = (t % 2); e.track(t).route = t % 5; e.track(t).pan = (t % 7 - 3) / 3.f;
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
    for (int t = 0; t < 24; ++t) { e.track(t).inputL = t % 8; e.track(t).armed = true; e.track(t).stereo = (t % 2); e.track(t).route = 1 + t % 4;
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
