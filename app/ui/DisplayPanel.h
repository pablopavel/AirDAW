// Display panel (mockup #set), phase 1: accent colour, glow, UI scale, reset.
// Eras / classic looks, surfaces, hardware, side trim and type follow in a later phase.
#pragma once
#include "Theme.h"

namespace ui {

class DisplayPanel : public juce::Component {
 public:
  DisplayPanel();
  void paint(juce::Graphics&) override;
  void resized() override;
  void syncFromTheme();
  static constexpr int kWidth = 400, kHeight = 300;
 private:
  struct Swatch : juce::Component, juce::SettableTooltipClient {
    int idx = 0; std::function<void(int)> onPick;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onPick) onPick(idx); }
  };
  juce::TextButton reset_{"Reset"};
  std::array<Swatch, 12> swatches_;
  std::array<juce::TextButton, 5> glow_;
  juce::Slider scale_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
  juce::Label scaleVal_;
};

}  // namespace ui
