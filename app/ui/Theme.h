// AirDAW look: palette, accent hue, glow strength and UI scale (ported from docs/airdaw-v4.html).
// Every amber-family colour of the mockup goes through Theme::c(), which rotates it to the chosen accent
// hue exactly like the mockup's mapHex(), so one accent recolours the whole UI uniformly.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

class Theme : public juce::ChangeBroadcaster {
 public:
  static Theme& get();

  // --- user settings (persisted) ---
  float hue = 37.f;   // accent hue in degrees; 37 = the mockup's amber
  float glow = 1.f;   // 0 Off, 0.5 Soft, 1 Normal, 1.6 Strong, 2.4 Max
  int scalePct = 100; // 100..200 in steps of 25

  void load(juce::PropertiesFile& p);
  void save(juce::PropertiesFile& p) const;
  void resetLook() { hue = 37.f; glow = 1.f; }
  void changed() { sendChangeMessage(); }

  // Maps a mockup colour (0xAARRGGBB) to the current accent: amber-family hues are rotated.
  juce::Colour c(juce::uint32 argb) const;

  // --- fixed palette (OLED theme of the mockup) ---
  static constexpr juce::uint32 kBg = 0xff000000, kM1 = 0xff1b1b1d, kM2 = 0xff0b0b0c, kPb = 0xff050505;
  static constexpr juce::uint32 kTx = 0xffe0d9c7, kDim = 0xff9a9486, kCream = 0xffe9dfc2;
  static constexpr juce::uint32 kAm = 0xffffb347, kAmd = 0xff8a5d26, kAmLight = 0xffffd27a, kLcdBg = 0xff100a03;
  static constexpr juce::uint32 kRed = 0xffe5484d;

  juce::Colour am() const { return c(kAm); }
  juce::Colour amd() const { return c(kAmd); }
  juce::Colour amLight() const { return c(kAmLight); }
  juce::Colour lcdBg() const { return juce::Colour(kLcdBg); }  // not remapped (dark, low saturation)
  static juce::Colour tx() { return juce::Colour(kTx); }
  static juce::Colour dim() { return juce::Colour(kDim); }
  static juce::Colour cream() { return juce::Colour(kCream); }

  // Glow alpha multiplier and blur radius following the Glow setting (mockup glowCss()).
  float glowAlpha(float a) const { return juce::jlimit(0.f, 1.f, a * glow); }
  int glowRadius(float r) const { return juce::roundToInt(r * (0.6f + 0.4f * glow)); }

  // --- drawing helpers shared by all components ---
  // Brushed-metal faceplate (mockup .brush): vertical gradient + fine vertical hairlines.
  void drawBrush(juce::Graphics& g, juce::Rectangle<float> r, bool bus = false) const;
  // Soft glow around a rectangle in colour `col` (scaled by the Glow setting).
  void drawGlow(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col, float radius, float alpha = 0.55f) const;
  // Text with an LCD-style glow halo.
  void drawGlowText(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j,
                    juce::Colour col, float glowAmt = 1.f) const;
  // Inset dark well (meters, LCDs, readouts).
  void drawWell(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float corner = 3.f) const;

  static juce::Font uiFont(float h, bool bold = false);
  static juce::Font monoFont(float h, bool bold = true);
  static juce::Font spaced(juce::Font f, float kerning) { return f.withExtraKerningFactor(kerning); }

  static constexpr int kAccentHues[12] = {37, 50, 18, 4, 335, 300, 265, 215, 200, 175, 140, 85};
  static constexpr juce::uint32 kAccentSwatch[12] = {0xffffb347, 0xffffd84a, 0xffff8a3d, 0xffff6060, 0xffff7ab6, 0xffe060e0,
                                                    0xffb38cff, 0xff5a8bff, 0xff6ec6ff, 0xff4fe0c8, 0xff6dff9a, 0xffb5e84f};
  static constexpr const char* kAccentNames[12] = {"Amber", "Gold", "Orange", "Red", "Rose", "Magenta",
                                                   "Violet", "Blue", "Ice", "Teal", "Phosphor", "Lime"};
};

inline Theme& T() { return Theme::get(); }

// Mockup fader law: slider position v in 0..100 (75 = 0 dB, 100 = +6 dB, 0 = -inf).
inline float dbOf(float v) {
  if (v <= 0.f) return -std::numeric_limits<float>::infinity();
  if (v >= 75.f) return (v - 75.f) / 25.f * 6.f;
  return -60.f * std::pow((75.f - v) / 75.f, 1.5f);
}
inline float vOf(float db) {
  if (!(db > -1000.f)) return 0.f;
  if (db >= 0.f) return juce::jmin(100.f, 75.f + db / 6.f * 25.f);
  return juce::jmax(0.f, 75.f - 75.f * std::pow(juce::jmin(1.f, -db / 60.f), 1.f / 1.5f));
}
juce::String dbText(float db, int decimals = 1);

}  // namespace ui
