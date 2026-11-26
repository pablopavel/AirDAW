// Master loudness and level analysis (ITU-R BS.1770-4 / EBU R128 / EBU Tech 3342), fed from the engine's
// master tap on the UI side. Not real-time critical: never runs on the audio thread.
//   momentary  400 ms window          short-term  3 s window          (both updated every 100 ms)
//   integrated gated (-70 LUFS absolute, -10 LU relative), LRA from short-term values (-20 LU relative, 10th..95th pct)
//   true peak  4x oversampled peak (dBTP)          correlation  ~300 ms Pearson L/R
#pragma once
#include <array>
#include <cstddef>
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
};

class LoudnessMeter {
 public:
  explicit LoudnessMeter(double sampleRate);
  // Interleaved stereo. `accumulate` = feed integrated loudness and LRA (e.g. only while the transport plays).
  void process(const float* interleaved, size_t frames, bool accumulate = true);
  LoudnessReadout read() const;
  void resetIntegrated();  // integrated, LRA, true/sample peak holds
  double sampleRate() const { return sr_; }

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
  // correlation / RMS / balance: exponential averages
  double cLR_ = 0, cLL_ = 0, cRR_ = 0, rmsAcc_ = 0, coef_ = 0, tpDecay_ = 0;
};

}  // namespace airdaw
