#include "Widgets.h"

namespace ui {

// ---------------- Lcd ----------------
void Lcd::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  auto& t = T();
  t.drawGlow(g, r, t.am(), 12.f, 0.08f);
  t.drawWell(g, r, t.lcdBg(), 4.f);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), t.rad(4.f), 1.f);
  auto in = r.reduced(h_ > 30.f ? 22.f : 10.f, 4.f);
  auto cap = in.removeFromBottom(juce::jmax(10.f, h_ * 0.28f));
  auto font = Theme::spaced(Theme::monoFont(h_), 0.1f);
  const float tw = juce::GlyphArrangement::getStringWidth(font, text_);
  if (tw > in.getWidth() && tw > 0.f) font = font.withHeight(juce::jmax(9.f, h_ * in.getWidth() / tw));  // shrink to fit narrow windows
  g.setFont(font);
  t.drawGlowText(g, text_, in, juce::Justification::centredLeft, col_.isTransparent() ? t.readout() : col_, 1.f);
  g.setFont(Theme::spaced(Theme::uiFont(juce::jmax(8.5f, h_ * 0.25f)), 0.2f));
  g.setColour(t.readoutCaption());
  g.drawText(caption_, cap, juce::Justification::topLeft, false);
}

// ---------------- Reels ----------------
// Darkroom palette: everything is lit by one red safelight.
static const juce::Colour kSafe(0xffff3b2a), kSafeEdge(0xffff4a35);
static constexpr float kTilt = 0.78f;  // the plate leans back: circles are drawn squashed vertically

// One reel: tape pack, six-window anodized flange and hub turn with `angle`; the red rim light stays put.
static void drawReel(juce::Graphics& g, juce::Point<float> c, float R, float angle, float pack, float hum) {
  const auto tilt = juce::AffineTransform::scale(1.f, kTilt).translated(c.x, c.y);
  const auto spin = juce::AffineTransform::rotation(angle).followedBy(tilt);
  // soft shadow on the plate, offset away from the light (top right)
  {
    juce::ColourGradient sg(juce::Colours::black.withAlpha(0.75f), c.x - 2.f, c.y + 4.f, juce::Colours::transparentBlack,
                            c.x - 2.f + R * 1.15f, c.y + 4.f, true);
    g.setGradientFill(sg);
    g.fillEllipse(c.x - 2.f - R * 1.15f, c.y + 4.f - R * 1.15f * kTilt, R * 2.3f, R * 2.3f * kTilt);
  }
  juce::Path packP; packP.addEllipse(-pack, -pack, 2 * pack, 2 * pack); packP.applyTransform(tilt);
  g.setColour(juce::Colour(0xff0a0505)); g.fillPath(packP);
  g.setColour(kSafe.withAlpha(0.25f)); g.strokePath(packP, juce::PathStrokeType(0.8f));

  juce::Path fl; fl.setUsingNonZeroWinding(false);  // even-odd: the windows and hub hole are holes
  fl.addEllipse(-R, -R, 2 * R, 2 * R);
  const float r1 = R * 0.88f, r0 = R * 0.31f, half = juce::degreesToRadians(20.f);
  for (int i = 0; i < 6; ++i) {
    const float a = juce::MathConstants<float>::twoPi * (float)i / 6.f;
    fl.addPieSegment(-r1, -r1, 2 * r1, 2 * r1, a - half, a + half, r0 / r1);
  }
  fl.addEllipse(-R * 0.2f, -R * 0.2f, R * 0.4f, R * 0.4f);
  fl.applyTransform(spin);
  g.setColour(juce::Colour(0xff1c1414)); g.fillPath(fl);
  g.setColour(kSafeEdge.withAlpha(0.55f)); g.strokePath(fl, juce::PathStrokeType(0.9f));

  juce::Path hub; hub.addEllipse(-R * 0.18f, -R * 0.18f, R * 0.36f, R * 0.36f); hub.applyTransform(tilt);
  g.setColour(juce::Colour(0xff2a1a1a)); g.fillPath(hub);
  g.setColour(juce::Colour(0xffff6a55).withAlpha(0.8f)); g.strokePath(hub, juce::PathStrokeType(1.f));
  juce::Path key; key.addRectangle(-R * 0.035f, -R * 0.16f, R * 0.07f, R * 0.1f); key.applyTransform(spin);
  g.setColour(juce::Colour(0xffff6a55).withAlpha(0.6f)); g.fillPath(key);

  // fixed red rim light from the safelight (upper right)
  juce::Path outer; outer.addEllipse(-R, -R, 2 * R, 2 * R); outer.applyTransform(tilt);
  {
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(outer);
    const juce::Point<float> hot(c.x + R * 0.62f, c.y - R * 0.78f * kTilt);
    g.setGradientFill(juce::ColourGradient(kSafe.withAlpha(0.55f * hum), hot.x, hot.y, kSafe.withAlpha(0.f), hot.x + R * 0.8f, hot.y, true));
    g.fillPath(outer);
  }
  juce::Path rim; rim.addCentredArc(0.f, 0.f, R - 0.5f, R - 0.5f, 0.f, juce::MathConstants<float>::pi * 0.04f, juce::MathConstants<float>::pi * 0.48f, true);
  rim.applyTransform(tilt);
  g.setColour(kSafe.withAlpha(0.75f * hum)); g.strokePath(rim, juce::PathStrokeType(1.3f));
}

void Reels::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  auto& t = T();
  const float corner = r.getHeight() / 2.f;
  t.drawGlow(g, r, kSafe, 16.f, 0.09f * hum_);
  juce::Path pill; pill.addRoundedRectangle(r, corner);
  {
  juce::Graphics::ScopedSaveState ss(g);
  g.reduceClipRegion(pill);
  g.setColour(juce::Colour(0xff040101)); g.fillRect(r);
  // the safelight, out of frame at the top right
  g.setGradientFill(juce::ColourGradient(kSafe.withAlpha(0.42f * hum_), r.getRight() - r.getWidth() * 0.12f, r.getY() - 6.f,
                                         kSafe.withAlpha(0.f), r.getRight() - r.getWidth() * 0.12f - r.getWidth() * 0.62f, r.getY() - 6.f, true));
  g.fillRect(r);
  {  // a glossy streak across the plate
    juce::ColourGradient st(kSafe.withAlpha(0.f), r.getX() + r.getWidth() * 0.32f, r.getY(), kSafe.withAlpha(0.f), r.getX() + r.getWidth() * 0.62f, r.getBottom(), false);
    st.addColour(0.5, kSafe.withAlpha(0.1f * hum_));
    g.setGradientFill(st); g.fillRect(r);
  }
  const float cy = r.getCentreY() - 1.f;
  const float R = juce::jmin((r.getHeight() - 12.f) / 2.f / kTilt, r.getWidth() * 0.215f);
  const float cx1 = r.getX() + corner, cx2 = r.getRight() - corner;
  const float p1 = R * 0.77f, p2 = R * 0.52f;
  // tape from the bottom of each pack past the head
  const float base = cy + R * kTilt * 0.96f;
  juce::Path tape;
  tape.startNewSubPath(cx1, cy + p1 * kTilt); tape.lineTo(r.getCentreX() - 9.f, base); tape.lineTo(r.getCentreX() + 9.f, base); tape.lineTo(cx2, cy + p2 * kTilt);
  g.setColour(juce::Colour(0xff100505)); g.strokePath(tape, juce::PathStrokeType(2.f));
  g.setColour(kSafe.withAlpha(0.35f)); g.strokePath(tape, juce::PathStrokeType(0.7f));
  drawReel(g, {cx1, cy}, R, a1_, p1, hum_);
  drawReel(g, {cx2, cy}, R, a2_, p2, hum_);
  juce::Rectangle<float> head(r.getCentreX() - 6.f, base - 4.f, 12.f, 7.f);
  g.setColour(juce::Colour(0xff1a1010)); g.fillRoundedRectangle(head, 1.5f);
  g.setColour(kSafe.withAlpha(0.45f)); g.drawRoundedRectangle(head, 1.5f, 0.8f);
  // low red haze, then the vignette
  g.setGradientFill(juce::ColourGradient(kSafe.withAlpha(0.f), 0.f, r.getY() + r.getHeight() * 0.55f, kSafe.withAlpha(0.12f * hum_), 0.f, r.getBottom(), false));
  g.fillRect(r);
  juce::ColourGradient vg(juce::Colours::transparentBlack, r.getCentreX() + r.getWidth() * 0.08f, r.getCentreY() - 4.f,
                          juce::Colours::black.withAlpha(0.8f), r.getRight() + 6.f, r.getCentreY(), true);
  vg.addColour(0.55, juce::Colours::transparentBlack);
  g.setGradientFill(vg); g.fillRect(r);
  }
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.f);
}
void Reels::timerCallback() {
  ++tick_;
  // The safelight hums: a slow breath plus a brief dip now and then.
  humPhase_ = std::fmod(humPhase_ + 0.105f, juce::MathConstants<float>::twoPi);
  hum_ = 0.93f + 0.05f * std::sin(humPhase_) - ((tick_ % 97) < 2 ? 0.12f : 0.f);
  if (isPlaying && isPlaying()) {
    const float tau = juce::MathConstants<float>::twoPi;
    a1_ = std::fmod(a1_ + tau / (3.1f * 30.f), tau);  // the full supply reel turns slower than the take-up reel
    a2_ = std::fmod(a2_ + tau / (2.2f * 30.f), tau);
    repaint();
  } else if (tick_ % 3 == 0) repaint();  // stopped: only the light moves, 10 frames a second
}

// ---------------- NamePlate ----------------
void NamePlate::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  T().drawGlow(g, r, Theme::cream(), 12.f, 0.09f);
  g.setColour(Theme::cream()); g.fillRoundedRectangle(r, T().rad(3.f));
  g.setColour(T().c(bar_)); g.fillRoundedRectangle(r.getX() + 14.f, r.getBottom() - 3.f, r.getWidth() - 28.f, 3.f, 1.5f);
  g.setColour(juce::Colour(0xff1b1b1b));
  g.setFont(Theme::spaced(Theme::uiFont(juce::jmin(15.f, r.getHeight() * 0.5f), true), 0.1f));
  g.drawFittedText(name_.toUpperCase(), getLocalBounds().reduced(4, 0).withTrimmedBottom(2), juce::Justification::centred, 1, 0.8f);
}
void NamePlate::mouseUp(const juce::MouseEvent& e) {
  if (e.mouseWasClicked() && e.getNumberOfClicks() == 1 && onClick) onClick();
}
void NamePlate::mouseDoubleClick(const juce::MouseEvent&) {
  if (!renamable_ || ed_) return;
  ed_ = std::make_unique<juce::TextEditor>();
  ed_->setText(name_, false); ed_->setInputRestrictions(16);
  ed_->setJustification(juce::Justification::centred);
  ed_->setFont(Theme::uiFont(13.f, true));
  ed_->onReturnKey = [this] { finishEdit(true); };
  ed_->onEscapeKey = [this] { finishEdit(false); };
  ed_->onFocusLost = [this] { finishEdit(true); };
  addAndMakeVisible(*ed_); ed_->setBounds(getLocalBounds());
  ed_->selectAll(); ed_->grabKeyboardFocus();
}
void NamePlate::finishEdit(bool ok) {
  if (!ed_) return;
  auto text = ed_->getText().trim().substring(0, 16);
  // Delete the editor after its callback has returned.
  juce::MessageManager::callAsync([sp = juce::Component::SafePointer<NamePlate>(this)] { if (sp) sp->ed_.reset(); });
  ed_->onFocusLost = nullptr;
  if (ok && text.isNotEmpty() && text != name_ && onRename) onRename(text);
}

// ---------------- DragControl ----------------
void DragControl::setValue(float v, bool notify) {
  v = juce::jlimit(min_, max_, v);
  if (v == v_) return;
  v_ = v; repaint();
  if (notify && onChange) onChange(v_);
}
void DragControl::mouseDown(const juce::MouseEvent&) { dragStart_ = v_; }
void DragControl::mouseDrag(const juce::MouseEvent& e) {
  const float d = horizontal ? (float)e.getDistanceFromDragStartX() : (float)-e.getDistanceFromDragStartY();
  const float fine = e.mods.isShiftDown() ? 0.25f : 1.f;
  setValue(dragStart_ + d / pixelsForRange * (max_ - min_) * fine, true);
}
void DragControl::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
  const float dy = std::abs(w.deltaY) > std::abs(w.deltaX) ? w.deltaY : -w.deltaX;
  if (dy == 0.f) return;
  const float step = (wheelStep > 0.f ? wheelStep : (max_ - min_) / 40.f) * (e.mods.isShiftDown() ? 0.2f : 1.f);
  setValue(v_ + (dy > 0 ? step : -step), true);
}

// ---------------- GainKnob ----------------
void GainKnob::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  g.setColour(juce::Colour(0x4d000000)); g.fillRoundedRectangle(r, 4.f);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0x99000000), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 4, false));
  g.fillRoundedRectangle(r, 4.f);
  const float ks = juce::jmin(32.f, r.getHeight() - 6.f);
  juce::Rectangle<float> k(r.getX() + 8.f, r.getCentreY() - ks / 2.f, ks, ks);
  g.setColour(juce::Colours::black.withAlpha(0.8f)); g.fillEllipse(k.translated(2.f, 4.f));
  if (!t.drawKnobCap(g, k)) {  // chrome
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4c4c50), k.getX() + ks * 0.35f, k.getY() + ks * 0.3f,
                                           juce::Colour(0xff19191b), k.getX() + ks * 0.9f, k.getBottom(), true));
    g.fillEllipse(k);
    g.setColour(juce::Colour(0x33ffffff)); g.drawEllipse(k.reduced(0.5f), 0.7f);
  }
  const float ang = v_ / 24.f * 135.f * juce::MathConstants<float>::pi / 180.f;
  juce::Line<float> ptr(0, -ks / 2.f + 3.f, 0, -ks / 2.f + 12.f);
  ptr.applyTransform(juce::AffineTransform::rotation(ang).translated(k.getCentreX(), k.getCentreY()));
  g.setColour(t.knobPointer()); g.drawLine(ptr, 2.f);
  auto txt = r.withTrimmedLeft(ks + 14.f);
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(9.5f), 0.14f));
  g.drawText("GAIN", txt.removeFromTop(txt.getHeight() * 0.48f), juce::Justification::centredBottom);
  g.setFont(Theme::monoFont(12.5f));
  t.drawGlowText(g, (v_ > 0 ? "+" : "") + juce::String(v_, 1), txt, juce::Justification::centredTop, t.readout(), 0.6f);
}

// ---------------- PanSlider ----------------
void PanSlider::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  const float cy = r.getCentreY();
  g.setColour(juce::Colour(0x55ffffff)); g.fillRect(r.getCentreX() - 1.f, cy - 6.f, 2.f, 12.f);
  juce::Rectangle<float> track(r.getX() + 2.f, cy - 2.f, r.getWidth() - 4.f, 4.f);
  g.setColour(juce::Colour(0xff050505)); g.fillRoundedRectangle(track, 2.f);
  g.setColour(juce::Colour(0x12ffffff)); g.drawRoundedRectangle(track, 2.f, 1.f);
  const float x = juce::jmap(v_, -1.f, 1.f, r.getX() + 7.5f, r.getRight() - 7.5f);
  juce::Rectangle<float> th(x - 7.5f, cy - 7.5f, 15.f, 15.f);
  t.drawGlow(g, th.reduced(2.5f), t.am(), 9.f, 0.7f);
  g.setColour(juce::Colours::black.withAlpha(0.7f)); g.fillEllipse(th.translated(0.f, 2.f));
  g.setGradientFill(juce::ColourGradient(t.c(0xffffe9bd), th.getX() + 5.f, th.getY() + 4.f, t.am(), th.getCentreX(), th.getBottom(), true));
  g.fillEllipse(th);
}

// ---------------- Divider ----------------
void Divider::paint(juce::Graphics& g) {
  auto& t = T();
  const bool hot = isMouseOverOrDragging() || dragging_;
  auto b = getLocalBounds().toFloat();
  // groove along the whole length
  auto groove = vertical_ ? b.withSizeKeepingCentre(2.f, b.getHeight() - 8.f) : b.withSizeKeepingCentre(b.getWidth() - 8.f, 2.f);
  if (hot) t.drawGlow(g, groove, t.am(), 8.f, 0.55f);
  g.setColour(hot ? t.am().withAlpha(0.85f) : juce::Colour(0xff3c3c41)); g.fillRect(groove);  // visible at rest: grab anywhere
  // grip in the middle
  auto grip = vertical_ ? b.withSizeKeepingCentre(4.f, 46.f) : b.withSizeKeepingCentre(90.f, 5.f);
  g.setColour(hot ? t.am() : juce::Colour(0xff5a5a60)); g.fillRoundedRectangle(grip, 2.f);
}
void Divider::mouseDown(const juce::MouseEvent&) {
  start_ = current ? current() : 0; dragging_ = true; dirty_ = false;
  if (onDragState) onDragState(true);
  repaint();
}
void Divider::mouseDrag(const juce::MouseEvent& e) {
  pending_ = start_ + (vertical_ ? e.getDistanceFromDragStartX() : -e.getDistanceFromDragStartY());
  dirty_ = true;
  if (!isTimerRunning()) { flush(); startTimerHz(60); }  // first move at once, then the screen rate
}
void Divider::mouseUp(const juce::MouseEvent&) {
  stopTimer(); flush();
  dragging_ = false;
  if (onDragState) onDragState(false);
  repaint();
}
void Divider::timerCallback() { if (dirty_) flush(); else stopTimer(); }

// ---------------- FaderBlock ----------------
static constexpr float kScaleW = 24.f, kFaderW = 34.f, kLadW = 22.f, kGap = 4.f, kReadH = 24.f;

juce::Rectangle<float> FaderBlock::trackArea() const {
  auto r = getLocalBounds().toFloat();
  r.removeFromBottom((bigReadout ? 52.f : kReadH) + 6.f);
  const float w = kScaleW + kFaderW + kLadW + 2 * kGap;
  return r.withSizeKeepingCentre(w, r.getHeight());
}

juce::Rectangle<int> FaderBlock::cornerArea() const {
  const auto a = trackArea();
  return juce::Rectangle<float>(0.f, a.getBottom() - 19.f, a.getX() + kScaleW, 17.f).toNearestInt();
}

void FaderBlock::mouseDown(const juce::MouseEvent& e) {
  pixelsForRange = juce::jmax(60.f, trackArea().getHeight() - 44.f);
  DragControl::mouseDown(e);
}

void FaderBlock::setMeter(float pl, float pr) {
  // Fast attack, ~300 ms release at 30 updates/s.
  mL_ = juce::jmax(pl, mL_ * 0.82f); mR_ = juce::jmax(pr, mR_ * 0.82f);
  auto a = trackArea();
  repaint(a.withLeft(a.getRight() - kLadW - 1.f).toNearestInt());
}

void FaderBlock::paint(juce::Graphics& g) {
  auto& t = T();
  const auto a = trackArea();
  const float H = a.getHeight();
  auto yOf = [&](float v) { return a.getBottom() - 22.f - (H - 44.f) * v / 100.f; };

  // dB scale
  g.setFont(Theme::monoFont(10.f, false)); g.setColour(Theme::dim());
  float lastY = -100.f;
  for (int d : {6, 0, -6, -12, -24, -48}) {
    const float y = yOf(vOf((float)d));
    if (y - lastY < 11.f) continue;  // short faders: skip labels that would overlap
    lastY = y;
    g.drawText(d > 0 ? "+" + juce::String(d) : juce::String(d), juce::Rectangle<float>(a.getX(), y - 6.f, kScaleW, 12.f),
               juce::Justification::centredRight, false);
  }
  // fader track and fill
  const float fx = a.getX() + kScaleW + kGap, cx = fx + kFaderW / 2.f, y = yOf(v_);
  g.setColour(juce::Colour(0xff050505)); g.fillRect(cx - 2.5f, a.getY(), 5.f, H);
  juce::Rectangle<float> fill(cx - 2.5f, y, 5.f, a.getBottom() - y);
  t.drawGlow(g, fill, t.am(), 5.f, 0.5f);
  g.setColour(t.am()); g.fillRect(fill);
  // fader cap
  t.drawFaderCap(g, juce::Rectangle<float>(fx, y - 22.f, kFaderW, 44.f));
  // ladder meters (aligned to the dB scale)
  const float lx = fx + kFaderW + kGap;
  for (int ch = 0; ch < 2; ++ch) {
    juce::Rectangle<float> col(lx + ch * 12.f, a.getY(), 10.f, H);
    g.setColour(juce::Colour(0xff030303)); g.fillRoundedRectangle(col, 1.f);
    const float lvl = ch ? mR_ : mL_;
    const bool audible = lvl > 0.001f;  // below -60 dB the ladder stays dark
    const float top = audible ? yOf(vOf(20.f * std::log10(lvl))) : a.getBottom();
    if (top < a.getBottom() - 1.f) {
      t.drawGlow(g, col.withTop(top), t.am(), 6.f, 0.35f);
      for (float sy = a.getBottom() - 4.f; sy >= top - 3.f; sy -= 5.f) {
        const float f = (a.getBottom() - sy) / H;
        g.setColour(f > 0.88f ? juce::Colour(0xffe5484d) : f > 0.65f ? t.amLight() : t.am());
        g.fillRect(col.getX(), juce::jmax(top, sy), 10.f, 4.f - juce::jmax(0.f, top - sy));
      }
    }
  }
  // dB readout
  auto rd = getLocalBounds().toFloat().removeFromBottom(bigReadout ? 52.f : kReadH).reduced(2.f, 0.f);
  t.drawWell(g, rd, t.lcdBg(), 3.f);
  g.setFont(Theme::monoFont(bigReadout ? 34.f : 14.f));
  t.drawGlowText(g, dbText(dbOf(v_)), rd, juce::Justification::centred, t.readout(), 0.8f);
}

// ---------------- PluginKnob ----------------
void PluginKnob::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  const float D = 58.f;
  juce::Rectangle<float> k(r.getCentreX() - D / 2.f, r.getY() + 2.f, D, D);
  const auto c = k.getCentre();
  const float v = (v_ - min_) / (max_ - min_);
  if (rackCap >= 0) {  // printed scale + hardware cap
    const float a0 = -0.75f * juce::MathConstants<float>::pi, span = 1.5f * juce::MathConstants<float>::pi;
    g.setColour(inkDim);
    for (int i = 0; i <= 10; ++i) {
      const float a = a0 + span * (float)i / 10.f;
      const bool major = i % 5 == 0;
      g.drawLine(juce::Line<float>(c.getPointOnCircumference(24.5f, a), c.getPointOnCircumference(major ? 29.5f : 28.f, a)), major ? 1.6f : 1.1f);
    }
    auto cap = juce::Rectangle<float>(44.f, 44.f).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.7f)); g.fillEllipse(cap.translated(2.5f, 4.5f));
    static const juce::uint32 kCaps[3][3] = {{0xff6a6a6e, 0xff26262a, 0xff0d0d0e}, {0xfffffaf0, 0xffe3d9bf, 0xffa99d82}, {0xfffff1c4, 0xffd9b46a, 0xff7a5a22}};
    const auto& cc = kCaps[juce::jlimit(0, 2, rackCap)];
    juce::ColourGradient cg(juce::Colour(cc[0]), cap.getX() + cap.getWidth() * 0.38f, cap.getY() + cap.getHeight() * 0.3f,
                            juce::Colour(cc[2]), cap.getRight(), cap.getBottom(), true);
    cg.addColour(0.5, juce::Colour(cc[1]));
    g.setGradientFill(cg); g.fillEllipse(cap);
    g.setColour(juce::Colours::white.withAlpha(rackCap == 0 ? 0.22f : 0.45f)); g.drawEllipse(cap.reduced(1.f), 0.8f);
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.drawEllipse(cap, 0.8f);
    juce::Path p; p.addRoundedRectangle(-1.5f, -19.f, 3.f, 11.f, 1.5f);
    p.applyTransform(juce::AffineTransform::rotation(a0 + span * v).translated(c.x, c.y));
    if (rackCap == 0) { g.setColour(t.am().withAlpha(t.glowAlpha(0.35f))); g.strokePath(p, juce::PathStrokeType(2.5f)); g.setColour(t.am()); }
    else g.setColour(juce::Colour(0xff1b1b1b));
    g.fillPath(p);
    auto tr = r.withTop(k.getBottom() + 2.f);
    g.setColour(ink); g.setFont(Theme::spaced(Theme::uiFont(10.f, true), 0.08f));
    g.drawFittedText(label.toUpperCase(), tr.removeFromTop(13.f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    auto font = Theme::monoFont(11.f, false);
    const float w = juce::jlimit(26.f, r.getWidth() - 2.f, juce::GlyphArrangement::getStringWidth(font, valueText) + 10.f);
    auto lcd = juce::Rectangle<float>(w, 14.f).withCentre({r.getCentreX(), tr.getY() + 8.5f});
    g.setColour(juce::Colour(0xff100a03)); g.fillRoundedRectangle(lcd, 3.f);
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(lcd, 3.f, 0.8f);
    g.setFont(font);
    t.drawGlowText(g, valueText, lcd, juce::Justification::centred, t.am(), 0.5f);
    return;
  }
  // glow grows with the value (mockup: drop-shadow v*11px)
  if (t.glow > 0.f && v > 0.02f) {
    g.setGradientFill(juce::ColourGradient(t.am().withAlpha(t.glowAlpha(0.42f * v)), c.x, c.y, t.am().withAlpha(0.f),
                                           c.x + D / 2.f + 11.f * v * t.glow, c.y, true));
    g.fillEllipse(k.expanded(11.f * v * t.glow));
  }
  // value ring 270 degrees from bottom-left
  const float a0 = -0.75f * juce::MathConstants<float>::pi, span = 1.5f * juce::MathConstants<float>::pi;
  juce::Path bg; bg.addCentredArc(c.x, c.y, 26.f, 26.f, 0.f, a0, a0 + span, true);
  g.setColour(t.c(0xff2a1d0c)); g.strokePath(bg, juce::PathStrokeType(6.f));
  if (v > 0.001f) {
    juce::Path fg; fg.addCentredArc(c.x, c.y, 26.f, 26.f, 0.f, a0, a0 + span * v, true);
    g.setColour(t.am()); g.strokePath(fg, juce::PathStrokeType(6.f));
  }
  // chrome cap
  auto cap = k.reduced(8.f);
  g.setColour(juce::Colours::black.withAlpha(0.85f)); g.fillEllipse(cap.translated(4.f, 6.f));
  if (!t.drawKnobCap(g, cap)) {  // chrome
    juce::ColourGradient cg(juce::Colour(0xff9a9a9f), cap.getX() + cap.getWidth() * 0.3f, cap.getY() + cap.getHeight() * 0.25f,
                            juce::Colour(0xff2b2b2d), cap.getRight(), cap.getBottom(), true);
    cg.addColour(0.45, juce::Colour(0xff5d5d61));
    g.setGradientFill(cg); g.fillEllipse(cap);
    g.setColour(juce::Colour(0x66ffffff)); g.drawEllipse(cap.reduced(1.f), 0.8f);
  }
  // pointer
  juce::Path p; p.addRoundedRectangle(-2.f, -D / 2.f + 10.f, 4.f, 11.f, 2.f);
  p.applyTransform(juce::AffineTransform::rotation(a0 + span * v).translated(c.x, c.y));
  if (t.glow > 0.f) { g.setColour(t.am().withAlpha(t.glowAlpha(0.35f))); g.strokePath(p, juce::PathStrokeType(3.f)); }
  g.setColour(t.am()); g.fillPath(p);
  // texts
  auto tr = r.withTop(k.getBottom() + 3.f);
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(10.f), 0.06f));
  g.drawFittedText(label.toUpperCase(), tr.removeFromTop(13.f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
  g.setFont(Theme::monoFont(12.f, false));
  t.drawGlowText(g, valueText, tr.removeFromTop(15.f), juce::Justification::centred, t.am(), 0.6f);
}

// ---------------- VuMeter ----------------
void VuMeter::paint(juce::Graphics& g) {
  auto& t = T();
  auto b = getLocalBounds().toFloat();
  const float s = juce::jmin(b.getWidth() / 120.f, b.getHeight() / 70.f);
  const float ox = b.getX() + (b.getWidth() - 120.f * s) / 2.f, oy = b.getY() + (b.getHeight() - 70.f * s) / 2.f;
  auto P = [&](float x, float y) { return juce::Point<float>(ox + x * s, oy + y * s); };
  juce::Rectangle<float> face(ox, oy, 120.f * s, 70.f * s);
  t.drawGlow(g, face, t.am(), 16.f, 0.16f);
  g.setColour(t.vuFaceColour()); g.fillRoundedRectangle(face, 3.f);
  for (int k = 0; k < 11; ++k) {
    const float a = (-50.f + k * 10.f) * juce::MathConstants<float>::pi / 180.f;
    g.setColour(juce::Colour(k > 6 ? 0xffcc2222 : 0xff222222));
    g.drawLine({P(60 + std::sin(a) * 48, 64 - std::cos(a) * 48), P(60 + std::sin(a) * 42, 64 - std::cos(a) * 42)}, 1.4f * s);
  }
  g.setColour(juce::Colour(0xff222222)); g.setFont(Theme::spaced(Theme::uiFont(9.f * s), 0.2f));
  g.drawText(word_, juce::Rectangle<float>(ox, oy + 43.f * s, 120.f * s, 11.f * s), juce::Justification::centred);
  g.setFont(Theme::uiFont(6.5f * s));
  g.drawText("-20", juce::Rectangle<float>(ox + 6 * s, oy + 7 * s, 20 * s, 8 * s), juce::Justification::left);
  g.setColour(juce::Colour(0xffcc2222));
  g.drawText("+3", juce::Rectangle<float>(ox + 94 * s, oy + 7 * s, 20 * s, 8 * s), juce::Justification::left);
  const float na = (-50.f + juce::jlimit(0.f, 1.05f, lv_) * 95.f) * juce::MathConstants<float>::pi / 180.f;
  g.setColour(juce::Colour(0xff111111));
  g.drawLine({P(60, 64), P(60 + std::sin(na) * 44, 64 - std::cos(na) * 44)}, 1.3f * s);
  g.setColour(juce::Colour(0xff161616)); g.fillRect(ox, oy + 62.f * s, 120.f * s, 8.f * s);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(face, 3.f, 2.f);
}

// ---------------- insert drag and drop ----------------
juce::String insertDragId(int channelId, int slot) { return "insert:" + juce::String(channelId) + ":" + juce::String(slot); }
bool parseInsertDrag(const juce::var& desc, int& channelId, int& slot) {
  const auto s = desc.toString();
  if (!s.startsWith("insert:")) return false;
  auto parts = juce::StringArray::fromTokens(s.substring(7), ":", "");
  if (parts.size() != 2) return false;
  channelId = parts[0].getIntValue(); slot = parts[1].getIntValue();
  return true;
}
void drawDropOutline(juce::Graphics& g, juce::Rectangle<float> r, float corner) {  // mockup .ov: 2px dashed accent
  juce::Path p; p.addRoundedRectangle(r.reduced(1.f), corner);
  const float dash[] = {5.f, 4.f};
  juce::PathStrokeType(2.f).createDashedStroke(p, p, dash, 2);
  T().drawGlow(g, r, T().am(), 10.f, 0.4f);
  g.setColour(T().am()); g.fillPath(p);
}

void InsertSlot::mouseDrag(const juce::MouseEvent& e) {
  if (name.isEmpty() || e.getDistanceFromDragStart() < 6) return;
  if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
    if (!dc->isDragAndDropActive()) dc->startDragging(insertDragId(channelId, slot), this);
}
bool InsertSlot::isInterestedInDragSource(const SourceDetails& d) {
  int ch, s; return parseInsertDrag(d.description, ch, s) && ch == channelId && s != slot;
}
void InsertSlot::itemDropped(const SourceDetails& d) {
  dropHover_ = false; repaint();
  int ch, s; if (parseInsertDrag(d.description, ch, s) && ch == channelId && onDropFrom) onDropFrom(s);
}

// ---------------- InsertSlot ----------------
void InsertSlot::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat().reduced(0.5f);
  const bool empty = name.isEmpty(), over = isMouseOver();
  if (!empty && lit) t.drawGlow(g, r, t.am(), 12.f, 0.13f);
  g.setGradientFill(juce::ColourGradient(juce::Colour(empty ? 0xff202021 : over ? 0xff47474b : 0xff3c3c3f), 0, r.getY(),
                                         juce::Colour(empty ? 0xff19191a : 0xff262628), 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, 4.f);
  g.setColour(open ? t.amd() : juce::Colours::black); g.drawRoundedRectangle(r, 4.f, 1.f);
  juce::Rectangle<float> led(r.getX() + 7.f, r.getCentreY() - 4.5f, 9.f, 9.f);
  if (!empty && lit) {
    t.drawGlow(g, led, t.am(), 8.f, 0.8f);
    g.setColour(t.am());
  } else g.setColour(empty ? t.c(0xff2a1a08) : t.c(0xff5b4a2c));
  g.fillEllipse(led);
  g.setColour(empty ? Theme::dim() : open ? Theme::cream() : Theme::tx());
  g.setFont(Theme::uiFont(11.5f));
  g.drawFittedText(empty ? juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b Insert")) : name,
                   r.withTrimmedLeft(22.f).withTrimmedRight(4.f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
  if (dropHover_) drawDropOutline(g, getLocalBounds().toFloat(), 4.f);
}

}  // namespace ui
