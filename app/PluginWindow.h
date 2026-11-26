#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>

// Floating window for a hosted plugin's own GUI (e.g. the Airwindows Hit Record Meter).
class PluginWindow : public juce::DocumentWindow {
 public:
  PluginWindow(std::shared_ptr<juce::AudioPluginInstance> p, std::function<void()> onClose)
      : DocumentWindow(p->getName(), juce::Colours::black, DocumentWindow::closeButton),
        plugin_(std::move(p)), onClose_(std::move(onClose)) {
    juce::AudioProcessorEditor* ed = plugin_->createEditorIfNeeded();
    if (!ed) ed = new juce::GenericAudioProcessorEditor(*plugin_);
    setUsingNativeTitleBar(true);
    setContentOwned(ed, true);
    setResizable(ed->isResizable(), false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }
  // The editor must be destroyed before the plugin instance is released.
  ~PluginWindow() override { clearContentComponent(); }
  void closeButtonPressed() override { if (onClose_) onClose_(); }
 private:
  std::shared_ptr<juce::AudioPluginInstance> plugin_;
  std::function<void()> onClose_;
};
