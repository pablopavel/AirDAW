#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include "AudioBridge.h"
#include "PluginWindow.h"

class MainComponent : public juce::Component, private juce::Timer {
 public:
  MainComponent();
  ~MainComponent() override;
  void paint(juce::Graphics&) override;
  void resized() override;
 private:
  void timerCallback() override;
  void newSession();
  void openSession();
  void saveSession(bool saveAs);
  void setSessionFile(const juce::File& f);
  void applyAutoSaveSettings();
  void checkRecovery();
  void collectTakes();
  void chooseMeterPlugin();
  void openMeterEditor();
  void loadMeter(const juce::File& vst3);
  juce::File sessionFolder() const;
  juce::String sessionName() const;
  void note(const juce::String& s) { notice_ = s; noticeUntil_ = juce::Time::getMillisecondCounter() + 6000; }

  juce::AudioDeviceManager devices_;
  AudioBridge bridge_;
  std::unique_ptr<juce::AudioDeviceSelectorComponent> selector_;
  juce::TextButton rtz_{"|<"}, play_{"Play"}, stop_{"Stop"}, rec_{"Rec"};
  juce::ToggleButton arm1_{"Arm 1 (in 1)"}, arm2_{"Arm 2 (in 2)"}, tone_{"Test tone on track 1"};
  juce::TextButton save_{"Save"}, saveAs_{"Save As..."}, open_{"Open..."}, new_{"New"};
  juce::TextButton meter_{"Hit Record Meter..."}, meterEd_{"Meter window"};
  std::unique_ptr<PluginWindow> meterWindow_;
  juce::ToggleButton autoSave_{"Auto-save every"};
  juce::ComboBox autoEvery_;
  juce::Label status_, pos_, notice_label_;
  juce::String notice_; juce::uint32 noticeUntil_ = 0;

  juce::File sessionFile_;                         // empty = unsaved "Untitled"
  std::unique_ptr<airdaw::AutoSaver> autoSaver_;
  airdaw::Engine* autoEngine_ = nullptr;
  std::unique_ptr<juce::PropertiesFile> props_;
  std::shared_ptr<juce::FileChooser> chooser_;
  bool recoveryChecked_ = false;
  float meterL_ = 0, meterR_ = 0;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
