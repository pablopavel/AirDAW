// Top deck (mockup #deck): logo + reels + position LCD | transport keys | readouts + buttons.
#pragma once
#include "AppCtx.h"
#include "Widgets.h"

namespace ui {

// The five tape-style transport keys (mockup .tp).
class TransportKeys : public juce::Component {
 public:
  enum Key { Rtz, Play, Stop, Rec, Loop, Click, kNum };
  std::function<void(Key)> onKey;
  void setLit(Key k, bool on) { if (lit_[k] != on) { lit_[k] = on; repaint(); } }
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  juce::Rectangle<float> keyRect(int k) const;
  static constexpr int kKeyW = 70, kKeyH = 58, kGap = 3;
 private:
  bool lit_[kNum] = {}; int down_ = -1;
};

class Deck : public juce::Component {
 public:
  explicit Deck(AppCtx& ctx);
  void sync();  // ~30x/s
  void paint(juce::Graphics&) override;
  void resized() override;
  int preferredHeight(int width) const;

  std::function<void(TransportKeys::Key)> onTransport;
  std::function<void()> onProject, onAudio, onDisplay, onMeters, onFull, onListen;
  void setListenLit(bool on) { listen_.setToggleState(on, juce::dontSendNotification); }
  juce::TextButton displayBtn{juce::String(juce::CharPointer_UTF8("\xe2\x9a\x99 Display"))};
  juce::TextButton metersBtn{juce::String(juce::CharPointer_UTF8("\xe2\x97\x94 Meters"))};
  juce::TextButton projectBtn{juce::String(juce::CharPointer_UTF8("Project \xe2\x96\xbe"))};
 private:
  AppCtx& ctx_;
  Reels reels_;
  Lcd pos_{"Position", 40.f}, rate_{"Rate / depth", 15.f}, lat_{"Latency", 15.f}, dsp_{"DSP", 15.f};
  TransportKeys keys_;
  juce::TextButton listen_{juce::String(juce::CharPointer_UTF8("\xe2\x97\x89 Listen"))}, midi_{"MIDI Learn"},
      plugins_{juce::String(juce::CharPointer_UTF8("\xe2\x9f\xb3 Plugins"))}, full_{juce::String(juce::CharPointer_UTF8("\xe2\x9b\xb6 Full"))};
  std::vector<juce::TextButton*> buttons_;
};

}  // namespace ui
