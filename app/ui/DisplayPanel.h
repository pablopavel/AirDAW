// Display panel (mockup #set): Look (eras, classics), Color & glow, Surfaces, Hardware, Side trim, Type, Screen.
// Collapsible sections; every option applies at once and is saved (Theme).
#pragma once
#include "Theme.h"

namespace ui {

class DisplayPanel : public juce::Component {
 public:
  DisplayPanel();
  void paint(juce::Graphics&) override;
  void resized() override;
  void syncFromTheme();
  int preferredHeight() const;  // everything the open sections need (the caller caps it to the window)
  void setAllOpen(bool on) { for (size_t i = 0; i < sections_.size(); ++i) { sections_[i].open = on; heads_[i]->open = on; } resized(); }
  static constexpr int kWidth = 470;

  struct Option { juce::String value, label; };
  struct Row { juce::String label, key; std::vector<Option> opts; int kind = 0; };  // kind: 0 options, 1 presets, 2 swatches, 3 scale
  struct Section { juce::String title; bool open; std::vector<Row> rows; };

 private:
  struct Swatch : juce::Component, juce::SettableTooltipClient {
    int idx = 0; std::function<void(int)> onPick;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onPick) onPick(idx); }
  };
  struct SectionHead : juce::Component {
    juce::String title; bool open = true; std::function<void()> onToggle;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onToggle) onToggle(); }
  };
  struct Content : juce::Component {
    DisplayPanel* owner = nullptr;
    void paint(juce::Graphics&) override;
  };
  struct Btn { int section, row; juce::String key, value; std::unique_ptr<juce::TextButton> b; };
  int layoutContent(bool place);  // returns the content height
  void build();

  std::vector<Section> sections_;
  std::vector<std::unique_ptr<SectionHead>> heads_;
  std::vector<Btn> buttons_;
  std::array<Swatch, 12> swatches_;
  juce::Slider scale_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
  juce::Label scaleVal_;
  juce::TextButton reset_{"Reset"};
  juce::Viewport view_;
  Content content_;
  std::vector<std::tuple<juce::Rectangle<int>, juce::String>> labels_;  // row labels (content coordinates)
};

}  // namespace ui
