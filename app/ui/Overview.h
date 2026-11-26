// Song overview (mockup #tl): elapsed LCD | summed waveform of the active takes with playhead and loop region | total LCD.
// Click seeks; Shift+drag sets a loop region; drag the region's edges to adjust it.
#pragma once
#include <vector>
#include "AppCtx.h"
#include "Widgets.h"

namespace ui {

class Overview : public juce::Component {
 public:
  explicit Overview(AppCtx& ctx);
  void sync();  // ~30x/s
  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseMove(const juce::MouseEvent&) override;
  double lengthSamples() const { return len_; }
 private:
  juce::Rectangle<float> bar() const;
  double sampleAt(float x) const;
  float xAt(double s) const;
  void rebuildWave(airdaw::Engine&);
  AppCtx& ctx_;
  Lcd elapsed_{"Elapsed", 15.f}, total_{"Total", 15.f};
  std::vector<float> wave_;          // peak per bucket, 0..1
  juce::String waveKey_;             // which takes the waveform was built from
  double len_ = 0.0, pos_ = 0.0, sr_ = 48000.0, notesKey_ = 0.0;
  juce::int64 loopIn_ = 0, loopOut_ = 0; bool loopOn_ = false;
  enum class Drag { None, Seek, NewLoop, LoopIn, LoopOut } drag_ = Drag::None;
  double dragAnchor_ = 0.0;
};

}  // namespace ui
