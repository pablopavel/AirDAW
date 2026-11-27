// Top deck (mockup #deck): logo + reels + position LCD | transport keys | readouts + buttons.
#pragma once
#include "AppCtx.h"
#include "Widgets.h"
#include "../midi/MidiControl.h"

namespace ui {

// The five tape-style transport keys (mockup .tp).
class TransportKeys : public juce::Component, public MidiRegionProvider {
 public:
  std::vector<std::pair<juce::String, juce::Rectangle<int>>> midiRegions() const override;
  enum Key { Rtz, Play, Stop, Rec, Loop, Click, kNum };
  std::function<void(Key)> onKey;
  void setLit(Key k, bool on) { if (lit_[k] != on) { lit_[k] = on; repaint(); } }
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  juce::Rectangle<float> keyRect(int k) const;
  juce::Rectangle<float> wellRect() const { return keyRect(0).getUnion(keyRect(kNum - 1)).expanded(10.f, 8.f).withTrimmedBottom(-2.f); }
  // Backlit keys: one even, dim light under the keys (faint in the gaps and under each key); most of it shines
  // through the frosted legends. Level and colour follow the transport (set by the deck each sync).
  void setBacklight(float level, juce::Colour c) {
    if (std::abs(level - lightLvl_) > 0.006f || c != lightCol_) { lightLvl_ = level; lightCol_ = c; repaint(); }
  }
  static constexpr int kKeyW = 70, kKeyH = 58, kGap = 3;
 private:
  bool lit_[kNum] = {}; int down_ = -1;
  float lightLvl_ = 0.32f; juce::Colour lightCol_{0xffffb347};
  static juce::Path loopSymbol(juce::Point<float> c);
  static juce::Path clickSymbol(juce::Point<float> c);
  static void drawFrostedLegend(juce::Graphics& g, const juce::Path& p, juce::Colour lc, float L);
};

class Deck : public juce::Component {
 public:
  explicit Deck(AppCtx& ctx);
  void sync();  // ~30x/s
  void paint(juce::Graphics&) override;
  void resized() override;
  int preferredHeight(int width) const;

  std::function<void(TransportKeys::Key)> onTransport;
  std::function<void()> onProject, onAudio, onDisplay, onMeters, onFull, onListen, onMidi, onPlugins;
  void setMidiState(bool learning, bool armed);
  void setPluginsBadge(bool on) { if (pluginsBadge_ != on) { pluginsBadge_ = on; repaint(); } }
  juce::Component& midiButton() { return midi_; }
  juce::Component& pluginsButton() { return plugins_; }
  void setListenLit(bool on) { listen_.setToggleState(on, juce::dontSendNotification); }
  void previewAmbient(int state) { ambPreview_ = state; }  // test hook: transport light as if 1 = playing, 2 = recording
  juce::TextButton displayBtn{juce::String(juce::CharPointer_UTF8("\xe2\x9a\x99 Display"))};
  juce::TextButton metersBtn{juce::String(juce::CharPointer_UTF8("\xe2\x97\x94 Meters"))};
  juce::TextButton projectBtn{juce::String(juce::CharPointer_UTF8("Project \xe2\x96\xbe"))};
 private:
  AppCtx& ctx_;
  Reels reels_;
  Lcd pos_{"Position", 40.f}, rate_{"Rate / depth", 15.f}, lat_{"Latency", 15.f}, dsp_{"DSP", 15.f};
  TransportKeys keys_;
  juce::TextButton tap_;
  juce::TextButton listen_{juce::String(juce::CharPointer_UTF8("\xe2\x97\x89 Listen"))}, midi_{"MIDI Learn"},
      plugins_{juce::String(juce::CharPointer_UTF8("\xe2\x9f\xb3 Plugins"))}, full_{juce::String(juce::CharPointer_UTF8("\xe2\x9b\xb6 Full"))};
  std::vector<juce::TextButton*> buttons_;
  bool pluginsBadge_ = false;
  void paintOverChildren(juce::Graphics&) override;
  // The light under the transport keys: dim amber when stopped, warmer and breathing while playing, a red pulse
  // while recording. Eased each sync() so changes fade instead of snapping.
  int ambPreview_ = 0;
  float amb_ = 0.35f, ambRed_ = 0.f, ambPhase_ = 0.f, ambNow_ = 0.35f;
};

}  // namespace ui
