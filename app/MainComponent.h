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
#include "ui/SettingsWindow.h"
#include "ui/TakesBar.h"

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
  static std::unique_ptr<juce::PropertiesFile> makeProps();

  const bool snapshotMode_;
  juce::AudioDeviceManager devices_;
  AudioBridge bridge_;
  std::unique_ptr<juce::PropertiesFile> props_ = makeProps();
  ui::AirLookAndFeel lnf_;
  ui::AppCtx ctx_{bridge_, devices_, *props_};
  ui::PresetStore presets_{props_->getFile().getParentDirectory().getChildFile("presets")};

  ui::Deck deck_{ctx_};
  ui::Overview overview_{ctx_};
  ui::TakesBar takes_{ctx_};
  ui::InsertPanel inserts_{ctx_, presets_};
  ui::Mixer mixer_{ctx_};
  ui::MeterDock dock_{ctx_};
  ui::DisplayPanel display_;
  struct Splitter : juce::Component, juce::SettableTooltipClient {
    std::function<void(int)> onDrag; int start = 0; std::function<int()> current;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { start = current ? current() : 0; }
    void mouseDrag(const juce::MouseEvent& e) override { if (onDrag) onDrag(start - e.getDistanceFromDragStartY()); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
  } splitter_;
  std::unique_ptr<ui::SettingsWindow> settings_;
  std::unique_ptr<PluginWindow> pluginWindow_;
  std::unique_ptr<juce::TooltipWindow> tooltips_;

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
  int tick_ = 0;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
