// The Airwindows library table (see airdaw/fx_library.h), implemented on airwin2rack's registry.
// Compiled into airdaw_core (the built-in library) and into AirwindowsFX.dll (the updatable library).
#include "airdaw/fx_library.h"
#include "AirwinRegistry.h"
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef AIRDAW_FXLIB_BUILD
#define AIRDAW_FXLIB_BUILD "unknown"
#endif

namespace {
bool valid(int i) { return i >= 0 && i < (int)AirwinRegistry::registry.size(); }
const AirwinRegistry::awReg& reg(int i) { return AirwinRegistry::registry[(size_t)i]; }
AirwinConsolidatedBase* fx(void* p) { return static_cast<AirwinConsolidatedBase*>(p); }
void copy64(const char* s, char* out) { std::strncpy(out, s, 63); out[63] = 0; }

int count() { return (int)AirwinRegistry::registry.size(); }
const char* name(int i) { return valid(i) ? reg(i).name.c_str() : ""; }
const char* category(int i) { return valid(i) ? reg(i).category.c_str() : ""; }
const char* what(int i) { return valid(i) ? reg(i).whatText.c_str() : ""; }
int numParams(int i) { return valid(i) ? reg(i).nParams : 0; }
int isMono(int i) { return valid(i) && reg(i).isMono ? 1 : 0; }
const char* documentation(int i) {
  static std::string doc;  // UI thread only
  doc = valid(i) ? AirwinRegistry::documentationStringFor(i) : std::string();
  return doc.c_str();
}
void* create(int i, float sr) {
  if (!valid(i)) return nullptr;
  AirwinConsolidatedBase::defaultSampleRate = sr;
  auto p = reg(i).generator();
  if (!p) return nullptr;
  p->setSampleRate(sr);
  return p.release();
}
void destroy(void* p) { delete fx(p); }
void setParam(void* p, int k, float v) { fx(p)->setParameter(k, v); }
float getParam(void* p, int k) { return fx(p)->getParameter(k); }
void paramName(void* p, int k, char* out) { char b[64] = {0}; fx(p)->getParameterName(k, b); copy64(b, out); }
void paramLabel(void* p, int k, char* out) { char b[64] = {0}; fx(p)->getParameterLabel(k, b); copy64(b, out); }
void paramDisplay(void* p, int k, char* out) { char b[64] = {0}; fx(p)->getParameterDisplay(k, b); copy64(b, out); }
void process(void* p, float** in, float** out, int n) { fx(p)->processReplacing(in, out, n); }
void seedRandom(unsigned s) { std::srand(s); }

const AwlApi kApi = {airdaw::kFxLibApi, AIRDAW_FXLIB_BUILD, count, name, category, what, numParams, isMono, documentation,
                     create, destroy, setParam, getParam, paramName, paramLabel, paramDisplay, process, seedRandom};
}  // namespace

extern "C" const AwlApi* airdaw_fxlib_builtin() { return &kApi; }
