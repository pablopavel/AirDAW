#include <array>
#include "airdaw/session.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include "airdaw/json_mini.h"

namespace fs = std::filesystem;
namespace airdaw {

// Paths in AirDAW are UTF-8 strings; std::filesystem on Windows would read a plain std::string in the ANSI code page.
static fs::path P(const std::string& s) { return fs::u8path(s); }
static std::string S(const fs::path& p) { return p.u8string(); }

static std::vector<int> allIds() {
  std::vector<int> v; for (int i = 0; i < kMaxTracks; ++i) v.push_back(i);
  for (int i = 0; i < kNumBuses; ++i) v.push_back(100 + i);
  v.push_back(200); return v;
}
static Channel& chan(Engine& e, int id) { return id < 100 ? e.track(id) : id < 200 ? e.bus(id - 100) : e.master(); }

static std::string relPath(const std::string& p, const std::string& base) {
  if (base.empty()) return p;
  std::error_code ec; auto r = fs::relative(P(p), P(base), ec);
  return (!ec && !r.empty() && *r.begin() != "..") ? r.generic_u8string() : p;
}

static JVal channelJVal(Engine& e, int id, const std::string& baseDir) {
  Channel& c = chan(e, id);
  JVal j = JVal::obj();
  j.set("id", JVal::num(id)).set("name", JVal::str(c.name)).set("gainDb", JVal::num(c.gainDb)).set("trimDb", JVal::num(c.trimDb))
   .set("pan", JVal::num(c.pan)).set("mute", JVal::boolean(c.mute)).set("solo", JVal::boolean(c.solo)).set("route", JVal::num(c.route))
   .set("stereo", JVal::boolean(c.stereo)).set("armed", JVal::boolean(c.armed)).set("monitor", JVal::boolean(c.monitor))
   .set("polarity", JVal::boolean(c.polarity)).set("inputL", JVal::num(c.inputL)).set("inputR", JVal::num(c.inputR))
   .set("inputMix", JVal::boolean(c.inputMix))
   .set("chainMix", JVal::num(c.chainMix));
  JVal ins = JVal::arr();
  for (int s = 0; s < kMaxInserts; ++s)
    if (auto* fx = e.insertAt(id, s)) {
      JVal f = JVal::obj(); f.set("slot", JVal::num(s)).set("effect", JVal::str(fx->sessionId())).set("bypass", JVal::boolean(fx->bypassed())).set("parallel", JVal::boolean(fx->parallel()));
      JVal ps = JVal::arr(); for (int p = 0; p < fx->numParams(); ++p) ps.push(JVal::num(fx->getParam(p)));
      f.set("params", ps);
      std::string st = fx->saveState(); if (!st.empty()) f.set("state", JVal::str(st));
      ins.push(f);
    }
  j.set("inserts", ins);
  if (id < kMaxTracks) {
    JVal tk = JVal::arr(); int active = -1; const Take* cur = c.take.load();
    for (int i = 0; i < e.numTakes(id); ++i) {
      const Take* x = e.takeAt(id, i); if (x == cur) active = i;
      JVal q = JVal::obj(); q.set("path", JVal::str(relPath(x->path, baseDir))).set("start", JVal::num((double)x->start));
      if (x->keep) q.set("keep", JVal::boolean(true));
      tk.push(q);
    }
    j.set("takes", tk).set("activeTake", JVal::num(active));
    if (const Comp* cp = e.comp(id)) {  // sections refer to takes by their index in "takes"
      JVal cj = JVal::obj(), secs = JVal::arr();
      for (auto& s : cp->sections)
        for (int i = 0; i < e.numTakes(id); ++i)
          if (e.takeAt(id, i) == s.take) {
            JVal q = JVal::obj(); q.set("take", JVal::num(i)).set("from", JVal::num((double)s.from)).set("to", JVal::num((double)s.to));
            secs.push(q); break;
          }
      cj.set("silenceOutside", JVal::boolean(cp->silenceOutside)).set("sections", secs);
      j.set("comp", cj);
    }
  }
  return j;
}

static JVal sessionJVal(Engine& e, const std::string& baseDir) {
  JVal root = JVal::obj();
  root.set("format", JVal::str("airdaw-session")).set("version", JVal::num(1)).set("sampleRate", JVal::num(e.sampleRate()));
  auto& t = e.transport();
  JVal tj = JVal::obj();
  tj.set("loopOn", JVal::boolean(t.loopOn)).set("loopIn", JVal::num((double)t.loopIn.load())).set("loopOut", JVal::num((double)t.loopOut.load()))
    .set("punchOn", JVal::boolean(t.punchOn)).set("punchIn", JVal::num((double)t.punchIn.load())).set("punchOut", JVal::num((double)t.punchOut.load()))
    .set("pos", JVal::num((double)t.pos.load()));
  root.set("transport", tj);
  auto& m = e.metronome();
  JVal mj = JVal::obj();
  mj.set("enabled", JVal::boolean(m.enabled)).set("bpm", JVal::num(m.bpm)).set("beatsPerBar", JVal::num(m.beatsPerBar))
    .set("levelDb", JVal::num(m.levelDb)).set("countInBars", JVal::num(m.countInBars)).set("sound", JVal::num(m.sound));
  if (auto* tm = e.tempoMap()) {  // tempo / time-signature changes: [bar, bpm, beats per bar]
    JVal ch = JVal::arr();
    for (auto& c : tm->changes) { JVal q = JVal::arr(); q.push(JVal::num(c.bar)); q.push(JVal::num(c.bpm)); q.push(JVal::num(c.beatsPerBar)); ch.push(q); }
    mj.set("tempoMap", ch);
  }
  root.set("metronome", mj);
  { JVal cj = JVal::obj(); cj.set("crossfadeMs", JVal::num(e.compCrossfadeMs.load())); root.set("comping", cj); }
  {  // app-side per-project data (reference track, plugin MIDI mappings...), kept as a JSON object
    JVal app; if (e.appData.empty() || !JVal::parse(e.appData, app) || app.type != JVal::Obj) app = JVal::obj();
    root.set("app", app);
  }
  JVal chs = JVal::arr();
  for (int id : allIds()) chs.push(channelJVal(e, id, baseDir));
  root.set("channels", chs);
  return root;
}

std::string sessionToJson(Engine& e, const std::string& baseDir) { return sessionJVal(e, baseDir).dump(); }

// The channel's mixer settings (everything but its inserts and takes). `withArm`: also the record arm.
static void applyChannelSettings(Channel& c, const JVal& j, bool withArm) {
  c.name = j.text("name", c.name); c.gainDb = (float)j.num("gainDb", 0); c.trimDb = (float)j.num("trimDb", 0);
  c.pan = (float)j.num("pan", 0); c.mute = j.flag("mute", false); c.solo = j.flag("solo", false);
  c.route = (int)j.num("route", 0); c.stereo = j.flag("stereo", c.stereo.load());
  if (withArm) c.armed = j.flag("armed", false);
  c.monitor = j.flag("monitor", true); c.polarity = j.flag("polarity", false); c.chainMix = std::clamp((float)j.num("chainMix", 1.0), 0.f, 1.f);
  c.inputL = (int)j.num("inputL", -1); c.inputR = (int)j.num("inputR", -1); c.inputMix = j.flag("inputMix", false);
}

// Puts the insert `f` (as saved) into `slot`: creates the plugin, then its bypass, parallel and settings.
static void createInsert(Engine& e, int id, const JVal& f, std::vector<std::string>& warnings) {
  int slot = (int)f.num("slot", -1); std::string name = f.text("effect");
  bool made;
  if (name.find(':') != std::string::npos) {  // hosted plugin, e.g. "vst3:/path/Meter.vst3"
    std::unique_ptr<InsertProcessor> p = e.insertFactory() ? e.insertFactory()(name, e.sampleRate()) : nullptr;
    made = p && e.setInsertProcessor(id, slot, std::move(p));
  } else made = e.setInsert(id, slot, name);
  if (!made) { warnings.push_back("plugin '" + name + "' on " + chan(e, id).name + " could not be loaded"); return; }
  if (auto* fx = e.insertAt(id, slot)) {
    fx->setBypass(f.flag("bypass", false));
    fx->setParallel(f.flag("parallel", false));
    std::string st = f.text("state");
    if (!st.empty()) fx->loadState(st);
    else if (auto* ps = f.get("params")) for (size_t p = 0; p < ps->a.size() && (int)p < fx->numParams(); ++p) fx->setParam((int)p, (float)ps->a[p].n);
  }
}

// The comp of a track from its saved form; `mapIdx`: saved take index -> loaded index.
static void applyComp(Engine& e, int id, const JVal& j, const std::vector<int>& mapIdx) {
  if (auto* cj = j.get("comp")) {
    if (auto* secs = cj->get("sections"))
      for (auto& q : secs->a) {
        const int k = (int)q.num("take", -1);
        if (k >= 0 && k < (int)mapIdx.size() && mapIdx[(size_t)k] >= 0)
          e.setCompSection(id, (int64_t)q.num("from", 0), (int64_t)q.num("to", 0), e.takeAt(id, mapIdx[(size_t)k]));
      }
    if (cj->flag("silenceOutside", false)) e.setCompSilenceOutside(id, true);
  }
}

static void applyChannel(Engine& e, int id, const JVal& j, const std::string& baseDir, std::vector<std::string>& warnings) {
  Channel& c = chan(e, id);
  applyChannelSettings(c, j, true);
  if (auto* ins = j.get("inserts")) for (auto& f : ins->a) createInsert(e, id, f, warnings);
  if (id < kMaxTracks) {
    std::vector<int> mapIdx;  // saved take index -> loaded index (a missing file shifts later ones)
    if (auto* tk = j.get("takes")) for (auto& q : tk->a) {
      std::string p = q.text("path"); if (!P(p).is_absolute()) p = S(P(baseDir) / P(p));
      int idx = e.addTake(id, p, (int64_t)q.num("start", 0));
      if (idx < 0) warnings.push_back("missing take: " + p);
      else e.setTakeKeep(id, idx, q.flag("keep", false));
      mapIdx.push_back(idx);
    }
    int saved = (int)j.num("activeTake", -1);
    e.setActiveTake(id, saved >= 0 && saved < (int)mapIdx.size() ? mapIdx[saved] : -1);
    applyComp(e, id, j, mapIdx);
  }
}

SessionResult saveSession(Engine& e, const std::string& path) {
  SessionResult r;
  fs::path p = P(path); std::error_code ec;
  if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  std::string text = sessionToJson(e, S(p.parent_path()));
  fs::path tmp = p; tmp += ".tmp";
  { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f << text; f.flush(); if (!f) { r.error = "cannot write " + S(tmp); return r; } }
  if (fs::exists(p)) { fs::path bak = p; bak += ".bak"; fs::copy_file(p, bak, fs::copy_options::overwrite_existing, ec); }
  fs::rename(tmp, p, ec);
  if (ec) { r.error = "rename failed: " + ec.message(); return r; }
  r.ok = true; return r;
}

SessionResult loadSession(Engine& e, const std::string& path) {
  SessionResult r;
  std::ifstream f(P(path), std::ios::binary); if (!f) { r.error = "cannot open " + path; return r; }
  std::stringstream ss; ss << f.rdbuf();
  JVal root; if (!JVal::parse(ss.str(), root) || root.text("format") != "airdaw-session") { r.error = "not a valid AirDAW session"; return r; }
  if (root.num("version", 0) > 1) r.warnings.push_back("session is from a newer AirDAW; some settings may be ignored");
  if ((int)root.num("sampleRate", 0) != (int)e.sampleRate())
    r.warnings.push_back("session sample rate differs from the audio device; takes will play at the wrong speed until sample-rate conversion exists");
  std::string baseDir = S(P(path).parent_path());
  { const JVal none = JVal::obj(); const JVal* cj = root.get("comping"); if (!cj) cj = &none;
    e.compCrossfadeMs = std::clamp((float)cj->num("crossfadeMs", 5), 0.f, 50.f); }
  if (auto* t = root.get("transport")) {
    auto& tr = e.transport();
    tr.loopIn = (int64_t)t->num("loopIn", 0); tr.loopOut = (int64_t)t->num("loopOut", 0); tr.loopOn = t->flag("loopOn", false);
    tr.punchIn = (int64_t)t->num("punchIn", 0); tr.punchOut = (int64_t)t->num("punchOut", 0); tr.punchOn = t->flag("punchOn", false);
    tr.seek((int64_t)t->num("pos", 0));
  }
  {  // older sessions have no "metronome" block: fall back to the defaults
    auto& m = e.metronome(); const JVal none = JVal::obj(); const JVal* mj = root.get("metronome"); if (!mj) mj = &none;
    m.enabled = mj->flag("enabled", false); m.bpm = std::clamp((float)mj->num("bpm", 120), 20.f, 400.f);
    m.beatsPerBar = std::clamp((int)mj->num("beatsPerBar", 4), 1, 16); m.levelDb = (float)mj->num("levelDb", -6);
    m.countInBars = std::clamp((int)mj->num("countInBars", 0), 0, 4);
    m.sound = std::clamp((int)mj->num("sound", 0), 0, kNumClickSounds - 1);
    std::vector<TempoChange> ch;
    if (auto* tm = mj->get("tempoMap"))
      for (auto& q : tm->a)
        if (q.a.size() >= 3) ch.push_back({(int)q.a[0].n, (float)q.a[1].n, (int)q.a[2].n});
    e.setTempoMap(std::move(ch));
  }
  { const JVal* app = root.get("app"); e.appData = app && app->type == JVal::Obj ? app->dump() : std::string(); }  // "" = older session
  // The loaded session's takes replace the current ones (not appended to them).
  for (int t = 0; t < kMaxTracks; ++t) e.clearTakeList(t);
  // Clear existing inserts (only occupied slots, to keep the command queue small).
  for (int id : allIds()) for (int s = 0; s < kMaxInserts; ++s) if (e.insertAt(id, s)) e.setInsert(id, s, "");
  e.collectGarbage();
  auto* chs = root.get("channels");
  if (chs) for (auto& j : chs->a) {
    int id = (int)j.num("id", -1);
    if (!(id >= 0 && id < kMaxTracks) && !(id >= 100 && id < 100 + kNumBuses) && id != 200) continue;
    applyChannel(e, id, j, baseDir, r.warnings);
  }
  r.ok = true; return r;
}

// ---------------- auto-save ----------------
std::string channelToJson(Engine& e, int id, const std::string& baseDir) { return channelJVal(e, id, baseDir).dump(); }

SessionResult channelFromJson(Engine& e, int id, const std::string& json, const std::string& baseDir) {
  SessionResult r;
  if (!(id >= 0 && id < kMaxTracks) && !(id >= 100 && id < 100 + kNumBuses) && id != 200) { r.error = "no such channel"; return r; }
  JVal j; if (!JVal::parse(json, j) || j.type != JVal::Obj) { r.error = "not a channel"; return r; }
  // the channel starts fresh: its inserts, takes and comp are replaced, not added to
  for (int s = 0; s < kMaxInserts; ++s) if (e.insertAt(id, s)) e.setInsert(id, s, "");
  if (id < kMaxTracks) { e.clearComp(id); e.clearTakeList(id); }
  applyChannel(e, id, j, baseDir, r.warnings);
  r.ok = true; return r;
}

// ---------------------------------------------------------------- undo
std::string undoState(Engine& e) {
  JVal root = sessionJVal(e, {});
  for (auto& [k, v] : root.o)
    if (k == "transport") { auto& o = v.o; o.erase(std::remove_if(o.begin(), o.end(), [](auto& p) { return p.first == "pos"; }), o.end()); }
    else if (k == "channels")
      for (auto& ch : v.a) { auto& o = ch.o; o.erase(std::remove_if(o.begin(), o.end(), [](auto& p) { return p.first == "armed"; }), o.end()); }
  return root.dump();
}

static std::string dumpOf(const JVal* v) { return v ? v->dump() : std::string(); }

// One channel: only what differs between `cur` and `tgt` is changed, so untouched plugins keep running and untouched
// takes are not read from disk again.
static void applyChannelDiff(Engine& e, int id, const JVal& tgt, const JVal& cur, std::vector<std::string>& warnings) {
  Channel& c = chan(e, id);
  applyChannelSettings(c, tgt, false);
  if (dumpOf(tgt.get("inserts")) != dumpOf(cur.get("inserts"))) {
    std::array<const JVal*, kMaxInserts> T{}, C{};
    if (auto* a = tgt.get("inserts")) for (auto& f : a->a) { int s = (int)f.num("slot", -1); if (s >= 0 && s < kMaxInserts) T[(size_t)s] = &f; }
    if (auto* a = cur.get("inserts")) for (auto& f : a->a) { int s = (int)f.num("slot", -1); if (s >= 0 && s < kMaxInserts) C[(size_t)s] = &f; }
    for (int s = 0; s < kMaxInserts; ++s) {
      const JVal* t = T[(size_t)s]; const JVal* k = C[(size_t)s];
      if (!t) { if (k || e.insertAt(id, s)) e.setInsert(id, s, ""); continue; }
      if (k && t->dump() == k->dump()) continue;
      auto* fx = e.insertAt(id, s);
      if (!k || !fx || t->text("effect") != k->text("effect")) { if (fx) e.setInsert(id, s, ""); createInsert(e, id, *t, warnings); continue; }
      fx->setBypass(t->flag("bypass", false)); fx->setParallel(t->flag("parallel", false));
      const std::string st = t->text("state");
      if (!st.empty()) { if (st != k->text("state")) fx->loadState(st); }
      else if (auto* ps = t->get("params")) {
        const JVal* pc = k->get("params");
        for (size_t p = 0; p < ps->a.size() && (int)p < fx->numParams(); ++p)
          if (!pc || p >= pc->a.size() || pc->a[p].n != ps->a[p].n) fx->setParam((int)p, (float)ps->a[p].n);
      }
    }
  }
  if (id >= kMaxTracks) return;
  auto takeList = [](const JVal& j) {  // path + start of each take (the star is not part of the list)
    std::string s; if (auto* tk = j.get("takes")) for (auto& q : tk->a) s += q.text("path") + "@" + std::to_string((long long)q.num("start", 0)) + ";";
    return s;
  };
  const bool sameTakes = takeList(tgt) == takeList(cur);
  const bool sameRest = dumpOf(tgt.get("takes")) == dumpOf(cur.get("takes")) && tgt.num("activeTake", -1) == cur.num("activeTake", -1)
                        && dumpOf(tgt.get("comp")) == dumpOf(cur.get("comp"));
  if (sameTakes && sameRest) return;
  std::vector<int> mapIdx;
  if (sameTakes) {  // the same takes: stars, the active one and the comp
    if (auto* tk = tgt.get("takes")) for (size_t i = 0; i < tk->a.size() && (int)i < e.numTakes(id); ++i) e.setTakeKeep(id, (int)i, tk->a[i].flag("keep", false));
    for (int i = 0; i < e.numTakes(id); ++i) mapIdx.push_back(i);
  } else {  // takes were added or removed: the list is read again (a deleted take's file may be in the Recycle Bin)
    e.clearComp(id); e.clearTakeList(id);
    if (auto* tk = tgt.get("takes")) for (auto& q : tk->a) {
      const std::string p = q.text("path");
      int idx = e.addTake(id, p, (int64_t)q.num("start", 0));
      if (idx < 0) warnings.push_back("missing take: " + p); else e.setTakeKeep(id, idx, q.flag("keep", false));
      mapIdx.push_back(idx);
    }
  }
  const int saved = (int)tgt.num("activeTake", -1);
  e.setActiveTake(id, saved >= 0 && saved < (int)mapIdx.size() ? mapIdx[(size_t)saved] : -1);
  if (dumpOf(tgt.get("comp")) != dumpOf(cur.get("comp")) || !sameTakes) { e.clearComp(id); applyComp(e, id, tgt, mapIdx); }
}

std::vector<std::string> applyUndoState(Engine& e, const std::string& target, const std::string& current, std::vector<std::string>& warnings) {
  std::vector<std::string> what;
  JVal T, C;
  if (!JVal::parse(target, T) || !JVal::parse(current, C)) { warnings.push_back("undo state unreadable"); return what; }
  const JVal none = JVal::obj();
  auto part = [&](const char* k, const JVal& j) { const JVal* v = j.get(k); return v ? v : &none; };
  if (part("metronome", T)->dump() != part("metronome", C)->dump()) {
    const JVal* mj = part("metronome", T); auto& m = e.metronome();
    m.enabled = mj->flag("enabled", false); m.bpm = std::clamp((float)mj->num("bpm", 120), 20.f, 400.f);
    m.beatsPerBar = std::clamp((int)mj->num("beatsPerBar", 4), 1, 16); m.levelDb = (float)mj->num("levelDb", -6);
    m.countInBars = std::clamp((int)mj->num("countInBars", 0), 0, 4); m.sound = std::clamp((int)mj->num("sound", 0), 0, kNumClickSounds - 1);
    if (dumpOf(mj->get("tempoMap")) != dumpOf(part("metronome", C)->get("tempoMap"))) {
      std::vector<TempoChange> ch;
      if (auto* tm = mj->get("tempoMap")) for (auto& q : tm->a) if (q.a.size() >= 3) ch.push_back({(int)q.a[0].n, (float)q.a[1].n, (int)q.a[2].n});
      e.setTempoMap(std::move(ch)); what.push_back("tempo");
    } else what.push_back("click");
  }
  if (part("transport", T)->dump() != part("transport", C)->dump()) {
    const JVal* t = part("transport", T); auto& tr = e.transport();
    tr.loopIn = (int64_t)t->num("loopIn", 0); tr.loopOut = (int64_t)t->num("loopOut", 0); tr.loopOn = t->flag("loopOn", false);
    tr.punchIn = (int64_t)t->num("punchIn", 0); tr.punchOut = (int64_t)t->num("punchOut", 0); tr.punchOn = t->flag("punchOn", false);
    what.push_back("loop region");
  }
  if (part("comping", T)->dump() != part("comping", C)->dump()) {
    e.compCrossfadeMs = std::clamp((float)part("comping", T)->num("crossfadeMs", 5), 0.f, 50.f); what.push_back("crossfade");
  }
  if (part("app", T)->dump() != part("app", C)->dump()) { e.appData = part("app", T)->dump(); what.push_back("layout"); }
  const JVal* tc = T.get("channels"); const JVal* cc = C.get("channels");
  if (tc && cc)
    for (auto& tj : tc->a) {
      const int id = (int)tj.num("id", -1);
      if (!(id >= 0 && id < kMaxTracks) && !(id >= 100 && id < 100 + kNumBuses) && id != 200) continue;
      const JVal* cj = nullptr; for (auto& q : cc->a) if ((int)q.num("id", -2) == id) { cj = &q; break; }
      if (cj && cj->dump() == tj.dump()) continue;
      applyChannelDiff(e, id, tj, cj ? *cj : none, warnings);
      const std::string nm = tj.text("name", cj ? cj->text("name") : std::string());
      what.push_back(nm.empty() ? "a channel" : nm);
    }
  return what;
}

void AutoSaver::configure(const AutoSaveSettings& s) {
  s_ = s; s_.intervalSec = std::clamp(s_.intervalSec, 30, 3600); s_.keep = std::clamp(s_.keep, 1, 100);
}
void AutoSaver::setLocation(const std::string& dir, const std::string& base) { dir_ = dir; base_ = base; haveHash_ = false; last_ = -1; }

std::string AutoSaver::saveNow() {
  if (dir_.empty()) return {};
  std::string text = sessionToJson(e_, dir_);
  std::error_code ec; fs::create_directories(P(dir_), ec);
  char stamp[32]; std::time_t t = std::time(nullptr); std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", &tmv);
  fs::path p = P(dir_) / P(base_ + "_autosave_" + stamp + ".airdaw");
  for (int n = 2; fs::exists(p); ++n) p = P(dir_) / P(base_ + "_autosave_" + stamp + "_" + std::to_string(n) + ".airdaw");
  fs::path tmp = p; tmp += ".tmp";
  { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f << text; f.flush(); if (!f) return {}; }
  fs::rename(tmp, p, ec); if (ec) return {};
  lastHash_ = std::hash<std::string>{}(text); haveHash_ = true;
  prune();
  return S(p);
}

std::string AutoSaver::tick(double now) {
  if (!s_.enabled || dir_.empty()) return {};
  if (last_ < 0) { last_ = now; return {}; }      // first tick starts the timer
  if (now - last_ < s_.intervalSec) return {};
  if (e_.transport().recording.load()) return {};  // never compete with a take for the disk; retry next tick
  last_ = now;
  std::string text = sessionToJson(e_, dir_);
  if (haveHash_ && std::hash<std::string>{}(text) == lastHash_) return {};  // unchanged since last autosave
  return saveNow();
}

static std::vector<fs::path> listAutosaves(const std::string& dir, const std::string& base) {
  std::vector<fs::path> v; std::error_code ec;
  for (auto& d : fs::directory_iterator(P(dir), ec)) {
    auto n = S(d.path().filename());
    if (n.rfind(base + "_autosave_", 0) == 0 && d.path().extension() == ".airdaw") v.push_back(d.path());
  }
  std::sort(v.begin(), v.end());  // timestamped names sort chronologically
  return v;
}
void AutoSaver::prune() {
  auto v = listAutosaves(dir_, base_); std::error_code ec;
  while ((int)v.size() > s_.keep) { fs::remove(v.front(), ec); v.erase(v.begin()); }
}
std::string AutoSaver::latestAutosave(const std::string& dir, const std::string& base) {
  auto v = listAutosaves(dir, base); return v.empty() ? std::string() : S(v.back());
}
}  // namespace airdaw
