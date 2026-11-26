#include "Strip.h"

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

// Slots shown: 4, growing by one past the last used slot, up to 8.
static int slotsToShow(airdaw::Engine& e, int id) {
  int last = -1;
  for (int s = 0; s < airdaw::kMaxInserts; ++s) if (e.insertAt(id, s)) last = s;
  return juce::jlimit(4, airdaw::kMaxInserts, last + 2);
}

static void syncSlot(AppCtx& ctx, airdaw::Engine& e, int id, int s, InsertSlot& slot) {
  auto* fx = e.insertAt(id, s);
  const juce::String n = fx ? juce::String(fx->name()) : juce::String();
  const bool lit = fx && !fx->bypassed(), open = fx && ctx.isCardOpen && ctx.isCardOpen(id, s);
  if (n != slot.name || lit != slot.lit || open != slot.open) { slot.name = n; slot.lit = lit; slot.open = open; slot.repaint(); }
}

// ---------------- MiniReadout ----------------
void MiniReadout::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  t.drawWell(g, r, t.lcdBg(), 3.f);
  auto cap = r.removeFromTop(r.getHeight() * 0.36f);
  g.setColour(t.amd()); g.setFont(Theme::spaced(Theme::uiFont(juce::jmin(10.f, cap.getHeight() * 0.8f)), 0.1f));
  g.drawText(cap_, cap, juce::Justification::centredBottom);
  g.setFont(Theme::monoFont(juce::jmin(26.f, r.getHeight() * 0.75f)));
  t.drawGlowText(g, v_, r, juce::Justification::centred, c_.isTransparent() ? t.am() : c_, 0.7f);
}

// ---------------- MonitorButton ----------------
void MonitorButton::paintButton(juce::Graphics& g, bool over, bool down) {
  getLookAndFeel().drawButtonBackground(g, *this, {}, over, down);
  auto r = getLocalBounds().toFloat().reduced(2.f);
  if (down) r = r.translated(0.f, 1.f);
  const float s = juce::jmin(r.getWidth(), r.getHeight()) * 0.62f, cx = r.getCentreX(), cy = r.getCentreY() + s * 0.06f;
  g.setColour(getToggleState() ? juce::Colour(0xff2a1800) : Theme::tx().withAlpha(isEnabled() ? 1.f : 0.5f));
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
  addAndMakeVisible(plate_);
  for (auto* b : {&chn_, &in_, &route_, &m_, &s_, &t_, &r_}) { addAndMakeVisible(*b); b->getProperties().set("fontH", 11.0); }
  m_.getProperties().set("kind", "m"); s_.getProperties().set("kind", "s");
  t_.getProperties().set("kind", "t"); r_.getProperties().set("kind", "r");
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
  for (int i = 0; i < (int)slots_.size(); ++i) {
    auto& sl = slots_[(size_t)i];
    addChildComponent(sl);
    sl.channelId = id; sl.slot = i;
    sl.onClick = [this, i] { ctx_.showInserts(id_, i); };
    sl.onDropFrom = [this, i](int from) { if (ctx_.moveInsert) ctx_.moveInsert(id_, from, i); };
  }
  addChildComponent(summary_);
  summary_.onClick = [this] { ctx_.showInserts(id_, -1); };
  addAndMakeVisible(pan_);
  pan_.onChange = [this](float v) { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->pan = v; };
  addAndMakeVisible(fader_);
  fader_.onChange = [this](float v) { if (auto* e = ctx_.engine()) if (auto* c = channelOf(*e, id_)) c->gainDb = juce::jmax(-200.f, dbOf(v)); };
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
  s_.setToggleState(c.solo.load(), juce::dontSendNotification);
  if (!gain_.isMouseButtonDown()) gain_.setValue(c.trimDb.load());
  if (!pan_.isMouseButtonDown()) pan_.setValue(c.pan.load());
  if (!fader_.isMouseButtonDown()) fader_.setDb(c.gainDb.load());
  fader_.setMeter(c.meter.peakL.load(), c.meter.peakR.load());
  const int want = slotsToShow(*e, id_);
  if (want != shownSlots_) { shownSlots_ = want; layoutSlots(); resized(); }
  for (int s = 0; s < shownSlots_; ++s) syncSlot(ctx_, *e, id_, s, slots_[(size_t)s]);
  if (compact_) {
    int used = 0; for (int s = 0; s < airdaw::kMaxInserts; ++s) used += e->insertAt(id_, s) != nullptr;
    const juce::String n = used ? juce::String(used) + (used == 1 ? " insert" : " inserts") : juce::String();
    if (n != summary_.name) { summary_.name = n; summary_.repaint(); }
  }
}

void Strip::paint(juce::Graphics& g) {
  T().drawBrush(g, getLocalBounds().toFloat(), isBus(id_));
  g.setColour(juce::Colours::black); g.fillRect(getWidth() - 1, 0, 1, getHeight());
  g.setColour(juce::Colour(0x14ffffff)); g.fillRect(getWidth() - 2, 0, 1, getHeight());
}

void Strip::layoutSlots() {
  for (int s = 0; s < (int)slots_.size(); ++s) slots_[(size_t)s].setVisible(!compact_ && s < shownSlots_);
  summary_.setVisible(compact_);
}

void Strip::resized() {
  auto r = getLocalBounds().reduced(6, 7);
  const int gap = 5;
  // Fixed rows take ~230 px; if the slot list would leave the fader less than 130 px, fold it into one row.
  compact_ = r.getHeight() - 230 - shownSlots_ * 21 < 130;
  auto take = [&](int h) { auto x = r.removeFromTop(h); r.removeFromTop(gap); return x; };
  plate_.setBounds(take(24));
  auto row = take(26);
  if (isBus(id_)) chn_.setBounds(row); else { chn_.setBounds(row.removeFromLeft(row.getWidth() / 2 + 6).reduced(1, 0)); in_.setBounds(row.reduced(1, 0)); }
  gain_.setBounds(take(40));
  layoutSlots();
  if (compact_) { summary_.setBounds(r.removeFromTop(19)); r.removeFromTop(2); }
  else for (int s = 0; s < shownSlots_; ++s) { slots_[(size_t)s].setBounds(r.removeFromTop(19)); r.removeFromTop(2); }
  r.removeFromTop(gap - 2);
  route_.setBounds(take(26));
  pan_.setBounds(take(20));
  row = take(26);
  const int nb = isBus(id_) ? 2 : 5, bw = row.getWidth() / nb;
  for (juce::Button* b : {(juce::Button*)&m_, (juce::Button*)&s_, (juce::Button*)&mon_, (juce::Button*)&t_, (juce::Button*)&r_})
    if (b->isVisible()) b->setBounds(row.removeFromLeft(bw).reduced(1, 0));
  fader_.setBounds(r);
}

// ---------------- MasterStrip ----------------
MasterStrip::MasterStrip(AppCtx& ctx) : ctx_(ctx) {
  plate_.setName2("Master"); plate_.setRenamable(false); plate_.setColourBar(channelColour(kMasterId));
  plate_.onClick = [this] { ctx_.showInserts(kMasterId, -1); };
  addAndMakeVisible(plate_);
  for (int i = 0; i < (int)slots_.size(); ++i) {
    auto& sl = slots_[(size_t)i];
    addChildComponent(sl);
    sl.channelId = kMasterId; sl.slot = i;
    sl.onClick = [this, i] { ctx_.showInserts(kMasterId, i); };
    sl.onDropFrom = [this, i](int from) { if (ctx_.moveInsert) ctx_.moveInsert(kMasterId, from, i); };
  }
  for (auto* c : std::initializer_list<juce::Component*>{&vuL_, &vuR_, &pkL_, &pkR_, &rms_, &clip_, &fader_}) addAndMakeVisible(c);
  clip_.setTooltip("Lights when the master output reaches 0 dBFS. Click to reset.");
  clip_.onClick = [this] { clipped_ = false; holdL_ = holdR_ = -200.f; };
  fader_.onChange = [this](float v) { if (auto* e = ctx_.engine()) e->master().gainDb = juce::jmax(-200.f, dbOf(v)); };
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
  auto db = [](float x) { return x > 0.f ? 20.f * std::log10(x) : -200.f; };
  // Peak readouts hold the highest value for ~2 s.
  const float dl = db(pl), dr = db(pr);
  if (++holdAge_ > 60) { holdL_ = dl; holdR_ = dr; holdAge_ = 0; }
  if (dl > holdL_) { holdL_ = dl; holdAge_ = 0; }
  if (dr > holdR_) { holdR_ = dr; holdAge_ = 0; }
  pkL_.set(holdL_ > -100.f ? juce::String(holdL_, 1) : "-");
  pkR_.set(holdR_ > -100.f ? juce::String(holdR_, 1) : "-");
  const float r = juce::jmax(rl, rr);
  rms_.set(r > 0.00001f ? juce::String(db(r), 1) : "-");  // below -100 dB counts as silence
  if (pl >= 0.999f || pr >= 0.999f) clipped_ = true;
  clip_.set(juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f")), clipped_ ? juce::Colour(0xffe5484d) : T().c(0xff5b4a2c));
  const int want = slotsToShow(*e, kMasterId);
  if (want != shownSlots_) { shownSlots_ = want; resized(); }
  for (int s = 0; s < shownSlots_; ++s) syncSlot(ctx_, *e, kMasterId, s, slots_[(size_t)s]);
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
  const int rows = (shownSlots_ + 1) / 2;
  auto grid = r.removeFromTop(rows * 24 + (rows - 1) * 4); r.removeFromTop(6);
  for (int s = 0; s < (int)slots_.size(); ++s) {
    slots_[(size_t)s].setVisible(s < shownSlots_);
    const int row = s / 2, col = s % 2, w = (grid.getWidth() - 4) / 2;
    slots_[(size_t)s].setBounds(grid.getX() + col * (w + 4), grid.getY() + row * 28, w, 24);
  }
  const int vuH = juce::jmin(86, (r.getWidth() - 6) / 2 * 70 / 120);
  const bool showVu = r.getHeight() - vuH - 6 - 48 >= 160;  // short window: the VU pair makes way for the fader
  vuL_.setVisible(showVu); vuR_.setVisible(showVu);
  if (showVu) {
    auto vu = r.removeFromTop(vuH); r.removeFromTop(6);
    vuL_.setBounds(vu.removeFromLeft((vu.getWidth() - 6) / 2)); vu.removeFromLeft(6); vuR_.setBounds(vu);
  }
  auto rd = r.removeFromTop(fader_.bigReadout ? 64 : 40); r.removeFromTop(8);
  const int w = (rd.getWidth() - 12) / 4;
  for (auto* m : {&pkL_, &pkR_, &rms_, &clip_}) { m->setBounds(rd.removeFromLeft(w)); rd.removeFromLeft(4); }
  fader_.setBounds(r);
}

}  // namespace ui
