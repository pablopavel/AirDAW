// Hosts one Airwindows effect as an insert. Effects come from the active Airwindows library
// (built in, or a newer AirwindowsFX.dll: see fx_library.h).
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "airdaw/fx_library.h"
#include "airdaw/insert.h"

namespace airdaw {

struct FxInfo {
  std::string name, category, description;
  int numParams;
  bool isMono;
};

// Registry queries (UI thread).
std::vector<FxInfo> listEffects();
bool effectExists(const std::string& name);
// Chris's full write-up for an effect (as on airwindows.com; Markdown-ish text, first line "# Name is ...").
// Falls back to the one-line description; empty for unknown names.
std::string effectDocumentation(const std::string& name);

class FxInstance : public InsertProcessor {
 public:
  static constexpr int kMaxParams = 16;
  // Creates the effect (allocates: call off the audio thread). Returns nullptr if unknown.
  static std::unique_ptr<FxInstance> create(const std::string& name, double sampleRate);
  ~FxInstance() override;

  const std::string& name() const override { return name_; }
  int numParams() const override { return numParams_; }
  bool isMono() const { return isMono_; }
  const std::vector<FxParamInfo>& params() const override { return params_; }

  // UI thread: normalised 0..1 value, picked up by the audio thread at the next block.
  void setParam(int i, float v) override { if (i >= 0 && i < numParams_) target_[i].store(v, std::memory_order_relaxed); }
  float getParam(int i) const override { return i >= 0 && i < numParams_ ? target_[i].load(std::memory_order_relaxed) : 0.f; }
  std::string paramDisplay(int i) override;  // UI thread only; formatted value e.g. "-3.5"

  void setBypass(bool b) override { bypass_.store(b, std::memory_order_relaxed); }
  bool bypassed() const override { return bypass_.load(std::memory_order_relaxed); }

  // Audio thread. In-place on L/R. No allocation, no locks.
  void process(float* l, float* r, int n) override;

 private:
  FxInstance() = default;
  std::string name_;
  const AwlApi* api_ = nullptr;  // the library that made it (destroy / process go back to it)
  void* fx_ = nullptr;
  int numParams_ = 0;
  bool isMono_ = false;
  std::vector<FxParamInfo> params_;
  std::array<std::atomic<float>, kMaxParams> target_{};
  std::array<float, kMaxParams> applied_{};
  std::atomic<bool> bypass_{false};
  std::vector<float> scratchL_, scratchR_;
};

}  // namespace airdaw
