#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class AirDawApp : public juce::JUCEApplication {
 public:
  const juce::String getApplicationName() override { return "AirDAW"; }
  const juce::String getApplicationVersion() override { return "0.1.0"; }
  void initialise(const juce::String&) override { window_ = std::make_unique<Window>(); }
  void shutdown() override { window_.reset(); }
  void systemRequestedQuit() override { quit(); }
 private:
  struct Window : juce::DocumentWindow {
    Window() : DocumentWindow("AirDAW", juce::Colour(0xff15171a), allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainComponent(), true);
      setResizable(true, true);
      centreWithSize(getWidth(), getHeight());
      setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
  };
  std::unique_ptr<Window> window_;
};
START_JUCE_APPLICATION(AirDawApp)
