#include "airdaw/fx_host.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace airdaw {

// ---------------------------------------------------------------- libraries
namespace {
struct Lib {
  const AwlApi* api = nullptr;
  std::unordered_map<std::string, int> index;
  void set(const AwlApi* a) {
    api = a; index.clear();
    for (int i = 0, n = a->count(); i < n; ++i) index[a->name(i)] = i;
  }
  int find(const std::string& n) const { auto it = index.find(n); return it == index.end() ? -1 : it->second; }
};
Lib& builtinLib() { static Lib l = [] { Lib x; x.set(airdaw_fxlib_builtin()); return x; }(); return l; }
Lib& loadedLib() { static Lib l; return l; }
const Lib& activeLib() { return loadedLib().api ? loadedLib() : builtinLib(); }
// The library that has `name` (the active one first; older effects may only exist in the built-in copy).
const Lib* libFor(const std::string& name, int& idx) {
  idx = activeLib().find(name);
  if (idx >= 0) return &activeLib();
  idx = builtinLib().find(name);
  return idx >= 0 ? &builtinLib() : nullptr;
}
}  // namespace

bool useFxLibrary(const AwlApi* api) {
  if (!api || api->apiVersion != kFxLibApi || !api->count || api->count() <= 0) return false;
  loadedLib().set(api);
  return true;
}
const AwlApi& activeFxLibrary() { return *activeLib().api; }
const AwlApi& builtinFxLibrary() { return *builtinLib().api; }
bool fxLibraryLoaded() { return loadedLib().api != nullptr; }
void seedFxRandom(unsigned seed) {
  builtinLib().api->seedRandom(seed);
  if (loadedLib().api) loadedLib().api->seedRandom(seed);
}

// ---------------------------------------------------------------- registry queries
std::vector<FxInfo> listEffects() {
  const auto& a = *activeLib().api;
  std::vector<FxInfo> out;
  out.reserve((size_t)a.count());
  for (int i = 0, n = a.count(); i < n; ++i) out.push_back({a.name(i), a.category(i), a.what(i), a.numParams(i), a.isMono(i) != 0});
  return out;
}

std::string effectDocumentation(const std::string& name) {
  int i; const Lib* l = libFor(name, i);
  if (!l) return {};
  std::string doc = l->api->documentation(i);
  return doc.empty() ? std::string(l->api->what(i)) : doc;
}

bool effectExists(const std::string& name) { int i; return libFor(name, i) != nullptr; }

// ---------------------------------------------------------------- instances
std::unique_ptr<FxInstance> FxInstance::create(const std::string& name, double sampleRate) {
  int idx; const Lib* l = libFor(name, idx);
  if (!l) return nullptr;
  const AwlApi* a = l->api;
  void* fx = a->create(idx, (float)sampleRate);
  if (!fx) return nullptr;
  std::unique_ptr<FxInstance> inst(new FxInstance());
  inst->api_ = a; inst->fx_ = fx;
  inst->name_ = name;
  inst->isMono_ = a->isMono(idx) != 0;
  inst->numParams_ = std::min(a->numParams(idx), kMaxParams);
  for (int i = 0; i < inst->numParams_; ++i) {
    char nm[64] = {0}, lb[64] = {0};
    a->paramName(fx, i, nm);
    a->paramLabel(fx, i, lb);
    const float d = a->getParam(fx, i);
    inst->params_.push_back({nm, lb, d});
    inst->target_[(size_t)i].store(d);
    inst->applied_[(size_t)i] = d;
  }
  return inst;
}

FxInstance::~FxInstance() { if (api_ && fx_) api_->destroy(fx_); }

std::string FxInstance::paramDisplay(int i) {
  if (i < 0 || i >= numParams_) return {};
  // Display text is computed from the effect's own state, so this must not race process():
  // it reads the value the audio thread last applied. Callers on the UI thread should treat
  // it as approximate for at most one block of lag.
  char buf[64] = {0};
  api_->paramDisplay(fx_, i, buf);
  return buf;
}

void FxInstance::process(float* l, float* r, int n) {
  for (int i = 0; i < numParams_; ++i) {
    const float t = target_[(size_t)i].load(std::memory_order_relaxed);
    if (t != applied_[(size_t)i]) { api_->setParam(fx_, i, t); applied_[(size_t)i] = t; }
  }
  if (bypass_.load(std::memory_order_relaxed)) return;
  float* in[2] = {l, r};
  float* out[2] = {l, r};
  api_->process(fx_, in, out, n);
}

}  // namespace airdaw
