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
#include "airdaw/insert.h"
#include "airdaw/spsc.h"
#include "airdaw/transport.h"
#include "airdaw/disk_writer.h"
#include "airdaw/work_pool.h"

namespace airdaw {

constexpr int kMaxTracks = 24;
constexpr int kNumBuses = 4;
constexpr int kMaxInserts = 8;
constexpr int kMaxBlock = 8192;

enum class Route : int { Master = 0, Bus1, Bus2, Bus3, Bus4 };  // value-1 = bus index

// A recorded take, held in RAM for playback (streaming from disk comes later).
// `keep` is the user's star in the takes bar (UI thread only; saved in sessions).
struct Take { std::vector<float> l, r; int64_t start = 0; std::string path; bool keep = false; };

struct Meter {
  std::atomic<float> peakL{0}, peakR{0}, rmsL{0}, rmsR{0};
};

// Click track. Mixed into the hardware output after the master chain, so it is never recorded,
// never reaches master inserts (e.g. a meter), and is not affected by the master fader.
// Clicks are computed from the transport position, so they stay on the grid through loops and seeks.
struct Metronome {
  std::atomic<bool> enabled{false};   // click while playing / recording
  std::atomic<float> bpm{120.f};      // 20..400
  std::atomic<int> beatsPerBar{4};    // 1..16; beat 1 of each bar is accented
  std::atomic<float> levelDb{-6.f};   // click level at the output
  std::atomic<int> countInBars{0};    // 0..4 bars of click before recording starts (plays even if `enabled` is off)
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
  // Live input is heard only on armed tracks with monitoring on (or while auditioning, e.g. the test tone).
  // Recording does not depend on monitoring: an armed track records its input either way.
  std::atomic<bool> monitor{true};
  std::atomic<bool> audition{false};
  std::atomic<int> inputL{-1}, inputR{-1};  // hardware input index, -1 none
  Meter meter;
  std::array<InsertProcessor*, kMaxInserts> insert{};  // audio-thread-owned pointers
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
  bool setInsert(int channelId, int slot, const std::string& effect);  // Airwindows by name; empty = remove
  // Installs a ready-made insert (e.g. a hosted VST3). The engine takes ownership.
  bool setInsertProcessor(int channelId, int slot, std::unique_ptr<InsertProcessor> p);
  // Moves the insert at `from` to position `to` of the same chain (the others shift), keeping the same
  // instance, so its settings and internal state survive. The audio thread sees the new order in one step.
  bool moveInsert(int channelId, int from, int to);
  // Used by session loading for ids that are not Airwindows names ("vst3:...").
  void setInsertFactory(InsertFactory f) { factory_ = std::move(f); }
  InsertFactory& insertFactory() { return factory_; }
  InsertProcessor* insertAt(int channelId, int slot);  // UI-side view (owned by the engine)
  void collectGarbage();                           // frees retired inserts

  // --- transport & recording (UI thread) ---
  Transport& transport() { return transport_; }
  Metronome& metronome() { return metro_; }
  // Records every armed track (post-input, pre-insert, i.e. the dry signal) into `folder`.
  // If the transport is stopped and a count-in is set, the click counts in first: the position is held
  // and nothing is recorded until the count-in ends, then recording starts on the exact sample.
  void startRecording(const std::string& folder);
  void stopRecording() { transport_.recording = false; }
  std::vector<FinishedTake> pollFinishedTakes() { return writer_->pollFinished(); }
  std::vector<std::string> pollRecordingErrors() { return writer_->pollErrors(); }  // takes that could not be written
  void flushRecording() { writer_->flushAndWait(); }
  uint64_t droppedFrames() const { return writer_->droppedFrames(); }
  // Loads a WAV as a take for a track; returns its index or -1. Takes live until the engine is destroyed.
  int addTake(int track, const std::string& path, int64_t startSample);
  void setActiveTake(int track, int index);  // -1 = none
  // Take list of the current session. Indices are relative to the list (what sessions save).
  int numTakes(int track) const { return (int)takes_[track].size() - takeBase_[track]; }
  const Take* takeAt(int track, int i) const { return takes_[track][takeBase_[track] + i].get(); }
  // Starts an empty take list for a track (session load / new session) and deactivates its take.
  // Earlier takes stay in memory until the engine is destroyed (the audio thread may still be reading
  // one), but are no longer listed or saved. UI thread only.
  void clearTakeList(int track);
  // Records that a take's file moved (e.g. into a project folder on Save As). UI thread only:
  // the audio thread never reads `path`.
  void setTakePath(int track, int i, const std::string& path) { takes_[track][takeBase_[track] + i]->path = path; }
  void setTakeKeep(int track, int i, bool keep) { takes_[track][takeBase_[track] + i]->keep = keep; }
  // Removes a take from the track's list (deactivating it if it was playing). Its audio stays in memory until
  // the engine is destroyed, because the audio thread may still be reading it. UI thread only.
  void removeTake(int track, int i);

  // Worker threads for parallel track/bus processing (-1 = automatic: cores-1, max 7; 0 = single-threaded).
  // Not real-time safe: call while audio is stopped. Output is bit-identical for any thread count.
  void setWorkerThreads(int n);
  int workerThreads() const { return pool_.workers(); }
  // Runs on each worker when it starts (the app uses it to raise thread priority).
  static inline std::function<void()> workerInit;

  // --- audio thread ---
  // in: nIn hardware input buffers; out: nOut (>=2) hardware output buffers (stereo main out).
  void process(const float* const* in, int nIn, float* const* out, int nOut, int n);

  // Fixed latency the engine adds (0: all Airwindows inserts are zero-latency).
  // Latency the engine adds. Hosted plugins that report latency are NOT yet delay-compensated.
  int latencySamples() const { return 0; }

 private:
  // slot >= 0: put `fx` in that slot (the old one is handed back to be freed).
  // slot == kChainCmd: replace the whole chain with `chain` (a reorder: same instances, nothing freed).
  static constexpr int kChainCmd = -1;
  struct Cmd { int channelId; int slot; InsertProcessor* fx; std::array<InsertProcessor*, kMaxInserts> chain{}; };
  struct SegCtx;
  void processTrack(int ti, const SegCtx& c);
  void processBus(int bi, int n, bool anyBusSolo);
  Channel* resolve(int id);
  void applyCommands();
  void runChain(Channel& c, float* l, float* r, int n);
  void updateMeter(Meter& m, const float* l, const float* r, int n);
  double beatSamples() const;
  void addClick(float* l, float* r, int n, int64_t pos);

  double sr_;
  Transport transport_;
  Metronome metro_;
  std::unique_ptr<DiskWriter> writer_;
  std::array<std::vector<std::unique_ptr<Take>>, kMaxTracks> takes_;
  std::array<int, kMaxTracks> takeBase_{};  // first take of the current session's list
  std::vector<std::unique_ptr<Take>> removedTakes_;  // kept alive until the engine is destroyed
  std::array<Channel, kMaxTracks> tracks_;
  std::array<Channel, kNumBuses> buses_;
  Channel master_;
  // UI-side ownership map (what the UI believes is installed).
  std::array<std::array<std::unique_ptr<InsertProcessor>, kMaxInserts>, kMaxTracks + kNumBuses + 1> owned_;
  std::vector<std::unique_ptr<InsertProcessor>> retiring_;
  InsertFactory factory_;  // removed, awaiting return from audio thread
  Spsc<Cmd, 1024> toAudio_;
  Spsc<InsertProcessor*, 1024> fromAudio_;

  std::array<std::vector<float>, 2> busBuf_[kNumBuses];
  std::array<std::vector<float>, 2> masterBuf_;
  std::array<std::vector<float>, 2> work_;
  std::array<std::array<std::vector<float>, 2>, kMaxTracks> trackBuf_;
  std::array<char, kMaxTracks> trackLive_{};
  WorkPool pool_;
};

}  // namespace airdaw
