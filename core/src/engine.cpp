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

// Adds clicks for samples [pos, pos+n) of the beat grid that starts at sample 0. Stateless, so seeks,
// loop wraps and block sizes cannot shift it. Beat 1 of a bar: 1500 Hz, other beats: 1000 Hz, ~30 ms decay.
void Engine::addClick(float* l, float* r, int n, int64_t pos) {
  const double beat = beatSamples(), clickLen = sr_ * 0.03, decay = sr_ * 0.006;
  const int bpb = std::clamp(metro_.beatsPerBar.load(), 1, 16);
  const float amp = dbToLin(std::min(metro_.levelDb.load(), 6.f));
  if (amp <= 0.f) return;
  for (int i = 0; i < n; ++i) {
    const double p = (double)(pos + i), b = std::floor(p / beat), off = p - b * beat;
    if (off >= clickLen) continue;
    const double f = ((int64_t)b % bpb == 0) ? 1500.0 : 1000.0;
    const float s = amp * (float)(std::exp(-off / decay) * std::sin(2.0 * 3.14159265358979 * f * off / sr_));
    l[i] += s; r[i] += s;
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
  removedTakes_.push_back(std::move(list[at]));
  list.erase(list.begin() + (std::ptrdiff_t)at);
}

size_t Engine::readMasterTap(float* dst, size_t maxFrames) {
  const size_t frames = std::min(maxFrames, tap_.readable() / 2);
  if (frames) tap_.read(dst, frames * 2);
  return frames;
}

void Engine::clearTakeList(int track) {
  if (track < 0 || track >= kMaxTracks) return;
  tracks_[track].take.store(nullptr);
  takeBase_[track] = (int)takes_[track].size();
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
  Cmd c{channelId, kChainCmd, nullptr};
  for (int s = 0; s < kMaxInserts; ++s) c.chain[(size_t)s] = own[(size_t)order[(size_t)s]].get();
  if (!toAudio_.push(c)) return false;  // queue full: nothing changed
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
    if (c.slot == kChainCmd) { for (int s = 0; s < kMaxInserts; ++s) ch->insert[(size_t)s] = c.chain[(size_t)s]; continue; }
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
  const bool rec = c.playing && c.recordingOn && armed && c.inRange;
  const bool useTake = c.playing && tk && !(armed && (!c.recordingOn || c.inRange));

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
  if (useTake) {
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

  applyGain(wl, wr, n, t.smoothTrim, dbToLin(t.trimDb.load(std::memory_order_relaxed)));
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
  applyGain(bl, br, n, b.smoothTrim, dbToLin(b.trimDb.load(std::memory_order_relaxed)));
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
      busy += (hasFx || t.take.load(std::memory_order_relaxed) || t.inputL.load(std::memory_order_relaxed) >= 0 || t.recActive);
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

    // Phase 3: master.
    runChain(master_, ml, mr, n);
    applyGain(ml, mr, n, master_.smoothGain,
              master_.mute.load(std::memory_order_relaxed) ? 0.f
                  : dbToLin(master_.gainDb.load(std::memory_order_relaxed)));
    updateMeter(master_.meter, ml, mr, n);

    // Master tap for analysis (before the click). Lock-free; drops if the reader falls behind.
    if (tapOn_.load(std::memory_order_relaxed)) {
      if (tap_.freeSpace() >= (size_t)n * 2) {
        float tmp[512];
        for (int i = 0; i < n; i += 256) {
          const int m = std::min(256, n - i);
          for (int k = 0; k < m; ++k) { tmp[2 * k] = ml[i + k]; tmp[2 * k + 1] = mr[i + k]; }
          tap_.write(tmp, (size_t)m * 2);
        }
      } else tapDropped_.fetch_add((uint64_t)n, std::memory_order_relaxed);
    }

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
