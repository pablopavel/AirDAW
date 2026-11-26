// Hosts one Airwindows effect (from the Consolidated registry) as an insert.
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct AirwinConsolidatedBase;

namespace airdaw {

struct FxParamInfo {
  std::string name;     // e.g. "Input"
  std::string label;    // unit label, e.g. "dB"
  float defaultValue;   // normalised 0..1
};

struct FxInfo {
  std::string name, category, description;
  int numParams;
  bool isMono;
};

// Registry queries (UI thread).
std::vector<FxInfo> listEffects();
bool effectExists(const std::string& name);

class FxInstance {
 public:
  static constexpr int kMaxParams = 16;
  // Creates the effect (allocates: call off the audio thread). Returns nullptr if unknown.
  static std::unique_ptr<FxInstance> create(const std::string& name, double sampleRate);
  ~FxInstance();

  const std::string& name() const { return name_; }
  int numParams() const { return numParams_; }
  bool isMono() const { return isMono_; }
  const std::vector<FxParamInfo>& params() const { return params_; }

  // UI thread: normalised 0..1 value, picked up by the audio thread at the next block.
  void setParam(int i, float v) { if (i >= 0 && i < numParams_) target_[i].store(v, std::memory_order_relaxed); }
  float getParam(int i) const { return i >= 0 && i < numParams_ ? target_[i].load(std::memory_order_relaxed) : 0.f; }
  std::string paramDisplay(int i);  // UI thread only; formatted value e.g. "-3.5"

  void setBypass(bool b) { bypass_.store(b, std::memory_order_relaxed); }
  bool bypassed() const { return bypass_.load(std::memory_order_relaxed); }

  // Audio thread. In-place on L/R. No allocation, no locks.
  void process(float* l, float* r, int n);

 private:
  FxInstance() = default;
  std::string name_;
  std::unique_ptr<AirwinConsolidatedBase> fx_;
  int numParams_ = 0;
  bool isMono_ = false;
  std::vector<FxParamInfo> params_;
  std::array<std::atomic<float>, kMaxParams> target_{};
  std::array<float, kMaxParams> applied_{};
  std::atomic<bool> bypass_{false};
  std::vector<float> scratchL_, scratchR_;
};

}  // namespace airdaw
