// Prints the manifest of the AirwindowsFX library it links against (used by the release workflow):
//   airwindows_fxlib_manifest <sha256 of AirwindowsFX.dll>  >  manifest.json
// Loading the DLL here also proves the build works before it is published.
#include <cstdio>
#include <string>
#include "airdaw/fx_library.h"

extern "C" const AwlApi* airdaw_fxlib();

static std::string esc(const char* s) {
  std::string o;
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') { o += '\\'; o += *s; }
    else if ((unsigned char)*s < 0x20) o += ' ';
    else o += *s;
  }
  return o;
}

int main(int argc, char** argv) {
  const AwlApi* a = airdaw_fxlib();
  if (!a || a->apiVersion != airdaw::kFxLibApi || a->count() <= 0) { std::fprintf(stderr, "bad library\n"); return 1; }
  std::printf("{\n  \"format\": \"airdaw-fxlib\",\n  \"api\": %d,\n  \"build\": \"%s\",\n  \"file\": \"AirwindowsFX.dll\",\n  \"sha256\": \"%s\",\n  \"effects\": [",
              a->apiVersion, esc(a->buildId).c_str(), argc > 1 ? esc(argv[1]).c_str() : "");
  for (int i = 0; i < a->count(); ++i) std::printf("%s\"%s\"", i ? ", " : "", esc(a->name(i)).c_str());
  std::printf("]\n}\n");
  return 0;
}
