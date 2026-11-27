// Song overview (mockup #tl): elapsed LCD | bar ruler over the summed waveform of the active takes with playhead and
// loop region | total LCD. Click seeks; Shift+drag sets a loop region; drag the region's edges to adjust it.
// The bar ruler shows the tempo map: double-click a bar to add a tempo / time-signature change there, click a marker
// to edit it, drag it to move it by whole bars, right-click it to delete it.
#pragma once
#include <vector>
#include "AppCtx.h"
#include "Widgets.h"

namespace ui {

class Overview : public juce::Component, public juce::TooltipClient {
 public:
  explicit Overview(AppCtx& ctx);
  void sync();  // ~30x/s
  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseMove(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;
  juce::String getTooltip() override;
  double lengthSamples() const { return len_; }
 private:
  juce::Rectangle<float> bar() const;
  juce::Rectangle<float> ruler() const;
  void paintRuler(juce::Graphics&);
  int barAtX(float x) const;           // 1-based bar under x
  int markerAt(juce::Point<float> p) const;  // the change (1-based bar) whose marker is under p, or -1
  void editTempo(int bar, juce::Rectangle<int> near);
  struct Marker { int bar; juce::Rectangle<float> r; };
  std::vector<Marker> markers_;
  const airdaw::TempoMap* mapKey_ = nullptr; float bpmKey_ = 0.f; int bpbKey_ = 0;
  int tempoDrag_ = -1; bool tempoMoved_ = false;
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
