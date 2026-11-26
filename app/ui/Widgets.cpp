#include "Widgets.h"

namespace ui {

// ---------------- Lcd ----------------
void Lcd::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  auto& t = T();
  t.drawGlow(g, r, t.am(), 12.f, 0.08f);
  t.drawWell(g, r, t.lcdBg(), 4.f);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), 4.f, 1.f);
  auto in = r.reduced(h_ > 30.f ? 22.f : 10.f, 4.f);
  auto cap = in.removeFromBottom(juce::jmax(10.f, h_ * 0.28f));
  auto font = Theme::spaced(Theme::monoFont(h_), 0.1f);
  const float tw = juce::GlyphArrangement::getStringWidth(font, text_);
  if (tw > in.getWidth() && tw > 0.f) font = font.withHeight(juce::jmax(9.f, h_ * in.getWidth() / tw));  // shrink to fit narrow windows
  g.setFont(font);
  t.drawGlowText(g, text_, in, juce::Justification::centredLeft, col_.isTransparent() ? t.am() : col_, 1.f);
  g.setFont(Theme::spaced(Theme::uiFont(juce::jmax(8.5f, h_ * 0.25f)), 0.2f));
  g.setColour(t.amd());
  g.drawText(caption_, cap, juce::Justification::topLeft, false);
}

// ---------------- Reels ----------------
static void drawReel(juce::Graphics& g, juce::Point<float> c, float R, float angle) {
  auto& t = T();
  const float k = R / 50.f;
  auto circle = [&](float r, juce::Colour col) { g.setColour(col); g.fillEllipse(c.x - r * k, c.y - r * k, 2 * r * k, 2 * r * k); };
  circle(48.f, juce::Colour(0xff161617));
  g.setColour(juce::Colours::black); g.drawEllipse(c.x - 48 * k, c.y - 48 * k, 96 * k, 96 * k, 2.f * k);
  circle(40.f, t.c(0xff3a2d1d));
  circle(30.f, juce::Colour(0xff111111));
  auto xf = juce::AffineTransform::rotation(angle).translated(c.x, c.y);
  for (int i = 0; i < 3; ++i) {
    const float a = juce::MathConstants<float>::pi * 2.f / 3.f * (float)i;
    juce::Path cut;  // window between the spokes
    cut.startNewSubPath(-9 * k, -30 * k); cut.quadraticTo(0, -33 * k, 9 * k, -30 * k);
    cut.lineTo(5 * k, -12 * k); cut.lineTo(-5 * k, -12 * k); cut.closeSubPath();
    cut.applyTransform(juce::AffineTransform::rotation(a + juce::MathConstants<float>::pi / 3.f).followedBy(xf));
    g.setColour(juce::Colour(0xff0b0b0b)); g.fillPath(cut);
    g.setColour(juce::Colour(0xff444444)); g.strokePath(cut, juce::PathStrokeType(0.8f));
    juce::Line<float> tick(0, -48 * k, 0, -40 * k);
    tick.applyTransform(juce::AffineTransform::rotation(a).followedBy(xf));
    g.setColour(t.am()); g.drawLine(tick, 2.f * k);
  }
  circle(11.f, juce::Colour(0xff9b9b9b));
  g.setColour(juce::Colours::black); g.drawEllipse(c.x - 11 * k, c.y - 11 * k, 22 * k, 22 * k, 1.f);
  circle(4.f, juce::Colour(0xff222222));
}

void Reels::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  auto& t = T();
  t.drawGlow(g, r, t.am(), 16.f, 0.09f);
  g.setColour(juce::Colour(0xff050505)); g.fillRoundedRectangle(r, r.getHeight() / 2.f);
  t.drawWell(g, r, juce::Colour(0xff050505), r.getHeight() / 2.f);
  const float R = (r.getHeight() - 12.f) / 2.f;
  const float cx1 = r.getX() + 14.f + R, cx2 = r.getRight() - 14.f - R;
  g.setColour(juce::Colour(0xff5a4630)); g.fillRect(cx1 + R + 4.f, r.getCentreY() - 1.f, cx2 - cx1 - 2 * R - 8.f, 2.f);
  drawReel(g, {cx1, r.getCentreY()}, R, a1_);
  drawReel(g, {cx2, r.getCentreY()}, R, a2_);
}
void Reels::timerCallback() {
  if (!isPlaying || !isPlaying()) return;
  const float tau = juce::MathConstants<float>::twoPi;
  a1_ = std::fmod(a1_ + tau / (2.2f * 30.f), tau);
  a2_ = std::fmod(a2_ + tau / (1.6f * 30.f), tau);
  repaint();
}

// ---------------- NamePlate ----------------
void NamePlate::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  T().drawGlow(g, r, Theme::cream(), 12.f, 0.09f);
  g.setColour(Theme::cream()); g.fillRoundedRectangle(r, 3.f);
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
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4c4c50), k.getX() + ks * 0.35f, k.getY() + ks * 0.3f,
                                         juce::Colour(0xff19191b), k.getX() + ks * 0.9f, k.getBottom(), true));
  g.fillEllipse(k);
  g.setColour(juce::Colour(0x33ffffff)); g.drawEllipse(k.reduced(0.5f), 0.7f);
  const float ang = v_ / 24.f * 135.f * juce::MathConstants<float>::pi / 180.f;
  juce::Line<float> ptr(0, -ks / 2.f + 3.f, 0, -ks / 2.f + 12.f);
  ptr.applyTransform(juce::AffineTransform::rotation(ang).translated(k.getCentreX(), k.getCentreY()));
  g.setColour(Theme::cream()); g.drawLine(ptr, 2.f);
  auto txt = r.withTrimmedLeft(ks + 14.f);
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(9.5f), 0.14f));
  g.drawText("GAIN", txt.removeFromTop(txt.getHeight() * 0.48f), juce::Justification::centredBottom);
  g.setFont(Theme::monoFont(12.5f));
  t.drawGlowText(g, (v_ > 0 ? "+" : "") + juce::String(v_, 1), txt, juce::Justification::centredTop, t.am(), 0.6f);
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

// ---------------- FaderBlock ----------------
static constexpr float kScaleW = 24.f, kFaderW = 34.f, kLadW = 22.f, kGap = 4.f, kReadH = 24.f;

juce::Rectangle<float> FaderBlock::trackArea() const {
  auto r = getLocalBounds().toFloat();
  r.removeFromBottom((bigReadout ? 52.f : kReadH) + 6.f);
  const float w = kScaleW + kFaderW + kLadW + 2 * kGap;
  return r.withSizeKeepingCentre(w, r.getHeight());
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
  juce::Rectangle<float> cap(fx, y - 22.f, kFaderW, 44.f);
  g.setColour(juce::Colours::black.withAlpha(0.85f)); g.fillRoundedRectangle(cap.translated(0.f, 6.f), 3.f);
  juce::ColourGradient cg(juce::Colour(0xff161616), cap.getX(), 0, juce::Colour(0xff161616), cap.getRight(), 0, false);
  cg.addColour(0.45, juce::Colour(0xff5e5e60));
  g.setGradientFill(cg); g.fillRoundedRectangle(cap, 3.f);
  g.setColour(juce::Colour(0xffcccccc)); g.fillRect(cap.getX() + 1.f, cap.getY(), cap.getWidth() - 2.f, 2.f);
  g.setColour(t.am()); g.fillRect(cap.getX(), cap.getCentreY() - 1.75f, cap.getWidth(), 3.5f);
  g.setColour(juce::Colour(0x88000000)); g.drawRoundedRectangle(cap, 3.f, 1.f);
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
  t.drawGlowText(g, dbText(dbOf(v_)), rd, juce::Justification::centred, t.am(), 0.8f);
}

// ---------------- PluginKnob ----------------
void PluginKnob::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  const float D = 58.f;
  juce::Rectangle<float> k(r.getCentreX() - D / 2.f, r.getY() + 2.f, D, D);
  const auto c = k.getCentre();
  const float v = (v_ - min_) / (max_ - min_);
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
  juce::ColourGradient cg(juce::Colour(0xff9a9a9f), cap.getX() + cap.getWidth() * 0.3f, cap.getY() + cap.getHeight() * 0.25f,
                          juce::Colour(0xff2b2b2d), cap.getRight(), cap.getBottom(), true);
  cg.addColour(0.45, juce::Colour(0xff5d5d61));
  g.setGradientFill(cg); g.fillEllipse(cap);
  g.setColour(juce::Colour(0x66ffffff)); g.drawEllipse(cap.reduced(1.f), 0.8f);
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
  g.setColour(Theme::cream()); g.fillRoundedRectangle(face, 3.f);
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
}

}  // namespace ui
