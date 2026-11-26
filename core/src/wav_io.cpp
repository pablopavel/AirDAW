#include "airdaw/wav_io.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace airdaw {

std::FILE* openFile(const std::string& utf8Path, const char* mode) {
#ifdef _WIN32
  const std::wstring wpath = std::filesystem::u8path(utf8Path).wstring();
  std::wstring wmode; for (const char* m = mode; *m; ++m) wmode += (wchar_t)*m;
  return _wfopen(wpath.c_str(), wmode.c_str());
#else
  return std::fopen(utf8Path.c_str(), mode);
#endif
}

static void w32(std::FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
static void w16(std::FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }

bool WavWriter::open(const std::string& path, int channels, int sampleRate, int bits) {
  close();
  if (bits != 32 && bits != 24) return false;
  f_ = openFile(path, "wb");
  if (!f_) return false;
  ch_ = channels; frames_ = 0; bytes_ = bits / 8;
  std::fwrite("RIFF", 1, 4, f_); w32(f_, 0);
  std::fwrite("WAVE", 1, 4, f_);
  std::fwrite("fmt ", 1, 4, f_); w32(f_, 16);
  w16(f_, bits == 32 ? 3 : 1); w16(f_, (uint16_t)channels); w32(f_, (uint32_t)sampleRate);  // 3 = IEEE float, 1 = PCM
  w32(f_, (uint32_t)(sampleRate * channels * bytes_)); w16(f_, (uint16_t)(channels * bytes_)); w16(f_, (uint16_t)bits);
  std::fwrite("fact", 1, 4, f_); w32(f_, 4); w32(f_, 0);  // also kept for PCM (optional there) so the layout is fixed
  std::fwrite("data", 1, 4, f_); w32(f_, 0);
  return true;
}

bool WavWriter::write(const float* d, size_t frames) {
  if (!f_) return false;
  if (bytes_ == 4) {
    if (std::fwrite(d, sizeof(float) * ch_, frames, f_) != frames) return false;
  } else {  // 24-bit PCM, clipped to full scale
    unsigned char buf[3 * 1024];
    const size_t total = frames * (size_t)ch_;
    for (size_t i = 0; i < total;) {
      const size_t n = std::min<size_t>(1024, total - i);
      for (size_t k = 0; k < n; ++k) {
        const float x = std::max(-1.f, std::min(d[i + k], 8388607.f / 8388608.f));
        const int32_t v = (int32_t)std::lrint((double)x * 8388608.0);
        buf[3 * k] = (unsigned char)(v & 0xff); buf[3 * k + 1] = (unsigned char)((v >> 8) & 0xff); buf[3 * k + 2] = (unsigned char)((v >> 16) & 0xff);
      }
      if (std::fwrite(buf, 3, n, f_) != n) return false;
      i += n;
    }
  }
  frames_ += frames;
  return true;
}

void WavWriter::close() {
  if (!f_) return;
  const uint32_t dataBytes = (uint32_t)(frames_ * ch_ * bytes_);
  std::fseek(f_, 4, SEEK_SET);  w32(f_, 4 + 8 + 16 + 12 + 8 + dataBytes);  // RIFF size
  std::fseek(f_, 44, SEEK_SET); w32(f_, (uint32_t)frames_);               // fact sample count
  std::fseek(f_, 52, SEEK_SET); w32(f_, dataBytes);                       // data size
  std::fclose(f_);
  f_ = nullptr;
}

bool readWav(const std::string& path, WavData& o) {
  std::FILE* f = openFile(path, "rb");
  if (!f) return false;
  auto rd32 = [&](uint32_t& v) { return std::fread(&v, 4, 1, f) == 1; };
  char id[4]; uint32_t sz;
  if (std::fread(id, 1, 4, f) != 4 || std::memcmp(id, "RIFF", 4) || !rd32(sz) ||
      std::fread(id, 1, 4, f) != 4 || std::memcmp(id, "WAVE", 4)) { std::fclose(f); return false; }
  uint16_t fmt = 0, ch = 0, bits = 0; uint32_t sr = 0;
  bool ok = false;
  while (std::fread(id, 1, 4, f) == 4 && rd32(sz)) {
    if (!std::memcmp(id, "fmt ", 4)) {
      uint8_t b[16]; if (sz < 16 || std::fread(b, 1, 16, f) != 16) break;
      std::memcpy(&fmt, b, 2); std::memcpy(&ch, b + 2, 2); std::memcpy(&sr, b + 4, 4); std::memcpy(&bits, b + 14, 2);
      std::fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR);
    } else if (!std::memcmp(id, "data", 4)) {
      if (!(ch == 1 || ch == 2)) break;
      const int bps = bits / 8;
      if (!((fmt == 3 && bits == 32) || (fmt == 1 && (bits == 16 || bits == 24)))) break;
      size_t frames = sz / (size_t)(bps * ch);
      std::vector<uint8_t> raw(frames * bps * ch);
      size_t got = std::fread(raw.data(), bps * ch, frames, f);  // tolerate truncated files
      frames = got;
      o.channels = ch; o.sampleRate = (int)sr;
      o.left.assign(frames, 0.f); if (ch == 2) o.right.assign(frames, 0.f);
      for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c) {
          const uint8_t* p = raw.data() + (i * ch + c) * bps; float v;
          if (fmt == 3) std::memcpy(&v, p, 4);
          else if (bits == 16) v = (int16_t)(p[0] | (p[1] << 8)) / 32768.f;
          else { int32_t x = (p[0] << 8) | (p[1] << 16) | ((uint32_t)p[2] << 24); v = (x >> 8) / 8388608.f; }
          (c == 0 ? o.left : o.right)[i] = v;
        }
      ok = true; break;
    } else std::fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
  }
  std::fclose(f);
  return ok;
}
}  // namespace airdaw
