// AirDAW look: palette, accent hue, glow, UI scale and every Display-panel option (ported from docs/airdaw-v4.html).
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
  float glow = 1.f;   // 0 Off, 0.5 Soft, 1 Normal (default), 1.6 Strong, 2.4 Max
  int scalePct = 100; // 100..200 in steps of 25
  // Display-panel options. Values are the mockup's option ids (see Theme.cpp).
  juce::String panel = "oled", faceplate = "matte", tint = "charcoal", corners = "sharp", knobs = "ivory",
               faders = "cream", vuFace = "cream", readouts = "accent", typeface = "industrial", trimMat = "wood",
               species = "walnut", grain = "fine", trimWidth = "normal";
  juce::String inserts = "rack";  // insert panel look: "cards" (the original) or "rack" (500-series modules)
  // Buttons: "console" (coloured console caps in a bezel), "toggle" (strip switches as bat-handle toggles), "classic"
  // (the original flat keys).
  juce::String buttons = "classic";
  bool screws = true, trim = true, pixelShift = true;
  bool trackColours = false;  // the coloured bar under each channel's name

  void load(juce::PropertiesFile& p);
  void save(juce::PropertiesFile& p) const;
  void resetLook();  // the Studio look (the default), 100% scale, pixel shift on, rack inserts, classic buttons, no track colours
  void changed() { tile_ = {}; sendChangeMessage(); }

  // Generic option access for the Display panel. Keys: hue, glow, panel, fp, tint, rd, knob, fd, vu, lcd, font,
  // mat, ws, gr, ww, scr, wood, shift, ins, btn, chc (booleans as "1" / "0").
  juce::String option(const juce::String& key) const;
  void setOption(const juce::String& key, const juce::String& value);
  // Eras (e50 e60 e70 e80 e90 e00) and classics (amber neon ice lounge vintage): set a whole look at once.
  void applyPreset(const juce::String& id);
  bool presetActive(const juce::String& id) const;

  // Maps a mockup colour (0xAARRGGBB) to the current accent: amber-family hues are rotated.
  juce::Colour c(juce::uint32 argb) const;

  // --- palette (OLED / Standard panel, tint) ---
  juce::Colour m1() const;  // faceplate gradient top
  juce::Colour m2() const;  // faceplate gradient bottom
  juce::Colour pb() const;  // panel background (mixer, overview)
  juce::Colour bg() const;  // window background
  static constexpr juce::uint32 kTx = 0xffe0d9c7, kDim = 0xff9a9486, kCream = 0xffe9dfc2;
  static constexpr juce::uint32 kAm = 0xffffb347, kAmd = 0xff8a5d26, kAmLight = 0xffffd27a, kLcdBg = 0xff100a03;
  static constexpr juce::uint32 kRed = 0xffe5484d;

  juce::Colour am() const { return c(kAm); }
  juce::Colour amd() const { return c(kAmd); }
  juce::Colour amLight() const { return c(kAmLight); }
  juce::Colour lcdBg() const;           // readout wells (dark amber, or near-black with a coloured readout)
  juce::Colour readout() const;         // readout text: the accent, or Red LED / Green / VFD cyan / White
  juce::Colour readoutCaption() const;  // small captions inside readouts
  juce::Colour vuFaceColour() const;
  static juce::Colour tx() { return juce::Colour(kTx); }
  static juce::Colour dim() { return juce::Colour(kDim); }
  static juce::Colour cream() { return juce::Colour(kCream); }

  // Corner radius following the Corners option (Sharp ~1 px, Normal as designed, Round much rounder).
  float rad(float r) const;
  // Side trim width and bottom rail height (0 when the trim is off).
  int trimW() const;
  int railH() const;

  // Glow alpha multiplier and blur radius following the Glow setting (mockup glowCss()).
  float glowAlpha(float a) const { return juce::jlimit(0.f, 1.f, a * glow); }
  int glowRadius(float r) const { return juce::roundToInt(r * (0.6f + 0.4f * glow)); }

  // --- drawing helpers shared by all components ---
  // Faceplate (mockup .brush and the Faceplate option): palette gradient + texture.
  void drawBrush(juce::Graphics& g, juce::Rectangle<float> r, bool bus = false) const;
  // Soft glow around a rectangle in colour `col` (scaled by the Glow setting). It follows the shape's rounded corners
  // (`corner` < 0: the Corners setting's small radius) and fades out evenly, so no square halo shows round round things.
  void drawGlow(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col, float radius, float alpha = 0.55f,
                float corner = -1.f) const;
  // The original soft blurred glow (a drop shadow round the rectangle). The transport keys keep this look.
  void drawSoftGlow(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col, float radius, float alpha = 0.55f) const;
  // Text with an LCD-style glow halo.
  void drawGlowText(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j,
                    juce::Colour col, float glowAmt = 1.f) const;
  // Inset dark well (meters, LCDs, readouts).
  void drawWell(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float corner = 3.f) const;
  // Side trim / rail (wood species + grain, rack ears, tolex, chrome). `vertical`: a side piece.
  void drawTrim(juce::Graphics& g, juce::Rectangle<int> r, bool vertical, bool leftSide) const;
  void drawScrew(juce::Graphics& g, juce::Point<float> centre) const;
  // Knob cap in the chosen material (Chrome, Bakelite, Ivory, Gold, Knurled, Skirted, Soft touch), with a soft contact
  // shadow, edge bevel and a fixed highlight from above-left.
  void drawKnobCap(juce::Graphics& g, juce::Rectangle<float> cap) const;
  juce::Colour knobPointer() const;  // pointer line on small knobs (dark on Ivory / Gold)
  void drawFaderCap(juce::Graphics& g, juce::Rectangle<float> cap) const;
  // Console push button: a dark bezel with a cap of coloured translucent plastic. Off: the cap's colour, unlit;
  // on: lit from behind. `pressed` sinks it. Returns the cap's face (for the legend).
  juce::Rectangle<float> drawConsoleButton(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour cap, bool on, bool pressed,
                                           bool hover, bool enabled = true) const;
  // Bat-handle toggle switch in `r` (lever down = on) with a small LED in `led` colour above it.
  void drawToggleSwitch(juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour led, bool hover) const;

  static juce::Font uiFont(float h, bool bold = false);
  static juce::Font monoFont(float h, bool bold = true);
  static juce::Font spaced(juce::Font f, float kerning) { return f.withExtraKerningFactor(kerning); }

  static constexpr int kAccentHues[12] = {37, 50, 18, 4, 335, 300, 265, 215, 200, 175, 140, 85};
  static constexpr juce::uint32 kAccentSwatch[12] = {0xffffb347, 0xffffd84a, 0xffff8a3d, 0xffff6060, 0xffff7ab6, 0xffe060e0,
                                                    0xffb38cff, 0xff5a8bff, 0xff6ec6ff, 0xff4fe0c8, 0xff6dff9a, 0xffb5e84f};
  static constexpr const char* kAccentNames[12] = {"Amber", "Gold", "Orange", "Red", "Rose", "Magenta",
                                                   "Violet", "Blue", "Ice", "Teal", "Phosphor", "Lime"};

 private:
  const juce::Image& texture() const;  // faceplate texture tile for the current faceplate / panel
  mutable juce::Image tile_;
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
