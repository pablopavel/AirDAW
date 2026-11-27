#include "Strip.h"
#include "AirLookAndFeel.h"

namespace ui {

juce::uint32 channelColour(int id) {
  static const juce::uint32 cols[] = {0xffe5484d, 0xffffb347, 0xff7bd88f, 0xff4aa3ff, 0xffb38cff, 0xffff7ab6};
  if (isTrack(id)) return cols[id % 6];
  if (isBus(id)) return 0xffc9b27a;
  return 0xffffb347;
}

static int numInputs(AppCtx& ctx) {
  if (auto* d = ctx.devices.getCurrentAudioDevice()) return juce::jmax(1, d->getActiveInputChannels().countNumberOfSetBits());
  return 2;
}

// A strip's one Inserts button: how many plugins the chain has (their names in the tooltip), lit while any of them
// runs, outlined while the insert panel shows this channel.
static void syncInserts(AppCtx& ctx, airdaw::Engine& e, int id, InsertSlot& b) {
  int n = 0; bool lit = false;
  juce::StringArray names;
  for (int s = 0; s < airdaw::kMaxInserts; ++s)
    if (auto* fx = e.insertAt(id, s)) {
      ++n; lit |= !fx->bypassed();
      names.add(juce::String(n) + ". " + juce::String(fx->name()) + (fx->bypassed() ? " (bypassed)" : ""));
    }
  const bool open = ctx.insertPanelChannel && ctx.insertPanelChannel() == id;
  if (n != b.count || lit != b.lit || open != b.open) { b.count = n; b.lit = lit; b.open = open; b.repaint(); }
  const auto tip = n == 0 ? juce::String("No inserts yet: click to add plugins") : "Inserts (click to open):\n" + names.joinIntoString("\n");
  if (b.getTooltip() != tip) b.setTooltip(tip);
}

// ---------------- MiniReadout ----------------
void MiniReadout::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  t.drawWell(g, r, t.lcdBg(), 3.f);
  auto cap = r.removeFromTop(r.getHeight() * 0.36f);
  g.setColour(t.readoutCaption()); g.setFont(Theme::spaced(Theme::uiFont(juce::jmin(10.f, cap.getHeight() * 0.8f)), 0.1f));
  g.drawText(cap_, cap, juce::Justification::centredBottom);
  g.setFont(Theme::monoFont(juce::jmin(26.f, r.getHeight() * 0.75f)));
  t.drawGlowText(g, v_, r, juce::Justification::centred, c_.isTransparent() ? t.readout() : c_, 0.7f);
}

// ---------------- MonitorButton ----------------
void MonitorButton::paintButton(juce::Graphics& g, bool over, bool down) {
  getLookAndFeel().drawButtonBackground(g, *this, {}, over, down);
  auto r = AirLookAndFeel::legendArea(*this, down);
  if (!AirLookAndFeel::isSwitch(*this)) r = r.reduced(1.f);
  const float s = juce::jmin(r.getWidth(), r.getHeight()) * (AirLookAndFeel::isSwitch(*this) ? 0.95f : 0.66f), cx = r.getCentreX(),
              cy = r.getCentreY() + s * 0.06f;
  g.setColour(AirLookAndFeel::legendColour(*this, down));
  juce::Path band;  // headband
  band.addCentredArc(cx, cy, s * 0.42f, s * 0.42f, 0.f, -1.9f, 1.9f, true);
  g.strokePath(band, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  const float cw = s * 0.22f, ch = s * 0.42f, cupY = cy + s * 0.02f;  // ear cups
  g.fillRoundedRectangle(cx - s * 0.47f, cupY, cw, ch, cw * 0.4f);
  g.fillRoundedRectangle(cx + s * 0.47f - cw, cupY, cw, ch, cw * 0.4f);
}

// ---------------- Strip ----------------
Strip::Strip(AppCtx& ctx, int id) : ctx_(ctx), id_(id) {
  plate_.setColourBar(channelColour(id));
  plate_.onClick = [this] { ctx_.showInserts(id_, -1); };
  plate_.onRename = [this](const juce::String& n) { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->name = n.toStdString(); };
  plate_.onMenu = [this] {  // right-click the name: rename, inserts, and (tracks) remove
    juce::Component::SafePointer<Strip> sp(this);  // made here: MSVC can't capture `this` into the nested lambda's init
    juce::PopupMenu m;
    m.addItem(1, juce::String(juce::CharPointer_UTF8("Rename\xe2\x80\xa6")));
    m.addItem(2, "Show inserts");
    if (id_ < airdaw::kMaxTracks) { m.addSeparator(); m.addItem(3, juce::String(juce::CharPointer_UTF8("Remove track\xe2\x80\xa6"))); }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&plate_), [sp](int r) {
      if (!sp) return;
      if (r == 1) sp->plate_.startRename();
      else if (r == 2) sp->ctx_.showInserts(sp->id_, -1);
      else if (r == 3 && sp->ctx_.removeTrack) sp->ctx_.removeTrack(sp->id_);
    });
  };
  plate_.onDragOut = [this] {
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
      if (!dc->isDragAndDropActive()) dc->startDragging("strip:" + juce::String(id_), this);  // the strip's picture follows the mouse
  };
  plate_.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
  addAndMakeVisible(plate_);
  for (auto* b : {&chn_, &in_, &route_, &m_, &s_, &t_, &r_}) { addAndMakeVisible(*b); b->getProperties().set("fontH", 11.0); }
  m_.getProperties().set("kind", "m"); s_.getProperties().set("kind", "s");
  t_.getProperties().set("kind", "t"); r_.getProperties().set("kind", "r"); mon_.getProperties().set("kind", "mon");
  for (juce::Button* b : {(juce::Button*)&m_, (juce::Button*)&s_, (juce::Button*)&mon_, (juce::Button*)&t_, (juce::Button*)&r_})
    b->getProperties().set("sw", true);  // bat-handle toggles when Display > Hardware > Buttons is Toggles
  if (isBus(id)) {
    chn_.setButtonText("Bus"); chn_.setEnabled(false); in_.setVisible(false);
    route_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x86\x92 Master"))); route_.setEnabled(false);
    t_.setVisible(false); r_.setVisible(false); mon_.setVisible(false);
  } else {
    addAndMakeVisible(mon_);
    mon_.setTooltip("Input monitoring: hear this track's input while it is armed (R). Recording works either way.");
    mon_.onClick = [this] { if (auto* e = ctx_.engine()) e->track(id_).monitor = !e->track(id_).monitor.load(); };
    chn_.onClick = [this] {
      auto* e = ctx_.engine(); if (!e) return;
      auto& c = e->track(id_);
      const bool st = !c.stereo.load();
      if (st) { const int l = c.inputL.load(); c.inputR = (l >= 0 && l + 1 < numInputs(ctx_)) ? l + 1 : -1; }
      c.stereo = st;
    };
    in_.onClick = [this] { chooseInput(); };
    route_.onClick = [this] { chooseRoute(); };
    t_.setTooltip("Show this track's takes in the takes bar (no arming needed)");
    t_.onClick = [this] { ctx_.viewTrack = ctx_.viewTrack == id_ ? -1 : id_; if (ctx_.layoutChanged) ctx_.layoutChanged(); };
    r_.onClick = [this] {
      auto* e = ctx_.engine(); if (!e) return;
      auto& c = e->track(id_);
      if (!c.armed.load() && c.inputL.load() < 0) {  // no input chosen yet: use In 1 (In 1/2 for stereo)
        const bool st = c.stereo.load() && numInputs(ctx_) > 1;
        c.inputL = 0; c.inputR = st ? 1 : -1;
        ctx_.note(juce::String(c.name) + ": input set to " + (st ? "In 1/2" : "In 1") + " (change it with the In button)");
      }
      c.armed = !c.armed.load();
    };
  }
  m_.onClick = [this] { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->mute = !c->mute.load(); };
  s_.onClick = [this] { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->solo = !c->solo.load(); };
  addAndMakeVisible(gain_);
  gain_.onChange = [this](float v) { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->trimDb = v; };
  ins_.summary = true; ins_.channelId = id;
  ins_.onClick = [this] { ctx_.showInserts(id_, -1); };
  addAndMakeVisible(ins_);
  addAndMakeVisible(pan_);
  pan_.onChange = [this](float v) { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->pan = v; };
  addAndMakeVisible(fader_);
  fader_.onChange = [this](float v) { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->gainDb = juce::jmax(-200.f, dbOf(v)); };
  addAndMakeVisible(pol_);  // after the fader: it sits on the fader block's free bottom-left corner
  pol_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x8c\x80")));
  pol_.getProperties().set("fontH", 11.0); pol_.getProperties().set("noUpper", true);
  pol_.setTooltip("Polarity: flip this channel (after its input and trim, before the inserts). Takes are still recorded as they come in.");
  pol_.onClick = [this] { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->polarity = !c->polarity.load(); };
  // MIDI Learn targets ("ch.<id>.<control>")
  const juce::String mid = "ch." + juce::String(id) + ".";
  tagMidi(fader_, mid + "fader"); tagMidi(pan_, mid + "pan"); tagMidi(gain_, mid + "trim");
  tagMidi(m_, mid + "mute"); tagMidi(s_, mid + "solo"); tagMidi(pol_, mid + "polarity");
  if (!isBus(id)) { tagMidi(r_, mid + "arm"); tagMidi(mon_, mid + "monitor"); }
  setSize(kWidth, 600);
}

void Strip::chooseInput() {
  auto* e = ctx_.engine(); if (!e) return;
  auto& c = e->track(id_);
  const int n = numInputs(ctx_); const bool st = c.stereo.load();
  juce::PopupMenu m;
  m.addSectionHeader(st ? "Stereo input" : "Mono input");
  m.addItem(1, "None", true, c.inputL.load() < 0);
  if (st) { for (int i = 0; i + 1 < n; i += 2) m.addItem(10 + i, "In " + juce::String(i + 1) + "/" + juce::String(i + 2), true, c.inputL.load() == i); }
  else for (int i = 0; i < n; ++i) m.addItem(10 + i, "In " + juce::String(i + 1), true, c.inputL.load() == i);
  m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&in_), [this, st](int r) {
    auto* e2 = ctx_.engine(); if (!e2 || r == 0) return;
    auto& ch = e2->track(id_);
    if (r == 1) { ch.inputL = -1; ch.inputR = -1; return; }
    ch.inputL = r - 10; ch.inputR = st ? r - 9 : -1;
  });
}

void Strip::chooseRoute() {
  auto* e = ctx_.engine(); if (!e) return;
  const int cur = e->track(id_).route.load();
  juce::PopupMenu m;
  m.addSectionHeader("Output");
  m.addItem(1, "Master", true, cur == 0);
  for (int b = 1; b <= airdaw::kNumBuses; ++b) m.addItem(1 + b, "Bus " + juce::String(b), true, cur == b);
  m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&route_), [this](int r) {
    if (r > 0) if (auto* e2 = ctx_.engine()) e2->track(id_).route = r - 1;
  });
}

void Strip::chooseTake() {
  auto* e = ctx_.engine(); if (!e) return;
  juce::PopupMenu m;
  m.addSectionHeader("Takes on " + juce::String(e->track(id_).name));
  const auto* cur = e->track(id_).take.load();
  m.addItem(1, "None (live input only)", true, cur == nullptr);
  for (int i = 0; i < e->numTakes(id_); ++i) {
    const auto* tk = e->takeAt(id_, i);
    const double secs = (double)tk->start / e->sampleRate(), len = (double)tk->l.size() / e->sampleRate();
    m.addItem(10 + i, juce::File(juce::String(tk->path)).getFileNameWithoutExtension() + "   @ " + juce::String(secs, 1) + " s, " +
                          juce::String(len, 1) + " s", true, tk == cur);
  }
  if (e->numTakes(id_) == 0) m.addItem(2, "No takes recorded yet", false, false);
  m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&t_), [this](int r) {
    auto* e2 = ctx_.engine(); if (!e2 || r == 0 || r == 2) return;
    e2->setActiveTake(id_, r == 1 ? -1 : r - 10);
  });
}

void Strip::sync() {
  auto* e = ctx_.engine(); if (!e) return;
  auto* cp = channelOf(*e, id_); if (!cp) return;
  auto& c = *cp;
  plate_.setName2(c.name);
  if (isTrack(id_)) {
    chn_.setButtonText(c.stereo.load() ? "Stereo" : "Mono"); chn_.setToggleState(true, juce::dontSendNotification);
    const int l = c.inputL.load();
    in_.setButtonText(l < 0 ? juce::String("In -") : c.stereo.load() && c.inputR.load() >= 0
                          ? "In " + juce::String(l + 1) + "/" + juce::String(c.inputR.load() + 1) : "In " + juce::String(l + 1));
    const int rt = c.route.load();
    route_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x86\x92 ")) + (rt == 0 ? "Master" : "Bus " + juce::String(rt)));
    r_.setToggleState(c.armed.load(), juce::dontSendNotification);
    mon_.setToggleState(c.monitor.load(), juce::dontSendNotification);
    t_.setColour(juce::TextButton::textColourOffId, e->numTakes(id_) > 0 ? T().am() : Theme::tx());  // amber: has takes
    t_.setToggleState(ctx_.viewTrack == id_, juce::dontSendNotification);
  }
  m_.setToggleState(c.mute.load(), juce::dontSendNotification);
  pol_.setToggleState(c.polarity.load(), juce::dontSendNotification);
  s_.setToggleState(c.solo.load(), juce::dontSendNotification);
  if (!gain_.isMouseButtonDown()) gain_.setValue(c.trimDb.load());
  if (!pan_.isMouseButtonDown()) pan_.setValue(c.pan.load());
  if (!fader_.isMouseButtonDown()) fader_.setDb(c.gainDb.load());
  fader_.setMeter(c.meter.peakL.load(), c.meter.peakR.load());
  syncInserts(ctx_, *e, id_, ins_);
}

void Strip::paint(juce::Graphics& g) {
  T().drawBrush(g, getLocalBounds().toFloat(), isBus(id_));
  g.setColour(juce::Colours::black); g.fillRect(getWidth() - 1, 0, 1, getHeight());
  g.setColour(juce::Colour(0x14ffffff)); g.fillRect(getWidth() - 2, 0, 1, getHeight());
}

bool Strip::isInterestedInDragSource(const SourceDetails& d) {
  const auto s = d.description.toString();
  if (!s.startsWith("strip:")) return false;
  const int from = s.fromFirstOccurrenceOf("strip:", false, false).getIntValue();
  return from != id_ && isBus(from) == isBus(id_);
}

void Strip::itemDropped(const SourceDetails& d) {
  const int from = d.description.toString().fromFirstOccurrenceOf("strip:", false, false).getIntValue();
  const bool after = dropSide_ > 0;
  dropSide_ = 0; repaint();
  if (ctx_.moveStrip) ctx_.moveStrip(from, id_, after);
}

void Strip::paintOverChildren(juce::Graphics& g) {  // where a dragged strip will land
  if (dropSide_ == 0) return;
  const float x = dropSide_ < 0 ? 1.5f : (float)getWidth() - 1.5f;
  T().drawGlow(g, {x - 1.5f, 0.f, 3.f, (float)getHeight()}, T().am(), 6.f, 0.8f, 1.5f);
  g.setColour(T().am()); g.fillRect(x - 1.5f, 0.f, 3.f, (float)getHeight());
}

void Strip::resized() {
  auto r = getLocalBounds().reduced(6, 7);
  const int gap = 5;
  auto take = [&](int h) { auto x = r.removeFromTop(h); r.removeFromTop(gap); return x; };
  plate_.setBounds(take(24));
  auto row = take(26);
  if (isBus(id_)) chn_.setBounds(row); else { chn_.setBounds(row.removeFromLeft(row.getWidth() / 2 + 6).reduced(1, 0)); in_.setBounds(row.reduced(1, 0)); }
  gain_.setBounds(take(40));
  ins_.setBounds(take(22));
  route_.setBounds(take(26));
  pan_.setBounds(take(20).expanded(0, PanSlider::kGlowPad));  // room for the thumb's glow (drawn over the rows around it)
  row = take(T().buttons == "toggle" ? 38 : 26);  // toggles: a printed label above each switch
  const int nb = isBus(id_) ? 2 : 5, bw = row.getWidth() / nb;
  for (juce::Button* b : {(juce::Button*)&m_, (juce::Button*)&s_, (juce::Button*)&mon_, (juce::Button*)&t_, (juce::Button*)&r_})
    if (b->isVisible()) b->setBounds(row.removeFromLeft(bw).reduced(1, 0));
  fader_.setBounds(r);
  pol_.setBounds(fader_.cornerArea().translated(r.getX(), r.getY()).withTrimmedLeft(2));
}

// ---------------- MasterStrip ----------------
MasterStrip::MasterStrip(AppCtx& ctx) : ctx_(ctx) {
  plate_.setName2("Master"); plate_.setRenamable(false); plate_.setColourBar(channelColour(kMasterId));
  plate_.onClick = [this] { ctx_.showInserts(kMasterId, -1); };
  addAndMakeVisible(plate_);
  ins_.summary = true; ins_.channelId = kMasterId;
  ins_.onClick = [this] { ctx_.showInserts(kMasterId, -1); };
  addAndMakeVisible(ins_);
  for (auto* c : std::initializer_list<juce::Component*>{&vuL_, &vuR_, &lm_, &ls_, &li_, &tp_, &rms_, &clip_, &corr_, &fader_}) addAndMakeVisible(c);
  lm_.setTooltip("Momentary loudness (400 ms), LUFS");
  ls_.setTooltip("Short-term loudness (3 s), LUFS");
  li_.setTooltip("Integrated loudness of what has played since the start (resets when you play from the beginning). Click to reset.");
  tp_.setTooltip("Highest true peak (inter-sample, 4x oversampled), dBTP. Red above -1 dBTP. Click to reset.");
  rms_.setTooltip("RMS level, last 300 ms, dBFS");
  clip_.setTooltip("Lights when the master reaches 0 dBFS / 0 dBTP. Click to reset.");
  auto reset = [this] { clipped_ = false; if (onResetLoudness) onResetLoudness(); };
  li_.onClick = reset; tp_.onClick = reset; clip_.onClick = reset;
  fader_.onChange = [this](float v) { if (auto* e = ctx_.engine()) e->master().gainDb = juce::jmax(-200.f, dbOf(v)); };
  tagMidi(fader_, "ch.200.fader");
}

void MasterStrip::sync() {
  auto* e = ctx_.engine(); if (!e) return;
  auto& m = e->master();
  const float pl = m.meter.peakL.load(), pr = m.meter.peakR.load(), rl = m.meter.rmsL.load(), rr = m.meter.rmsR.load();
  if (!fader_.isMouseButtonDown()) fader_.setDb(m.gainDb.load());
  fader_.setMeter(pl, pr);
  // VU: RMS with ~300 ms ballistics; 0 VU = -18 dBFS RMS, scale -20..+3 VU.
  auto vu = [](float rms) { const float db = rms > 0.f ? 20.f * std::log10(rms) + 18.f : -40.f; return juce::jlimit(0.f, 1.05f, (db + 20.f) / 23.f); };
  vuL += (vu(rl) - vuL) * 0.25f; vuR += (vu(rr) - vuR) * 0.25f;
  vuL_.setLevel(vuL); vuR_.setLevel(vuR);
  syncInserts(ctx_, *e, kMasterId, ins_);
}

void MasterStrip::setLoudness(const airdaw::LoudnessReadout& r) {
  // Below -70 LUFS (the BS.1770 absolute gate) / -90 dB it is the plugins' noise floor, not programme: show "-".
  auto txt = [](float v, float floor) { return v > floor ? juce::String(v, 1) : juce::String("-"); };
  lm_.set(txt(r.momentary, -70.f)); ls_.set(txt(r.shortTerm, -70.f)); li_.set(txt(r.integrated, -70.f));
  tp_.set(txt(r.truePeak, -90.f), r.truePeak > -1.f ? juce::Colour(0xffe5484d) : juce::Colour());  // red above -1 dBTP (streaming limit)
  rms_.set(txt(r.rms, -90.f));
  if (r.truePeak >= 0.f || r.samplePeak >= -0.01f) clipped_ = true;
  clip_.set(juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f")), clipped_ ? juce::Colour(0xffe5484d) : T().c(0xff5b4a2c));
  corr_.set(r.rms > -70.f ? r.correlation : 0.f);  // near silence the correlation is meaningless
}

void CorrBar::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  auto lab = r.removeFromTop(14.f);
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(10.f), 0.14f));
  g.drawText("CORR", lab, juce::Justification::centredLeft);
  g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x88\x92" "1 \xc2\xb7 0 \xc2\xb7 +1")), lab, juce::Justification::centredRight);
  auto bar = r.withHeight(11.f).withY(r.getY() + 2.f);
  t.drawWell(g, bar, juce::Colour(0xff030303), 2.f);
  // From the centre: amber to the right for in-phase material, red to the left only when it is out of phase.
  const float cx = bar.getCentreX(), x = bar.getX() + (c_ + 1.f) * 0.5f * bar.getWidth();
  if (std::abs(c_) > 0.01f) {
    auto fill = c_ > 0 ? bar.withLeft(cx).withRight(x) : bar.withLeft(x).withRight(cx);
    const juce::Colour col = c_ > 0 ? (c_ > 0.9f ? t.amLight() : t.am()) : juce::Colour(0xffe5484d);
    t.drawGlow(g, fill, col, 8.f, 0.4f);
    g.setColour(col);
    for (float sx = fill.getX(); sx < fill.getRight(); sx += 6.f) g.fillRect(sx, fill.getY(), juce::jmin(5.f, fill.getRight() - sx), fill.getHeight());
  }
  g.setColour(juce::Colour(0x55ffffff)); g.fillRect(bar.getCentreX() - 0.5f, bar.getY() - 2.f, 1.f, bar.getHeight() + 4.f);
}

void MasterStrip::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  t.drawBrush(g, r);
  g.setColour(juce::Colours::black); g.fillRect(0, 0, 2, getHeight());
  g.setGradientFill(juce::ColourGradient(t.am().withAlpha(t.glowAlpha(0.11f)), 0, 0, juce::Colours::transparentBlack, 24, 0, false));
  g.fillRect(2, 0, 24, getHeight());
}

void MasterStrip::resized() {
  auto r = getLocalBounds().reduced(8, 7);
  r.removeFromLeft(2);
  plate_.setBounds(r.removeFromTop(26)); r.removeFromTop(6);
  ins_.setBounds(r.removeFromTop(24)); r.removeFromTop(6);
  const int vuH = 64;
  // Short windows: the VU pair, then the correlation bar, make way for the fader (kept >= 110 px).
  const int rowH = fader_.bigReadout ? 64 : 34, readH = 2 * rowH + 4 + 8, corrH = 30, faderMin = 110 + 30;
  const bool showVu = r.getHeight() - vuH - 6 - readH - corrH >= faderMin;
  showCorr_ = r.getHeight() - readH - corrH >= faderMin;
  vuL_.setVisible(showVu); vuR_.setVisible(showVu); corr_.setVisible(showCorr_);
  if (showVu) {
    auto vu = r.removeFromTop(vuH); r.removeFromTop(6);
    const int vw = juce::jmin((vu.getWidth() - 6) / 2, vuH * 120 / 70);  // keep the meter face's proportions
    auto both = vu.withSizeKeepingCentre(2 * vw + 6, vuH);
    vuL_.setBounds(both.removeFromLeft(vw)); both.removeFromLeft(6); vuR_.setBounds(both);
  }
  for (auto row : {std::array<MiniReadout*, 3>{&lm_, &ls_, &li_}, std::array<MiniReadout*, 3>{&tp_, &rms_, &clip_}}) {
    auto rd = r.removeFromTop(rowH); r.removeFromTop(4);
    const int w = (rd.getWidth() - 8) / 3;
    for (auto* m : row) { m->setBounds(rd.removeFromLeft(w)); rd.removeFromLeft(4); }
  }
  r.removeFromTop(4);
  if (showCorr_) { corr_.setBounds(r.removeFromTop(corrH - 6)); r.removeFromTop(6); }
  fader_.setBounds(r);
}

}  // namespace ui
