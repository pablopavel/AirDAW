#include "AirLookAndFeel.h"

namespace ui {

AirLookAndFeel::AirLookAndFeel() { refreshColours(); }

void AirLookAndFeel::refreshColours() {
  auto& t = T();
  setColour(juce::ResizableWindow::backgroundColourId, juce::Colours::black);
  setColour(juce::Label::textColourId, Theme::tx());
  setColour(juce::TextButton::textColourOffId, Theme::tx());
  setColour(juce::TextButton::textColourOnId, juce::Colour(0xff2a1800));
  setColour(juce::ToggleButton::textColourId, Theme::tx());
  setColour(juce::ToggleButton::tickColourId, t.am());
  setColour(juce::ComboBox::textColourId, t.am());
  setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff080808));
  setColour(juce::ComboBox::arrowColourId, t.am());
  setColour(juce::ComboBox::outlineColourId, juce::Colours::black);
  setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff121213));
  setColour(juce::PopupMenu::textColourId, Theme::tx());
  setColour(juce::PopupMenu::highlightedBackgroundColourId, t.am());
  setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xff2a1800));
  setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff080808));
  setColour(juce::TextEditor::textColourId, Theme::tx());
  setColour(juce::TextEditor::highlightColourId, t.am().withAlpha(0.35f));
  setColour(juce::TextEditor::outlineColourId, juce::Colours::black);
  setColour(juce::TextEditor::focusedOutlineColourId, t.amd());
  setColour(juce::CaretComponent::caretColourId, t.am());
  setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff3a3a3c));
  setColour(juce::Slider::thumbColourId, t.am());
  setColour(juce::Slider::trackColourId, t.am());
  setColour(juce::Slider::backgroundColourId, juce::Colour(0xff050505));
  setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff161617));
  setColour(juce::AlertWindow::textColourId, Theme::tx());
  setColour(juce::AlertWindow::outlineColourId, juce::Colours::black);
  setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff0b0b0c));
  setColour(juce::ListBox::textColourId, Theme::tx());
  setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, t.am().withAlpha(0.4f));
  setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff161617));
  setColour(juce::TooltipWindow::textColourId, Theme::tx());
  setColour(juce::GroupComponent::outlineColourId, juce::Colour(0xff2a2a2c));
  setColour(juce::GroupComponent::textColourId, Theme::cream());
}

// Colours of a lit button by kind (top, bottom, text). Amber-family ones follow the accent.
static void litColours(const juce::String& kind, juce::Colour& top, juce::Colour& bot, juce::Colour& txt, juce::Colour& glow) {
  auto& t = T();
  if (kind == "m") { top = t.c(0xffffd27a); bot = t.c(0xffe0a02a); txt = juce::Colour(0xff2a1800); glow = t.am(); }
  else if (kind == "s") { top = juce::Colour(0xff9fd6ff); bot = juce::Colour(0xff3b8fd6); txt = juce::Colour(0xff00182c); glow = juce::Colour(0xff4aa3ff); }
  else if (kind == "r") { top = juce::Colour(0xffff6b6f); bot = juce::Colour(0xffc22b30); txt = juce::Colours::white; glow = juce::Colour(0xffe5484d); }
  else if (kind == "t") { top = juce::Colour(0xffd9c8ff); bot = juce::Colour(0xff8b6fd6); txt = juce::Colour(0xff120a2a); glow = juce::Colour(0xffb38cff); }
  else { top = t.c(0xffffc46b); bot = t.c(0xffd98e24); txt = juce::Colour(0xff2a1800); glow = t.am(); }
}

void AirLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
  auto r = b.getLocalBounds().toFloat().reduced(0.5f);
  const auto kind = b.getProperties()["kind"].toString();
  const float corner = T().rad(4.f);
  if (kind == "flat") {
    g.setColour(juce::Colour(over ? 0x22ffffff : 0x10ffffff));
    float dash[] = {5.f, 4.f};
    juce::Path p; p.addRoundedRectangle(r, corner);
    juce::PathStrokeType(1.f).createDashedStroke(p, p, dash, 2);
    g.setColour(Theme::dim().withAlpha(over ? 0.9f : 0.5f)); g.fillPath(p);
    return;
  }
  if (kind == "lcd") {  // a small amber readout that opens a menu (rack module presets)
    g.setColour(juce::Colour(0xff100a03)); g.fillRoundedRectangle(r, 3.f);
    g.setColour(juce::Colours::black.withAlpha(0.8f)); g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 2, r.getRight() - 2);
    g.setColour(over ? T().amd() : juce::Colours::black); g.drawRoundedRectangle(r, 3.f, 1.f);
    return;
  }
  if (kind == "key") {  // deck menu keys: the transport keys' look at button size
    auto& t = T();
    const bool on = b.getToggleState(), pressed = on || down;
    auto k = r.withTrimmedBottom(4.f);
    const float kc = T().rad(4.f);
    if (!pressed) { g.setColour(juce::Colour(0xff070708)); g.fillRoundedRectangle(k.translated(0, 2), kc);
                    g.setColour(juce::Colours::black.withAlpha(0.7f)); g.fillRoundedRectangle(k.translated(0, 4), kc); }
    else k = k.translated(0, 3);
    if (on) {
      t.drawGlow(g, k, t.am(), 14.f, 0.5f);
      juce::ColourGradient cg(t.c(0xffffe3a3), 0, k.getY(), t.c(0xffe3952a), 0, k.getBottom(), false); cg.addColour(0.45, t.c(0xffffbe55));
      g.setGradientFill(cg);
    } else {
      const bool hot = over && b.isEnabled();
      juce::ColourGradient cg(juce::Colour(hot ? 0xff66666c : 0xff5c5c62), 0, k.getY(), juce::Colour(0xff252528), 0, k.getBottom(), false);
      cg.addColour(0.14, juce::Colour(hot ? 0xff4a4a50 : 0xff404045)); cg.addColour(0.58, juce::Colour(0xff303034));
      g.setGradientFill(cg);
    }
    g.fillRoundedRectangle(k, kc);
    g.setColour(juce::Colour(on ? 0x1f000000 : 0x2a000000));
    for (float y = k.getY() + 3.f; y < k.getBottom(); y += 4.f) g.fillRect(k.getX() + 1.f, y, k.getWidth() - 2.f, 1.f);
    g.setColour(juce::Colour(on ? 0xccffffff : 0x6effffff)); g.drawHorizontalLine((int)k.getY() + 1, k.getX() + 3, k.getRight() - 3);
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(k, kc, 1.f);
    return;
  }
  if (down) r = r.translated(0.f, 1.f);
  const bool on = b.getToggleState();
  if (on) {
    juce::Colour top, bot, txt, glow; litColours(kind, top, bot, txt, glow);
    T().drawGlow(g, r, glow, 12.f, 0.33f);
    g.setGradientFill(juce::ColourGradient(top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colour(0x66ffffff)); g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 2, r.getRight() - 2);
  } else {
    g.setColour(juce::Colour(0xcc000000)); g.fillRoundedRectangle(r.translated(0.f, 2.f), corner);  // drop shadow
    const bool en = b.isEnabled();
    g.setGradientFill(juce::ColourGradient(juce::Colour(over && en ? 0xff47474b : 0xff3c3c3f), r.getX(), r.getY(),
                                           juce::Colour(over && en ? 0xff2d2d30 : 0xff262628), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colour(0x14ffffff)); g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 2, r.getRight() - 2);
  }
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r, corner, 1.f);
}

juce::Font AirLookAndFeel::getTextButtonFont(juce::TextButton& b, int) {
  const float h = (float)b.getProperties().getWithDefault("fontH", 12.0);
  return Theme::spaced(Theme::uiFont(h), b.getProperties()["noUpper"] ? 0.f : 0.06f);
}

void AirLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) {
  auto font = getTextButtonFont(b, b.getHeight());
  g.setFont(font);
  juce::Colour col = b.findColour(juce::TextButton::textColourOffId);
  if (b.getToggleState()) { juce::Colour a, c, txt, gl; litColours(b.getProperties()["kind"].toString(), a, c, txt, gl); col = txt; }
  if (!b.isEnabled()) col = col.withMultipliedAlpha(0.75f);
  if (b.getProperties()["kind"].toString() == "flat") col = Theme::dim();
  auto text = b.getButtonText();
  if (!b.getProperties()["noUpper"]) text = text.toUpperCase();
  auto r = b.getLocalBounds().reduced(4, 1);
  if (b.getProperties()["kind"].toString() == "lcd") {
    g.setFont(Theme::monoFont((float)b.getProperties().getWithDefault("fontH", 11.0), false));
    auto arrow = r.removeFromRight(10);
    T().drawGlowText(g, text, r.toFloat().withTrimmedLeft(3.f), juce::Justification::centredLeft, T().am(), 0.5f);
    g.setColour(T().amd()); g.setFont(Theme::uiFont(10.f));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xbe")), arrow, juce::Justification::centred);
    return;
  }
  if (b.getProperties()["kind"].toString() == "key") {  // follows the key face (see drawButtonBackground)
    r = r.withTrimmedBottom(4);
    if (down || b.getToggleState()) r = r.translated(0, 3);
  } else if (down) r = r.translated(0, 1);
  g.setColour(col);
  g.drawFittedText(text, r, juce::Justification::centred, 3, 0.7f);
}

void AirLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
  auto r = b.getLocalBounds().toFloat();
  auto box = r.removeFromLeft(r.getHeight()).reduced(r.getHeight() * 0.22f);
  g.setColour(juce::Colour(0xff080808)); g.fillRoundedRectangle(box, 3.f);
  g.setColour(over ? T().amd() : juce::Colour(0xff3a3a3c)); g.drawRoundedRectangle(box, 3.f, 1.f);
  if (b.getToggleState()) {
    T().drawGlow(g, box.reduced(3.f), T().am(), 8.f, 0.4f);
    g.setColour(T().am()); g.fillRoundedRectangle(box.reduced(3.5f), 2.f);
  }
  g.setColour(Theme::tx()); g.setFont(Theme::uiFont(13.f));
  g.drawText(b.getButtonText(), r.withTrimmedLeft(4.f), juce::Justification::centredLeft, true);
}

void AirLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
  juce::Rectangle<float> r(0.f, 0.f, (float)w, (float)h);
  g.setColour(box.findColour(juce::ComboBox::backgroundColourId)); g.fillRoundedRectangle(r.reduced(0.5f), T().rad(4.f));
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), T().rad(4.f), 1.f);
  juce::Path arrow; const float ax = (float)w - 14.f, ay = (float)h * 0.5f;
  arrow.addTriangle(ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
  g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.f : 0.4f)); g.fillPath(arrow);
}
juce::Font AirLookAndFeel::getComboBoxFont(juce::ComboBox&) { return Theme::uiFont(13.f); }
void AirLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& l) {
  l.setBounds(6, 1, box.getWidth() - 26, box.getHeight() - 2);
  l.setFont(getComboBoxFont(box));
}

void AirLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
  g.fillAll(juce::Colour(0xff121213));
  g.setColour(juce::Colours::black); g.drawRect(0, 0, w, h);
}
void AirLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool sep, bool active, bool hi,
                                       bool ticked, bool hasSub, const juce::String& text, const juce::String& shortcut,
                                       const juce::Drawable* icon, const juce::Colour* tc) {
  if (sep) { g.setColour(juce::Colour(0x22ffffff)); g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY())); return; }
  auto r = area.reduced(1);
  if (hi && active) { g.setColour(T().am()); g.fillRect(r); }
  g.setColour(hi && active ? juce::Colour(0xff2a1800) : tc ? *tc : Theme::tx().withAlpha(active ? 1.f : 0.4f));
  g.setFont(getPopupMenuFont());
  auto tr = r.reduced(10, 0);
  if (ticked) { g.drawText(juce::CharPointer_UTF8("\xe2\x97\x8f"), tr.removeFromLeft(14), juce::Justification::centredLeft); }
  else tr.removeFromLeft(14);
  g.drawFittedText(text, tr, juce::Justification::centredLeft, 1);
  if (shortcut.isNotEmpty()) { g.setColour(Theme::dim()); g.drawText(shortcut, tr, juce::Justification::centredRight); }
  if (hasSub) g.drawText(">", tr, juce::Justification::centredRight);
  juce::ignoreUnused(icon);
}
void AirLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) {
  g.setColour(T().amd()); g.setFont(Theme::spaced(Theme::uiFont(11.f, true), 0.15f));
  g.drawText(name.toUpperCase(), area.reduced(12, 0), juce::Justification::bottomLeft);
}

void AirLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                   int ts, int tsz, bool over, bool down) {
  g.setColour(juce::Colour(0xff050505)); g.fillRect(x, y, w, h);
  juce::Rectangle<float> th = vertical ? juce::Rectangle<float>((float)x + 2, (float)ts, (float)w - 4, (float)tsz)
                                       : juce::Rectangle<float>((float)ts, (float)y + 2, (float)tsz, (float)h - 4);
  g.setColour(over || down ? T().amd() : juce::Colour(0xff3a3a3c)); g.fillRoundedRectangle(th, 3.f);
}

void AirLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) {
  g.setColour(juce::Colour(0xff080808)); g.fillRoundedRectangle(0.f, 0.f, (float)w, (float)h, 3.f);
}
void AirLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
  g.setColour(e.hasKeyboardFocus(true) ? T().amd() : juce::Colours::black);
  g.drawRoundedRectangle(0.5f, 0.5f, (float)w - 1.f, (float)h - 1.f, 3.f, 1.f);
}

// Horizontal sliders (Display panel scale, click level): thin dark track, amber fill, glowing round thumb.
void AirLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                      juce::Slider::SliderStyle style, juce::Slider& s) {
  if (style != juce::Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s); return; }
  const float cy = (float)y + h * 0.5f;
  juce::Rectangle<float> track((float)x, cy - 2.f, (float)w, 4.f);
  g.setColour(juce::Colour(0xff050505)); g.fillRoundedRectangle(track, 2.f);
  g.setColour(juce::Colour(0x12ffffff)); g.drawRoundedRectangle(track, 2.f, 1.f);
  g.setColour(T().am().withAlpha(0.8f)); g.fillRoundedRectangle(track.withRight(pos), 2.f);
  juce::Rectangle<float> th(pos - 7.5f, cy - 7.5f, 15.f, 15.f);
  T().drawGlow(g, th.reduced(2.f), T().am(), 9.f, 0.6f);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xffffe9bd), th.getX() + 4.f, th.getY() + 4.f, T().am(), th.getCentreX(),
                                         th.getBottom(), true));
  g.fillEllipse(th);
}

void AirLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) {
  g.fillAll(juce::Colour(0xff161617)); g.setColour(juce::Colours::black); g.drawRect(0, 0, w, h);
  g.setColour(Theme::tx()); g.setFont(Theme::uiFont(13.f));
  g.drawFittedText(text, juce::Rectangle<int>(w, h).reduced(6, 3), juce::Justification::centredLeft, 4);
}

void AirLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) {
  g.setColour(juce::Colour(0xff161617)); g.fillPath(path);
  g.setColour(juce::Colours::black); g.strokePath(path, juce::PathStrokeType(1.f));
}

}  // namespace ui
