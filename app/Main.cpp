#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "ui/AppIcon.h"
#include "ui/FullScreen.h"

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
    else window_->setFull(true);  // opens full screen (Esc or the Full key leaves it)
  }
  void shutdown() override {
    ui::fullscreen::isOn = [] { return false; }; ui::fullscreen::set = [](bool) {};
    window_.reset();
  }
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
      ui::fullscreen::isOn = [this] { return full_; };
      ui::fullscreen::set = [this](bool on) { setFull(on); };
    }
    // Full screen: the window is recreated without a frame and covers the whole display it is on. Unlike JUCE's kiosk
    // mode it stays when another window or monitor takes the focus.
    void setFull(bool on) {
      if (on == full_) return;
      if (on) {
        restoreMax_ = isFullScreen();  // maximised (native title bar)
        if (restoreMax_) setFullScreen(false);
        restore_ = getBounds();
        const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
        full_ = true;
        recreateDesktopWindow();
        if (d) setBounds(d->totalArea);
      } else {
        full_ = false;
        recreateDesktopWindow();
        setBounds(restore_);
        if (restoreMax_) setFullScreen(true);
      }
      toFront(true);
      if (content) content->grabKeyboardFocus();
    }
    int getDesktopWindowStyleFlags() const override {
      return full_ ? juce::ComponentPeer::windowAppearsOnTaskbar : DocumentWindow::getDesktopWindowStyleFlags();
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    MainComponent* content = nullptr;
   private:
    bool full_ = false, restoreMax_ = false;
    juce::Rectangle<int> restore_;
  };
  std::unique_ptr<Window> window_;
};
START_JUCE_APPLICATION(AirDawApp)
