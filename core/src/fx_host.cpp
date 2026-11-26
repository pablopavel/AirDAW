#include "airdaw/fx_host.h"
#include "AirwinRegistry.h"
#include <algorithm>
#include <cstring>

namespace airdaw {

std::vector<FxInfo> listEffects() {
  std::vector<FxInfo> out;
  out.reserve(AirwinRegistry::registry.size());
  for (const auto& r : AirwinRegistry::registry)
    out.push_back({r.name, r.category, r.whatText, r.nParams, r.isMono});
  return out;
}

bool effectExists(const std::string& name) {
  return AirwinRegistry::nameToIndex.count(name) > 0;
}

std::unique_ptr<FxInstance> FxInstance::create(const std::string& name, double sampleRate) {
  auto it = AirwinRegistry::nameToIndex.find(name);
  if (it == AirwinRegistry::nameToIndex.end()) return nullptr;
  const auto& reg = AirwinRegistry::registry[it->second];

  AirwinConsolidatedBase::defaultSampleRate = static_cast<float>(sampleRate);
  auto fx = reg.generator();
  if (!fx) return nullptr;
  fx->setSampleRate(static_cast<float>(sampleRate));

  std::unique_ptr<FxInstance> inst(new FxInstance());
  inst->name_ = name;
  inst->isMono_ = reg.isMono;
  inst->numParams_ = std::min(reg.nParams, kMaxParams);
  for (int i = 0; i < inst->numParams_; ++i) {
    char nm[64] = {0}, lb[64] = {0};
    fx->getParameterName(i, nm);
    fx->getParameterLabel(i, lb);
    float d = fx->getParameter(i);
    inst->params_.push_back({nm, lb, d});
    inst->target_[i].store(d);
    inst->applied_[i] = d;
  }
  inst->fx_ = std::move(fx);
  return inst;
}

FxInstance::~FxInstance() = default;

std::string FxInstance::paramDisplay(int i) {
  if (i < 0 || i >= numParams_) return {};
  // Display text is computed from the effect's own state, so this must not race process():
  // it reads the value the audio thread last applied. Callers on the UI thread should treat
  // it as approximate for at most one block of lag.
  char buf[64] = {0};
  fx_->getParameterDisplay(i, buf);
  return buf;
}

void FxInstance::process(float* l, float* r, int n) {
  for (int i = 0; i < numParams_; ++i) {
    float t = target_[i].load(std::memory_order_relaxed);
    if (t != applied_[i]) { fx_->setParameter(i, t); applied_[i] = t; }
  }
  if (bypass_.load(std::memory_order_relaxed)) return;
  float* in[2] = {l, r};
  float* out[2] = {l, r};
  fx_->processReplacing(in, out, n);
}

}  // namespace airdaw
