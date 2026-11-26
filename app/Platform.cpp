// Platform-specific bits kept out of headers (windows.h must not leak into JUCE-using headers).
#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#endif

void raiseWorkerThreadPriority() {
#if JUCE_WINDOWS
  ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);  // highest non-realtime priority
#endif
  // Linux/macOS: left at default for now (real-time scheduling needs rtkit / entitlements).
}
