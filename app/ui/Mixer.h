// Mixer area (mockup #mixw): scrolling tracks + "add track" + buses, master fixed on the right.
#pragma once
#include <memory>
#include <vector>
#include "Strip.h"

namespace ui {

class Mixer : public juce::Component {
 public:
  explicit Mixer(AppCtx& ctx);
  void sync();  // ~30x/s
  void resized() override;
  void paint(juce::Graphics&) override;
  void paintOverChildren(juce::Graphics&) override;
  int tracksShown() const { return shown_; }
  MasterStrip& master() { return master_; }
 private:
  void relayout();
  AppCtx& ctx_;
  juce::Viewport view_;
  juce::Component content_;
  std::vector<std::unique_ptr<Strip>> tracks_, buses_;
  juce::TextButton add_;
  MasterStrip master_;
  int shown_ = 8;
};

}  // namespace ui
