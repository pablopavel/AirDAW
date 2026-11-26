// Master loudness and level analysis (ITU-R BS.1770-4 / EBU R128 / EBU Tech 3342), fed from the engine's
// master tap on the UI side. Not real-time critical: never runs on the audio thread.
//   momentary  400 ms window          short-term  3 s window          (both updated every 100 ms)
//   integrated gated (-70 LUFS absolute, -10 LU relative), LRA from short-term values (-20 LU relative, 10th..95th pct)
//   true peak  4x oversampled peak (dBTP)          correlation  ~300 ms Pearson L/R
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace airdaw {

struct LoudnessReadout {
  float momentary = -200.f, shortTerm = -200.f, integrated = -200.f;  // LUFS (-200 = no signal yet)
  float lra = 0.f;                                                    // LU
  float truePeak = -200.f;      // dBTP, highest since reset
  float truePeakNow = -200.f;   // dBTP, last ~300 ms
  float samplePeak = -200.f;    // dBFS, highest since reset
  float rms = -200.f;           // dBFS, last ~300 ms (both channels)
  float correlation = 0.f;      // -1..+1
  float balance = 0.f;          // -1 (all left) .. +1 (all right), last ~300 ms
  // Programme statistics over what was accumulated (since the last reset):
  float plr = 0.f;              // peak-to-loudness ratio: true peak - integrated, dB
  float crest = 0.f;            // sample peak - RMS, dB
  float dr = 0.f;               // dynamic range (DR meter method: 3 s blocks, 2nd-highest peak vs top-20% RMS)
  // Stereo image, last ~300 ms:
  float midDb = -200.f, sideDb = -200.f;  // RMS of (L+R)/2 and (L-R)/2, dBFS
  float width = 0.f;            // side / mid level, % (0 = mono)
  float monoLoss = 0.f;         // level change when summed to mono, dB (0 = identical channels, -3 = unrelated)
  int overs = 0;                // true-peak excursions above 0 dBTP since reset
};

class LoudnessMeter {
 public:
  explicit LoudnessMeter(double sampleRate);
  // Interleaved stereo. `accumulate` = feed integrated loudness and LRA (e.g. only while the transport plays).
  void process(const float* interleaved, size_t frames, bool accumulate = true);
  LoudnessReadout read() const;
  void resetIntegrated();  // integrated, LRA, true/sample peak holds, overs, DR/crest statistics, history
  void resetPeaks() { tpHold_ = 0.f; spHold_ = 0.f; overs_ = 0; }  // peak holds and overs only
  double sampleRate() const { return sr_; }
  // Short-term loudness once per second while accumulating (for the loudness history tool), newest last.
  const std::vector<float>& history() const { return history_; }

 private:
  struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
  double sr_;
  std::array<Biquad, 2> shelf_{}, hp_{};          // K-weighting per channel
  size_t subLen_ = 0, subFill_ = 0;               // 100 ms sub-blocks
  double subSum_ = 0;                             // K-weighted energy (L + R) of the current sub-block
  std::vector<double> subs_;                      // ring of the last 30 sub-blocks' mean energy
  size_t subCount_ = 0;
  std::vector<double> gatedBlocks_;               // 400 ms block energies, for integrated loudness
  std::vector<double> shortTerms_;                // 3 s block energies, for LRA
  float momentary_ = -200.f, shortTerm_ = -200.f;
  // true peak: 4x polyphase interpolation
  static constexpr int kPhases = 4, kTaps = 12;
  std::array<std::array<float, kTaps>, kPhases> fir_{};
  std::array<std::array<float, kTaps>, 2> hist_{};
  size_t histPos_ = 0;
  float tpHold_ = 0.f, tpNow_ = 0.f, spHold_ = 0.f;
  // correlation / RMS / balance / mid-side: exponential averages
  double cLR_ = 0, cLL_ = 0, cRR_ = 0, rmsAcc_ = 0, midAcc_ = 0, sideAcc_ = 0, coef_ = 0, tpDecay_ = 0;
  // programme statistics (accumulated)
  double sumSq_ = 0; uint64_t sumN_ = 0;
  struct DrBlock { float peak[2], rms[2]; };
  std::vector<DrBlock> drBlocks_;
  double drSq_[2] = {0, 0}; float drPeak_[2] = {0, 0}; size_t drFill_ = 0, drLen_ = 0;
  int overs_ = 0; bool over_ = false;
  std::vector<float> history_;
  int subsSinceHistory_ = 0;
};

// 31-band 1/3-octave real-time analyser (ISO centres 20 Hz .. 20 kHz) on the mono sum. Each band is two
// cascaded band-pass biquads (about -3 dB at the band edges, -9 dB at the neighbouring centre).
class BandAnalyzer {
 public:
  static constexpr int kBands = 31;
  static const float kCentres[kBands];
  explicit BandAnalyzer(double sampleRate, double smoothingSeconds = 0.3);
  void process(const float* interleaved, size_t frames);
  float bandDb(int b) const;  // RMS level of band b, dBFS (sine-calibrated)
 private:
  struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
  std::array<std::array<Biquad, 2>, kBands> f_{};
  std::array<double, kBands> energy_{};
  std::array<bool, kBands> active_{};  // bands above Nyquist are skipped
  double coef_;
};

// Correlation and width in 4 bands (sub < 120 Hz, low-mid 120-500 Hz, mid 500 Hz-4 kHz, high > 4 kHz).
class StereoBandMeter {
 public:
  static constexpr int kBands = 4;
  explicit StereoBandMeter(double sampleRate);
  void process(const float* interleaved, size_t frames);
  float correlation(int band) const;
 private:
  struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
  std::array<std::array<std::array<Biquad, 2>, 2>, kBands> f_{};  // [band][channel][stage]
  std::array<double, kBands> lr_{}, ll_{}, rr_{};
  double coef_;
};

}  // namespace airdaw
