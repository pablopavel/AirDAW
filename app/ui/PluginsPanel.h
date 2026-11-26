// Plugins panel (mockup #up): the Airwindows library AirDAW runs, updates from the repo's releases, roll back,
// update-check schedule, older versions in the picker.
//
// Library builds live in %APPDATA%/AirDAW/airwindows/<build>/ (AirwindowsFX.dll + manifest.json). Settings:
//   plugins.active    build loaded at launch ("" = the built-in copy)
//   plugins.previous  earlier active builds, newest last (Roll back)
// Nothing is downloaded or installed unless the user presses "Install update"; a new build activates at the next
// launch. A build that fails to load or whose SHA-256 does not match its manifest is ignored (built-in is used).
#pragma once
#include <juce_cryptography/juce_cryptography.h>
#include <atomic>
#include "AppCtx.h"
#include "Theme.h"

namespace ui {

namespace pluginlib {
juce::File root();  // %APPDATA%/AirDAW/airwindows
void setRoot(const juce::File& f);  // test snapshots keep their builds elsewhere
// Startup: load the active build (before any effect is created). Returns "" or why the built-in copy is used.
juce::String activate(juce::PropertiesFile& props);
juce::String runningBuild();             // build id of the library in use
bool runningIsBuiltin();
const juce::StringArray& newEffects();   // effects the running build added since the build used before
}  // namespace pluginlib

class PluginsPanel : public juce::Component, private juce::Timer {
 public:
  explicit PluginsPanel(AppCtx& ctx);
  ~PluginsPanel() override;
  void paint(juce::Graphics&) override;
  void resized() override;
  int preferredHeight() const;
  static constexpr int kWidth = 500;

  void startupCheck();                  // honours the "Check for updates" setting
  void check();                         // ask the repo's releases for a newer build
  void installForTest() { install(); }  // test hook (snapshots)
  std::function<void(bool)> onUpdateAvailable;  // badge on the deck's Plugins button
  std::function<void()> layoutChanged;
  juce::String loadError;               // from pluginlib::activate (shown in the panel)

 private:
  struct Release { juce::String build, tag, zipUrl, manifestUrl, sha; int api = 0; juce::StringArray effects; };
  enum class St { Idle, Checking, UpToDate, Available, Downloading, Installed, Error };
  void install();
  void rollBack();
  void setState(St s, const juce::String& msg = {});
  void sync();
  void timerCallback() override { repaint(); }
  juce::String pendingBuild() const;    // what will load at the next launch

  AppCtx& ctx_;
  St st_ = St::Idle;
  juce::String msg_;
  Release rel_;
  juce::StringArray whatsNew_;
  std::atomic<float> progress_{-1.f};
  juce::TextButton checkBtn_{"Check now"}, installBtn_{"Install update"}, rollBtn_{"Roll back"};
  std::array<juce::TextButton, 3> mode_;
  std::array<juce::TextButton, 2> old_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  int whatsNewY_ = 0;
};

}  // namespace ui
