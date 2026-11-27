#include "Overview.h"
#include "Tempo.h"

namespace ui {

static constexpr int kBuckets = 600;
static constexpr float kRulerH = 17.f;

Overview::Overview(AppCtx& ctx) : ctx_(ctx) {
  addAndMakeVisible(elapsed_); addAndMakeVisible(total_);
  elapsed_.setInterceptsMouseClicks(false, false); total_.setInterceptsMouseClicks(false, false);
}

static juce::String mmss(double sec) {
  const int s = (int)sec;
  return juce::String::formatted("%02d:%02d", s / 60, s % 60);
}

// The overview shows every track's active take summed (peak per bucket), over the song length.
void Overview::rebuildWave(airdaw::Engine& e) {
  juce::String key; double end = 0.0;
  for (int t = 0; t < airdaw::kMaxTracks; ++t)
    if (e.track(t).take.load() || e.comp(t)) {
      key << juce::String::toHexString((juce::pointer_sized_int)e.track(t).take.load()) << "/"
          << juce::String::toHexString((juce::pointer_sized_int)e.comp(t)) << ";";
      end = juce::jmax(end, (double)e.trackEnd(t));
    }
  key << e.sampleRate();
  if (key == waveKey_) return;
  waveKey_ = key;
  wave_.assign(kBuckets, 0.f);
  len_ = juce::jmax(end, e.sampleRate() * 30.0);  // at least 30 s so a new song has room
  for (int t = 0; t < airdaw::kMaxTracks; ++t) {
    if (e.comp(t)) {  // a comped track: what it actually plays, section by section
      const auto tEnd = e.trackEnd(t);
      for (juce::int64 p = 0; p < tEnd; p += 16) {
        float l, r;
        if (!e.trackFrameAt(t, p, l, r)) continue;
        const int b = juce::jlimit(0, kBuckets - 1, (int)((double)p / len_ * kBuckets));
        wave_[(size_t)b] = juce::jmax(wave_[(size_t)b], juce::jmax(std::abs(l), std::abs(r)));
      }
      continue;
    }
    auto* tk = e.track(t).take.load(); if (!tk) continue;
    const bool st = !tk->r.empty();
    for (size_t i = 0; i < tk->l.size(); i += 16) {  // every 16th sample is plenty for a ~600-pixel overview
      const double s = (double)tk->start + (double)i;
      const int b = juce::jlimit(0, kBuckets - 1, (int)(s / len_ * kBuckets));
      const float a = juce::jmax(std::abs(tk->l[i]), st ? std::abs(tk->r[i]) : 0.f);
      wave_[(size_t)b] = juce::jmax(wave_[(size_t)b], a);
    }
  }
  float mx = 0.f; for (float w : wave_) mx = juce::jmax(mx, w);
  if (mx > 0.f) for (float& w : wave_) w = std::pow(w / mx, 0.7f);  // normalise, lift quiet parts a little
  repaint();
}

void Overview::sync() {
  auto* e = ctx_.engine(); if (!e) return;
  rebuildWave(*e);
  auto& tr = e->transport();
  sr_ = e->sampleRate();
  const double p = (double)tr.pos.load();
  const auto li = tr.loopIn.load(), lo = tr.loopOut.load(); const bool lon = tr.loopOn.load();
  if (p > len_) { len_ = p * 1.25; waveKey_.clear(); }
  double nk = (double)ctx_.notes.items.size(); for (auto& n : ctx_.notes.items) nk += n.seconds;  // notes added / removed
  if (e->tempoMap() != mapKey_ || e->metronome().bpm.load() != bpmKey_ || e->metronome().beatsPerBar.load() != bpbKey_) {
    mapKey_ = e->tempoMap(); bpmKey_ = e->metronome().bpm.load(); bpbKey_ = e->metronome().beatsPerBar.load(); repaint();
  }
  if (p != pos_ || li != loopIn_ || lo != loopOut_ || lon != loopOn_ || nk != notesKey_) {
    pos_ = p; loopIn_ = li; loopOut_ = lo; loopOn_ = lon; notesKey_ = nk; repaint();
  }
  elapsed_.setText(mmss(pos_ / sr_));
  total_.setText(mmss(len_ / sr_));
}

juce::Rectangle<float> Overview::bar() const {
  return getLocalBounds().reduced(16, 8).withTrimmedLeft(92).withTrimmedRight(92).toFloat().withTrimmedTop(kRulerH + 2.f);
}
juce::Rectangle<float> Overview::ruler() const {
  return getLocalBounds().reduced(16, 8).withTrimmedLeft(92).withTrimmedRight(92).toFloat().withHeight(kRulerH);
}

int Overview::barAtX(float x) const {
  auto* e = ctx_.engine(); if (!e) return 1;
  airdaw::TempoSection sec[airdaw::kMaxTempoChanges + 1];
  const int n = airdaw::tempoSections(e->tempoMap(), e->metronome().bpm.load(), e->metronome().beatsPerBar.load(), sr_, sec);
  return 1 + (int)std::floor(airdaw::barAtSample(sec, n, sampleAt(x)) + 1e-9);
}

int Overview::markerAt(juce::Point<float> p) const {
  for (auto it = markers_.rbegin(); it != markers_.rend(); ++it) if (it->r.expanded(3.f, 2.f).contains(p)) return it->bar;
  return -1;
}

void Overview::editTempo(int bar, juce::Rectangle<int> near) {
  juce::CallOutBox::launchAsynchronously(std::make_unique<TempoEditor>(ctx_, bar), localAreaToGlobal(near), nullptr);
}

juce::String Overview::getTooltip() {
  const auto p = getMouseXYRelative().toFloat();
  if (!ruler().expanded(0.f, 2.f).contains(p)) return {};
  if (markerAt(p) > 1) return "Tempo change: click to edit, drag to move by bars, right-click to delete";
  if (markerAt(p) == 1) return "Bar 1: the metronome's tempo (also set with Tap). Click to edit.";
  return "Bars. Double-click a bar to add a tempo / time-signature change there.";
}

// The bar ruler: bar lines and numbers (as dense as there is room for), a marker for bar 1 and for each change.
void Overview::paintRuler(juce::Graphics& g) {
  auto& t = T();
  auto* e = ctx_.engine(); if (!e) return;
  const auto rr = ruler();
  t.drawWell(g, rr, juce::Colour(0xff060504), 3.f);
  airdaw::TempoSection sec[airdaw::kMaxTempoChanges + 1];
  const int n = airdaw::tempoSections(e->tempoMap(), e->metronome().bpm.load(), e->metronome().beatsPerBar.load(), sr_, sec);
  const double bars = juce::jmax(1.0, airdaw::barAtSample(sec, n, len_));
  const double pxPerBar = rr.getWidth() / bars;
  int step = 1; while (step * pxPerBar < 26.0 && step < 1024) step *= 2;
  g.setFont(Theme::monoFont(9.5f, false));
  juce::Graphics::ScopedSaveState ss(g);
  g.reduceClipRegion(rr.toNearestInt());
  for (int b = 0; b < 4096; ++b) {
    const double s = airdaw::sampleAtBar(sec, n, (double)b);
    if (s > len_) break;
    const float x = xAt(s);
    const bool major = b % step == 0;
    if (!major && pxPerBar < 4.0) continue;
    g.setColour(t.am().withAlpha(major ? 0.45f : 0.16f));
    g.fillRect(x, major ? rr.getY() + 3.f : rr.getBottom() - 5.f, 1.f, major ? rr.getHeight() - 3.f : 5.f);
    if (major) { g.setColour(t.amd()); g.drawText(juce::String(b + 1), juce::Rectangle<float>(x + 3.f, rr.getY(), 40.f, rr.getHeight()), juce::Justification::centredLeft, false); }
  }
  // markers: bar 1 (the metronome) and each change
  markers_.clear();
  float lastRight = -1e9f;
  for (int k = 0; k < n; ++k) {
    const float x = xAt(sec[k].start);
    const float bpm = (float)(sr_ * 60.0 / sec[k].beat);
    const auto txt = TempoEdit::label(bpm, sec[k].bpb);
    const float w = juce::GlyphArrangement::getStringWidth(Theme::monoFont(10.f, false), txt) + 10.f;
    juce::Rectangle<float> m(x, rr.getY() + 1.f, w, rr.getHeight() - 2.f);
    const bool full = x > lastRight + 2.f;  // markers too close together show only their line
    if (!full) m = m.withWidth(5.f);
    g.setColour(juce::Colour(0xff1c1408)); g.fillRoundedRectangle(m, 2.f);
    g.setColour(t.am().withAlpha(0.9f)); g.fillRect(m.getX(), m.getY(), 2.f, m.getHeight());
    if (full) { g.setFont(Theme::monoFont(10.f, false)); t.drawGlowText(g, txt, m.withTrimmedLeft(5.f), juce::Justification::centredLeft, t.am(), 0.4f); lastRight = m.getRight(); }
    markers_.push_back({k == 0 ? 1 : sec[k].bar + 1, m});
  }
}
double Overview::sampleAt(float x) const {
  auto b = bar(); return juce::jlimit(0.0, len_, (double)(x - b.getX()) / b.getWidth() * len_);
}
float Overview::xAt(double s) const { auto b = bar(); return b.getX() + (float)(s / len_) * b.getWidth(); }

void Overview::resized() {
  auto r = getLocalBounds().reduced(16, 8);
  elapsed_.setBounds(r.removeFromLeft(76).withSizeKeepingCentre(76, 40));
  total_.setBounds(r.removeFromRight(76).withSizeKeepingCentre(76, 40));
}

void Overview::paint(juce::Graphics& g) {
  auto& t = T();
  g.fillAll(T().pb());
  g.setColour(juce::Colours::black); g.fillRect(0, getHeight() - 2, getWidth(), 2);
  auto b = bar();
  t.drawGlow(g, b, t.am(), 18.f, 0.06f);
  t.drawWell(g, b, juce::Colour(0xff030303), 4.f);
  const float px = xAt(pos_);
  // waveform: dim before the playhead is reached, bright where already played
  const float cy = b.getCentreY(), half = (b.getHeight() - 16.f) / 2.f, bw = b.getWidth() / kBuckets;
  bool any = false;
  for (int i = 0; i < kBuckets; ++i) {
    const float a = wave_.empty() ? 0.f : wave_[(size_t)i]; if (a <= 0.002f) continue;
    any = true;
    const float x = b.getX() + i * bw, hh = juce::jmax(0.5f, a * half);
    g.setColour(x < px ? t.c(0xffffc46b) : t.am().withAlpha(0.45f));
    g.fillRect(x, cy - hh, juce::jmax(1.f, bw - 0.6f), hh * 2.f);
  }
  if (!any) {
    g.setColour(Theme::dim().withAlpha(0.7f)); g.setFont(Theme::spaced(Theme::uiFont(12.f), 0.12f));
    g.drawText("NO TAKES YET  -  ARM A TRACK (R) AND PRESS REC", b, juce::Justification::centred);
  }
  // played region wash + playhead
  g.setGradientFill(juce::ColourGradient(t.am().withAlpha(0.f), b.getX(), 0, t.am().withAlpha(0.08f), px, 0, false));
  g.fillRect(b.withRight(px));
  // Playhead glow: a bell-shaped band centred on the line (a drop shadow around a 2 px rectangle sat off to one side).
  if (t.glow > 0.f) {
    const float gw = 15.f * (0.6f + 0.4f * t.glow);
    juce::ColourGradient lg(t.am().withAlpha(0.f), px - gw, 0.f, t.am().withAlpha(0.f), px + gw, 0.f, false);
    for (auto [p, a] : {std::pair{0.22, 0.05f}, {0.36, 0.14f}, {0.45, 0.32f}, {0.5, 0.5f}, {0.55, 0.32f}, {0.64, 0.14f}, {0.78, 0.05f}})
      lg.addColour(p, t.am().withAlpha(t.glowAlpha(a)));
    g.setGradientFill(lg);
    g.fillRect(juce::Rectangle<float>(px - gw, b.getY(), 2.f * gw, b.getHeight()).getIntersection(b));
  }
  g.setColour(t.am()); g.fillRect(px - 1.f, b.getY(), 2.f, b.getHeight());
  g.setColour(juce::Colour(0xfffff0d0).withAlpha(0.85f)); g.fillRect(px - 0.5f, b.getY(), 1.f, b.getHeight());  // hot core
  // listening-note markers (mockup .mk): a hairline with an accent triangle on top
  for (auto& n : ctx_.notes.items) {
    const float x = xAt(n.seconds * sr_);
    if (x < b.getX() || x > b.getRight()) continue;
    g.setColour(juce::Colour(0x66ffffff)); g.fillRect(x - 1.f, b.getY(), 2.f, b.getHeight());
    juce::Path tri; tri.addTriangle(x - 5.f, b.getY(), x + 5.f, b.getY(), x, b.getY() + 6.f);
    t.drawGlow(g, {x - 3.f, b.getY(), 6.f, 4.f}, t.am(), 6.f, 0.8f);
    g.setColour(t.am()); g.fillPath(tri);
  }
  // loop region
  if (loopOut_ > loopIn_) {
    const float x0 = xAt((double)loopIn_), x1 = xAt((double)loopOut_);
    juce::Rectangle<float> lr(x0, b.getY(), x1 - x0, b.getHeight());
    auto* e = ctx_.engine();
    const bool rec = e && loopOn_ && e->transport().recording.load();
    const juce::Colour lc = rec ? juce::Colour(0xffe5484d) : t.am();
    if (loopOn_) t.drawGlow(g, lr, lc, 16.f, 0.33f);
    g.setColour(lc.withAlpha(loopOn_ ? 0.16f : 0.05f)); g.fillRect(lr);
    g.setColour(lc.withAlpha(loopOn_ ? 1.f : 0.5f));
    g.fillRect(x0 - 1.f, b.getY(), 2.f, b.getHeight()); g.fillRect(x1 - 1.f, b.getY(), 2.f, b.getHeight());
  }
  // tempo changes: a faint line down through the waveform
  if (auto* e = ctx_.engine(); e && e->tempoMap()) {
    airdaw::TempoSection sec[airdaw::kMaxTempoChanges + 1];
    const int n = airdaw::tempoSections(e->tempoMap(), e->metronome().bpm.load(), e->metronome().beatsPerBar.load(), sr_, sec);
    g.setColour(t.am().withAlpha(0.22f));
    for (int k = 1; k < n; ++k) { const float x = xAt(sec[k].start); if (x > b.getX() && x < b.getRight()) g.fillRect(x, b.getY(), 1.f, b.getHeight()); }
  }
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(b, T().rad(4.f), 1.f);
  paintRuler(g);
}

void Overview::mouseDoubleClick(const juce::MouseEvent& e) {
  auto* en = ctx_.engine(); if (!en || !ruler().expanded(0.f, 2.f).contains(e.position) || markerAt(e.position) > 0) return;
  const int bar = barAtX(e.position.x);
  if (bar >= 2 && !TempoEdit::has(*en, bar)) { const auto t = TempoEdit::at(*en, bar); TempoEdit::set(*en, bar, t.bpm, t.beatsPerBar); }
  editTempo(juce::jmax(1, bar), juce::Rectangle<int>((int)e.position.x - 2, (int)ruler().getY(), 4, (int)kRulerH));
}

void Overview::mouseMove(const juce::MouseEvent& e) {
  if (ruler().expanded(0.f, 2.f).contains(e.position)) {
    setMouseCursor(markerAt(e.position) > 1 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
    return;
  }
  const bool nearEdge = loopOut_ > loopIn_ && (std::abs(e.position.x - xAt((double)loopIn_)) < 8.f || std::abs(e.position.x - xAt((double)loopOut_)) < 8.f);
  setMouseCursor(nearEdge ? juce::MouseCursor::LeftRightResizeCursor : bar().contains(e.position) ? juce::MouseCursor::PointingHandCursor
                                                                                                   : juce::MouseCursor::NormalCursor);
}

void Overview::mouseDown(const juce::MouseEvent& e) {
  tempoDrag_ = -1; tempoMoved_ = false;
  if (auto* en = ctx_.engine(); en && ruler().expanded(0.f, 2.f).contains(e.position)) {  // the bar ruler
    drag_ = Drag::None;
    const int m = markerAt(e.position);
    if (e.mods.isPopupMenu()) {
      juce::PopupMenu menu;
      const int bar = m > 0 ? m : barAtX(e.position.x);
      if (m > 0) { menu.addItem(1, juce::String(juce::CharPointer_UTF8("Editâ¦"))); if (m > 1) menu.addItem(2, "Delete change"); }
      else if (bar >= 2) menu.addItem(3, "Add tempo change at bar " + juce::String(bar));
      const auto near = juce::Rectangle<int>((int)e.position.x - 2, (int)ruler().getY(), 4, (int)kRulerH);
      menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                         [this, bar, near](int r) {
                           auto* en2 = ctx_.engine(); if (!en2) return;
                           if (r == 1) editTempo(bar, near);
                           if (r == 2) TempoEdit::remove(*en2, bar);
                           if (r == 3) { const auto t = TempoEdit::at(*en2, bar); TempoEdit::set(*en2, bar, t.bpm, t.beatsPerBar); editTempo(bar, near); }
                         });
      return;
    }
    if (m > 0) tempoDrag_ = m;  // click: edit (on release); drag: move (not bar 1)
    return;
  }
  auto* en = ctx_.engine(); if (!en || !bar().expanded(0, 4).contains(e.position)) { drag_ = Drag::None; return; }
  if (loopOut_ > loopIn_ && std::abs(e.position.x - xAt((double)loopIn_)) < 8.f) drag_ = Drag::LoopIn;
  else if (loopOut_ > loopIn_ && std::abs(e.position.x - xAt((double)loopOut_)) < 8.f) drag_ = Drag::LoopOut;
  else if (e.mods.isShiftDown()) { drag_ = Drag::NewLoop; dragAnchor_ = sampleAt(e.position.x); }
  else { drag_ = Drag::Seek; en->transport().seek((juce::int64)sampleAt(e.position.x)); }
}

void Overview::mouseDrag(const juce::MouseEvent& e) {
  auto* en = ctx_.engine(); if (!en) return;
  if (tempoDrag_ > 1 && e.getDistanceFromDragStart() > 3) {  // move the change by whole bars
    const int to = juce::jmax(2, (int)std::lround(barAtX(e.position.x + 0.f)));
    if (to != tempoDrag_ && TempoEdit::move(*en, tempoDrag_, to)) { tempoDrag_ = to; repaint(); }
    tempoMoved_ = true;
    return;
  }
  if (tempoDrag_ > 0) return;
  auto& tr = en->transport();
  const double s = sampleAt(e.position.x), minLen = sr_ * 0.25;
  switch (drag_) {
    case Drag::Seek: tr.seek((juce::int64)s); break;
    case Drag::NewLoop: tr.loopIn = (juce::int64)juce::jmin(s, dragAnchor_); tr.loopOut = (juce::int64)juce::jmax(s, dragAnchor_); break;
    case Drag::LoopIn: tr.loopIn = (juce::int64)juce::jmin(s, (double)tr.loopOut.load() - minLen); break;
    case Drag::LoopOut: tr.loopOut = (juce::int64)juce::jmax(s, (double)tr.loopIn.load() + minLen); break;
    default: break;
  }
  sync();
}

void Overview::mouseUp(const juce::MouseEvent& e) {
  auto* en = ctx_.engine();
  if (tempoDrag_ > 0) {
    if (!tempoMoved_) editTempo(tempoDrag_, juce::Rectangle<int>((int)e.position.x - 2, (int)ruler().getY(), 4, (int)kRulerH));
    tempoDrag_ = -1; tempoMoved_ = false; return;
  }
  if (en && drag_ == Drag::NewLoop) {
    auto& tr = en->transport();
    if ((double)(tr.loopOut.load() - tr.loopIn.load()) < sr_ * 0.25) { tr.loopIn = 0; tr.loopOut = 0; tr.loopOn = false; }
    else { tr.loopOn = true; ctx_.note("Loop on: every recording pass inside the loop becomes its own take"); }
  }
  drag_ = Drag::None;
}

}  // namespace ui
