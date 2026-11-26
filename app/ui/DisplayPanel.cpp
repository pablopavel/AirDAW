#include "DisplayPanel.h"

namespace ui {

static constexpr int kPad = 14, kLabelW = 86, kRowH = 24, kGap = 4, kHeadH = 30, kTitleH = 40;

void DisplayPanel::Swatch::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat().reduced(3.f);
  const bool sel = std::abs(t.hue - (float)Theme::kAccentHues[idx]) < 0.5f;
  if (sel) { g.setColour(juce::Colours::white); g.drawEllipse(r.expanded(2.f), 2.f); }
  g.setColour(juce::Colours::black.withAlpha(0.8f)); g.fillEllipse(r.translated(0, 2));
  g.setColour(juce::Colour(Theme::kAccentSwatch[idx])); g.fillEllipse(r);
  g.setColour(juce::Colour(0x40ffffff)); g.drawEllipse(r.reduced(0.5f), 1.f);
}

void DisplayPanel::SectionHead::paint(juce::Graphics& g) {
  auto r = getLocalBounds();
  g.setColour(juce::Colour(0x14ffffff)); g.fillRect(r.getX(), r.getY(), r.getWidth(), 1);
  g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(11.f, true), 0.18f));
  g.drawText(juce::String(juce::CharPointer_UTF8(open ? "\xe2\x96\xbe  " : "\xe2\x96\xb8  ")) + title.toUpperCase(), r.withTrimmedTop(2),
             juce::Justification::centredLeft);
}

void DisplayPanel::Content::paint(juce::Graphics& g) {
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(11.5f), 0.06f));
  for (auto& [r, s] : owner->labels_) g.drawText(s, r, juce::Justification::centredLeft, true);
}

DisplayPanel::DisplayPanel() {
  using O = std::vector<Option>;
  const O onOff = {{"1", "On"}, {"0", "Off"}};
  sections_ = {
      {"Look", true, {{"Eras", "", O{{"e50", "50s Broadcast"}, {"e60", "60s Reel Deck"}, {"e70", "70s Console"}, {"e80", "80s Rack"}, {"e90", "90s Digital"}, {"e00", "2000s Glass"}}, 1},
                      {"Classics", "", O{{"amber", "Studio"}, {"neon", "Neon"}, {"ice", "Ice"}, {"lounge", "Lounge"}, {"vintage", "Vintage"}}, 1}}},
      {"Color & glow", true, {{"Accent", "", {}, 2},
                              {"Glow", "glow", O{{"0", "Off"}, {"0.5", "Soft"}, {"1", "Normal"}, {"1.6", "Strong"}, {"2.4", "Max"}}},
                              {"Readouts", "lcd", O{{"accent", "Accent"}, {"red", "Red LED"}, {"green", "Green"}, {"cyan", "VFD cyan"}, {"white", "White"}}}}},
      {"Surfaces", false, {{"Faceplate", "fp", O{{"brushed", "Brushed"}, {"matte", "Matte"}, {"glass", "Glass"}, {"carbon", "Carbon"}, {"hammer", "Hammertone"},
                                                {"ribbed", "Ribbed"}, {"crinkle", "Crinkle"}, {"leather", "Leather"}, {"perf", "Perforated"}}},
                           {"Tint", "tint", O{{"charcoal", "Charcoal"}, {"slate", "Slate"}, {"hammer", "Green"}, {"olive", "Olive"}, {"oxblood", "Oxblood"}, {"navy", "Navy"}, {"bronze", "Bronze"}}},
                           {"Corners", "rd", O{{"sharp", "Sharp"}, {"normal", "Normal"}, {"round", "Round"}}},
                           {"Screws", "scr", onOff}}},
      {"Hardware", false, {{"Knobs", "knob", O{{"chrome", "Chrome"}, {"bake", "Bakelite"}, {"ivory", "Ivory"}, {"gold", "Gold"}}},
                           {"Faders", "fd", O{{"chrome", "Chrome"}, {"black", "Black"}, {"cream", "Cream"}}},
                           {"VU face", "vu", O{{"cream", "Cream"}, {"amber", "Amber"}, {"white", "White"}, {"mint", "Mint"}}},
                           {"Reels", "reels", O{{"1", "Show"}, {"0", "Hide"}}}}},
      {"Side trim", false, {{"Trim", "wood", onOff},
                            {"Material", "mat", O{{"wood", "Wood"}, {"rack", "Rack ears"}, {"tolex", "Tolex"}, {"chrome", "Chrome"}}},
                            {"Species", "ws", O{{"walnut", "Walnut"}, {"oak", "Oak"}, {"mahogany", "Mahog."}, {"ebony", "Ebony"}, {"maple", "Maple"}, {"cherry", "Cherry"}, {"rosewood", "Rosewood"}, {"birch", "Birch"}}},
                            {"Grain", "gr", O{{"fine", "Fine"}, {"bold", "Bold"}, {"burl", "Burl"}, {"satin", "Satin"}, {"wave", "Wave"}, {"plank", "Plank"}}},
                            {"Width", "ww", O{{"thin", "Thin"}, {"normal", "Normal"}, {"wide", "Wide"}}}}},
      {"Type", false, {{"Typeface", "font", O{{"modern", "Modern"}, {"grotesque", "Grotesque"}, {"geometric", "Geometric"}, {"typewriter", "Typewriter"}, {"engraved", "Engraved"}}}}},
      {"Screen", false, {{"Panel", "panel", O{{"oled", "OLED"}, {"std", "Standard"}}}, {"Scale", "", {}, 3}, {"Pixel shift", "shift", onOff}}},
  };
  content_.owner = this;
  addAndMakeVisible(view_);
  view_.setViewedComponent(&content_, false);
  view_.setScrollBarsShown(true, false);
  view_.setScrollBarThickness(8);
  addAndMakeVisible(reset_);
  reset_.setTooltip("Back to the Studio look (amber, normal glow, 100%)");
  reset_.onClick = [] { auto& t = T(); t.resetLook(); t.changed(); };
  for (int i = 0; i < 12; ++i) {
    auto& s = swatches_[(size_t)i]; s.idx = i; s.setTooltip(Theme::kAccentNames[i]);
    s.onPick = [](int k) { auto& t = T(); t.hue = (float)Theme::kAccentHues[k]; t.changed(); };
    content_.addAndMakeVisible(s);
  }
  scale_.setRange(100.0, 200.0, 25.0);
  scale_.setMouseDragSensitivity(600);  // low sensitivity, as the design asks
  scale_.onValueChange = [this] { scaleVal_.setText(juce::String((int)scale_.getValue()) + "%", juce::dontSendNotification); };
  scale_.onDragEnd = [this] { auto& t = T(); t.scalePct = (int)scale_.getValue(); t.changed(); };
  scale_.setTooltip("Scales the whole window. Applied when you let go of the slider.");
  content_.addAndMakeVisible(scale_);
  scaleVal_.setJustificationType(juce::Justification::centredRight);
  scaleVal_.setFont(Theme::monoFont(12.f));
  content_.addAndMakeVisible(scaleVal_);
  build();
  syncFromTheme();
  setSize(kWidth, preferredHeight());
}

void DisplayPanel::build() {
  heads_.clear(); buttons_.clear();
  for (int si = 0; si < (int)sections_.size(); ++si) {
    auto h = std::make_unique<SectionHead>();
    h->title = sections_[(size_t)si].title; h->open = sections_[(size_t)si].open;
    h->onToggle = [this, si] { sections_[(size_t)si].open = !sections_[(size_t)si].open; heads_[(size_t)si]->open = sections_[(size_t)si].open;
                               heads_[(size_t)si]->repaint(); resized(); if (auto* p = getParentComponent()) p->resized(); };
    content_.addAndMakeVisible(*h); heads_.push_back(std::move(h));
    for (int ri = 0; ri < (int)sections_[(size_t)si].rows.size(); ++ri) {
      const auto& row = sections_[(size_t)si].rows[(size_t)ri];
      for (auto& o : row.opts) {
        Btn b{si, ri, row.key, o.value, std::make_unique<juce::TextButton>(o.label)};
        b.b->getProperties().set("noUpper", true); b.b->getProperties().set("fontH", 11.5);
        const juce::String key = row.key, val = o.value; const bool preset = row.kind == 1;
        b.b->onClick = [key, val, preset] {
          auto& t = T();
          if (preset) t.applyPreset(val); else t.setOption(key, val);
          t.changed();
        };
        content_.addChildComponent(*b.b);
        buttons_.push_back(std::move(b));
      }
    }
  }
}

void DisplayPanel::syncFromTheme() {
  auto& t = T();
  for (auto& b : buttons_) {
    bool on;
    if (b.key.isEmpty()) on = t.presetActive(b.value);
    else if (b.key == "glow") on = std::abs(t.glow - b.value.getFloatValue()) < 0.01f;
    else on = t.option(b.key) == b.value;
    b.b->setToggleState(on, juce::dontSendNotification);
  }
  for (auto& s : swatches_) s.repaint();
  scale_.setValue(t.scalePct, juce::dontSendNotification);
  scaleVal_.setText(juce::String(t.scalePct) + "%", juce::dontSendNotification);
  scaleVal_.setColour(juce::Label::textColourId, t.am());
  repaint();
}

// Lays the sections out in content coordinates (option buttons wrap); returns the height.
int DisplayPanel::layoutContent(bool place) {
  const int w = kWidth - 2 * kPad - 10;
  int y = 0;
  if (place) labels_.clear();
  for (auto& b : buttons_) if (place) b.b->setVisible(false);
  if (place) { for (auto& s : swatches_) s.setVisible(false); scale_.setVisible(false); scaleVal_.setVisible(false); }
  for (int si = 0; si < (int)sections_.size(); ++si) {
    const auto& sec = sections_[(size_t)si];
    if (place) heads_[(size_t)si]->setBounds(0, y, w, kHeadH);
    y += kHeadH;
    if (!sec.open) continue;
    for (int ri = 0; ri < (int)sec.rows.size(); ++ri) {
      const auto& row = sec.rows[(size_t)ri];
      const int x0 = kLabelW, rowY = y;
      int rowH = kRowH;
      if (row.kind == 2) {  // accent swatches: two rows of six
        for (int i = 0; i < 12; ++i)
          if (place) { swatches_[(size_t)i].setBounds(x0 + (i % 6) * 32, y + (i / 6) * 30, 28, 28); swatches_[(size_t)i].setVisible(true); }
        rowH = 60;
      } else if (row.kind == 3) {
        if (place) {
          scale_.setBounds(x0, y, w - x0 - 56, kRowH); scaleVal_.setBounds(w - 52, y, 52, kRowH);
          scale_.setVisible(true); scaleVal_.setVisible(true);
        }
      } else {
        int x = x0, lineY = y;
        for (auto& b : buttons_) {
          if (b.section != si || b.row != ri) continue;
          const int bw = juce::GlyphArrangement::getStringWidthInt(Theme::uiFont(11.5f), b.b->getButtonText()) + 18;
          if (x > x0 && x + bw > w) { x = x0; lineY += kRowH + kGap; }
          if (place) { b.b->setBounds(x, lineY, bw, kRowH); b.b->setVisible(true); }
          x += bw + kGap;
        }
        rowH = lineY - y + kRowH;
      }
      if (place) labels_.push_back({juce::Rectangle<int>(0, rowY, kLabelW - 6, kRowH), row.label});
      y += rowH + 8;
    }
    y += 4;
  }
  return y + 6;
}

int DisplayPanel::preferredHeight() const { return kTitleH + 12 + const_cast<DisplayPanel*>(this)->layoutContent(false) + kPad; }

void DisplayPanel::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  t.drawBrush(g, r);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), t.rad(6.f), 1.f);
  g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(11.5f, true), 0.2f));
  g.drawText("DISPLAY", getLocalBounds().reduced(kPad, 0).withHeight(kTitleH + 6), juce::Justification::centredLeft);
}

void DisplayPanel::resized() {
  auto in = getLocalBounds().reduced(kPad, 0);
  reset_.setBounds(in.getRight() - 70, 12, 70, 26);
  view_.setBounds(in.withTrimmedTop(kTitleH + 12).withTrimmedBottom(kPad));
  const int h = layoutContent(true);
  content_.setSize(kWidth - 2 * kPad - 10, h);
  content_.repaint();
}

}  // namespace ui
