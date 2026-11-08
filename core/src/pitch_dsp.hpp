// SPDX-License-Identifier: GPL-3.0-or-later
// Private DSP building blocks for the pitch detector: FFT plans, FFT autocorrelation (NSDF for
// McLeod, cumulative mean normalised difference for YIN), the Hann-windowed spectrum, and the
// piano partial model. Everything is sized in the constructor; the per-frame calls never allocate.
#pragma once

#include <kiss_fftr.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace etude::pitch_dsp {

inline constexpr int kMinFft = 64;

// Smallest power of two >= n (n >= 1).
int next_pow2(int n);

// Real FFT plans for every power of two from kMinFft to max_size, made up front. KISS FFT's
// out-of-place real transforms at power-of-two sizes never allocate, so forward() and inverse()
// are safe on a real-time thread.
class RealFft {
  public:
    explicit RealFft(int max_size);
    ~RealFft();
    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;

    int max_size() const { return max_size_; }
    // n must be a power of two in [kMinFft, max_size]. `out` gets n/2+1 bins.
    void forward(int n, const float* in, kiss_fft_cpx* out) const;
    // Unscaled inverse: the result is n times the true inverse.
    void inverse(int n, const kiss_fft_cpx* in, float* out) const;

  private:
    int index_of(int n) const;
    int max_size_;
    std::vector<kiss_fftr_cfg> forward_;
    std::vector<kiss_fftr_cfg> inverse_;
};

// One analysis frame: autocorrelation through the FFT (zero-padded to at least twice the frame,
// so there is no wrap-around), then
//   NSDF   n(t) = 2 r(t) / m(t)                                   (McLeod and Wyvill)
//   CMNDF  d'(t) = d(t) t / sum_{j<=t} d(j),  d(t) = m(t) - 2 r(t)  (de Cheveigne and Kawahara)
// with m(t) = sum over the overlap of x_j^2 + x_{j+t}^2, and the magnitude spectrum of the
// Hann-windowed frame, scaled so a sine of amplitude A peaks at about A.
class FrameAnalyzer {
  public:
    FrameAnalyzer(int sample_rate, int max_frame);

    int sample_rate() const { return sample_rate_; }
    int max_frame() const { return max_frame_; }

    // Analyse x[0, length). Lags run 0..max_lag (clamped to length / 2).
    void analyze(const float* x, int length, int max_lag);

    int length() const { return length_; }
    int max_lag() const { return max_lag_; }
    int fft_size() const { return nfft_; }
    float rms() const { return rms_; }

    const float* nsdf() const { return nsdf_.data(); }
    const float* cmndf() const { return cmndf_.data(); }
    // Linear interpolation of the NSDF at a fractional lag; 0 outside the analysed range.
    float nsdf_at(float lag) const;

    float bin_hz() const { return static_cast<float>(sample_rate_) / static_cast<float>(nfft_); }
    // The true resolution of the frame (not the zero-padded bin): sample_rate / length.
    float resolution_hz() const {
        return static_cast<float>(sample_rate_) / static_cast<float>(length_);
    }
    int bins() const { return nfft_ / 2 + 1; }
    const float* magnitude() const { return magnitude_.data(); }
    // Largest magnitude within [hz - tol, hz + tol]; 0 outside the spectrum.
    float peak_magnitude(float hz, float tol_hz) const;
    // As above, also returning the parabolic-interpolated frequency of that peak.
    float peak(float hz, float tol_hz, float* peak_hz) const;
    // Sum of squared magnitudes over [lo, hi].
    float band_energy(float lo_hz, float hi_hz) const;

  private:
    int sample_rate_;
    int max_frame_;
    RealFft fft_;
    std::vector<float> hann_table_;
    std::vector<float> time_;
    std::vector<kiss_fft_cpx> spectrum_;
    std::vector<float> acf_;
    std::vector<double> energy_prefix_;
    std::vector<float> nsdf_;
    std::vector<float> cmndf_;
    std::vector<float> magnitude_;
    int length_ = 0;
    int max_lag_ = 0;
    int nfft_ = kMinFft;
    float rms_ = 0.0f;
};

// A period estimate from one frame.
struct LagCandidate {
    float lag = 0.0f;   // fractional, in samples
    float score = 0.0f; // clarity, 0..1 (NSDF peak, or 1 - CMNDF dip)
};

inline constexpr int kMaxLagCandidates = 4;

// McLeod: key maxima of the NSDF; the first one within `k` of the highest wins. Writes the winner
// first, then the highest key maximum if it is a different peak and not a multiple of the
// winner's lag, then key maxima at the winner's lag divided by 2..6 (clarity >= 0.5): when a
// second tone makes the mix periodic at a common multiple, the true period is among those.
// Lags below min_lag are ignored. Returns the count (0..kMaxLagCandidates).
int mpm_candidates(const FrameAnalyzer& a, float min_lag, float k, LagCandidate* out);
// YIN: the first CMNDF dip under `threshold` (followed down to its local minimum), else the global
// minimum, then the global minimum and dips at the winner's lag divided by 2..6, as above.
int yin_candidates(const FrameAnalyzer& a, float min_lag, float threshold, LagCandidate* out);

// Typical inharmonicity coefficient B for a piano string at this MIDI note, interpolated in the
// log domain between per-register anchors (wound bass strings, a minimum near the break, then
// steeply rising in the short treble strings).
float piano_inharmonicity(int note);
// Frequency of partial n (1-based) for fundamental f0: n f0 sqrt(1 + B n^2).
inline float partial_hz(float f0, int n, float b) {
    const float nn = static_cast<float>(n);
    return f0 * nn * std::sqrt(1.0f + b * nn * nn);
}

// Summed squared peak magnitude of partials n = first, first + step, ... of f0, below max_hz.
// Each partial is searched within tol(n) = min(max(2 bins, 1.5% of the partial), cap_hz).
// `notch_hz` > 0 skips partials within 3% of its multiples (see partials_complete).
float partial_energy(const FrameAnalyzer& a, float f0, float b, int first, int step, int max_n,
                     float max_hz, float cap_hz, float notch_hz = 0.0f);
// True when `f` lies within 3% (at most 15% of notch_hz) of a multiple of `notch_hz` (> 0).
bool in_notch(float f, float notch_hz);

// Evidence that a period estimate is an octave off, from the windowed spectrum.
struct OctaveEvidence {
    float odd_ratio = 1.0f;    // energy at odd partials / even partials of f0 (low: octave above)
    float half_ratio = 0.0f;   // energy at f0/2's odd partials / f0's partials (high: octave below)
    float third_ratio = 0.0f;  // the same for f0/3 (partials not on f0's series)
    float triple_ratio = 1.0f; // f0's partials off the 3n series / on it (low: 3 f0 sounds)
    bool odd_resolvable = false;
    bool half_resolvable = false;
    bool third_resolvable = false;
};
OctaveEvidence octave_evidence(const FrameAnalyzer& a, float f0, float b_f0, float b_sub,
                               float min_f0_hz, float max_hz, float notch_hz = 0.0f);

// Fraction of spectral energy between 0.7 f0 and the last partial below max_hz that sits on f0's
// stretched partial series: near 1 for a clean piano tone, low for noise and thumps.
float harmonicity(const FrameAnalyzer& a, float f0, float b, float max_hz);

// Whether f0's first partials (up to 6, below max_hz) are there, each within 26 dB of the
// strongest spectral peak from 0.7 f0 up: at most one may be missing (the fundamental alone when
// only one or two fit). A piano note has nearly all of them; the "missing fundamental" of two
// notes sounding together (their common subharmonic) lacks several. True while the window is
// too short to resolve the partials; then at least a quarter of the energy from 0.5 f0 to max_hz
// must lie below 16.5 f0, so a treble tone cannot pass for a deep bass note.
// `notch_hz` > 0: the frame was comb-filtered at that fundamental; partials within 3% of its
// multiples were filtered away with the old note and are skipped.
bool partials_complete(const FrameAnalyzer& a, float f0, float b, float max_hz,
                       float notch_hz = 0.0f);

// The energy at f0's fundamental over that of its strongest partial among 1..8 below max_hz, each
// searched as in partial_energy: near 0 for the period several notes share an octave or more
// below the lowest of them, whose partials are only where those notes are. In a frame too short
// to separate partials well the fundamental's search stops where the second partial's main lobe
// begins, so that partial's leak is not taken for it.
float fundamental_share(const FrameAnalyzer& a, float f0, float b, float max_hz);
// Whether f0's fundamental stands out of the spectrum around it: its energy at least `ratio`
// times that half a fundamental below it or above it (one of the two; the other may be another
// note's partial). Broadband sound, such as hammer noise, fills the fundamental's place and its
// surroundings alike. True when the frame cannot separate partials closer than a third of f0.
bool fundamental_stands_out(const FrameAnalyzer& a, float f0, float b, float ratio);
// The frames fundamental_stands_out can judge: a Hann main lobe under this fraction of f0.
inline constexpr float kBesideLobe = 0.3f;

// Whether a note at f0 is really there next to a still-ringing note at `ringing_hz`, a whole
// multiple of f0. Two notes in a simple ratio repeat together at a common subharmonic, which
// carries only the partials the two notes share with it. A real note at f0 also has its own
// partials, off the ringing note's series: of f0's partials 1..6 that are not near a multiple of
// `ringing_hz`, all must stand within 26 dB of the strongest peak (one may be missing when four
// or more are checked). False while the frame cannot separate partials f0 apart.
bool own_partials_present(const FrameAnalyzer& a, float f0, float b, float max_hz,
                          float ringing_hz);

// What a note at f0 shows of new sound: the frame `after` (audio after the onset) against `before`
// (audio just before the onset), both of the same length, partial by partial over partials 1..8
// below max_hz. A piano partial only grows at an attack, so growth there is the new strike; a
// partial that does not grow was already ringing.
struct FreshEvidence {
    bool resolved = false;   // `after` separates partials f0 apart
    float before = 0.0f;     // summed squared magnitude of the partials before the onset
    float after = 0.0f;      // ... after it
    float grown = 0.0f;      // summed squared growth, max(0, after - before) per partial
    float grown_odd = 0.0f;  // growth at the odd partials
    float grown_even = 0.0f; // ... at the even ones
    // The least, over k >= 3, of the growth off the multiples of k against the growth on them
    // (1 when none grew): near 0 when only the partials of a note k times higher grew.
    float off_series = 1.0f;
    float before_odd = 0.0f; // `before` and `after` at the odd partials only
    float after_odd = 0.0f;
    float before_fundamental = 0.0f; // `before` and `after` at the fundamental only
    float after_fundamental = 0.0f;
    float grown_fundamental = 0.0f; // the growth at the fundamental
    float grown_strongest = 0.0f;   // ... and at the partial that grew most
};
FreshEvidence fresh_evidence(const FrameAnalyzer& before, const FrameAnalyzer& after, float f0,
                             float b, float max_hz);

// Growth at an onset at f0's own partials against growth at the partials of the notes a
// semitone and a tone above and below, over the partials (1..8, below max_hz) where `after`
// separates a semitone. A note struck again grows at its own partials; a neighbour struck under
// it grows a step away. `separable` counts the partials compared.
struct NeighbourGrowth {
    int separable = 0;
    float own = 0.0f;
    float own_after = 0.0f;  // the pitch's own partials after the onset
    float neighbours = 0.0f; // the largest neighbour's growth
};
NeighbourGrowth neighbour_growth(const FrameAnalyzer& before, const FrameAnalyzer& after, float f0,
                                 float b, float max_hz);

// Relative sharpening of a period-based f0 estimate caused by stretched partials, for when the
// spectrum cannot resolve the partials to refine_f0: the autocorrelation peak moves by about
// (B/2) <n^4>/<n^2>, the moments taken over the spectrum's energy up to 16 f0 (no need to
// resolve individual partials). Divide the estimate by (1 + result).
float stretch_bias(const FrameAnalyzer& a, float f0, float b, float max_hz);

// Fundamental refined from the spectral peaks of partials 1..4, undoing the stretch: the mean of
// peak_n / (n sqrt(1 + B n^2)) weighted by magnitude / n, over the partials whose weight is at
// least a fifth of the best one's (14 dB). Stretched partials pull the autocorrelation period
// sharp (tens of cents in the treble); this does not. Returns `f0` if no partial is found.
// `search` is the relative half-width searched around each partial (0.02 by default; 0.06, a
// semitone, to pull in an estimate that is still far off).
float refine_f0(const FrameAnalyzer& a, float f0, float b, float max_hz, float search = 0.02f);

} // namespace etude::pitch_dsp
