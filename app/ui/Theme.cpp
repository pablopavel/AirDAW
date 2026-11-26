#include "Theme.h"

namespace ui {

Theme& Theme::get() { static Theme t; return t; }

// ---------------------------------------------------------------- options
namespace {
struct Opt { const char* key; juce::String Theme::*s; bool Theme::*b; };
const Opt kOpts[] = {
    {"panel", &Theme::panel, nullptr}, {"fp", &Theme::faceplate, nullptr}, {"tint", &Theme::tint, nullptr},
    {"rd", &Theme::corners, nullptr}, {"knob", &Theme::knobs, nullptr}, {"fd", &Theme::faders, nullptr},
    {"vu", &Theme::vuFace, nullptr}, {"lcd", &Theme::readouts, nullptr}, {"font", &Theme::typeface, nullptr},
    {"mat", &Theme::trimMat, nullptr}, {"ws", &Theme::species, nullptr}, {"gr", &Theme::grain, nullptr},
    {"ww", &Theme::trimWidth, nullptr}, {"scr", nullptr, &Theme::screws}, {"reels", nullptr, &Theme::reels},
    {"wood", nullptr, &Theme::trim}, {"shift", nullptr, &Theme::pixelShift},
};
// Mockup LD (the Studio look) and LOOKS: eras and classics.
const char* kStudio = "hue=37 glow=1 ws=walnut gr=fine fp=brushed rd=normal ww=normal panel=oled wood=1 tint=charcoal mat=wood "
                      "knob=chrome fd=chrome vu=cream lcd=accent font=modern scr=0 reels=1";
const std::pair<const char*, const char*> kPresets[] = {
    {"amber", ""},
    {"neon", "hue=300 glow=1.6 ws=ebony gr=satin fp=glass rd=round ww=thin"},
    {"ice", "hue=200 ws=ebony fp=carbon rd=sharp ww=thin"},
    {"lounge", "hue=22 glow=0.5 ws=rosewood gr=bold rd=round ww=wide panel=std"},
    {"vintage", "hue=50 glow=0.5 ws=oak gr=wave fp=matte ww=wide panel=std"},
    {"e50", "hue=45 glow=0.5 ws=oak gr=plank fp=hammer panel=std tint=hammer mat=rack knob=bake fd=black font=engraved scr=1"},
    {"e60", "hue=30 glow=0.5 ws=walnut gr=bold fp=brushed ww=wide panel=std font=geometric scr=1"},
    {"e70", "hue=15 ws=mahogany gr=bold fp=ribbed rd=sharp ww=wide panel=std tint=slate knob=bake fd=black lcd=red font=grotesque scr=1"},
    {"e80", "hue=140 glow=1.6 ws=ebony gr=satin fp=matte rd=sharp mat=rack knob=bake fd=black vu=white lcd=green font=grotesque reels=0"},
    {"e90", "hue=215 ws=ebony gr=satin fp=crinkle ww=thin panel=std tint=navy mat=chrome knob=bake fd=black vu=white lcd=cyan font=geometric reels=0"},
    {"e00", "hue=200 glow=1.6 ws=ebony gr=satin fp=glass rd=round ww=thin mat=chrome fd=black vu=white lcd=white reels=0"},
};
std::vector<std::pair<juce::String, juce::String>> presetPairs(const juce::String& id) {
  std::vector<std::pair<juce::String, juce::String>> v;
  const char* extra = nullptr;
  for (auto& p : kPresets) if (id == p.first) extra = p.second;
  if (!extra) return v;
  for (auto* src : {kStudio, extra})
    for (auto& tok : juce::StringArray::fromTokens(src, " ", ""))
      if (tok.containsChar('=')) {
        const auto k = tok.upToFirstOccurrenceOf("=", false, false), val = tok.fromFirstOccurrenceOf("=", false, false);
        bool found = false;
        for (auto& x : v) if (x.first == k) { x.second = val; found = true; }
        if (!found) v.push_back({k, val});
      }
  return v;
}
}  // namespace

juce::String Theme::option(const juce::String& key) const {
  if (key == "hue") return juce::String(juce::roundToInt(hue));
  if (key == "glow") return juce::String(glow, 1);
  for (auto& o : kOpts)
    if (key == o.key) return o.s ? this->*(o.s) : juce::String(this->*(o.b) ? "1" : "0");
  return {};
}

void Theme::setOption(const juce::String& key, const juce::String& v) {
  if (key == "hue") { hue = (float)v.getIntValue(); return; }
  if (key == "glow") { glow = juce::jlimit(0.f, 2.4f, v.getFloatValue()); return; }
  for (auto& o : kOpts)
    if (key == o.key) { if (o.s) this->*(o.s) = v; else this->*(o.b) = v.getIntValue() != 0; }
}

void Theme::applyPreset(const juce::String& id) { for (auto& [k, v] : presetPairs(id)) setOption(k, v); }

bool Theme::presetActive(const juce::String& id) const {
  auto v = presetPairs(id);
  if (v.empty()) return false;
  for (auto& [k, val] : v) {
    if (k == "glow") { if (std::abs(glow - val.getFloatValue()) > 0.01f) return false; }
    else if (option(k) != val) return false;
  }
  return true;
}

void Theme::resetLook() { applyPreset("amber"); scalePct = 100; pixelShift = true; }

void Theme::load(juce::PropertiesFile& p) {
  hue = (float)p.getDoubleValue("look.hue", 37.0);
  glow = juce::jlimit(0.f, 2.4f, (float)p.getDoubleValue("look.glow", 1.0));
  scalePct = juce::jlimit(100, 200, p.getIntValue("look.scale", 100));
  for (auto& o : kOpts) {
    const juce::String k = juce::String("look.") + o.key;
    if (p.containsKey(k)) setOption(o.key, p.getValue(k));
  }
}
void Theme::save(juce::PropertiesFile& p) const {
  p.setValue("look.hue", hue); p.setValue("look.glow", glow); p.setValue("look.scale", scalePct);
  for (auto& o : kOpts) p.setValue(juce::String("look.") + o.key, option(o.key));
  p.saveIfNeeded();
}

// ---------------------------------------------------------------- colour
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

// Palette: mockup :root (Standard) / html.oled, overridden by a tint (TINT: m1 m2 pb bg).
static const juce::uint32* tintColours(const juce::String& t) {
  static const juce::uint32 slate[4] = {0xff2a3038, 0xff1a1f26, 0xff10141a, 0xff0a0d12}, hammer[4] = {0xff2f3a35, 0xff1d2622, 0xff101613, 0xff0a0f0c},
                            olive[4] = {0xff3a3a28, 0xff25251a, 0xff14140d, 0xff0b0b07}, oxblood[4] = {0xff3d2428, 0xff26151a, 0xff150b0e, 0xff0c0608},
                            navy[4] = {0xff222c42, 0xff151c2e, 0xff0b101c, 0xff070a12}, bronze[4] = {0xff3a2e22, 0xff241b13, 0xff130e09, 0xff0a0705};
  if (t == "slate") return slate; if (t == "hammer") return hammer; if (t == "olive") return olive;
  if (t == "oxblood") return oxblood; if (t == "navy") return navy; if (t == "bronze") return bronze;
  return nullptr;
}
static juce::Colour pal(const Theme& t, int i) {
  if (auto* c = tintColours(t.tint)) return juce::Colour(c[i]);
  static const juce::uint32 oled[4] = {0xff1b1b1d, 0xff0b0b0c, 0xff050505, 0xff000000}, std_[4] = {0xff2c2c2f, 0xff1c1c1e, 0xff121213, 0xff0a0908};
  return juce::Colour(t.panel == "std" ? std_[i] : oled[i]);
}
juce::Colour Theme::m1() const { return pal(*this, 0); }
juce::Colour Theme::m2() const { return pal(*this, 1); }
juce::Colour Theme::pb() const { return pal(*this, 2); }
juce::Colour Theme::bg() const { return pal(*this, 3); }

juce::Colour Theme::lcdBg() const { return readouts == "accent" ? juce::Colour(kLcdBg) : juce::Colour(0xff07090a); }
juce::Colour Theme::readout() const {
  if (readouts == "red") return juce::Colour(0xffff4a3a);
  if (readouts == "green") return juce::Colour(0xff6dff9a);
  if (readouts == "cyan") return juce::Colour(0xff5ae6ff);
  if (readouts == "white") return juce::Colour(0xfff4f1e6);
  return am();
}
juce::Colour Theme::readoutCaption() const {
  if (readouts == "red") return juce::Colour(0xff7a2118);
  if (readouts == "green") return juce::Colour(0xff1f6b3a);
  if (readouts == "cyan") return juce::Colour(0xff1d6a7a);
  if (readouts == "white") return juce::Colour(0xff6b6a62);
  return amd();
}
juce::Colour Theme::vuFaceColour() const {
  if (vuFace == "amber") return juce::Colour(0xffffcf70);
  if (vuFace == "white") return juce::Colour(0xfff1efe8);
  if (vuFace == "mint") return juce::Colour(0xffcfe6d2);
  return cream();
}

float Theme::rad(float r) const {
  if (corners == "sharp") return juce::jmin(r, 1.f);
  if (corners == "round") return r <= 1.5f ? r : juce::jmin(14.f, r * 2.4f);
  return r;
}
int Theme::trimW() const { return !trim ? 0 : trimWidth == "thin" ? 16 : trimWidth == "wide" ? 52 : 30; }
int Theme::railH() const { return !trim ? 0 : trimWidth == "thin" ? 8 : trimWidth == "wide" ? 24 : 14; }

// ---------------------------------------------------------------- faceplate
// Texture tiles drawn once per faceplate (mockup body.fp-* backgrounds), laid over the palette gradient.
const juce::Image& Theme::texture() const {
  if (tile_.isValid() || faceplate == "matte" || faceplate == "glass") return tile_;
  auto make = [](int w, int h) { return juce::Image(juce::Image::ARGB, w, h, true); };
  juce::Random rng(42);
  if (faceplate == "ribbed") {
    tile_ = make(5, 4); juce::Graphics g(tile_);
    g.setColour(juce::Colour(0x50000000)); g.fillRect(0, 0, 1, 4);
    g.setColour(juce::Colour(0x10ffffff)); g.fillRect(1, 0, 1, 4);
  } else if (faceplate == "carbon") {
    tile_ = make(6, 6);
    for (int y = 0; y < 6; ++y) for (int x = 0; x < 6; ++x) {
      int a = 0;
      if ((x + y) % 6 < 2) a += 0x0a;
      if (((x - y) % 6 + 6) % 6 < 2) a += 0x08;
      if (a) tile_.setPixelAt(x, y, juce::Colour((juce::uint8)255, 255, 255, (juce::uint8)a));
    }
  } else if (faceplate == "hammer") {
    tile_ = make(72, 72); juce::Graphics g(tile_);
    for (int y = 0; y < 72; y += 8) for (int x = 0; x < 72; x += 8) {
      g.setColour(juce::Colour(0x14ffffff)); g.fillEllipse(x + 2.f * 0.25f * 8.f + rng.nextFloat() * 2.f - 1.f, y + 0.3f * 8.f + rng.nextFloat() * 2.f - 1.f, 3.f, 3.f);
    }
    for (int y = 4; y < 72; y += 9) for (int x = 4; x < 72; x += 9) {
      g.setColour(juce::Colour(0x40000000)); g.fillEllipse(x + rng.nextFloat() * 2.f, y + rng.nextFloat() * 2.f, 3.f, 3.f);
    }
    for (int y = 6; y < 72; y += 6) for (int x = 2; x < 72; x += 6) { g.setColour(juce::Colour(0x0cffffff)); g.fillEllipse((float)x, (float)y, 2.f, 2.f); }
  } else if (faceplate == "crinkle") {
    tile_ = make(84, 84); juce::Graphics g(tile_);
    for (int i = 0; i < 520; ++i) {
      const float x = rng.nextFloat() * 84.f, y = rng.nextFloat() * 84.f, r = 1.f + rng.nextFloat() * 2.f;
      g.setColour(juce::Colour(i % 2 ? 0x30000000 : 0x0bffffff)); g.drawEllipse(x - r, y - r, 2 * r, 2 * r, 1.f);
    }
  } else if (faceplate == "leather") {
    tile_ = make(4, 4); juce::Graphics g(tile_);
    g.setColour(juce::Colour(0x40000000)); g.fillEllipse(-1.f, -1.f, 2.f, 2.f); g.fillEllipse(3.f, -1.f, 2.f, 2.f);
    g.fillEllipse(-1.f, 3.f, 2.f, 2.f); g.fillEllipse(3.f, 3.f, 2.f, 2.f);
    g.setColour(juce::Colour(0x0dffffff)); g.fillEllipse(1.f, 1.f, 2.f, 2.f);
  } else if (faceplate == "perf") {
    tile_ = make(9, 9); juce::Graphics g(tile_);
    g.setColour(juce::Colour(0x10ffffff)); g.fillEllipse(4.5f - 2.2f, 4.5f - 2.2f, 4.4f, 4.4f);
    g.setColour(juce::Colours::black); g.fillEllipse(4.5f - 1.5f, 4.5f - 1.5f, 3.f, 3.f);
  } else {  // brushed: fine vertical hairlines
    tile_ = make(3, 4); juce::Graphics g(tile_);
    g.setColour(juce::Colour(panel == "std" ? 0x05ffffff : 0x04ffffff)); g.fillRect(0, 0, 1, 4);
  }
  return tile_;
}

void Theme::drawBrush(juce::Graphics& g, juce::Rectangle<float> r, bool bus) const {
  auto a = m1(), b = m2();
  if (bus) { a = a.interpolatedWith(juce::Colour(0xff2f2a20), 0.45f); b = b.interpolatedWith(juce::Colour(0xff1a1813), 0.45f); }
  g.setGradientFill(juce::ColourGradient(a, r.getX(), r.getY(), b, r.getX(), r.getBottom(), false));
  g.fillRect(r);
  if (faceplate == "glass") {
    juce::ColourGradient gl(juce::Colour(0x22ffffff), r.getX(), r.getY(), juce::Colour(0x30000000), r.getX(), r.getBottom(), false);
    gl.addColour(0.42, juce::Colour(0x06ffffff));
    g.setGradientFill(gl); g.fillRect(r);
    g.setColour(juce::Colour(0x40ffffff)); g.fillRect(r.getX(), r.getY(), r.getWidth(), 1.f);
    g.setColour(juce::Colours::black); g.fillRect(r.getX(), r.getBottom() - 1.f, r.getWidth(), 1.f);
    return;
  }
  if (auto& tex = texture(); tex.isValid()) {
    g.setTiledImageFill(tex, (int)r.getX(), (int)r.getY(), 1.f);
    g.fillRect(r);
  }
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
  corner = rad(corner);
  g.setColour(fill); g.fillRoundedRectangle(r, corner);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0x99000000), r.getX(), r.getY(), juce::Colours::transparentBlack,
                                         r.getX(), r.getY() + 5.f, false));
  g.fillRoundedRectangle(r, corner);
  g.setColour(juce::Colour(0x14ffffff)); g.drawHorizontalLine((int)r.getBottom(), r.getX() + corner, r.getRight() - corner);
}

// ---------------------------------------------------------------- side trim, screws
void Theme::drawTrim(juce::Graphics& g, juce::Rectangle<int> ri, bool vertical, bool leftSide) const {
  if (ri.isEmpty()) return;
  const auto r = ri.toFloat();
  auto across = [&](juce::Colour c0, std::initializer_list<std::pair<float, juce::Colour>> stops, juce::Colour c1) {
    juce::ColourGradient cg(c0, r.getX(), r.getY(), c1, vertical ? r.getRight() : r.getX(), vertical ? r.getY() : r.getBottom(), false);
    for (auto& [p, c] : stops) cg.addColour(p, c);
    g.setGradientFill(cg); g.fillRect(r);
  };
  juce::Graphics::ScopedSaveState ss(g);
  g.reduceClipRegion(ri);
  if (trimMat == "rack") {
    if (vertical) {
      across(juce::Colour(0xff9d9da2), {{0.35f, juce::Colour(0xffc9c9ce)}, {0.65f, juce::Colour(0xff8c8c91)}}, juce::Colour(0xffb3b3b8));
      for (float y = r.getY() + 22.f; y < r.getBottom(); y += 64.f) {  // rack-ear slots
        juce::Rectangle<float> s(r.getCentreX() - 5.f, y - 11.f, 10.f, 22.f);
        g.setColour(juce::Colour(0x44ffffff)); g.fillEllipse(s.expanded(1.f));
        g.setColour(juce::Colours::black); g.fillEllipse(s);
      }
    } else across(juce::Colour(0xffc9c9ce), {}, juce::Colour(0xff8c8c91));
  } else if (trimMat == "tolex") {
    across(juce::Colour(0xff0b0b0b), {{0.5f, juce::Colour(0xff1b1b1b)}}, juce::Colour(0xff0e0e0e));
    for (int y = ri.getY(); y < ri.getBottom(); y += 5)
      for (int x = ri.getX(); x < ri.getRight(); x += 5) {
        g.setColour(juce::Colour(0x10ffffff)); g.fillRect(x, y, 1, 1);
        g.setColour(juce::Colour(0x70000000)); g.fillRect(x + 2, y + 2, 1, 1);
      }
  } else if (trimMat == "chrome") {
    if (vertical) across(juce::Colour(0xff4a4a4f), {{0.28f, juce::Colour(0xffe8e8ec)}, {0.52f, juce::Colour(0xff6c6c71)}, {0.78f, juce::Colour(0xffd4d4d9)}}, juce::Colour(0xff46464b));
    else across(juce::Colour(0xffe8e8ec), {{0.5f, juce::Colour(0xff6c6c71)}}, juce::Colour(0xffc8c8cd));
  } else {  // wood: species colours + grain (lines run along the piece)
    static const std::pair<const char*, std::array<juce::uint32, 3>> kWood[] = {
        {"walnut", {0xff2b170b, 0xff4d2e19, 0xff35200f}}, {"oak", {0xff5a4021, 0xff8a6a3a, 0xff6b5028}},
        {"mahogany", {0xff3a0f0a, 0xff6b2517, 0xff4a170e}}, {"ebony", {0xff0d0b0a, 0xff2e2825, 0xff171412}},
        {"maple", {0xff6b5030, 0xffb89560, 0xff8d6f42}}, {"cherry", {0xff3a140a, 0xff7a3520, 0xff52230f}},
        {"rosewood", {0xff1d0c0a, 0xff4a2118, 0xff2b130d}}, {"birch", {0xff7a6544, 0xffc4ac7c, 0xff9b8458}}};
    std::array<juce::uint32, 3> w = kWood[0].second;
    for (auto& [n, c] : kWood) if (species == n) w = c;
    if (vertical) across(juce::Colour(w[0]), {{0.45f, juce::Colour(w[1])}}, juce::Colour(w[2]));
    else across(juce::Colour(w[1]), {}, juce::Colour(w[0]));
    const float W = vertical ? r.getWidth() : r.getHeight(), L = vertical ? r.getHeight() : r.getWidth();
    auto line = [&](float p, float thick, juce::uint32 col) {  // a grain line at `p` across the piece
      g.setColour(juce::Colour(col));
      if (vertical) g.fillRect(r.getX() + p, r.getY(), thick, L); else g.fillRect(r.getX(), r.getY() + p, L, thick);
    };
    auto fine = [&](float step) { for (float p = 0; p < W; p += step) { line(p, 1.f, 0x1c000000); if (p + 5.f < W) line(p + 5.f, 1.f, 0x08ffffff); } };
    auto rings = [&](float fx, float fy, float step, juce::uint32 col) {
      const juce::Point<float> c(r.getX() + r.getWidth() * fx, r.getY() + r.getHeight() * fy);
      const float maxR = std::hypot(r.getWidth(), r.getHeight());
      g.setColour(juce::Colour(col));
      for (float k = step; k < maxR; k += step) g.drawEllipse(c.x - k * 0.6f, c.y - k, k * 1.2f, k * 2.f, 2.f);
    };
    juce::Random rng(vertical ? (leftSide ? 7 : 11) : 13);
    if (grain == "bold") {
      for (float p = 0; p < W; p += 17.f + rng.nextFloat() * 8.f) { line(p, 2.f, 0x40000000); if (p + 9.f < W) line(p + 9.f, 2.f, 0x0cffffff); }
      for (float p = 3.f; p < W; p += 37.f) line(p, 4.f, 0x50000000);
    } else if (grain == "burl") {
      rings(0.3f, 0.35f, 9.f, 0x34000000); rings(0.7f, 0.75f, 12.f, 0x28000000); fine(6.f);
    } else if (grain == "satin") {
      for (float p = 0; p < W; p += 3.f) line(p, 1.f, 0x06ffffff);
    } else if (grain == "wave") {
      rings(0.2f, 0.5f, 14.f, 0x2e000000); fine(7.f);
    } else if (grain == "plank") {
      fine(11.f);
      g.setColour(juce::Colour(0xaa000000));
      for (float q = 70.f; q < L; q += 70.f) { if (vertical) g.fillRect(r.getX(), r.getY() + q, r.getWidth(), 2.f); else g.fillRect(r.getX() + q, r.getY(), 2.f, r.getHeight()); }
    } else {  // fine
      for (float p = 0; p < W; p += 2.f + (float)rng.nextInt(4)) line(p, 1.f, rng.nextBool() ? 0x1c000000 : 0x08ffffff);
      for (float p = 4.f; p < W; p += 23.f) line(p, 2.f, 0x30000000);
    }
  }
  // inner shadow towards the panel
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xaa000000), vertical ? (leftSide ? r.getRight() : r.getX()) : 0.f,
                                         vertical ? 0.f : r.getY(), juce::Colours::transparentBlack,
                                         vertical ? (leftSide ? r.getRight() - 8.f : r.getX() + 8.f) : 0.f,
                                         vertical ? 0.f : r.getY() + 4.f, false));
  g.fillRect(r);
}

void Theme::drawScrew(juce::Graphics& g, juce::Point<float> c) const {
  juce::Rectangle<float> s(c.x - 5.f, c.y - 5.f, 10.f, 10.f);
  g.setColour(juce::Colour(0xcc000000)); g.fillEllipse(s.translated(0.f, 1.f));
  juce::ColourGradient cg(juce::Colour(0xffd8d8dc), s.getX() + 3.5f, s.getY() + 3.f, juce::Colour(0xff2c2c30), s.getRight(), s.getBottom(), true);
  cg.addColour(0.6, juce::Colour(0xff6a6a70));
  g.setGradientFill(cg); g.fillEllipse(s);
  g.setColour(juce::Colour(0xbb000000)); g.drawLine(s.getX() + 2.f, s.getBottom() - 2.f, s.getRight() - 2.f, s.getY() + 2.f, 1.3f);
  g.setColour(juce::Colour(0x77000000)); g.drawEllipse(s, 1.f);
}

// ---------------------------------------------------------------- knobs, fader caps
bool Theme::drawKnobCap(juce::Graphics& g, juce::Rectangle<float> k) const {
  const float hx = k.getX() + k.getWidth() * 0.35f, hy = k.getY() + k.getHeight() * 0.3f;
  if (knobs == "bake") {
    juce::ColourGradient cg(juce::Colour(0xff3b3b3d), hx, hy, juce::Colour(0xff0c0c0d), k.getRight(), k.getBottom(), true);
    g.setGradientFill(cg); g.fillEllipse(k);
  } else if (knobs == "ivory") {
    juce::ColourGradient cg(juce::Colour(0xfffff6dd), hx, hy, juce::Colour(0xffa89a72), k.getRight(), k.getBottom(), true);
    cg.addColour(0.62, juce::Colour(0xffd6c8a0));
    g.setGradientFill(cg); g.fillEllipse(k);
  } else if (knobs == "gold") {  // mockup conic gradient, drawn as thin wedges
    static const juce::uint32 stops[] = {0xff6b4b12, 0xffe6c36a, 0xff7a5718, 0xffd9b45a, 0xff7a5718, 0xffe6c36a, 0xff6b4b12};
    const auto c = k.getCentre(); const float R = k.getWidth() / 2.f; const int N = 72;
    for (int i = 0; i < N; ++i) {
      const float f = (float)i / N, pos = f * 6.f; const int a = juce::jmin(5, (int)pos);
      g.setColour(juce::Colour(stops[a]).interpolatedWith(juce::Colour(stops[a + 1]), pos - (float)a));
      juce::Path p; const float a0 = juce::MathConstants<float>::twoPi * f + 0.52f, a1 = a0 + juce::MathConstants<float>::twoPi / N + 0.01f;
      p.addPieSegment(c.x - R, c.y - R, 2 * R, 2 * R, a0, a1, 0.f);
      g.fillPath(p);
    }
  } else return false;
  g.setColour(juce::Colour(0x33ffffff)); g.drawEllipse(k.reduced(0.5f), 0.7f);
  return true;
}

juce::Colour Theme::knobPointer() const {
  if (knobs == "ivory") return juce::Colour(0xff2a1a08);
  if (knobs == "gold") return juce::Colour(0xff1a1004);
  return cream();
}

void Theme::drawFaderCap(juce::Graphics& g, juce::Rectangle<float> cap) const {
  g.setColour(juce::Colours::black.withAlpha(0.85f)); g.fillRoundedRectangle(cap.translated(0.f, 6.f), 3.f);
  juce::uint32 side = 0xff161616, mid = 0xff5e5e60, top = 0xffcccccc;
  juce::Colour stripe = am();
  if (faders == "black") { side = 0xff0c0c0d; mid = 0xff2c2c2f; top = 0xff555555; stripe = juce::Colour(0xfff2f2f2); }
  else if (faders == "cream") { side = 0xffb8aa84; mid = 0xfff1e6c3; top = 0xffffffff; stripe = juce::Colour(0xff3a2a10); }
  juce::ColourGradient cg(juce::Colour(side), cap.getX(), 0, juce::Colour(side), cap.getRight(), 0, false);
  cg.addColour(0.45, juce::Colour(mid));
  g.setGradientFill(cg); g.fillRoundedRectangle(cap, 3.f);
  g.setColour(juce::Colour(top)); g.fillRect(cap.getX() + 1.f, cap.getY(), cap.getWidth() - 2.f, 2.f);
  g.setColour(stripe); g.fillRect(cap.getX(), cap.getCentreY() - 1.75f, cap.getWidth(), 3.5f);
  g.setColour(juce::Colour(0x88000000)); g.drawRoundedRectangle(cap, 3.f, 1.f);
}

// ---------------------------------------------------------------- type
juce::Font Theme::uiFont(float h, bool bold) {
  const auto& t = get().typeface;
  const char* name = t == "grotesque" ? "Arial" : t == "geometric" ? "Century Gothic" : t == "typewriter" ? "Courier New"
                   : t == "engraved" ? "Georgia" : "Segoe UI";
  return juce::Font(juce::FontOptions(name, h, bold ? juce::Font::bold : juce::Font::plain));
}
juce::Font Theme::monoFont(float h, bool bold) {
  return juce::Font(juce::FontOptions("Consolas", h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::String dbText(float db, int decimals) {
  if (!(db > -200.f)) return juce::String(juce::CharPointer_UTF8("-\xe2\x88\x9e"));  // -inf
  return (db > 0.f ? "+" : "") + juce::String(db, decimals);
}

}  // namespace ui
