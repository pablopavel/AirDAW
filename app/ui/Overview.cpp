#include "Overview.h"

namespace ui {

static constexpr int kBuckets = 600;

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
  if (p != pos_ || li != loopIn_ || lo != loopOut_ || lon != loopOn_ || nk != notesKey_) {
    pos_ = p; loopIn_ = li; loopOut_ = lo; loopOn_ = lon; notesKey_ = nk; repaint();
  }
  elapsed_.setText(mmss(pos_ / sr_));
  total_.setText(mmss(len_ / sr_));
}

juce::Rectangle<float> Overview::bar() const {
  return getLocalBounds().reduced(16, 8).withTrimmedLeft(92).withTrimmedRight(92).toFloat();
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
  t.drawGlow(g, {px - 1.f, b.getY(), 2.f, b.getHeight()}, t.am(), 16.f, 0.6f);
  g.setColour(t.am()); g.fillRect(px - 1.f, b.getY(), 2.f, b.getHeight());
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
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(b, T().rad(4.f), 1.f);
}

void Overview::mouseMove(const juce::MouseEvent& e) {
  const bool nearEdge = loopOut_ > loopIn_ && (std::abs(e.position.x - xAt((double)loopIn_)) < 8.f || std::abs(e.position.x - xAt((double)loopOut_)) < 8.f);
  setMouseCursor(nearEdge ? juce::MouseCursor::LeftRightResizeCursor : bar().contains(e.position) ? juce::MouseCursor::PointingHandCursor
                                                                                                   : juce::MouseCursor::NormalCursor);
}

void Overview::mouseDown(const juce::MouseEvent& e) {
  auto* en = ctx_.engine(); if (!en || !bar().expanded(0, 4).contains(e.position)) { drag_ = Drag::None; return; }
  if (loopOut_ > loopIn_ && std::abs(e.position.x - xAt((double)loopIn_)) < 8.f) drag_ = Drag::LoopIn;
  else if (loopOut_ > loopIn_ && std::abs(e.position.x - xAt((double)loopOut_)) < 8.f) drag_ = Drag::LoopOut;
  else if (e.mods.isShiftDown()) { drag_ = Drag::NewLoop; dragAnchor_ = sampleAt(e.position.x); }
  else { drag_ = Drag::Seek; en->transport().seek((juce::int64)sampleAt(e.position.x)); }
}

void Overview::mouseDrag(const juce::MouseEvent& e) {
  auto* en = ctx_.engine(); if (!en) return;
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

void Overview::mouseUp(const juce::MouseEvent&) {
  auto* en = ctx_.engine();
  if (en && drag_ == Drag::NewLoop) {
    auto& tr = en->transport();
    if ((double)(tr.loopOut.load() - tr.loopIn.load()) < sr_ * 0.25) { tr.loopIn = 0; tr.loopOut = 0; tr.loopOn = false; }
    else { tr.loopOn = true; ctx_.note("Loop on: every recording pass inside the loop becomes its own take"); }
  }
  drag_ = Drag::None;
}

}  // namespace ui
