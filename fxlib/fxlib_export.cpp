// AirwindowsFX.dll: exports the Airwindows library table (airdaw/fx_library.h) for AirDAW to load.
#include "airdaw/fx_library.h"

#ifdef _WIN32
#define AIRDAW_EXPORT __declspec(dllexport)
#else
#define AIRDAW_EXPORT __attribute__((visibility("default")))
#endif

extern "C" AIRDAW_EXPORT const AwlApi* airdaw_fxlib() { return airdaw_fxlib_builtin(); }
