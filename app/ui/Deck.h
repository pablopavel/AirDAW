// Top deck (mockup #deck): logo + position LCD | transport keys | readouts + buttons.
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
  std::function<void(Key)> onMenu;  // right-click on a key
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

// A menu key that shows an icon instead of a word (Display: a gear; Full: expand / contract by the window's mode).
class IconKey : public juce::TextButton {
 public:
  enum Icon { Gear, Expand, Contract };
  explicit IconKey(Icon i) : icon_(i) { getProperties().set("icon", true); }
  void setIcon(Icon i) { if (i != icon_) { icon_ = i; repaint(); } }
  void paintButton(juce::Graphics&, bool over, bool down) override;
 private:
  Icon icon_;
};

// Tap tempo key: tap in time, or hold and drag up / down or scroll (1 BPM steps), to set the tempo. Whole BPM only.
class TapButton : public juce::TextButton {
 public:
  std::function<float()> getBpm;
  std::function<void(float)> setBpm;
  std::function<void()> resetTaps;  // a drag or scroll is not a tap
  void mouseDown(const juce::MouseEvent& e) override { dragging_ = false; startBpm_ = getBpm ? getBpm() : 120.f; juce::TextButton::mouseDown(e); }
  void mouseDrag(const juce::MouseEvent& e) override {
    const int dy = -e.getDistanceFromDragStartY();
    if (!dragging_ && std::abs(dy) > 4) { dragging_ = true; if (resetTaps) resetTaps(); }
    if (dragging_) set(std::round(startBpm_) + (float)(dy / 6));  // 6 px per BPM
  }
  void mouseUp(const juce::MouseEvent& e) override { if (dragging_ && resetTaps) resetTaps(); dragging_ = false; juce::TextButton::mouseUp(e); }
  void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
    const float d = std::abs(w.deltaY) > std::abs(w.deltaX) ? w.deltaY : -w.deltaX;
    if (d == 0.f || !getBpm) return;
    if (resetTaps) resetTaps();
    set(std::round(getBpm()) + (d > 0.f ? 1.f : -1.f));
  }
 private:
  void set(float v) { if (setBpm) setBpm(juce::jlimit(20.f, 400.f, std::round(v))); }  // whole BPM
  bool dragging_ = false;
  float startBpm_ = 120.f;
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
  void setPluginsLit(bool on) { plugins_.setToggleState(on, juce::dontSendNotification); }
  void previewAmbient(int state) { ambPreview_ = state; }  // test hook: transport light as if 1 = playing, 2 = recording
  IconKey displayBtn{IconKey::Gear};
  juce::TextButton metersBtn{"Meters"};
  juce::TextButton projectBtn{"Project"};
 private:
  AppCtx& ctx_;
  Lcd pos_{"Position", 40.f}, rate_{"Rate / depth", 15.f}, lat_{"Latency", 15.f}, dsp_{"DSP", 15.f};
  TransportKeys keys_;
  TapButton tap_;
  void showClickMenu();  // right-click on the Click key: the click sound
  juce::TextButton listen_{"Listen"}, midi_{"MIDI Learn"}, plugins_{"Plugins"};
  IconKey full_{IconKey::Expand};
  std::vector<juce::TextButton*> buttons_;
  bool pluginsBadge_ = false;
  void paintOverChildren(juce::Graphics&) override;
  // The light under the transport keys: dim amber when stopped, warmer and breathing while playing, a red pulse
  // while recording. Eased each sync() so changes fade instead of snapping.
  int ambPreview_ = 0;
  float amb_ = 0.35f, ambRed_ = 0.f, ambPhase_ = 0.f, ambNow_ = 0.35f;
};

}  // namespace ui
