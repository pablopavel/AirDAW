#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "ui/AppIcon.h"

// Command line:
//   AirDAW.exe <project.airdaw>   open that project (what Explorer passes on double-click / "Open with").
//   --snapshot <file.png> [--scenario <name>] [--size <W>x<H>]   test hook: set up a UI state, save a picture, quit.
//   --render-icon <folder>   write the application icon PNGs (resources/icon) and quit.

// The first argument that names an existing .airdaw file (Explorer quotes paths with spaces).
static juce::File projectArg(const juce::String& commandLine) {
  juce::StringArray a; a.addTokens(commandLine, true);
  for (auto t : a) {
    t = t.trim().unquoted();
    const juce::File f = juce::File::isAbsolutePath(t) ? juce::File(t) : juce::File::getCurrentWorkingDirectory().getChildFile(t);
    if (f.hasFileExtension("airdaw") && f.existsAsFile()) return f;
  }
  return {};
}

class AirDawApp : public juce::JUCEApplication {
 public:
  const juce::String getApplicationName() override { return "AirDAW"; }
  const juce::String getApplicationVersion() override { return "0.1.0"; }
  // One AirDAW at a time (two would fight over the ASIO driver): opening another project from Explorer hands it to the
  // running window. Snapshot test runs are separate instances.
  bool moreThanOneInstanceAllowed() override {
    return getCommandLineParameters().contains("--snapshot") || getCommandLineParameters().contains("--render-icon");
  }
  void anotherInstanceStarted(const juce::String& commandLine) override {
    if (!window_) return;
    window_->toFront(true);
    const auto f = projectArg(commandLine);
    if (f != juce::File()) window_->content->openFromOtherInstance(f);
  }
  void initialise(const juce::String&) override {
    const auto args = getCommandLineParameterArray();
    auto arg = [&](const char* key) { const int i = args.indexOf(key); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };
    const juce::String snap = arg("--snapshot"), size = arg("--size");
    if (const auto iconDir = arg("--render-icon"); iconDir.isNotEmpty()) {
      setApplicationReturnValue(ui::renderAppIcons(juce::File::getCurrentWorkingDirectory().getChildFile(iconDir)) ? 0 : 1);
      quit(); return;
    }
    window_ = std::make_unique<Window>(snap.isNotEmpty());
    if (size.containsChar('x')) window_->setSize(size.upToFirstOccurrenceOf("x", false, false).getIntValue(),
                                                 size.fromFirstOccurrenceOf("x", false, false).getIntValue());
    if (const auto f = projectArg(getCommandLineParameters()); f != juce::File()) window_->content->openOnStart(f);
    if (snap.isNotEmpty()) window_->content->takeSnapshot(juce::File(snap), arg("--scenario"));
  }
  void shutdown() override { window_.reset(); }
  void systemRequestedQuit() override { quit(); }

 private:
  struct Window : juce::DocumentWindow {
    explicit Window(bool snapshot) : DocumentWindow("AirDAW", juce::Colours::black, allButtons) {
      setUsingNativeTitleBar(true);
      content = new MainComponent(snapshot);
      setContentOwned(content, false);
      setResizable(true, false);
      setResizeLimits(1100, 700, 16000, 10000);
      auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
      centreWithSize(juce::jmin(1760, area.getWidth() * 92 / 100), juce::jmin(1060, area.getHeight() * 92 / 100));
      setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    MainComponent* content = nullptr;
  };
  std::unique_ptr<Window> window_;
};
START_JUCE_APPLICATION(AirDawApp)
