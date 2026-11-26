// Records tracks to disk without ever blocking the audio thread.
// Audio thread: beginTake / push / endTake (lock-free). Writer thread: drains rings into WAV files.
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "airdaw/spsc.h"
#include "airdaw/wav_io.h"

namespace airdaw {

struct FinishedTake {
  int track; std::string path; int64_t start; int64_t length; uint64_t droppedFrames;
};

class FloatRing {
 public:
  explicit FloatRing(size_t cap) : buf_(cap), mask_(cap - 1) {}
  size_t freeSpace() const { return buf_.size() - (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire)); }
  size_t readable() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed); }
  uint64_t written() const { return head_.load(std::memory_order_acquire); }
  void write(const float* d, size_t n) { auto h = head_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < n; ++i) buf_[(h + i) & mask_] = d[i]; head_.store(h + n, std::memory_order_release); }
  void read(float* d, size_t n) { auto t = tail_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < n; ++i) d[i] = buf_[(t + i) & mask_]; tail_.store(t + n, std::memory_order_release); }
 private:
  std::vector<float> buf_; size_t mask_;
  std::atomic<uint64_t> head_{0}, tail_{0};
};

class DiskWriter {
 public:
  DiskWriter(int numTracks, int sampleRate);
  ~DiskWriter();
  void setFolder(const std::string& dir, const std::vector<std::string>& trackNames);  // UI thread, before recording

  // audio thread
  void beginTake(int track, int64_t startPos, int channels);
  void push(int track, const float* l, const float* r, int n, int channels);  // r ignored for mono
  void endTake(int track);

  // UI thread
  std::vector<FinishedTake> pollFinished();
  std::vector<std::string> pollErrors();  // e.g. a take file that could not be created
  uint64_t droppedFrames() const { return dropped_.load(); }
  void flushAndWait();  // blocks until everything queued so far is on disk (UI thread / tests)

 private:
  struct Marker { uint64_t atCount; int64_t start; int channels; };  // start<0 => end of take
  struct Rec {
    FloatRing ring{1u << 21};  // 2M floats: ~10 s of stereo @96k of slack if the disk stalls
    Spsc<Marker, 64> markers;
    // writer-thread state
    bool hasPending = false; Marker pending{};
    WavWriter wav; int channels = 1; int64_t start = 0; std::string path; int takeNo = 0;
    uint64_t droppedAtStart = 0; std::atomic<uint64_t> droppedTake{0};
  };
  void loop();
  void sweep(int t, std::vector<float>& tmp);
  std::string nextPath(int t);

  int sr_;
  std::vector<std::unique_ptr<Rec>> recs_;
  std::string dir_; std::vector<std::string> names_;
  std::atomic<uint64_t> dropped_{0};
  std::mutex mu_; std::vector<FinishedTake> finished_; std::vector<std::string> errors_;
  std::atomic<bool> run_{true};
  std::thread th_;
};
}  // namespace airdaw
