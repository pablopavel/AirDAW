// Minimal WAV reader/writer. Writer produces 32-bit IEEE float WAV (format 3), 1 or 2 channels.
// (Standard RIFF: files are limited to 4 GB, about 3 h of stereo at 96 kHz. RF64 can come later.)
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace airdaw {

// fopen for UTF-8 paths. On Windows plain fopen reads paths in the ANSI code page, so names like
// "Café" would fail; this uses the wide-character API there.
std::FILE* openFile(const std::string& utf8Path, const char* mode);

class WavWriter {
 public:
  ~WavWriter() { close(); }
  bool open(const std::string& path, int channels, int sampleRate);
  bool write(const float* interleaved, size_t frames);
  void close();  // patches header sizes
  bool isOpen() const { return f_ != nullptr; }
  uint64_t framesWritten() const { return frames_; }
 private:
  std::FILE* f_ = nullptr;
  int ch_ = 0;
  uint64_t frames_ = 0;
};

struct WavData {
  int channels = 0, sampleRate = 0;
  std::vector<float> left, right;  // right empty for mono
};
// Reads float32, PCM16 and PCM24. Returns false on unsupported/invalid files.
bool readWav(const std::string& path, WavData& out);

}  // namespace airdaw
