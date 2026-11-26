#include "DisplayPanel.h"

namespace ui {

static constexpr float kGlowLevels[5] = {0.f, 0.5f, 1.f, 1.6f, 2.4f};
static const char* kGlowNames[5] = {"Off", "Soft", "Normal", "Strong", "Max"};

void DisplayPanel::Swatch::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat().reduced(3.f);
  const bool sel = std::abs(t.hue - (float)Theme::kAccentHues[idx]) < 0.5f;
  if (sel) { g.setColour(juce::Colours::white); g.drawEllipse(r.expanded(2.f), 2.f); }
  g.setColour(juce::Colours::black.withAlpha(0.8f)); g.fillEllipse(r.translated(0, 2));
  g.setColour(juce::Colour(Theme::kAccentSwatch[idx])); g.fillEllipse(r);
  g.setColour(juce::Colour(0x40ffffff)); g.drawEllipse(r.reduced(0.5f), 1.f);
}

DisplayPanel::DisplayPanel() {
  addAndMakeVisible(reset_);
  reset_.setTooltip("Back to the Studio defaults (amber, normal glow, 100%)");
  reset_.onClick = [] { auto& t = T(); t.resetLook(); t.scalePct = 100; t.changed(); };
  for (int i = 0; i < 12; ++i) {
    auto& s = swatches_[(size_t)i]; s.idx = i; s.setTooltip(Theme::kAccentNames[i]);
    s.onPick = [](int k) { auto& t = T(); t.hue = (float)Theme::kAccentHues[k]; t.changed(); };
    addAndMakeVisible(s);
  }
  for (int i = 0; i < 5; ++i) {
    auto& b = glow_[(size_t)i]; b.setButtonText(kGlowNames[i]); b.getProperties().set("fontH", 11.0);
    b.onClick = [i] { auto& t = T(); t.glow = kGlowLevels[i]; t.changed(); };
    addAndMakeVisible(b);
  }
  scale_.setRange(100.0, 200.0, 25.0);
  scale_.setMouseDragSensitivity(600);  // low sensitivity, as the design asks
  scale_.onValueChange = [this] { scaleVal_.setText(juce::String((int)scale_.getValue()) + "%", juce::dontSendNotification); };
  scale_.onDragEnd = [this] { auto& t = T(); t.scalePct = (int)scale_.getValue(); t.changed(); };
  scale_.setTooltip("Scales the whole window. Applied when you let go of the slider.");
  addAndMakeVisible(scale_);
  scaleVal_.setJustificationType(juce::Justification::centredRight);
  scaleVal_.setFont(Theme::monoFont(12.f));
  addAndMakeVisible(scaleVal_);
  syncFromTheme();
  setSize(kWidth, kHeight);
}

void DisplayPanel::syncFromTheme() {
  auto& t = T();
  for (int i = 0; i < 5; ++i) glow_[(size_t)i].setToggleState(std::abs(t.glow - kGlowLevels[i]) < 0.01f, juce::dontSendNotification);
  scale_.setValue(t.scalePct, juce::dontSendNotification);
  scaleVal_.setText(juce::String(t.scalePct) + "%", juce::dontSendNotification);
  scaleVal_.setColour(juce::Label::textColourId, t.am());
  repaint();
}

void DisplayPanel::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  t.drawBrush(g, r);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
  auto in = getLocalBounds().reduced(16, 14);
  auto title = in.removeFromTop(28);
  g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(11.5f, true), 0.2f));
  g.drawText("DISPLAY", title, juce::Justification::centredLeft);
  auto section = [&](juce::Rectangle<int> row, const char* name) {
    g.setColour(juce::Colour(0x14ffffff)); g.fillRect(row.getX(), row.getY(), row.getWidth(), 1);
    g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(11.f, true), 0.18f));
    g.drawText(name, row.withHeight(26).withTrimmedTop(4), juce::Justification::centredLeft);
  };
  auto label = [&](int y, const char* s) {
    g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(11.5f), 0.08f));
    g.drawText(s, in.getX(), y, 76, 26, juce::Justification::centredLeft);
  };
  section(in.withY(46), "COLOR & GLOW");
  label(swatches_[0].getY() + 10, "Accent");
  label(glow_[0].getY(), "Glow");
  section(in.withY(scale_.getY() - 36), "SCREEN");
  label(scale_.getY(), "Scale");
  g.setColour(Theme::dim()); g.setFont(Theme::uiFont(11.f));
  g.drawFittedText("Eras, classic looks, surfaces, hardware, side trim and type arrive in a later phase.",
                   in.withTop(getHeight() - 44).withHeight(30), juce::Justification::centredLeft, 2);
}

void DisplayPanel::resized() {
  auto in = getLocalBounds().reduced(16, 14);
  reset_.setBounds(in.getRight() - 70, in.getY(), 70, 26);
  int y = 78; const int x0 = in.getX() + 80;
  for (int i = 0; i < 12; ++i) swatches_[(size_t)i].setBounds(x0 + (i % 6) * 34, y + (i / 6) * 32, 30, 30);
  y += 72;
  const int gw = (in.getRight() - x0 - 4 * 4) / 5;  // five equal buttons that always fit
  for (int i = 0; i < 5; ++i) glow_[(size_t)i].setBounds(x0 + i * (gw + 4), y, gw, 26);
  y += 74;
  scale_.setBounds(x0, y, in.getRight() - x0 - 56, 26);
  scaleVal_.setBounds(in.getRight() - 52, y, 52, 26);
}

}  // namespace ui
