#include "airdaw/engine.h"
#include <thread>
#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define AIRDAW_X86 1
#endif

namespace airdaw {

static inline float dbToLin(float db) { return db <= -100.f ? 0.f : std::pow(10.f, db * 0.05f); }

Engine::Engine(double sampleRate) : sr_(sampleRate) {
  for (auto& b : busBuf_) for (auto& c : b) c.assign(kMaxBlock, 0.f);
  for (auto& c : masterBuf_) c.assign(kMaxBlock, 0.f);
  for (auto& c : refBuf_) c.assign(kMaxBlock, 0.f);
  for (auto& c : work_) c.assign(kMaxBlock, 0.f);
  for (auto& t : trackBuf_) for (auto& c : t) c.assign(kMaxBlock, 0.f);
  for (int i = 0; i < kMaxTracks; ++i) tracks_[i].name = "TRACK " + std::to_string(i + 1);
  for (int i = 0; i < kNumBuses; ++i) { buses_[i].name = "BUS " + std::to_string(i + 1); buses_[i].stereo = true; }
  master_.name = "MASTER"; master_.stereo = true;
  writer_ = std::make_unique<DiskWriter>(kMaxTracks, (int)sampleRate);
}

Engine::~Engine() = default;

void Engine::startRecording(const std::string& folder) {
  std::vector<std::string> names;
  for (auto& t : tracks_) names.push_back(t.name);
  writer_->setFolder(folder, names);
  const int bars = std::clamp(metro_.countInBars.load(), 0, 4);
  int64_t ci = 0;
  if (bars > 0 && !transport_.playing.load())  // no count-in when punching in on a running transport
    ci = (int64_t)std::llround(bars * std::clamp(metro_.beatsPerBar.load(), 1, 16) * beatSamples());
  transport_.countInTotal = ci;
  transport_.countInLeft = ci;  // set before record(): the audio thread checks it once playing is true
  transport_.record();
}

double Engine::beatSamples() const { return sr_ * 60.0 / std::clamp((double)metro_.bpm.load(), 20.0, 400.0); }

// ---------------- tempo map ----------------
int tempoSections(const TempoMap* m, double bpm0, int bpb0, double sr, TempoSection* out) {
  out[0] = {0.0, sr * 60.0 / std::clamp(bpm0, 20.0, 400.0), std::clamp(bpb0, 1, 16), 0};
  int n = 1;
  if (m)
    for (auto& c : m->changes) {
      if (n > kMaxTempoChanges) break;
      const auto& p = out[n - 1];
      const int bar = c.bar - 1;  // 0-based
      if (bar <= p.bar) continue;
      out[n++] = {p.start + (double)(bar - p.bar) * p.bpb * p.beat, sr * 60.0 / std::clamp((double)c.bpm, 20.0, 400.0),
                  std::clamp(c.beatsPerBar, 1, 16), bar};
    }
  return n;
}
double barAtSample(const TempoSection* s, int n, double sample) {
  int k = 0; while (k + 1 < n && sample >= s[k + 1].start) ++k;
  return s[k].bar + (sample - s[k].start) / (s[k].beat * s[k].bpb);
}
double sampleAtBar(const TempoSection* s, int n, double bar) {
  int k = 0; while (k + 1 < n && bar >= s[k + 1].bar) ++k;
  return s[k].start + (bar - s[k].bar) * s[k].bpb * s[k].beat;
}

void Engine::setTempoMap(std::vector<TempoChange> changes) {
  std::sort(changes.begin(), changes.end(), [](const TempoChange& a, const TempoChange& b) { return a.bar < b.bar; });
  std::vector<TempoChange> clean;
  for (auto c : changes) {
    if (c.bar < 2 || (!clean.empty() && clean.back().bar == c.bar)) continue;
    c.bpm = std::clamp(c.bpm, 20.f, 400.f); c.beatsPerBar = std::clamp(c.beatsPerBar, 1, 16);
    clean.push_back(c);
    if ((int)clean.size() == kMaxTempoChanges) break;
  }
  if (clean.empty()) { tempo_.store(nullptr); return; }
  auto m = std::make_unique<TempoMap>(); m->changes = std::move(clean);
  tempo_.store(m.get());
  tempoMaps_.push_back(std::move(m));
}

// ---------------- click ----------------
const char* clickSoundName(int s) {
  static const char* names[kNumClickSounds] = {"Beep", "Woodblock", "Clave", "Cowbell", "Stick", "Hi-hat", "Soft"};
  return s >= 0 && s < kNumClickSounds ? names[s] : names[0];
}

// One sample of a click, `t` seconds after its start (0..0.12 s). Pure functions of t (and the beat for the noise),
// so the click stays stateless. Beat 1 of a bar is accented (higher, a little brighter).
static float clickSample(int sound, bool accent, double t, uint64_t beat) {
  constexpr double kTau = 6.283185307179586;
  auto noise = [&](double tt) {  // repeatable white noise from the time and the beat
    uint64_t x = (uint64_t)(tt * 192000.0) * 0x9E3779B97F4A7C15ull ^ (beat + 1) * 0xBF58476D1CE4E5B9ull;
    x ^= x >> 31; x *= 0x94D049BB133111EBull; x ^= x >> 29;
    return (double)(x >> 11) * (1.0 / 4503599627370496.0) - 1.0;
  };
  switch (sound) {
    case kClickWoodblock: { const double f = accent ? 1850.0 : 1300.0;
      return (float)(std::exp(-t / 0.011) * (std::sin(kTau * f * t) + 0.45 * std::exp(-t / 0.004) * std::sin(kTau * 2.63 * f * t)) * 0.9); }
    case kClickClave: { const double f = accent ? 2700.0 : 2250.0;
      return (float)(std::exp(-t / 0.018) * std::sin(kTau * f * t) * 0.85); }
    case kClickCowbell: { const double k = accent ? 1.26 : 1.0;
      const double a = std::tanh(3.0 * std::sin(kTau * 587.0 * k * t)) + 0.8 * std::tanh(3.0 * std::sin(kTau * 845.0 * k * t));
      return (float)(std::exp(-t / (accent ? 0.06 : 0.045)) * a * 0.42); }
    case kClickStick: { const double f = accent ? 2100.0 : 1700.0;
      return (float)(std::exp(-t / 0.0025) * noise(t) * 0.9 + std::exp(-t / 0.008) * std::sin(kTau * f * t) * 0.5); }
    case kClickHat: {
      const double hp = noise(t) - noise(t - 1.0 / 192000.0);  // bright: a first difference of the noise
      return (float)(std::exp(-t / (accent ? 0.04 : 0.022)) * hp * 0.5); }
    case kClickSoft: { const double f = accent ? 880.0 : 660.0;
      return (float)(std::min(1.0, t / 0.002) * std::exp(-t / 0.022) * std::sin(kTau * f * t) * 0.9); }
    default: { const double f = accent ? 1500.0 : 1000.0;  // Beep: the original click
      return (float)(std::exp(-t / 0.006) * std::sin(kTau * f * t)); }
  }
}

// Adds clicks for samples [pos, pos+n) of the beat grid that starts at sample 0, following the tempo map. Stateless,
// so seeks, loop wraps and block sizes cannot shift it.
void Engine::addClick(float* l, float* r, int n, int64_t pos) {
  const float amp = dbToLin(std::min(metro_.levelDb.load(), 6.f));
  if (amp <= 0.f) return;
  TempoSection sec[kMaxTempoChanges + 1];
  const int ns = tempoSections(tempo_.load(), metro_.bpm.load(), metro_.beatsPerBar.load(), sr_, sec);
  const int sound = metro_.sound.load(std::memory_order_relaxed);
  const double clickLen = sr_ * (sound == kClickBeep ? 0.03 : 0.12);
  int k = 0;
  for (int i = 0; i < n; ++i) {
    const double p = (double)(pos + i);
    while (k + 1 < ns && p >= sec[k + 1].start) ++k;
    while (k > 0 && p < sec[k].start) --k;
    const auto& s = sec[k];
    const double b = std::floor((p - s.start) / s.beat), off = p - s.start - b * s.beat;
    // (beats are at least 0.15 s apart and a click lasts at most 0.12 s, so one click never runs into the next)
    if (off >= clickLen) continue;
    const int64_t beatNo = (int64_t)b;
    const float v = amp * clickSample(sound, beatNo % s.bpb == 0, off / sr_, (uint64_t)(beatNo + 1000 * k));
    l[i] += v; r[i] += v;
  }
}

int Engine::addTake(int track, const std::string& path, int64_t start) {
  if (track < 0 || track >= kMaxTracks) return -1;
  WavData w;
  if (!readWav(path, w)) return -1;
  auto t = std::make_unique<Take>();
  t->l = std::move(w.left); t->r = std::move(w.right); t->start = start; t->path = path;
  takes_[track].push_back(std::move(t));
  return numTakes(track) - 1;
}

void Engine::setActiveTake(int track, int i) {
  if (track < 0 || track >= kMaxTracks) return;
  tracks_[track].take.store(i >= 0 && i < numTakes(track) ? takeAt(track, i) : nullptr);
}

void Engine::removeTake(int track, int i) {
  if (track < 0 || track >= kMaxTracks || i < 0 || i >= numTakes(track)) return;
  auto& list = takes_[track];
  const size_t at = (size_t)(takeBase_[track] + i);
  if (tracks_[track].take.load() == list[at].get()) tracks_[track].take.store(nullptr);
  if (const Comp* cp = tracks_[track].comp.load(); cp && takeInComp(track, list[at].get())) {  // its sections go too
    std::vector<CompSection> keep;
    for (auto& s : cp->sections) if (s.take != list[at].get()) keep.push_back(s);
    publishComp(track, std::move(keep), cp->silenceOutside);
  }
  removedTakes_.push_back(std::move(list[at]));
  list.erase(list.begin() + (std::ptrdiff_t)at);
}

size_t Engine::readMasterTap(float* dst, size_t maxFrames) {
  const size_t frames = std::min(maxFrames, tap_.readable() / 2);
  if (frames) tap_.read(dst, frames * 2);
  return frames;
}
size_t Engine::readHeardTap(float* dst, size_t maxFrames) {
  const size_t frames = std::min(maxFrames, heardTap_.readable() / 2);
  if (frames) heardTap_.read(dst, frames * 2);
  return frames;
}

size_t Engine::readRefTap(float* dst, size_t maxFrames) {
  const size_t frames = std::min(maxFrames, refTap_.readable() / 2);
  if (frames) refTap_.read(dst, frames * 2);
  return frames;
}

// Lock-free copy into a tap ring; drops (and counts) if the reader falls behind.
void Engine::writeTap(FloatRing& ring, const float* l, const float* r, int n) {
  if (!tapOn_.load(std::memory_order_relaxed)) return;
  if (ring.freeSpace() < (size_t)n * 2) { tapDropped_.fetch_add((uint64_t)n, std::memory_order_relaxed); return; }
  float tmp[512];
  for (int i = 0; i < n; i += 256) {
    const int m = std::min(256, n - i);
    for (int k = 0; k < m; ++k) { tmp[2 * k] = l[i + k]; tmp[2 * k + 1] = r[i + k]; }
    ring.write(tmp, (size_t)m * 2);
  }
}

void Engine::setReference(std::unique_ptr<Reference> r) {
  const Reference* raw = r.get();
  if (r) refs_.push_back(std::move(r));
  mon_.refPlaying = false;
  mon_.refSeek = 0;
  ref_.store(raw, std::memory_order_release);
}

// Monitor section (what reaches the speakers only).
void Engine::applyMonitor(float* l, float* r, int n, int64_t) {
  const int solo = mon_.solo.load(std::memory_order_relaxed);
  const bool mono = mon_.mono.load(std::memory_order_relaxed), pol = mon_.polarity.load(std::memory_order_relaxed);
  const float g = mon_.dim.load(std::memory_order_relaxed) ? dbToLin(mon_.dimDb.load(std::memory_order_relaxed)) : 1.f;
  if (solo == Monitor::Stereo && !mono && !pol && g == 1.f) return;
  for (int i = 0; i < n; ++i) {
    float a = l[i], b = pol ? -r[i] : r[i];
    switch (solo) {
      case Monitor::Left: b = a; break;
      case Monitor::Right: a = b; break;
      case Monitor::Mid: a = b = 0.5f * (a + b); break;
      case Monitor::Side: a = b = 0.5f * (a - b); break;  // side signal on both speakers, in phase (as monitor controllers do)
      default: break;
    }
    if (mono) a = b = 0.5f * (a + b);
    l[i] = a * g; r[i] = b * g;
  }
}

void Engine::clearTakeList(int track) {
  if (track < 0 || track >= kMaxTracks) return;
  tracks_[track].take.store(nullptr);
  tracks_[track].comp.store(nullptr);
  takeBase_[track] = (int)takes_[track].size();
}

// ---------------------------------------------------------------- comping
void Engine::publishComp(int track, std::vector<CompSection> s, bool silence) {
  if (track < 0 || track >= kMaxTracks) return;
  s.erase(std::remove_if(s.begin(), s.end(), [](const CompSection& x) { return !x.take || x.to <= x.from; }), s.end());
  std::sort(s.begin(), s.end(), [](const CompSection& a, const CompSection& b) { return a.from < b.from; });
  if (s.empty() && !silence) { tracks_[track].comp.store(nullptr, std::memory_order_release); return; }
  auto c = std::make_unique<Comp>();
  c->sections = std::move(s); c->silenceOutside = silence;
  tracks_[track].comp.store(c.get(), std::memory_order_release);
  comps_.push_back(std::move(c));
}

void Engine::setCompSection(int track, int64_t from, int64_t to, const Take* take) {
  if (track < 0 || track >= kMaxTracks || to <= from) return;
  const Comp* cur = tracks_[track].comp.load();
  std::vector<CompSection> out;
  if (cur)
    for (auto s : cur->sections) {  // keep what lies outside [from, to), trimming sections that straddle it
      if (s.to <= from || s.from >= to) { out.push_back(s); continue; }
      if (s.from < from) out.push_back({s.take, s.from, from});
      if (s.to > to) out.push_back({s.take, to, s.to});
    }
  if (take) out.push_back({take, from, to});
  publishComp(track, std::move(out), cur && cur->silenceOutside);
}

void Engine::setCompSilenceOutside(int track, bool silence) {
  if (track < 0 || track >= kMaxTracks) return;
  const Comp* cur = tracks_[track].comp.load();
  publishComp(track, cur ? cur->sections : std::vector<CompSection>{}, silence);
}

void Engine::clearComp(int track) { if (track >= 0 && track < kMaxTracks) tracks_[track].comp.store(nullptr); }

bool Engine::takeInComp(int track, const Take* t) const {
  const Comp* cp = tracks_[track].comp.load();
  if (!cp) return false;
  for (auto& s : cp->sections) if (s.take == t) return true;
  return false;
}

static inline void takeFrame(const Take* tk, int64_t p, float& l, float& r) {
  if (tk) {
    const int64_t i = p - tk->start;
    if (i >= 0 && i < (int64_t)tk->l.size()) { l = tk->l[(size_t)i]; r = tk->r.empty() ? l : tk->r[(size_t)i]; return; }
  }
  l = r = 0.f;
}

// The take playing at p (no crossfade): the section containing p, else the active take (or silence).
static const Take* compSource(const Comp& cp, const Take* base, int64_t p) {
  const auto& s = cp.sections;
  auto it = std::upper_bound(s.begin(), s.end(), p, [](int64_t v, const CompSection& x) { return v < x.from; });
  if (it != s.begin() && p < (it - 1)->to) return (it - 1)->take;
  return cp.silenceOutside ? nullptr : base;
}

bool Engine::trackFrameAt(int track, int64_t p, float& l, float& r) const {
  const Take* base = tracks_[track].take.load();
  const Comp* cp = tracks_[track].comp.load();
  takeFrame(cp ? compSource(*cp, base, p) : base, p, l, r);
  return l != 0.f || r != 0.f;
}

int64_t Engine::trackEnd(int track) const {
  const Take* base = tracks_[track].take.load();
  const Comp* cp = tracks_[track].comp.load();
  int64_t end = 0;
  if (base && !(cp && cp->silenceOutside)) end = base->start + (int64_t)base->l.size();
  if (cp) for (auto& s : cp->sections) end = std::max(end, std::min(s.to, s.take->start + (int64_t)s.take->l.size()));
  return end;
}

// Does the take have audio over [a, z)? Silence (no take) fades fine anywhere.
static bool covers(const Take* tk, int64_t a, int64_t z) { return !tk || (a >= tk->start && z <= tk->start + (int64_t)tk->l.size()); }

// Audio thread. Each boundary b between two sources gets an equal-power crossfade, placed where both takes have
// audio: centred on b if they can, else just after b (the new take starts at b, e.g. a loop pass), else just before
// b (the old take ends at b). If neither fits, a hard cut (never a fade into missing audio, which would click).
void Engine::renderComp(const Comp& cp, const Take* base, int64_t pos, int n, float* wl, float* wr) const {
  const auto& s = cp.sections;
  const int64_t fade = (int64_t)std::llround(std::clamp(compCrossfadeMs.load(std::memory_order_relaxed), 0.f, 50.f) * 0.001 * sr_);
  for (int i = 0; i < n; ++i) {
    const int64_t p = pos + i;
    float l, r;
    const Take *a = nullptr, *c = nullptr;
    int64_t ws = 0, we = 0;
    bool fading = false;
    if (fade > 1) {  // section edges close enough for p to be inside their fade window
      auto it = std::upper_bound(s.begin(), s.end(), p, [](int64_t v, const CompSection& x) { return v < x.from; });
      int64_t edges[3]; int ne = 0;
      if (it != s.end()) edges[ne++] = it->from;
      if (it != s.begin()) { edges[ne++] = (it - 1)->from; edges[ne++] = (it - 1)->to; }
      for (int k = 0; k < ne && !fading; ++k) {
        const int64_t b = edges[k];
        if (std::llabs(p - b) > fade) continue;
        const Take *s1 = compSource(cp, base, b - 1), *s2 = compSource(cp, base, b);
        if (s1 == s2) continue;
        const int64_t h = fade / 2;
        const int64_t starts[3] = {b - h, b, b - fade};
        for (int64_t st : starts)
          if (covers(s1, st, st + fade) && covers(s2, st, st + fade)) {
            if (p >= st && p < st + fade) { a = s1; c = s2; ws = st; we = st + fade; fading = true; }
            break;  // the first window that fits is this edge's window
          }
      }
    }
    if (fading) {
      float l1, r1, l2, r2;
      takeFrame(a, p, l1, r1); takeFrame(c, p, l2, r2);
      const float t = ((float)(p - ws) + 0.5f) / (float)(we - ws);
      const float gIn = std::sin(t * 1.5707963f), gOut = std::cos(t * 1.5707963f);
      l = l1 * gOut + l2 * gIn; r = r1 * gOut + r2 * gIn;
    } else takeFrame(compSource(cp, base, p), p, l, r);
    wl[i] = l; wr[i] = r;
  }
}

Channel* Engine::resolve(int id) {
  if (id >= 0 && id < kMaxTracks) return &tracks_[id];
  if (id >= 100 && id < 100 + kNumBuses) return &buses_[id - 100];
  if (id == 200) return &master_;
  return nullptr;
}

static int ownedIndex(int id) {
  if (id >= 0 && id < kMaxTracks) return id;
  if (id >= 100 && id < 104) return kMaxTracks + (id - 100);
  return kMaxTracks + kNumBuses;  // master
}

bool Engine::setInsertProcessor(int channelId, int slot, std::unique_ptr<InsertProcessor> fresh) {
  if (!resolve(channelId) || slot < 0 || slot >= kMaxInserts) return false;
  auto& own = owned_[ownedIndex(channelId)][slot];
  InsertProcessor* raw = fresh.get();
  if (!toAudio_.push({channelId, slot, raw})) return false;  // queue full: nothing changed
  if (own) retiring_.push_back(std::move(own));
  own = std::move(fresh);
  return true;
}

bool Engine::setInsert(int channelId, int slot, const std::string& effect) {
  if (!resolve(channelId) || slot < 0 || slot >= kMaxInserts) return false;
  std::unique_ptr<InsertProcessor> fresh;
  if (!effect.empty()) {
    fresh = FxInstance::create(effect, sr_);
    if (!fresh) return false;
  }
  return setInsertProcessor(channelId, slot, std::move(fresh));
}

bool Engine::moveInsert(int channelId, int from, int to) {
  if (!resolve(channelId) || from < 0 || to < 0 || from >= kMaxInserts || to >= kMaxInserts) return false;
  auto& own = owned_[ownedIndex(channelId)];
  if (!own[from]) return false;
  if (from == to) return true;
  std::array<int, kMaxInserts> order{};  // new slot -> old slot
  { std::vector<int> v; for (int s = 0; s < kMaxInserts; ++s) if (s != from) v.push_back(s);
    v.insert(v.begin() + to, from);
    for (int s = 0; s < kMaxInserts; ++s) order[(size_t)s] = v[(size_t)s]; }
  if (!toAudio_.push({channelId, kChainCmd, nullptr, from, to})) return false;  // queue full: nothing changed
  std::array<std::unique_ptr<InsertProcessor>, kMaxInserts> next;
  for (int s = 0; s < kMaxInserts; ++s) next[(size_t)s] = std::move(own[(size_t)order[(size_t)s]]);
  own = std::move(next);
  return true;
}

InsertProcessor* Engine::insertAt(int channelId, int slot) {
  if (!resolve(channelId) || slot < 0 || slot >= kMaxInserts) return nullptr;
  return owned_[ownedIndex(channelId)][slot].get();
}

void Engine::collectGarbage() {
  InsertProcessor* p;
  while (fromAudio_.pop(p)) {
    retiring_.erase(std::remove_if(retiring_.begin(), retiring_.end(),
                                   [p](const auto& u) { return u.get() == p; }),
                    retiring_.end());
  }
}

void Engine::applyCommands() {
  Cmd c;
  while (toAudio_.pop(c)) {
    Channel* ch = resolve(c.channelId);
    if (!ch) continue;
    if (c.slot == kChainCmd) {  // reorder: move from -> to, shifting the slots between (in place, no allocation)
      auto& a = ch->insert;
      InsertProcessor* moved = a[(size_t)c.from];
      if (c.from < c.to) std::move(a.begin() + c.from + 1, a.begin() + c.to + 1, a.begin() + c.from);
      else std::move_backward(a.begin() + c.to, a.begin() + c.from, a.begin() + c.from + 1);
      a[(size_t)c.to] = moved;
      continue;
    }
    InsertProcessor* old = ch->insert[c.slot];
    ch->insert[c.slot] = c.fx;
    if (old) fromAudio_.push(old);
  }
}

void Engine::runChain(Channel& c, float* l, float* r, int n) {
  for (auto* fx : c.insert)
    if (fx) fx->process(l, r, n);
}

void Engine::updateMeter(Meter& m, const float* l, const float* r, int n) {
  float pl = 0, pr = 0; double sl = 0, sr = 0;
  for (int i = 0; i < n; ++i) {
    float a = std::fabs(l[i]), b = std::fabs(r[i]);
    pl = std::max(pl, a); pr = std::max(pr, b);
    sl += double(l[i]) * l[i]; sr += double(r[i]) * r[i];
  }
  m.peakL.store(pl, std::memory_order_relaxed);
  m.peakR.store(pr, std::memory_order_relaxed);
  m.rmsL.store(float(std::sqrt(sl / n)), std::memory_order_relaxed);
  m.rmsR.store(float(std::sqrt(sr / n)), std::memory_order_relaxed);
}

// Linear gain ramp from the previous block's value to the new target (click-free faders).
static void applyGain(float* l, float* r, int n, float& state, float target) {
  if (state == target) {
    if (target == 1.f) return;
    for (int i = 0; i < n; ++i) { l[i] *= target; r[i] *= target; }
    return;
  }
  float step = (target - state) / n, g = state;
  for (int i = 0; i < n; ++i) { g += step; l[i] *= g; r[i] *= g; }
  state = target;
}

// Everything one track needs for the current segment (read-only while workers run).
struct Engine::SegCtx {
  const float* const* in; int nIn; int done; int n;
  bool playing, recordingOn, punchOn, inRange, anySolo;
  int64_t pos;
};

void Engine::processTrack(int ti, const SegCtx& c) {
  Channel& t = tracks_[ti];
  float* wl = trackBuf_[ti][0].data();
  float* wr = trackBuf_[ti][1].data();
  const int n = c.n;
  trackLive_[ti] = 0;

  const int il = t.inputL.load(std::memory_order_relaxed);
  const bool hasInput = il >= 0 && il < c.nIn;
  const bool stereo = t.stereo.load(std::memory_order_relaxed);
  const bool armed = t.armed.load(std::memory_order_relaxed);
  const Take* tk = t.take.load(std::memory_order_acquire);
  const Comp* cp = t.comp.load(std::memory_order_acquire);
  const bool rec = c.playing && c.recordingOn && armed && c.inRange;
  const bool useTake = c.playing && (tk || cp) && !(armed && (!c.recordingOn || c.inRange));

  if (rec && !t.recActive) { writer_->beginTake(ti, c.pos, stereo ? 2 : 1); t.recActive = true; }
  else if (!rec && t.recActive) { writer_->endTake(ti); t.recActive = false; }

  // Live input is heard only on armed tracks with monitoring on (or while auditioning). Armed tracks always
  // read their input (to record it, and to show its level on the meter even when not monitoring).
  const bool audible = hasInput && ((armed && t.monitor.load(std::memory_order_relaxed)) || t.audition.load(std::memory_order_relaxed));
  const bool readInput = hasInput && (audible || armed);
  bool meterDone = false;

  if (!readInput && !useTake && !rec) {
    t.meter.peakL = t.meter.peakR = t.meter.rmsL = t.meter.rmsR = 0.f;
    bool hasFx = false; for (auto* f : t.insert) hasFx |= (f != nullptr);
    if (!hasFx) return;  // idle track, nothing to ring out
  }
  if (useTake && cp) {
    renderComp(*cp, tk, c.pos, n, wl, wr);
  } else if (useTake) {
    const bool tStereo = !tk->r.empty();
    const int64_t base = c.pos - tk->start, len = (int64_t)tk->l.size();
    for (int i = 0; i < n; ++i) {
      int64_t p = base + i; bool ok = p >= 0 && p < len;
      wl[i] = ok ? tk->l[(size_t)p] : 0.f;
      wr[i] = ok ? (tStereo ? tk->r[(size_t)p] : wl[i]) : 0.f;
    }
  } else {
    if (readInput) std::memcpy(wl, c.in[il] + c.done, n * sizeof(float)); else std::fill_n(wl, n, 0.f);
    int ir = t.inputR.load(std::memory_order_relaxed);
    if (readInput && stereo && ir >= 0 && ir < c.nIn) std::memcpy(wr, c.in[ir] + c.done, n * sizeof(float));
    else std::memcpy(wr, wl, n * sizeof(float));
    if (rec) writer_->push(ti, wl, wr, n, stereo ? 2 : 1);
    if (!audible) {  // armed but not monitoring: meter shows the input level, nothing reaches the mix
      if (readInput) { updateMeter(t.meter, wl, wr, n); meterDone = true; }
      std::fill_n(wl, n, 0.f); std::fill_n(wr, n, 0.f);
    }
  }

  applyGain(wl, wr, n, t.smoothTrim, dbToLin(t.trimDb.load(std::memory_order_relaxed)) * (t.polarity.load(std::memory_order_relaxed) ? -1.f : 1.f));
  runChain(t, wl, wr, n);

  bool muted = t.mute.load(std::memory_order_relaxed) || (c.anySolo && !t.solo.load(std::memory_order_relaxed));
  float target = muted ? 0.f : dbToLin(t.gainDb.load(std::memory_order_relaxed));
  float pan = std::clamp(t.pan.load(std::memory_order_relaxed), -1.f, 1.f);
  float gl, gr;
  if (stereo) { gl = std::min(1.f, 1.f - pan); gr = std::min(1.f, 1.f + pan); }
  else { float a = (pan + 1.f) * 0.7853981634f; gl = std::cos(a); gr = std::sin(a); }  // -3 dB pan law
  float sg = t.smoothGain, step = (target - sg) / n;
  for (int i = 0; i < n; ++i) { sg += step; wl[i] *= sg * gl; wr[i] *= sg * gr; }
  t.smoothGain = target;

  if (!meterDone) updateMeter(t.meter, wl, wr, n);
  trackLive_[ti] = 1;
}

void Engine::processBus(int bi, int n, bool anyBusSolo) {
  Channel& b = buses_[bi];
  float* bl = busBuf_[bi][0].data(); float* br = busBuf_[bi][1].data();
  applyGain(bl, br, n, b.smoothTrim, dbToLin(b.trimDb.load(std::memory_order_relaxed)) * (b.polarity.load(std::memory_order_relaxed) ? -1.f : 1.f));
  runChain(b, bl, br, n);
  bool muted = b.mute.load(std::memory_order_relaxed) || (anyBusSolo && !b.solo.load(std::memory_order_relaxed));
  applyGain(bl, br, n, b.smoothGain, muted ? 0.f : dbToLin(b.gainDb.load(std::memory_order_relaxed)));
  updateMeter(b.meter, bl, br, n);
}

void Engine::setWorkerThreads(int n) {
  if (n < 0) { unsigned hc = std::thread::hardware_concurrency(); n = hc > 1 ? (int)std::min(7u, hc - 1) : 0; }
  pool_.start(n, workerInit);
}

void Engine::process(const float* const* in, int nIn, float* const* out, int nOut, int total) {
#ifdef AIRDAW_X86
  unsigned oldCsr = _mm_getcsr();
  _mm_setcsr(oldCsr | 0x8040);  // flush-to-zero + denormals-are-zero: no CPU spikes in reverb tails
#endif
  applyCommands();

  Transport& tr = transport_;
  int done = 0;
  while (done < total) {
    { int64_t sk = tr.seekReq.exchange(-1); if (sk >= 0) tr.pos.store(sk); }
    SegCtx c{in, nIn, done, 0, tr.playing.load(), tr.recording.load(), tr.punchOn.load(), true, false, tr.pos.load()};
    const int64_t loopIn = tr.loopIn.load(), loopOut = tr.loopOut.load();
    const bool loopActive = tr.loopOn.load() && loopOut > loopIn;
    const int64_t pIn = tr.punchIn.load(), pOut = tr.punchOut.load();
    int n = std::min(kMaxBlock, total - done);
    // Count-in: tracks only monitor live input, nothing records, the position is held. The segment is
    // cut where the count-in ends so recording starts on the exact sample.
    int64_t ciLeft = c.playing ? tr.countInLeft.load() : 0;
    const bool countIn = ciLeft > 0;
    if (countIn) { n = (int)std::min<int64_t>(n, ciLeft); c.playing = false; }
    if (c.playing) {  // split blocks at loop end and punch points so they land on the exact sample
      auto lim = [&](int64_t b) { if (b > c.pos) n = (int)std::min<int64_t>(n, b - c.pos); };
      if (loopActive && c.pos < loopOut) lim(loopOut);
      if (c.punchOn) { lim(pIn); lim(pOut); }
    }
    c.n = n;
    c.inRange = !c.punchOn || (c.pos >= pIn && c.pos < pOut);
    for (auto& b : busBuf_) { std::fill_n(b[0].data(), n, 0.f); std::fill_n(b[1].data(), n, 0.f); }
    std::fill_n(masterBuf_[0].data(), n, 0.f);
    std::fill_n(masterBuf_[1].data(), n, 0.f);
    for (auto& t : tracks_) c.anySolo |= t.solo.load(std::memory_order_relaxed);

    // Phase 1: tracks in parallel (each touches only its own state and buffers).
    int busy = 0;
    for (auto& t : tracks_) {
      bool hasFx = false; for (auto* f : t.insert) hasFx |= (f != nullptr);
      busy += (hasFx || t.take.load(std::memory_order_relaxed) || t.comp.load(std::memory_order_relaxed) ||
               t.inputL.load(std::memory_order_relaxed) >= 0 || t.recActive);
    }
    struct Job { Engine* e; const SegCtx* c; } job{this, &c};
    auto trackFn = [](void* p, int i) { auto* j = (Job*)p; j->e->processTrack(i, *j->c); };
    if (busy >= 4) pool_.run(kMaxTracks, trackFn, &job);
    else for (int i = 0; i < kMaxTracks; ++i) processTrack(i, c);

    // Sum in fixed track order on this thread, so the result is bit-identical for any thread count.
    for (int ti = 0; ti < kMaxTracks; ++ti) {
      if (!trackLive_[ti]) continue;
      int rt = tracks_[ti].route.load(std::memory_order_relaxed);
      float* dl; float* dr;
      if (rt >= 1 && rt <= kNumBuses) { dl = busBuf_[rt - 1][0].data(); dr = busBuf_[rt - 1][1].data(); }
      else { dl = masterBuf_[0].data(); dr = masterBuf_[1].data(); }
      const float* wl = trackBuf_[ti][0].data(); const float* wr = trackBuf_[ti][1].data();
      for (int i = 0; i < n; ++i) { dl[i] += wl[i]; dr[i] += wr[i]; }
    }

    // Phase 2: buses in parallel, then summed into master in bus order.
    bool anyBusSolo = false;
    for (auto& b : buses_) anyBusSolo |= b.solo.load(std::memory_order_relaxed);
    struct BJob { Engine* e; int n; bool solo; } bjob{this, n, anyBusSolo};
    auto busFn = [](void* p, int i) { auto* j = (BJob*)p; j->e->processBus(i, j->n, j->solo); };
    if (busy >= 4) pool_.run(kNumBuses, busFn, &bjob);
    else for (int i = 0; i < kNumBuses; ++i) processBus(i, n, anyBusSolo);
    float* ml = masterBuf_[0].data(); float* mr = masterBuf_[1].data();
    for (int bi = 0; bi < kNumBuses; ++bi) {
      const float* bl = busBuf_[bi][0].data(); const float* br = busBuf_[bi][1].data();
      for (int i = 0; i < n; ++i) { ml[i] += bl[i]; mr[i] += br[i]; }
    }

    // Phase 3: master. On B the listening slot (the Hit Record Meter) is skipped here and runs on the reference below.
    const Reference* ref = ref_.load(std::memory_order_acquire);
    const bool onB = ref && mon_.listenB.load(std::memory_order_relaxed);
    const int ls = mon_.listenSlot.load(std::memory_order_relaxed);
    InsertProcessor* listenFx = (onB && ls >= 0 && ls < kMaxInserts) ? master_.insert[(size_t)ls] : nullptr;
    for (auto* fx : master_.insert)
      if (fx && fx != listenFx) fx->process(ml, mr, n);
    applyGain(ml, mr, n, master_.smoothGain,
              master_.mute.load(std::memory_order_relaxed) ? 0.f
                  : dbToLin(master_.gainDb.load(std::memory_order_relaxed)));
    updateMeter(master_.meter, ml, mr, n);

    // Master tap for analysis: the mix (before the reference, monitor section and click).
    writeTap(tap_, ml, mr, n);

    // Reference A/B. The reference runs on its own transport (or follows the song with sync). Its raw samples go
    // to the reference tap whenever it plays; on B they replace the mix (with the level-match gain).
    if (ref) {
      const int64_t len = (int64_t)ref->l.size();
      const bool sync = mon_.refSync.load(std::memory_order_relaxed);
      { const int64_t sk = mon_.refSeek.exchange(-1); if (sk >= 0 && !sync) mon_.refPos.store(std::min(sk, len)); }
      const int64_t rp = sync ? c.pos : mon_.refPos.load(std::memory_order_relaxed);
      const bool playing = sync ? c.playing : mon_.refPlaying.load(std::memory_order_relaxed);
      float* rl = refBuf_[0].data(); float* rr = refBuf_[1].data();
      if (playing || onB) {
        const bool st = !ref->r.empty();
        for (int i = 0; i < n; ++i) {
          const int64_t p = rp + i;
          const bool in = playing && p >= 0 && p < len;
          rl[i] = in ? ref->l[(size_t)p] : 0.f;
          rr[i] = in ? (st ? ref->r[(size_t)p] : rl[i]) : 0.f;
        }
      }
      if (playing) writeTap(refTap_, rl, rr, n);
      if (onB) {
        const float target = dbToLin(mon_.refGainDb.load(std::memory_order_relaxed));
        const float step = (target - refGain_) / (float)n;
        for (int i = 0; i < n; ++i) { refGain_ += step; ml[i] = rl[i] * refGain_; mr[i] = rr[i] * refGain_; }
        refGain_ = target;
        if (listenFx) listenFx->process(ml, mr, n);  // the meter measures what is heard
      }
      if (sync) {
        mon_.refPlaying.store(playing, std::memory_order_relaxed);
        mon_.refPos.store(std::clamp<int64_t>(playing ? rp + n : rp, 0, len), std::memory_order_relaxed);
      } else if (playing) {
        const int64_t np = std::min(len, rp + n);
        mon_.refPos.store(np);
        if (np >= len) mon_.refPlaying.store(false);  // stops at the end
      }
    }
    writeTap(heardTap_, ml, mr, n);  // what is heard (mix or reference), for the listening tools

    applyMonitor(ml, mr, n, 0);

    // Click goes to the output only (after master inserts and meter, so it is never recorded or metered).
    if (countIn) {
      addClick(ml, mr, n, tr.countInTotal.load() - ciLeft);
      tr.countInLeft.compare_exchange_strong(ciLeft, ciLeft - n);  // a stop() in between wins
    } else if (c.playing && metro_.enabled.load(std::memory_order_relaxed)) {
      addClick(ml, mr, n, c.pos);
    }

    for (int o = 0; o < nOut; ++o) {
      if (o == 0) std::memcpy(out[0] + done, ml, n * sizeof(float));
      else if (o == 1) std::memcpy(out[1] + done, mr, n * sizeof(float));
      else std::fill_n(out[o] + done, n, 0.f);
    }

    if (c.playing) {
      int64_t np = c.pos + n;
      if (loopActive && np >= loopOut) {  // loop wrap: every recording pass becomes its own take
        for (int ti = 0; ti < kMaxTracks; ++ti)
          if (tracks_[ti].recActive) { writer_->endTake(ti); tracks_[ti].recActive = false; }
        np = loopIn;
      }
      tr.pos.store(np);
    }
    done += n;
  }
#ifdef AIRDAW_X86
  _mm_setcsr(oldCsr);
#endif
}

}  // namespace airdaw
