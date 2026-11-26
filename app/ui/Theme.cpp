#include "Theme.h"

namespace ui {

Theme& Theme::get() { static Theme t; return t; }

void Theme::load(juce::PropertiesFile& p) {
  hue = (float)p.getDoubleValue("look.hue", 37.0);
  glow = juce::jlimit(0.f, 2.4f, (float)p.getDoubleValue("look.glow", 1.0));
  scalePct = juce::jlimit(100, 200, p.getIntValue("look.scale", 100));
}
void Theme::save(juce::PropertiesFile& p) const {
  p.setValue("look.hue", hue); p.setValue("look.glow", glow); p.setValue("look.scale", scalePct);
  p.saveIfNeeded();
}

// HSL helpers (the mockup works in HSL; JUCE's Colour is HSB).
static void toHSL(float r, float g, float b, float& h, float& s, float& l) {
  const float M = std::max({r, g, b}), m = std::min({r, g, b}), d = M - m;
  l = (M + m) / 2.f; h = 0.f; s = 0.f;
  if (d > 0.f) {
    s = d / (1.f - std::abs(2.f * l - 1.f));
    if (M == r) h = std::fmod((g - b) / d, 6.f);
    else if (M == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h *= 60.f; if (h < 0.f) h += 360.f;
  }
}
static void fromHSL(float h, float s, float l, float& r, float& g, float& b) {
  const float c = (1.f - std::abs(2.f * l - 1.f)) * s, x = c * (1.f - std::abs(std::fmod(h / 60.f, 2.f) - 1.f)), m = l - c / 2.f;
  float q[3];
  if (h < 60) { q[0] = c; q[1] = x; q[2] = 0; } else if (h < 120) { q[0] = x; q[1] = c; q[2] = 0; }
  else if (h < 180) { q[0] = 0; q[1] = c; q[2] = x; } else if (h < 240) { q[0] = 0; q[1] = x; q[2] = c; }
  else if (h < 300) { q[0] = x; q[1] = 0; q[2] = c; } else { q[0] = c; q[1] = 0; q[2] = x; }
  r = q[0] + m; g = q[1] + m; b = q[2] + m;
}

juce::Colour Theme::c(juce::uint32 argb) const {
  juce::Colour col(argb);
  float h, s, l; toHSL(col.getFloatRed(), col.getFloatGreen(), col.getFloatBlue(), h, s, l);
  if (h >= 16.f && h <= 60.f && s >= 0.25f && (l < 0.8f || (s >= 0.8f && l < 0.92f))) {
    float r, g, b; fromHSL(std::fmod(h - 37.f + hue + 360.f, 360.f), s, l, r, g, b);
    return juce::Colour::fromFloatRGBA(r, g, b, col.getFloatAlpha());
  }
  return col;
}

void Theme::drawBrush(juce::Graphics& g, juce::Rectangle<float> r, bool bus) const {
  g.setGradientFill(juce::ColourGradient(juce::Colour(bus ? 0xff262420 : kM1), r.getX(), r.getY(),
                                         juce::Colour(bus ? 0xff161512 : kM2), r.getX(), r.getBottom(), false));
  g.fillRect(r);
  g.setColour(juce::Colour(0x04ffffff));
  for (float x = r.getX(); x < r.getRight(); x += 3.f) g.fillRect(x, r.getY(), 1.f, r.getHeight());
  g.setColour(juce::Colour(0x14ffffff)); g.fillRect(r.getX(), r.getY(), r.getWidth(), 1.f);
}

void Theme::drawGlow(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col, float radius, float alpha) const {
  if (glow <= 0.f) return;
  const int rad = glowRadius(radius);
  if (rad <= 0) return;
  juce::DropShadow(col.withAlpha(glowAlpha(alpha)), rad, {}).drawForRectangle(g, r.toNearestInt());
}

void Theme::drawGlowText(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j,
                         juce::Colour col, float glowAmt) const {
  if (glow > 0.f && glowAmt > 0.f) {
    g.setColour(col.withAlpha(glowAlpha(0.16f * glowAmt)));
    for (auto [dx, dy] : {std::pair{-1.5f, 0.f}, {1.5f, 0.f}, {0.f, -1.5f}, {0.f, 1.5f}, {-1.f, -1.f}, {1.f, 1.f}})
      g.drawText(s, r.translated(dx, dy), j, false);
  }
  g.setColour(col);
  g.drawText(s, r, j, false);
}

void Theme::drawWell(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float corner) const {
  g.setColour(fill); g.fillRoundedRectangle(r, corner);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0x99000000), r.getX(), r.getY(), juce::Colours::transparentBlack,
                                         r.getX(), r.getY() + 5.f, false));
  g.fillRoundedRectangle(r, corner);
  g.setColour(juce::Colour(0x14ffffff)); g.drawHorizontalLine((int)r.getBottom(), r.getX() + corner, r.getRight() - corner);
}

juce::Font Theme::uiFont(float h, bool bold) {
  return juce::Font(juce::FontOptions("Segoe UI", h, bold ? juce::Font::bold : juce::Font::plain));
}
juce::Font Theme::monoFont(float h, bool bold) {
  return juce::Font(juce::FontOptions("Consolas", h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::String dbText(float db, int decimals) {
  if (!(db > -200.f)) return juce::String(juce::CharPointer_UTF8("-\xe2\x88\x9e"));  // -inf
  return (db > 0.f ? "+" : "") + juce::String(db, decimals);
}

}  // namespace ui
