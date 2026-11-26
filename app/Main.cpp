#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

// Command line (test hook, not needed in normal use):
//   --snapshot <file.png> [--scenario <name>] [--size <W>x<H>]   set up a UI state, save a picture of the window, quit.
class AirDawApp : public juce::JUCEApplication {
 public:
  const juce::String getApplicationName() override { return "AirDAW"; }
  const juce::String getApplicationVersion() override { return "0.1.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }
  void initialise(const juce::String&) override {
    const auto args = getCommandLineParameterArray();
    auto arg = [&](const char* key) { const int i = args.indexOf(key); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };
    const juce::String snap = arg("--snapshot"), size = arg("--size");
    window_ = std::make_unique<Window>(snap.isNotEmpty());
    if (size.containsChar('x')) window_->setSize(size.upToFirstOccurrenceOf("x", false, false).getIntValue(),
                                                 size.fromFirstOccurrenceOf("x", false, false).getIntValue());
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
