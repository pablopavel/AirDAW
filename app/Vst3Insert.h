// Hosts a VST3 plugin as an AirDAW insert (used for the Airwindows Hit Record Meter).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "airdaw/engine.h"
#include "airdaw/insert.h"

class Vst3Insert : public airdaw::InsertProcessor {
 public:
  // Message thread only. Returns nullptr and fills `error` on failure.
  static std::unique_ptr<Vst3Insert> load(const juce::File& vst3, double sampleRate, juce::String& error) {
    static juce::AudioPluginFormatManager manager;
    static bool init = false;
    if (!init) { manager.addFormat(new juce::VST3PluginFormat()); init = true; }

    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* fmt : manager.getFormats()) fmt->findAllTypesForFile(found, vst3.getFullPathName());
    if (found.isEmpty()) { error = "No VST3 plugin found in " + vst3.getFileName(); return nullptr; }

    auto instance = manager.createPluginInstance(*found[0], sampleRate, airdaw::kMaxBlock, error);
    if (!instance) return nullptr;

    // Ask for stereo in/out; plugins that cannot do it keep their own layout.
    juce::AudioProcessor::BusesLayout want = instance->getBusesLayout();
    if (instance->getBusCount(true) > 0) want.inputBuses.set(0, juce::AudioChannelSet::stereo());
    if (instance->getBusCount(false) > 0) want.outputBuses.set(0, juce::AudioChannelSet::stereo());
    if (instance->checkBusesLayoutSupported(want)) instance->setBusesLayout(want);
    instance->prepareToPlay(sampleRate, airdaw::kMaxBlock);

    std::unique_ptr<Vst3Insert> r(new Vst3Insert());
    r->plugin_ = std::shared_ptr<juce::AudioPluginInstance>(std::move(instance));
    r->path_ = vst3.getFullPathName().toStdString();
    r->name_ = found[0]->name.toStdString();
    r->channels_ = juce::jmax(2, r->plugin_->getTotalNumInputChannels(), r->plugin_->getTotalNumOutputChannels());
    r->scratch_.setSize(r->channels_, airdaw::kMaxBlock);
    r->scratch_.clear();
    r->inCh_ = r->plugin_->getTotalNumInputChannels(); r->outCh_ = r->plugin_->getTotalNumOutputChannels();
    int n = 0;
    for (auto* p : r->plugin_->getParameters()) {
      if (n++ >= 256) break;
      r->params_.push_back({p->getName(64).toStdString(), p->getLabel().toStdString(), p->getDefaultValue()});
    }
    return r;
  }

  // The editor window keeps its own reference, so it stays valid while the engine is being rebuilt.
  std::shared_ptr<juce::AudioPluginInstance> plugin() const { return plugin_; }

  const std::string& name() const override { return name_; }
  std::string sessionId() const override { return "vst3:" + path_; }
  int numParams() const override { return (int)params_.size(); }
  const std::vector<airdaw::FxParamInfo>& params() const override { return params_; }
  void setParam(int i, float v) override { if (auto* p = param(i)) p->setValueNotifyingHost(v); }
  float getParam(int i) const override { auto* p = param(i); return p ? p->getValue() : 0.f; }
  std::string paramDisplay(int i) override { auto* p = param(i); return p ? p->getCurrentValueAsText().toStdString() : std::string(); }
  void setBypass(bool b) override { bypass_.store(b); }
  bool bypassed() const override { return bypass_.load(); }
  int latencySamples() const override { return plugin_->getLatencySamples(); }

  std::string saveState() override {
    juce::MemoryBlock mb; plugin_->getStateInformation(mb); return mb.toBase64Encoding().toStdString();
  }
  void loadState(const std::string& s) override {
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding(juce::String(s))) plugin_->setStateInformation(mb.getData(), (int)mb.getSize());
  }

  void process(float* l, float* r, int n) override {
    if (bypass_.load(std::memory_order_relaxed)) return;
    juce::ScopedNoDenormals noDenormals;
    float* ch0 = scratch_.getWritePointer(0);
    std::copy(l, l + n, ch0);
    if (channels_ > 1) std::copy(r, r + n, scratch_.getWritePointer(1));
    for (int c = 2; c < channels_; ++c) juce::FloatVectorOperations::clear(scratch_.getWritePointer(c), n);
    juce::AudioBuffer<float> view(scratch_.getArrayOfWritePointers(), channels_, n);  // no allocation
    midi_.clear();
    plugin_->processBlock(view, midi_);
    std::copy(scratch_.getReadPointer(0), scratch_.getReadPointer(0) + n, l);
    const float* src = scratch_.getReadPointer(outCh_ >= 2 ? 1 : 0);
    std::copy(src, src + n, r);
  }

 private:
  Vst3Insert() = default;
  juce::AudioProcessorParameter* param(int i) const {
    auto& ps = plugin_->getParameters(); return i >= 0 && i < ps.size() ? ps[i] : nullptr;
  }
  std::shared_ptr<juce::AudioPluginInstance> plugin_;
  std::string name_, path_;
  std::vector<airdaw::FxParamInfo> params_;
  juce::AudioBuffer<float> scratch_;
  juce::MidiBuffer midi_;
  int channels_ = 2, inCh_ = 2, outCh_ = 2;
  std::atomic<bool> bypass_{false};
};
