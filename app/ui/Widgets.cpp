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
// "Amber Night": the deck plate leans back under one warm lamp. The reels turn, but the light on them stays put.
static const juce::Colour kLamp(0xffffbe6e), kLampHot(0xffffd7a0);
static constexpr float kTilt = 0.78f;  // the plate leans back: circles are drawn squashed vertically

// One reel: tape pack, three-window aluminium flange and hub turn with `angle`; sheen and lamp highlight stay fixed.
static void drawReel(juce::Graphics& g, juce::Point<float> c, float R, float angle, float pack, float lamp, float lampX) {
  const auto tilt = juce::AffineTransform::scale(1.f, kTilt).translated(c.x, c.y);
  const auto spin = juce::AffineTransform::rotation(angle).followedBy(tilt);
  {  // soft shadow on the plate below the reel (the lamp is above and in front)
    juce::ColourGradient sg(juce::Colours::black.withAlpha(0.8f), c.x, c.y + 5.f, juce::Colours::transparentBlack, c.x + R * 1.2f, c.y + 5.f, true);
    g.setGradientFill(sg);
    g.fillEllipse(c.x - R * 1.2f, c.y + 5.f - R * 1.2f * kTilt, R * 2.4f, R * 2.4f * kTilt);
  }
  juce::Path packP; packP.addEllipse(-pack, -pack, 2 * pack, 2 * pack); packP.applyTransform(tilt);
  g.setColour(juce::Colour(0xff3b2614)); g.fillPath(packP);
  g.setColour(juce::Colour(0xff6b4426)); g.strokePath(packP, juce::PathStrokeType(0.9f));
  juce::Path ring; ring.addEllipse(-pack * 0.78f, -pack * 0.78f, pack * 1.56f, pack * 1.56f); ring.applyTransform(tilt);
  g.setColour(juce::Colour(0xff4a2f18)); g.strokePath(ring, juce::PathStrokeType(0.6f));

  juce::Path fl; fl.setUsingNonZeroWinding(false);  // even-odd: the windows and hub hole are holes
  fl.addEllipse(-R, -R, 2 * R, 2 * R);
  const float r1 = R * 0.84f, r0 = R * 0.26f, half = juce::degreesToRadians(40.f);
  for (int i = 0; i < 3; ++i) {
    const float a = juce::MathConstants<float>::twoPi * (float)i / 3.f;
    fl.addPieSegment(-r1, -r1, 2 * r1, 2 * r1, a - half, a + half, r0 / r1);
  }
  fl.addEllipse(-R * 0.19f, -R * 0.19f, R * 0.38f, R * 0.38f);
  fl.applyTransform(spin);
  g.setColour(juce::Colour(0xffb5b1a8)); g.fillPath(fl);
  g.setColour(juce::Colour(0xfff2eee4).withAlpha(0.45f)); g.strokePath(fl, juce::PathStrokeType(0.8f));

  juce::Path hub; hub.addEllipse(-R * 0.17f, -R * 0.17f, R * 0.34f, R * 0.34f); hub.applyTransform(tilt);
  g.setColour(juce::Colour(0xff262626)); g.fillPath(hub);
  g.setColour(juce::Colour(0xff8a8a8a)); g.strokePath(hub, juce::PathStrokeType(0.9f));
  juce::Path keys;
  for (int i = 0; i < 3; ++i) {
    juce::Path k; k.addRectangle(-R * 0.03f, -R * 0.165f, R * 0.06f, R * 0.06f);
    k.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::twoPi * (float)i / 3.f));
    keys.addPath(k);
  }
  keys.applyTransform(spin);
  g.setColour(juce::Colour(0xffc8c8c8)); g.fillPath(keys);

  // fixed light: a polished sheen band across the metal, and the lamp's hot spot on the side facing it
  juce::Path outer; outer.addEllipse(-R, -R, 2 * R, 2 * R); outer.applyTransform(tilt);
  juce::Graphics::ScopedSaveState ss(g);
  g.reduceClipRegion(outer);
  juce::ColourGradient sheen(juce::Colours::transparentWhite, c.x - R, c.y - R * kTilt, juce::Colours::transparentBlack, c.x + R, c.y + R * kTilt, false);
  sheen.addColour(0.30, juce::Colour(0xfffff0d8).withAlpha(0.30f * lamp));
  sheen.addColour(0.46, juce::Colours::transparentWhite);
  sheen.addColour(0.74, juce::Colours::black.withAlpha(0.2f));
  g.setGradientFill(sheen); g.fillPath(outer);
  const juce::Point<float> hot(c.x + (lampX - c.x) * 0.22f, c.y - R * 0.62f * kTilt);
  g.setGradientFill(juce::ColourGradient(kLampHot.withAlpha(0.5f * lamp), hot.x, hot.y, kLampHot.withAlpha(0.f), hot.x + R * 0.55f, hot.y, true));
  g.fillPath(outer);
}

void Reels::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  auto& t = T();
  const float corner = r.getHeight() / 2.f, cx = r.getCentreX();
  t.drawGlow(g, r, kLamp, 16.f, 0.1f * lamp_);
  juce::Path pill; pill.addRoundedRectangle(r, corner);
  {
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(pill);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0d0b09), 0.f, r.getY(), juce::Colour(0xff050403), 0.f, r.getBottom(), false));
    g.fillRect(r);
    // warm haze and the lamp's cone from above
    g.setGradientFill(juce::ColourGradient(kLamp.withAlpha(0.34f * lamp_), cx, r.getY() - 8.f, kLamp.withAlpha(0.f), cx + r.getWidth() * 0.55f, r.getY() - 8.f, true));
    g.fillRect(r);
    juce::Path cone;
    cone.startNewSubPath(cx - 7.f, r.getY()); cone.lineTo(cx + 7.f, r.getY());
    cone.lineTo(cx + r.getWidth() * 0.2f, r.getBottom()); cone.lineTo(cx - r.getWidth() * 0.2f, r.getBottom()); cone.closeSubPath();
    g.setGradientFill(juce::ColourGradient(kLamp.withAlpha(0.16f * lamp_), cx, r.getY(), kLamp.withAlpha(0.f), cx, r.getBottom(), false));
    g.fillPath(cone);

    // the deck plate, leaning back (narrower at the far edge), with a front lip
    const float farY = r.getY() + 9.f, nearY = r.getBottom() - 7.f;
    juce::Path plate;
    plate.startNewSubPath(r.getX() + r.getWidth() * 0.14f, farY); plate.lineTo(r.getRight() - r.getWidth() * 0.14f, farY);
    plate.lineTo(r.getRight() - 2.f, nearY); plate.lineTo(r.getX() + 2.f, nearY); plate.closeSubPath();
    g.setColour(juce::Colour(0xff0b0a09)); g.fillPath(plate, juce::AffineTransform::translation(0.f, 3.f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c2a27), 0.f, farY, juce::Colour(0xff151413), 0.f, nearY, false));
    g.fillPath(plate);
    g.setGradientFill(juce::ColourGradient(kLamp.withAlpha(0.26f * lamp_), cx, farY + 6.f, kLamp.withAlpha(0.f), cx + r.getWidth() * 0.36f, farY + 6.f, true));
    g.fillPath(plate);
    g.setColour(juce::Colours::white.withAlpha(0.14f)); g.drawLine(r.getX() + r.getWidth() * 0.14f, farY + 0.5f, r.getRight() - r.getWidth() * 0.14f, farY + 0.5f, 0.8f);

    const float cy = r.getCentreY() - 2.f;
    const float R = juce::jmin((r.getHeight() - 14.f) / 2.f / kTilt, r.getWidth() * 0.2f);
    const float cx1 = r.getX() + corner + 1.f, cx2 = r.getRight() - corner - 1.f;
    const float p1 = R * 0.76f, p2 = R * 0.52f;
    // tape from the bottom of each pack round the guides, past the head (drawn first: the front flanges hide its ends)
    const float base = cy + R * kTilt * 0.92f;
    juce::Path tape;
    tape.startNewSubPath(cx1, cy + p1 * kTilt); tape.lineTo(cx - 13.f, base); tape.lineTo(cx, base + 2.f);
    tape.lineTo(cx + 13.f, base); tape.lineTo(cx2, cy + p2 * kTilt);
    g.setColour(juce::Colour(0xff2a1a0c)); g.strokePath(tape, juce::PathStrokeType(1.8f));
    g.setColour(juce::Colour(0xff7a5230).withAlpha(0.6f)); g.strokePath(tape, juce::PathStrokeType(0.6f));
    drawReel(g, {cx1, cy}, R, a1_, p1, lamp_, cx);
    drawReel(g, {cx2, cy}, R, a2_, p2, lamp_, cx);
    for (float gx : {cx - 13.f, cx + 13.f}) {
      g.setColour(juce::Colour(0xffd6d3cc)); g.fillEllipse(gx - 2.6f, base - 2.6f, 5.2f, 5.2f);
      g.setColour(juce::Colour(0xff555555)); g.drawEllipse(gx - 2.6f, base - 2.6f, 5.2f, 5.2f, 0.5f);
    }
    juce::Rectangle<float> head(cx - 5.f, base - 1.f, 10.f, 6.f);
    g.setColour(juce::Colour(0xffc9c6be)); g.fillRoundedRectangle(head, 1.2f);
    g.setColour(juce::Colour(0xff222222)); g.fillRect(cx - 0.4f, head.getY(), 0.8f, head.getHeight());
    g.setGradientFill(juce::ColourGradient(kLamp.withAlpha(0.3f * lamp_), cx, head.getBottom() + 2.f, kLamp.withAlpha(0.f), cx + 18.f, head.getBottom() + 2.f, true));
    g.fillEllipse(cx - 18.f, head.getBottom(), 36.f, 4.f);

    // REC lamp: pulses red while recording
    const juce::Point<float> led(cx - r.getWidth() * 0.12f, nearY - 3.f);
    if (rec_ > 0.f) { g.setColour(juce::Colour(0xffff5a4a).withAlpha(0.35f * rec_)); g.fillEllipse(led.x - 5.f, led.y - 5.f, 10.f, 10.f); }
    g.setColour(rec_ > 0.f ? juce::Colour(0xffff5a4a).withAlpha(0.55f + 0.45f * rec_) : juce::Colour(0xff3a1612));
    g.fillEllipse(led.x - 2.f, led.y - 2.f, 4.f, 4.f);

    // the lamp's hood at the top, then the vignette
    g.setGradientFill(juce::ColourGradient(kLamp.withAlpha(0.45f * lamp_), cx, r.getY() + 2.f, kLamp.withAlpha(0.f), cx + 22.f, r.getY() + 2.f, true));
    g.fillEllipse(cx - 22.f, r.getY() - 6.f, 44.f, 16.f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2724), cx, r.getY(), juce::Colour(0xff0c0b0a), cx, r.getY() + 5.f, false));
    g.fillRoundedRectangle(cx - 11.f, r.getY() - 3.f, 22.f, 7.f, 3.f);
    juce::ColourGradient vg(juce::Colours::transparentBlack, cx, r.getCentreY(), juce::Colours::black.withAlpha(0.78f), r.getRight() + 8.f, r.getCentreY(), true);
    vg.addColour(0.6, juce::Colours::transparentBlack);
    g.setGradientFill(vg); g.fillRect(r);
  }
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.f);
}

void Reels::timerCallback() {
  ++tick_;
  // The lamp flickers now and then (a 6-second cycle with a short wobble).
  const int f = tick_ % 180;
  const float lamp = f >= 84 && f < 90 ? 0.85f : f >= 90 && f < 92 ? 1.f : f >= 92 && f < 96 ? 0.88f : 0.95f;
  const bool recording = isRecording && isRecording();
  const float rec = recording ? 0.5f + 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)(tick_ % 42) / 42.f) : 0.f;
  const bool lightChanged = lamp != lamp_ || std::abs(rec - rec_) > 0.04f;
  lamp_ = lamp; rec_ = rec;
  if (isPlaying && isPlaying()) {
    const float tau = juce::MathConstants<float>::twoPi;
    a1_ = std::fmod(a1_ + tau / (3.1f * 30.f), tau);  // the full supply reel turns slower than the take-up reel
    a2_ = std::fmod(a2_ + tau / (2.2f * 30.f), tau);
    repaint();
  } else if (lightChanged) repaint();  // stopped: repaint only when the lamp flickers
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
