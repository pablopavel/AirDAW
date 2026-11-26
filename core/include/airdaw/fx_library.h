// Airwindows effect library interface (plain C, function pointers only).
//
// AirDAW always has a built-in copy of Airwindows Consolidated. A newer build can be installed as a separate
// library (AirwindowsFX.dll, built by the repo's GitHub workflow) and activated at the next launch. Both are
// the same source (core/src/fxlib_api.cpp) behind this table, so the engine has one code path for either.
// Bump kFxLibApi on any change to AwlApi; AirDAW refuses a library with a different version.
#pragma once

extern "C" {

struct AwlApi {
  int apiVersion;          // kFxLibApi
  const char* buildId;     // Airwindows build, e.g. "2026.11.22-91b3343" (airwin2rack commit date + hash)
  int (*count)();
  const char* (*name)(int index);
  const char* (*category)(int index);
  const char* (*what)(int index);           // one-line description
  int (*numParams)(int index);
  int (*isMono)(int index);
  const char* (*documentation)(int index);  // Chris's write-up ("" if none); valid until the next call
  void* (*create)(int index, float sampleRate);
  void (*destroy)(void* fx);
  void (*setParam)(void* fx, int param, float value);
  float (*getParam)(void* fx, int param);
  void (*paramName)(void* fx, int param, char* out64);
  void (*paramLabel)(void* fx, int param, char* out64);
  void (*paramDisplay)(void* fx, int param, char* out64);
  void (*process)(void* fx, float** in, float** out, int frames);  // in-place allowed
  // Seeds the C runtime random generator of the module the effects live in (Airwindows seeds its dither
  // noise from rand() when an effect is created; a DLL has its own runtime). Tests use it for repeatability.
  void (*seedRandom)(unsigned seed);
};

// The table compiled into this binary (AirDAW's built-in copy, or the one inside AirwindowsFX.dll).
const AwlApi* airdaw_fxlib_builtin();

}  // extern "C"

namespace airdaw {

constexpr int kFxLibApi = 1;
// The DLL exports this symbol (returns its table).
constexpr const char* kFxLibEntryPoint = "airdaw_fxlib";

// Makes `api` (a loaded library) the source of effects. Call at startup, before any effect is created.
// Returns false (and keeps the built-in library) for nullptr or a different API version. Effects the library
// does not have are still created from the built-in copy, so older sessions keep loading.
bool useFxLibrary(const AwlApi* api);
const AwlApi& activeFxLibrary();   // the loaded library, or the built-in one
const AwlApi& builtinFxLibrary();
void seedFxRandom(unsigned seed);  // srand() for the built-in and the loaded library
bool fxLibraryLoaded();            // a separate library is active

}  // namespace airdaw
