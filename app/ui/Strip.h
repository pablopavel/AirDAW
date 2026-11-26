// Mixer channel strips (mockup .st / .mst), wired to the real engine.
#pragma once
#include <array>
#include "AppCtx.h"
#include "airdaw/analysis.h"
#include "Widgets.h"
#include "../midi/MidiControl.h"

namespace ui {

// Small caption + amber value box (mockup .mrd div).
class MiniReadout : public juce::Component, public juce::SettableTooltipClient {
 public:
  explicit MiniReadout(juce::String cap) : cap_(std::move(cap)) {}
  void set(const juce::String& v, juce::Colour c = {}) { if (v != v_ || c != c_) { v_ = v; c_ = c; repaint(); } }
  void paint(juce::Graphics&) override;
  std::function<void()> onClick;
  void mouseUp(const juce::MouseEvent& e) override { if (onClick && e.mouseWasClicked()) onClick(); }
 private:
  juce::String cap_, v_; juce::Colour c_;
};

// Strip button with a drawn headphone icon (input monitoring on/off).
class MonitorButton : public juce::TextButton {
 public:
  void paintButton(juce::Graphics&, bool over, bool down) override;
};

// Track or bus strip.
class Strip : public juce::Component {
 public:
  Strip(AppCtx& ctx, int channelId);
  int id() const { return id_; }
  void sync();  // pull state + meters from the engine (~30x/s)
  void paint(juce::Graphics&) override;
  void resized() override;
  static constexpr int kWidth = 128;
 private:
  void chooseInput();
  void chooseRoute();
  void chooseTake();
  void layoutSlots();
  AppCtx& ctx_;
  const int id_;
  NamePlate plate_;
  juce::TextButton chn_, in_, route_, m_{"M"}, s_{"S"}, t_{"T"}, r_{"R"}, pol_;
  MonitorButton mon_;
  GainKnob gain_;
  std::array<InsertSlot, airdaw::kMaxInserts> slots_;
  InsertSlot summary_;  // compact mode (short window): one row "N inserts" instead of the slot list
  int shownSlots_ = 4, used_ = 0;
  bool compact_ = false;
  PanSlider pan_;
  FaderBlock fader_;
};

// Correlation bar (mockup .hb under "Corr  -1 . 0 . +1").
class CorrBar : public juce::Component {
 public:
  void set(float c) { if (std::abs(c - c_) > 0.005f) { c_ = c; repaint(); } }
  void paint(juce::Graphics&) override;
 private:
  float c_ = 0.f;
};

// Master strip: insert grid, VU pair, loudness readouts (LUFS M/S/I, true peak, RMS, clip), correlation, fader.
class MasterStrip : public juce::Component {
 public:
  explicit MasterStrip(AppCtx& ctx);
  void sync();
  void setLoudness(const airdaw::LoudnessReadout& r);   // from the master analysis (MainComponent)
  std::function<void()> onResetLoudness;                // click on LUFS I / TRUE PK / CLIP
  void paint(juce::Graphics&) override;
  void resized() override;
  void setListenLayout(bool big) { fader_.bigReadout = big; resized(); }
  static constexpr int kWidth = 300;
 private:
  AppCtx& ctx_;
  NamePlate plate_;
  std::array<InsertSlot, airdaw::kMaxInserts> slots_;
  int shownSlots_ = 4;
  VuMeter vuL_{"VU L"}, vuR_{"VU R"};
  MiniReadout lm_{"LUFS M"}, ls_{"LUFS S"}, li_{"LUFS I"}, tp_{"TRUE PK"}, rms_{"RMS"}, clip_{"CLIP"};
  CorrBar corr_;
  bool showCorr_ = true;
  FaderBlock fader_;
  float vuL = 0.f, vuR = 0.f;
  bool clipped_ = false;
};

}  // namespace ui
