#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include "AudioBridge.h"
#include "PluginWindow.h"
#include "ui/AirLookAndFeel.h"
#include "ui/AppCtx.h"
#include "ui/Deck.h"
#include "ui/DisplayPanel.h"
#include "ui/InsertPanel.h"
#include "ui/MeterDock.h"
#include "ui/Mixer.h"
#include "ui/Overview.h"
#include "ui/PluginInfo.h"
#include "ui/PluginsPanel.h"
#include "ui/SettingsWindow.h"
#include "ui/TakesBar.h"
#include "ui/tools/AnalysisHub.h"
#include "ui/tools/ToolSet.h"
#include "ui/tools/ListenRow.h"
#include "midi/MidiControl.h"
#include "midi/MidiLearnOverlay.h"

// The AirDAW main window (layout and look from docs/airdaw-v4.html):
// deck | song overview | insert panel (when open) | mixer | meter dock | footer.
class MainComponent : public juce::Component, public juce::DragAndDropContainer, private juce::Timer, private juce::ChangeListener,
                      private juce::KeyListener {
 public:
  explicit MainComponent(bool snapshotMode = false);
  ~MainComponent() override;
  void paint(juce::Graphics&) override;
  void resized() override;

  // Test hook: set up a named UI state, save a PNG of the window, quit. Not used in normal runs.
  void takeSnapshot(const juce::File& png, const juce::String& scenario);

 private:
  // --- app logic (projects, recording, devices) ---
  void timerCallback() override;
  void changeListenerCallback(juce::ChangeBroadcaster*) override;
  bool keyPressed(const juce::KeyPress&, juce::Component*) override;
  void transport(ui::TransportKeys::Key k);
  void showProjectMenu();
  void newSession();
  void openSession();
  void openSessionFile(const juce::File& f);
 public:
  // A project opened from Explorer (double-click / "Open with"): loaded once the audio engine is up.
  void openOnStart(const juce::File& f) { pendingOpen_ = f; }
  void openFromOtherInstance(const juce::File& f) { if (autoEngine_) openSessionFile(f); else pendingOpen_ = f; }
 private:
  juce::File pendingOpen_;
  void saveSession(bool saveAs);
  void saveProjectAs(const juce::File& chosen);
  void setSessionFile(const juce::File& f);
  void applyAutoSaveSettings();
  void checkRecovery();
  void collectTakes();
  void chooseMeterPlugin();
  void loadMeter(const juce::File& vst3, bool quiet = false);
  void openPluginWindow(int channelId, int slot);
  void updateDeviceStatus();
  bool requireAudio(const char* what);
  void toggleDisplayPanel();
  void showAudioSettings();
  void applyTheme();
  void note(const juce::String& s, bool warn = false);
  void runScenario(const juce::String& name);
  void exportMix();  // options + file dialog, then startExport
  void startExport(const juce::File& out, bool loopRange, int bits);
  juce::File sessionFolder() const;
  juce::String sessionName() const;
  static juce::File projectsRoot();   // Documents/AirDAW
  static juce::File scratchFolder();  // Documents/AirDAW/Untitled (unsaved session)
  static std::unique_ptr<juce::PropertiesFile> makeProps(bool snapshot);

  const bool snapshotMode_;
  juce::AudioDeviceManager devices_;
  AudioBridge bridge_;
  std::unique_ptr<juce::PropertiesFile> props_ = makeProps(snapshotMode_);
  ui::AirLookAndFeel lnf_;
  ui::AppCtx ctx_{bridge_, devices_, *props_};
  ui::PresetStore presets_{props_->getFile().getParentDirectory().getChildFile("presets")};

  ui::Deck deck_{ctx_};
  ui::Overview overview_{ctx_};
  ui::TakesBar takes_{ctx_};
  ui::InsertPanel inserts_{ctx_, presets_};
  ui::Mixer mixer_{ctx_};
  ui::ToolSet tools_{ctx_};   // reference-suite / Listen tools (before the dock, which shows some of them)
  ui::MeterDock dock_{ctx_, tools_};
  ui::ListenRow listenRow_{tools_};   // Listen mode: every tool that is not in the reference suite
  bool listen_ = false;
  void setListen(bool on);
  ui::DisplayPanel display_;
  ui::PluginsPanel pluginsPanel_{ctx_};
  void togglePluginsPanel();
  bool pluginsChecked_ = false;
  ui::Divider splitter_{false};  // between the mixer and the meter dock
  std::unique_ptr<ui::SettingsWindow> settings_;
  std::unique_ptr<PluginWindow> pluginWindow_;
  std::unique_ptr<ui::PluginInfoWindow> pluginInfo_;
  std::unique_ptr<ui::AnalysisHub> hub_;  // master analysis for the meters and tools, fed from the engine's master tap
  std::vector<float> tapBuf_;
  bool wasPlaying_ = false;
  bool snapshotFrozen_ = false;  // test hook: synthetic meter readings stay on screen
  std::unique_ptr<airdaw::LoudnessMeter> mixLoud_;  // the master strip's meter: always the mix (master tap)
  std::unique_ptr<airdaw::LoudnessMeter> refLoud_;  // the reference as it plays (reference tap), for the A/B levels
  bool wasRefPlaying_ = false;
  bool snapshotLevels_ = false;  // test hook: synthetic A/B levels stay on screen
  float mixIntegrated_ = -200.f;
  bool wasOnB_ = false;
  void resetMeters();
  void chooseReference();
  void decodeReference(const juce::File& f, bool quiet);
  int refGen_ = 0;  // newest reference decode (an older one finishing later is dropped)
  // Per-project app data (stored in the session's "app" object): reference track, plugin MIDI mappings.
  juce::var projectData() const;
  void setProjectValue(const juce::String& key, const juce::var& v);
  // Loads the project's own reference (or none). `adoptGlobal`: a project saved before references were per
  // project takes the last reference used app-wide.
  void restoreProjectReference(bool adoptGlobal);
  void pumpAnalysis(airdaw::Engine& e);
  std::unique_ptr<juce::TooltipWindow> tooltips_;
  // MIDI Learn
  std::unique_ptr<ui::MidiControl> midi_;
  std::unique_ptr<ui::MidiLearnOverlay> learnOverlay_;
  std::unique_ptr<ui::MidiMappingsWindow> midiWindow_;
  void midiChanged();
  void showMidiMappings();

  int dockHeight_ = 330;
  bool dockShown_ = true;
  juce::File sessionFile_;  // empty = unsaved "Untitled"
  std::unique_ptr<airdaw::AutoSaver> autoSaver_;
  airdaw::Engine* autoEngine_ = nullptr;
  std::shared_ptr<juce::FileChooser> chooser_;
  bool recoveryChecked_ = false, audioRunning_ = false;
  bool autoOn_ = false; int autoId_ = 3;
  juce::String lastAutoSave_, initError_, status_, notice_;
  bool noticeWarn_ = false;
  juce::uint32 noticeUntil_ = 0, lastCallbacks_ = 0;
  double lastCallbackSeen_ = 0;
  double lastReconnect_ = 0, reconnectDelay_ = 5000.0;  // automatic reopen of the saved device after it dropped out
  bool reconnectStopped_ = false;
  std::unique_ptr<juce::FileLogger> log_;  // %APPDATA%/AirDAW/AirDAW.log: startup, devices, MIDI (diagnostics)
  bool reconnecting_ = false;
  int tick_ = 0;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
