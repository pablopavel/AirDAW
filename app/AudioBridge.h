// Connects JUCE's audio device callback to the AirDAW engine.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>
#include <filesystem>
#include "airdaw/engine.h"
#include "airdaw/session.h"

class AudioBridge : public juce::AudioIODeviceCallback {
 public:
  AudioBridge() {
    owned_ = std::make_unique<airdaw::Engine>(48000.0);
    engine_.store(owned_.get());
    sr_ = 48000.0;
  }
  airdaw::Engine* engine() { return engine_.load(std::memory_order_acquire); }

  // Built-in test signal into track 1 so the output path can be checked with no inputs connected.
  std::atomic<bool> testTone{false};

  // How sessions recreate hosted plugins (VST3). Applied to the current and every future engine.
  void setInsertFactory(airdaw::InsertFactory f) { factory_ = std::move(f); if (owned_) owned_->setInsertFactory(factory_); }

  void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
    // Called with audio stopped. Rebuild the engine if the device runs at a new sample rate.
    const double sr = d->getCurrentSampleRate();
    if (!owned_ || owned_->sampleRate() != sr) {
      // New sample rate: carry the session over into a fresh engine (inserts are created per rate).
      auto fresh = std::make_unique<airdaw::Engine>(sr);
      fresh->setInsertFactory(factory_);
      if (owned_) {
        auto tmp = (std::filesystem::temp_directory_path() / "airdaw_rate_change.airdaw").string();
        if (airdaw::saveSession(*owned_, tmp).ok) airdaw::loadSession(*fresh, tmp);
      }
      engine_.store(fresh.get(), std::memory_order_release);
      owned_ = std::move(fresh);  // audio is stopped here, so the old engine can be destroyed
    }
    sr_ = sr;
    phase_ = 0;
    tone_.assign(airdaw::kMaxBlock, 0.f);
    bufferSize_ = d->getCurrentBufferSizeSamples();
    latency_ = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
  }
  void audioDeviceStopped() override {}

  void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut,
                                        int n, const juce::AudioIODeviceCallbackContext&) override {
    auto* e = engine();
    if (!e) { for (int c = 0; c < nOut; ++c) juce::FloatVectorOperations::clear(out[c], n); return; }
    if (testTone.load(std::memory_order_relaxed) && n <= airdaw::kMaxBlock) {
      for (int i = 0; i < n; ++i) { tone_[i] = 0.25f * std::sin(phase_); phase_ += 2.0 * 3.14159265358979 * 440.0 / sr_; }
      if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
      const int m = std::min(nIn, 32);
      const float* merged[33];
      for (int c = 0; c < m; ++c) merged[c] = in[c];
      merged[m] = tone_.data();               // virtual input just past the hardware channels
      e->track(0).inputL = m; toneOn_ = true;
      e->process(merged, m + 1, out, nOut, n);
    } else {
      if (toneOn_) { e->track(0).inputL = 0; toneOn_ = false; }
      e->process(in, nIn, out, nOut, n);
    }
  }

  int bufferSamples() const { return bufferSize_; }
  int deviceLatencySamples() const { return latency_; }
  double sampleRate() const { return sr_; }

 private:
  airdaw::InsertFactory factory_;
  std::unique_ptr<airdaw::Engine> owned_;
  std::atomic<airdaw::Engine*> engine_{nullptr};
  std::vector<float> tone_;
  bool toneOn_ = false;
  double sr_ = 48000, phase_ = 0;
  int bufferSize_ = 0, latency_ = 0;
};
