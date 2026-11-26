#include "MidiControl.h"
#include "../ui/AppCtx.h"
#include "../ui/Theme.h"

namespace ui {

// ---------------------------------------------------------------- MidiMapping
juce::String MidiMapping::sourceText() const {
  juce::String s = src == CC ? "CC " + juce::String(num)
                 : src == Note ? "Note " + juce::MidiMessage::getMidiNoteName(num, true, true, 3) + " (" + juce::String(num) + ")"
                               : juce::String("Pitch bend");
  s << juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ch ")) << ch;
  if (device.isNotEmpty()) s << juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) << device;
  return s;
}

juce::var MidiMapping::toVar() const {
  auto* o = new juce::DynamicObject();
  o->setProperty("src", (int)src); o->setProperty("ch", ch); o->setProperty("num", num); o->setProperty("dev", device);
  o->setProperty("target", target); if (fx.isNotEmpty()) o->setProperty("fx", fx);
  o->setProperty("mode", (int)mode);
  return juce::var(o);
}

bool MidiMapping::fromVar(const juce::var& v, MidiMapping& m) {
  if (!v.getDynamicObject()) return false;
  m = MidiMapping();
  m.src = (Src)juce::jlimit(0, 2, (int)v["src"]); m.ch = juce::jlimit(1, 16, (int)v["ch"]); m.num = juce::jlimit(0, 127, (int)v["num"]);
  m.device = v["dev"].toString(); m.target = v["target"].toString(); m.fx = v["fx"].toString();
  m.mode = (Mode)juce::jlimit(0, (int)Rel16, (int)v["mode"]);
  return m.target.isNotEmpty();
}

// ---------------------------------------------------------------- MidiControl
MidiControl::MidiControl(juce::AudioDeviceManager& dm, juce::PropertiesFile& props, Host host, bool openDevices)
    : dm_(dm), props_(props), host_(std::move(host)), open_(openDevices) {
  const juce::var saved = juce::JSON::parse(props_.getValue("midi.map"));  // keep it alive while reading
  if (auto* a = saved.getArray())
    for (auto& v : *a) { MidiMapping m; if (MidiMapping::fromVar(v, m) && !isProjectTarget(m.target)) maps_.push_back(m); }
  if (!open_) return;
  enableNewDevices();
  dm_.addMidiInputDeviceCallback({}, this);  // every enabled input
  startTimer(3000);
}

MidiControl::~MidiControl() {
  if (open_) dm_.removeMidiInputDeviceCallback({}, this);
  cancelPendingUpdate();
}

// Controllers are switched on the first time they are seen; after that the user's choice in Audio settings
// (saved with the device setup) is kept.
void MidiControl::enableNewDevices() {
  auto known = juce::StringArray::fromTokens(props_.getValue("midi.known"), "\n", "");
  bool any = false;
  for (auto& d : juce::MidiInput::getAvailableDevices())
    if (!known.contains(d.identifier)) {
      const double t0 = juce::Time::getMillisecondCounterHiRes();
      dm_.setMidiInputDeviceEnabled(d.identifier, true); known.add(d.identifier); any = true;
      juce::Logger::writeToLog("MIDI input switched on: " + d.name + " (" + juce::String((int)(juce::Time::getMillisecondCounterHiRes() - t0)) + " ms)");
    }
  if (any) { props_.setValue("midi.known", known.joinIntoString("\n")); props_.saveIfNeeded(); }
}

void MidiControl::timerCallback() { enableNewDevices(); }

void MidiControl::handleIncomingMidiMessage(juce::MidiInput* src, const juce::MidiMessage& m) {
  if (!(m.isController() || m.isNoteOnOrOff() || m.isPitchWheel())) return;
  {
    std::lock_guard<std::mutex> lk(mx_);
    if (queue_.size() > 1024) queue_.pop_front();
    queue_.emplace_back(src ? src->getName() : juce::String(), m);
  }
  triggerAsyncUpdate();
}

void MidiControl::handleAsyncUpdate() {
  std::deque<std::pair<juce::String, juce::MidiMessage>> q;
  { std::lock_guard<std::mutex> lk(mx_); q.swap(queue_); }
  for (auto& [dev, m] : q) handle(dev, m);
}

void MidiControl::handle(const juce::String& device, const juce::MidiMessage& m) {
  MidiMapping::Src src; int num = 0, raw = 0; const int ch = m.getChannel();
  if (m.isController()) { src = MidiMapping::CC; num = m.getControllerNumber(); raw = m.getControllerValue(); }
  else if (m.isNoteOn()) { src = MidiMapping::Note; num = m.getNoteNumber(); raw = m.getVelocity(); }
  else if (m.isNoteOff()) { src = MidiMapping::Note; num = m.getNoteNumber(); raw = 0; }
  else if (m.isPitchWheel()) { src = MidiMapping::PitchBend; raw = m.getPitchWheelValue(); }
  else return;
  {  // shown in the learn banner, so you can see what a knob actually sends
    MidiMapping tmp; tmp.src = src; tmp.ch = ch; tmp.num = num; tmp.device = device;
    lastMessage_ = tmp.sourceText().replaceFirstOccurrenceOf(juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ch")), " = " + juce::String(raw) + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ch")));
    if (learning_) changed();
  }
  if (learning_ && armed_.isNotEmpty()) {
    const bool cont = isContinuous(armed_);
    if (cont && src == MidiMapping::Note) return;        // pads don't drive faders
    if (!cont && src == MidiMapping::PitchBend) return;
    if (!cont && raw == 0) return;                       // a button's release
    learn(device, src, ch, num, raw);
    return;
  }
  for (auto& mp : maps_)
    if (mp.src == src && mp.ch == ch && mp.num == num && (mp.device.isEmpty() || mp.device == device))
      apply(mp, raw, src == MidiMapping::Note ? raw > 0 : raw >= 64);
}

void MidiControl::learn(const juce::String& device, MidiMapping::Src src, int ch, int num, int raw) {
  MidiMapping m;
  m.src = src; m.ch = ch; m.num = num; m.device = device; m.target = armed_;
  m.mode = isContinuous(armed_) ? MidiMapping::Absolute : MidiMapping::PressToggles;
  if (isProjectTarget(armed_)) {
    auto t = juce::StringArray::fromTokens(armed_, ".", "");
    if (auto* e = host_.engine ? host_.engine() : nullptr)
      if (auto* fx = e->insertAt(t[1].getIntValue(), t[2].getIntValue())) m.fx = juce::String(fx->name());
  }
  m.lastRaw = raw; m.detect = 1;
  // One controller per control and one control per controller.
  maps_.erase(std::remove_if(maps_.begin(), maps_.end(), [&](const MidiMapping& x) {
    return x.target == m.target || (x.src == src && x.ch == ch && x.num == num && x.device == device); }), maps_.end());
  maps_.push_back(m);
  if (isProjectTarget(m.target)) saveProject(); else saveApp();
  if (host_.note) host_.note(juce::String(juce::CharPointer_UTF8("MIDI: ")) + m.sourceText() + juce::String(juce::CharPointer_UTF8(" \xe2\x86\x92 ")) + describe(m.target));
  armed_.clear();
  changed();
}

void MidiControl::apply(MidiMapping& mp, int raw, bool on) {
  if (!isContinuous(mp.target)) {
    if (mp.mode == MidiMapping::FollowsValue) { if (stateOf(mp.target) != on) press(mp.target); }
    else if (on) press(mp.target);
    return;
  }
  if (mp.fx.isNotEmpty()) {  // a plugin mapping only drives the plugin it was learned on
    auto t = juce::StringArray::fromTokens(mp.target, ".", "");
    auto* e = host_.engine ? host_.engine() : nullptr;
    auto* fx = e ? e->insertAt(t[1].getIntValue(), t[2].getIntValue()) : nullptr;
    if (!fx || juce::String(fx->name()) != mp.fx) return;
  }
  const float cur = valueOf(mp.target);
  if (cur < 0.f) return;
  if (mp.detect > 0 && mp.detect <= 16)
    juce::Logger::writeToLog("MIDI " + mp.sourceText() + " = " + juce::String(raw) + "  (" + describe(mp.target) + ")");
  // Endless knobs send small steps around a centre ("+1" / "-1"): 65 / 63 (centre 64), 17 / 15 (centre 16) or
  // 1 / 127 (two's complement), larger when turned fast. A fixed knob sweeps through values in one direction.
  if (mp.mode == MidiMapping::Absolute && mp.src == MidiMapping::CC) {
    auto& h = mp.recent;
    if (mp.nRecent == 0 && mp.lastRaw >= 0) h[(size_t)mp.nRecent++] = mp.lastRaw;  // the value it was learned on
    if (mp.nRecent < (int)h.size()) h[(size_t)mp.nRecent++] = raw;
    else { std::rotate(h.begin(), h.begin() + 1, h.end()); h.back() = raw; }
    const int n = mp.nRecent;
    auto all = [&](auto pred) { for (int i = 0; i < n; ++i) if (!pred(h[(size_t)i])) return false; return true; };
    auto count = [&](int v) { int c = 0; for (int i = 0; i < n; ++i) c += h[(size_t)i] == v; return c; };
    // Each family: values near its centre. The centre itself means "stop": some encoders (e.g. the Arturia MiniLab)
    // send a step and then the centre for every click (65, 64, 65, 64 ...).
    const bool r2C = all([](int x) { return x <= 8 || x >= 120; }), r16 = all([](int x) { return x >= 9 && x <= 23; }),
               r64 = all([](int x) { return x >= 57 && x <= 71; });
    const int c2C = count(0), c16 = count(16), c64 = count(64);
    const bool in2C = r2C && c2C < n, in16 = r16 && c16 < n, in64 = r64 && c64 < n;  // ... with at least one real step
    const int centres = in2C ? c2C : in16 ? c16 : in64 ? c64 : 0;
    bool repeats = false, oneWay = true;
    int sign = 0, runOfSame = 1, maxRun = 1;
    for (int i = 1; i < n; ++i) {
      const int d = h[(size_t)i] - h[(size_t)(i - 1)];
      if (d == 0) { repeats = true; maxRun = juce::jmax(maxRun, ++runOfSame); continue; }
      runOfSame = 1;
      const int s = d > 0 ? 1 : -1;
      if (sign != 0 && s != sign) oneWay = false;
      sign = s;
    }
    MidiMapping::Mode found = MidiMapping::Absolute;
    const bool learning = mp.detect > 0;
    // Right after learning: decide from the pattern of the first values. Later (e.g. a mapping learned before this
    // check existed): only on unmistakable signs, the same small step three times in a row, or steps that keep
    // returning to the centre (a fixed knob never comes back to the same value like that).
    const bool stepThenStop = centres >= 2 && n - centres >= 2;
    if ((learning && n >= 4 && (repeats || !oneWay)) || maxRun >= 3 || stepThenStop)
      found = in2C ? MidiMapping::Rel2C : in16 ? MidiMapping::Rel16 : in64 ? MidiMapping::Rel64 : MidiMapping::Absolute;
    if (found != MidiMapping::Absolute) {
      mp.mode = found; mp.detect = 0; mp.nRecent = 0;
      saveProject(); saveApp();
      juce::Logger::writeToLog("MIDI " + mp.sourceText() + ": endless knob, mode " + juce::String((int)found));
      if (host_.note) host_.note(describe(mp.target) + ": endless knob detected (relative mode)");
      changed();
      return;  // the values so far were steps, not positions
    }
    if (learning) {
      const bool couldBeEndless = r2C || r16 || r64;
      if ((n >= 3 && !couldBeEndless) || ++mp.detect > 8) mp.detect = 0;  // a fixed knob: from now on it picks up as usual
      else { mp.lastRaw = raw; return; }                                   // still deciding: don't move the control yet
    }
  }
  mp.lastRaw = raw;
  float v = cur;
  switch (mp.mode) {
    case MidiMapping::Absolute: {
      const float in = mp.src == MidiMapping::PitchBend ? (float)raw / 16383.f : (float)raw / 127.f;
      if (mp.picked && mp.lastSet >= 0.f && std::abs(cur - mp.lastSet) > 0.004f) mp.picked = false;  // moved on screen
      if (!mp.picked) {
        const bool near = std::abs(in - cur) <= 0.015f;
        const bool crossed = mp.lastIn >= 0.f && (mp.lastIn - cur) * (in - cur) <= 0.f;
        mp.lastIn = in;
        if (!(near || crossed)) {
          const double now = juce::Time::getMillisecondCounterHiRes();
          if (host_.note && now - lastHint_ > 1500.0) {
            lastHint_ = now;
            host_.note(describe(mp.target) + ": move the control " + (in < cur ? "up" : "down") + " past " +
                       juce::String(juce::roundToInt(cur * 100.f)) + "% to pick it up");
          }
          return;
        }
        mp.picked = true;
      }
      mp.lastIn = in; v = in; break;
    }
    case MidiMapping::Rel64: v = cur + (float)(raw - 64) / 200.f; break;
    case MidiMapping::Rel2C: v = cur + (float)(raw < 64 ? raw : raw - 128) / 200.f; break;
    case MidiMapping::RelSign: v = cur + (float)((raw & 64) ? -(raw & 63) : (raw & 63)) / 200.f; break;
    case MidiMapping::Rel16: v = cur + (float)(raw - 16) / 200.f; break;
    default: return;
  }
  setValue(mp.target, juce::jlimit(0.f, 1.f, v));
  mp.lastSet = valueOf(mp.target);
}

void MidiControl::setLearning(bool on) { learning_ = on; armed_.clear(); changed(); }

const MidiMapping* MidiControl::mappingFor(const juce::String& target) const {
  for (auto& m : maps_) if (m.target == target) return &m;
  return nullptr;
}

void MidiControl::removeMapping(const juce::String& target) {
  const bool proj = isProjectTarget(target);
  maps_.erase(std::remove_if(maps_.begin(), maps_.end(), [&](const MidiMapping& x) { return x.target == target; }), maps_.end());
  if (proj) saveProject(); else saveApp();
  changed();
}

void MidiControl::setMode(const juce::String& target, MidiMapping::Mode md) {
  for (auto& m : maps_) if (m.target == target) { m.mode = md; m.picked = false; m.detect = 0; m.nRecent = 0; }
  if (isProjectTarget(target)) saveProject(); else saveApp();
  changed();
}

void MidiControl::loadProjectMappings() {
  maps_.erase(std::remove_if(maps_.begin(), maps_.end(), [](const MidiMapping& x) { return isProjectTarget(x.target); }), maps_.end());
  const juce::var data = host_.projectData ? host_.projectData() : juce::var();  // keep it alive while reading
  if (auto* a = data["midi"].getArray())
      for (auto& v : *a) { MidiMapping m; if (MidiMapping::fromVar(v, m) && isProjectTarget(m.target)) maps_.push_back(m); }
  changed();
}

void MidiControl::insertMoved(int channelId, int from, int to) {
  bool any = false;
  for (auto& m : maps_) {
    if (!isProjectTarget(m.target)) continue;
    auto t = juce::StringArray::fromTokens(m.target, ".", "");
    if (t.size() < 4 || t[1].getIntValue() != channelId) continue;
    int s = t[2].getIntValue();
    if (s == from) s = to;
    else if (from < to && s > from && s <= to) --s;
    else if (from > to && s >= to && s < from) ++s;
    else continue;
    t.set(2, juce::String(s)); m.target = t.joinIntoString("."); any = true;
  }
  if (any) { saveProject(); changed(); }
}

void MidiControl::saveApp() {
  juce::Array<juce::var> a;
  for (auto& m : maps_) if (!isProjectTarget(m.target)) a.add(m.toVar());
  props_.setValue("midi.map", juce::JSON::toString(juce::var(a), true));
  props_.saveIfNeeded();
}

void MidiControl::saveProject() {
  juce::Array<juce::var> a;
  for (auto& m : maps_) if (isProjectTarget(m.target)) a.add(m.toVar());
  if (host_.setProjectValue) host_.setProjectValue("midi", juce::var(a));
}

// ---------------------------------------------------------------- targets
bool MidiControl::isContinuous(const juce::String& id) {
  if (id.startsWith("ch.")) return id.endsWith(".fader") || id.endsWith(".pan") || id.endsWith(".trim");
  if (id.startsWith("fx.")) return !id.endsWith(".bypass");
  return false;
}

float MidiControl::valueOf(const juce::String& id) const {
  auto* e = host_.engine ? host_.engine() : nullptr; if (!e) return -1.f;
  auto t = juce::StringArray::fromTokens(id, ".", "");
  if (t[0] == "ch" && t.size() == 3) {
    auto* c = channelOf(*e, t[1].getIntValue()); if (!c) return -1.f;
    if (t[2] == "fader") return vOf(c->gainDb.load()) / 100.f;
    if (t[2] == "pan") return (c->pan.load() + 1.f) / 2.f;
    if (t[2] == "trim") return (c->trimDb.load() + 24.f) / 48.f;
  }
  if (t[0] == "fx" && t.size() == 4 && t[3] != "bypass") {
    auto* fx = e->insertAt(t[1].getIntValue(), t[2].getIntValue());
    const int p = t[3].getIntValue();
    if (fx && p >= 0 && p < fx->numParams()) return fx->getParam(p);
  }
  return -1.f;
}

void MidiControl::setValue(const juce::String& id, float v) {
  auto* e = host_.engine ? host_.engine() : nullptr; if (!e) return;
  auto t = juce::StringArray::fromTokens(id, ".", "");
  v = juce::jlimit(0.f, 1.f, v);
  if (t[0] == "ch" && t.size() == 3) {
    auto* c = channelOf(*e, t[1].getIntValue()); if (!c) return;
    if (t[2] == "fader") c->gainDb = juce::jmax(-200.f, dbOf(v * 100.f));
    else if (t[2] == "pan") c->pan = v * 2.f - 1.f;
    else if (t[2] == "trim") c->trimDb = v * 48.f - 24.f;
  } else if (t[0] == "fx" && t.size() == 4) {
    auto* fx = e->insertAt(t[1].getIntValue(), t[2].getIntValue());
    const int p = t[3].getIntValue();
    if (fx && p >= 0 && p < fx->numParams()) fx->setParam(p, v);
  }
}

juce::Component* MidiControl::findTagged(const juce::String& id) const {
  std::function<juce::Component*(juce::Component*)> walk = [&](juce::Component* c) -> juce::Component* {
    if (c->getProperties()["midi"].toString() == id) return c;
    for (auto* k : c->getChildren()) if (auto* f = walk(k)) return f;
    return nullptr;
  };
  auto* root = host_.root ? host_.root() : nullptr;
  return root ? walk(root) : nullptr;
}

bool MidiControl::stateOf(const juce::String& id) const {
  if (id.startsWith("fx.") && id.endsWith(".bypass")) {
    auto t = juce::StringArray::fromTokens(id, ".", "");
    auto* e = host_.engine ? host_.engine() : nullptr;
    auto* fx = e ? e->insertAt(t[1].getIntValue(), t[2].getIntValue()) : nullptr;
    return fx && fx->bypassed();
  }
  if (auto* b = dynamic_cast<juce::Button*>(findTagged(id))) return b->getToggleState();
  return false;
}

void MidiControl::press(const juce::String& id) {
  if (host_.press && host_.press(id)) return;
  if (id.startsWith("fx.") && id.endsWith(".bypass")) {
    auto t = juce::StringArray::fromTokens(id, ".", "");
    auto* e = host_.engine ? host_.engine() : nullptr;
    if (auto* fx = e ? e->insertAt(t[1].getIntValue(), t[2].getIntValue()) : nullptr) fx->setBypass(!fx->bypassed());
    return;
  }
  if (auto* b = dynamic_cast<juce::Button*>(findTagged(id))) b->triggerClick();
}

juce::String MidiControl::describe(const juce::String& id) const {
  auto* e = host_.engine ? host_.engine() : nullptr;
  auto t = juce::StringArray::fromTokens(id, ".", "");
  auto chName = [&](int cid) -> juce::String {
    auto* c = e ? channelOf(*e, cid) : nullptr;
    return c ? juce::String(c->name) : "Channel " + juce::String(cid);
  };
  if (t[0] == "ch" && t.size() == 3) {
    const juce::String what = t[2] == "trim" ? "gain" : t[2] == "arm" ? "record arm" : t[2] == "monitor" ? "input monitoring" : t[2];
    return chName(t[1].getIntValue()) + " " + what;
  }
  if (t[0] == "fx" && t.size() == 4) {
    const int cid = t[1].getIntValue(), slot = t[2].getIntValue();
    juce::String s = chName(cid) + " insert " + juce::String(slot + 1);
    auto* fx = e ? e->insertAt(cid, slot) : nullptr;
    if (fx) s << " " << juce::String(fx->name());
    if (t[3] == "bypass") return s + " bypass";
    const int p = t[3].getIntValue();
    if (fx && p >= 0 && p < fx->numParams()) return s + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + juce::String(fx->params()[(size_t)p].name);
    return s + " parameter " + juce::String(p + 1);
  }
  if (t[0] == "transport") {
    const juce::String k = t[1];
    return "Transport: " + (k == "rtz" ? juce::String("return to zero") : k == "rec" ? juce::String("record") : k);
  }
  if (id == "tap") return "Tap tempo";
  if (id == "listen") return "Listen mode";
  if (t[0] == "mon") return "Monitor: " + (t[1] == "pol" ? juce::String("polarity") : t[1]);
  if (t[0] == "ref") {
    const juce::String k = t[1];
    return "Reference: " + (k == "a" ? juce::String("A (mix)") : k == "b" ? juce::String("B (reference)") : k == "play" ? juce::String("play / stop")
                            : k == "match" ? juce::String("match loudness") : k);
  }
  return id;
}

}  // namespace ui
