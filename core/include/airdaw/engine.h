// AirDAW mixing engine: 24 tracks (mono/stereo), 4 buses, master. All inserts are Airwindows.
// Threading model:
//   * UI thread calls the setters / insert commands. Nothing here blocks the audio thread.
//   * Audio thread calls process(). It never allocates, locks, or does I/O.
//   * Inserts are created on the UI thread, handed over via a lock-free queue, and handed back
//     the same way when removed so they are freed off the audio thread.
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "airdaw/fx_host.h"
#include "airdaw/spsc.h"
#include "airdaw/transport.h"
#include "airdaw/disk_writer.h"

namespace airdaw {

constexpr int kMaxTracks = 24;
constexpr int kNumBuses = 4;
constexpr int kMaxInserts = 8;
constexpr int kMaxBlock = 8192;

enum class Route : int { Master = 0, Bus1, Bus2, Bus3, Bus4 };  // value-1 = bus index

// A recorded take, held in RAM for playback (streaming from disk comes later).
struct Take { std::vector<float> l, r; int64_t start = 0; std::string path; };

struct Meter {
  std::atomic<float> peakL{0}, peakR{0}, rmsL{0}, rmsR{0};
};

// One mixer channel (track, bus or master).
struct Channel {
  std::atomic<float> gainDb{0.f};     // fader, -inf..+6
  std::atomic<float> trimDb{0.f};     // clean input gain knob
  std::atomic<float> pan{0.f};        // -1..1 (stereo: balance)
  std::atomic<bool> mute{false}, solo{false};
  std::atomic<int> route{0};          // Route
  std::atomic<bool> stereo{false};    // tracks only
  std::atomic<bool> armed{false};
  std::atomic<int> inputL{-1}, inputR{-1};  // hardware input index, -1 none
  Meter meter;
  std::array<FxInstance*, kMaxInserts> insert{};  // audio-thread-owned pointers
  std::atomic<const Take*> take{nullptr};         // active take for playback
  bool recActive = false;                          // audio thread only
  float smoothGain = 1.f, smoothTrim = 1.f;       // audio thread only
  std::string name;                                // UI thread only
};

class Engine {
 public:
  explicit Engine(double sampleRate);
  ~Engine();

  double sampleRate() const { return sr_; }
  Channel& track(int i) { return tracks_[i]; }
  Channel& bus(int i) { return buses_[i]; }
  Channel& master() { return master_; }

  // --- UI thread ---
  // channelId: 0..23 tracks, 100..103 buses, 200 master.
  bool setInsert(int channelId, int slot, const std::string& effect);  // empty = remove
  FxInstance* insertAt(int channelId, int slot);  // UI-side view (owned by the engine)
  void collectGarbage();                           // frees retired inserts

  // --- transport & recording (UI thread) ---
  Transport& transport() { return transport_; }
  // Records every armed track (post-input, pre-insert, i.e. the dry signal) into `folder`.
  void startRecording(const std::string& folder);
  void stopRecording() { transport_.recording = false; }
  std::vector<FinishedTake> pollFinishedTakes() { return writer_->pollFinished(); }
  void flushRecording() { writer_->flushAndWait(); }
  uint64_t droppedFrames() const { return writer_->droppedFrames(); }
  // Loads a WAV as a take for a track; returns its index or -1. Takes live until the engine is destroyed.
  int addTake(int track, const std::string& path, int64_t startSample);
  void setActiveTake(int track, int index);  // -1 = none
  int numTakes(int track) const { return (int)takes_[track].size(); }
  const Take* takeAt(int track, int i) const { return takes_[track][i].get(); }

  // --- audio thread ---
  // in: nIn hardware input buffers; out: nOut (>=2) hardware output buffers (stereo main out).
  void process(const float* const* in, int nIn, float* const* out, int nOut, int n);

  // Fixed latency the engine adds (0: all Airwindows inserts are zero-latency).
  int latencySamples() const { return 0; }

 private:
  struct Cmd { int channelId; int slot; FxInstance* fx; };
  Channel* resolve(int id);
  void applyCommands();
  void runChain(Channel& c, float* l, float* r, int n);
  void updateMeter(Meter& m, const float* l, const float* r, int n);

  double sr_;
  Transport transport_;
  std::unique_ptr<DiskWriter> writer_;
  std::array<std::vector<std::unique_ptr<Take>>, kMaxTracks> takes_;
  std::array<Channel, kMaxTracks> tracks_;
  std::array<Channel, kNumBuses> buses_;
  Channel master_;
  // UI-side ownership map (what the UI believes is installed).
  std::array<std::array<std::unique_ptr<FxInstance>, kMaxInserts>, kMaxTracks + kNumBuses + 1> owned_;
  std::vector<std::unique_ptr<FxInstance>> retiring_;  // removed, awaiting return from audio thread
  Spsc<Cmd, 1024> toAudio_;
  Spsc<FxInstance*, 1024> fromAudio_;

  std::array<std::vector<float>, 2> busBuf_[kNumBuses];
  std::array<std::vector<float>, 2> masterBuf_;
  std::array<std::vector<float>, 2> work_;
};

}  // namespace airdaw
