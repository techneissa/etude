// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of pitch_detect.cpp share: the detector's constants and its state. Not part of the
// library's interface. The short helpers are defined here, in the class, so they stay inline;
// the longer steps are defined in pitch_detect*.cpp.
#pragma once

#include "etude/pitch_detect.hpp"

#include "pitch_dsp.hpp"
#include "etude/pitch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace etude {

namespace pitch_detect_detail {

namespace dsp = pitch_dsp;

inline constexpr int kHistory = 8192;      // power of two; the longest frame plus the onset look-back

inline constexpr int kMaxFrame = 4096;     // longest analysis frame (85 ms at 48 kHz)

inline constexpr int kMinFrame = 192;      // shortest frame worth analysing

inline constexpr int kOnsetWindow = 512;   // spectral-flux frame

inline constexpr int kOnsetLookBack = 640; // samples searched to place the onset precisely

inline constexpr int kOnsetBlock = 32;

inline constexpr int kGateHops = 4;          // gate level: RMS over the last 512 samples

inline constexpr int kTrackEvery = 4;        // hops between pitch checks while a note sounds

inline constexpr int kTrackMisses = 3;       // failed checks in a row before the note counts as gone

inline constexpr int kDeadlineHops = 8;      // hops past the longest frame before giving up with NotSure

inline constexpr float kCombSeconds = 0.25f; // how long after an onset the last note is filtered out

inline constexpr float kRestrikeResidual = 0.1f; // comb residual below this: the same note struck again

// Onsets: the spectral flux must pass this level and this multiple of its running average.
// Low enough for a soft bass note under a ringing one; the extra onsets that brings shortly
// after a real attack continue it (see start_note).
inline constexpr float kOnsetFluxMin = 0.04f;

inline constexpr float kOnsetFluxRise = 2.5f;

// Shortest audio before an onset worth searching for what still rings (21 ms).
inline constexpr int kMinBeforeSearch = 1024;

// Periods of a ringing pitch its frame holds: enough to judge it, and short enough to leave out
// the attack and any note before it.
inline constexpr float kRingingPeriods = 4.0f;

// Two periods within this fraction of each other are the same; a semitone is 6%.
inline constexpr float kSamePeriod = 0.03f;

// A ringing pitch is taken at this judged confidence, or this raw clarity: it only filters and
// is never reported, so a quiet note whose partials do not all stand out still counts.
inline constexpr float kRingingConfidence = 0.5f;

inline constexpr float kRingingClarity = 0.9f;

// The last note heard still rings when the waveform repeats this well at its period.
inline constexpr float kTailClarity = 0.5f;

// MPM's "within k of the highest peak" and YIN's dip threshold, as in the main search.
inline constexpr float kMpmK = 0.9f;

inline constexpr float kYinThreshold = 0.15f;

// A frame resolves a pitch's partials when the Hann main lobe (two resolution bins) is under
// this fraction of f0.
inline constexpr float kResolvedLobe = 0.35f;

// Sub-periods searched for a mix of two notes: down to a sixth of the period, 2% wide; under a
// ringing note before the partials resolve, down to a sixteenth, 4% wide.
inline constexpr int kSubPeriods = 6;

inline constexpr float kSubTolerance = 0.02f;

inline constexpr int kWideSubPeriods = 16;

inline constexpr float kWideSubTolerance = 0.04f;

// The half-period search is this wide on each side.
inline constexpr float kHalfTolerance = 0.02f;

// A partial is searched at most this fraction of f0 to each side, so its neighbours stay out.
inline constexpr float kPartialSearch = 0.3f;

// Under a ringing note, a pitch whose waveform repeats this well at half its period may be the
// octave above.
inline constexpr float kHalfPeriodLimit = 0.5f;

// Under a ringing note, an unresolved pitch needs this share of its energy below its 7th partial
// (the band ends at kOwnLowTop times f0).
inline constexpr float kOwnLowShare = 0.1f;

inline constexpr float kOwnLowTop = 6.5f;

// A pitch is a note only when its own lowest partials carry sound: the first three at least this
// share of the energy from half the pitch up (the band ends at kFirstPartialsTop times f0), and
// its fundamental at least this share of its strongest partial's energy (-20 dB; a piano's bass
// strings have weak fundamentals). The period several notes share (a chord's common subharmonic,
// or an octave below its lowest note) has partials only where those notes are. A ringing note
// found before an onset needs the first.
inline constexpr float kFirstPartialsShare = 0.05f;

inline constexpr float kFirstPartialsTop = 3.5f;

inline constexpr float kOwnFundamentalShare = 0.01f;

// A fundamental under this share of the strongest partial (-5 dB) may be other notes' sound
// leaking into its search window, so it must be new: its growth at the onset at least
// kOwnFundamentalShare of the growth of the partial that grew most. A note struck out of silence
// grows by all its sound; one struck again grows at all its partials alike.
inline constexpr float kWeakFundamentalShare = 0.3f;

// A fundamental counts as already sounding when, before the onset, it had at least this share of
// its energy after it (it grew by less than 6 dB).
inline constexpr float kHeldFundamentalShare = 0.25f;

// A fundamental stands out of the spectrum beside it by at least this much (10 dB).
inline constexpr float kFundamentalProminence = 10.0f;

// The old note counts as loud enough to blend with the new one when the mean square just
// before the onset is at least this share of the analysis frame's (-13 dB).
inline constexpr float kLoudRinging = 0.05f;

inline constexpr int kBeforeSamples = 1024;        // audio measured before an onset

inline constexpr int kBeforeGuard = 64;            // keeps the new attack out of it

inline constexpr float kQuickOnsetSeconds = 0.08f; // an onset closer to the last one continues it

// A pitch's partials hold old sound when, before the onset, they had at least this share of their
// energy after it (-17 dB); below that the old sound cannot be what was heard.
inline constexpr float kOldSoundShare = 0.02f;

// ... and then their growth at the onset must carry this share of their energy (-13 dB). Not more:
// a bass note's partials lie close to the old note's, whose sound in the same search windows hides
// part of the growth.
inline constexpr float kGrownShare = 0.05f;

// ... and the odd partials this share of the even ones' growth, or the new note is an octave up.
inline constexpr float kGrownOddShare = 0.05f;

// ... and, for every k from 3 up, the partials off the multiples of k this share of the growth on
// them (-20 dB). A pitch that grew only at every k-th partial is a blend whose new note is k times
// higher: C#5 struck under a ringing G4 grows at D#2's seventh partial alone, or at F#3's third
// and sixth.
inline constexpr float kOffSeriesShare = 0.01f;

// Struck again, the old key's own partials must grow at least this many times more (in energy)
// than those of the key a semitone or a tone away on either side.
inline constexpr float kRestrikeOwnGrowth = 4.0f;

// ... and such a neighbour must not grow by this share of the old key's energy (-20 dB): the high
// partials of a ringing note beat fast enough to grow on their own, but sound does not appear a
// step away unless another key was struck.
inline constexpr float kNeighbourRise = 0.01f;

// Struck again, the old key's partials are this many times louder (in energy) than just before
// the onset; less is the old note's own beating and the leak of a soft neighbour.
inline constexpr float kRestrikeLouder = 1.25f;

// Audio before an onset whose spectrum is kept: as long as the longest frame it is compared with,
// back to the onset before it, but never less than kMinBeforeSpectrum (43 ms): a shorter window
// cannot tell new sound from old.
inline constexpr int kBeforeSpectrum = kMaxFrame;

inline constexpr int kMinBeforeSpectrum = 2048;

// When the gate closes on a note not yet confirmed, a sound that lasted this long after its onset
// (43 ms) was a note whose pitch was not found, and gets "not sure"; a shorter one was a knock or
// a click and gets nothing.
inline constexpr int kShortestUnsure = 2048;

// No further flux onset this long after one: the same attack keeps raising the flux.
inline constexpr int kOnsetRefractoryMs = 30;

// Lags analysed past the one read, so its interpolation has both neighbours.
inline constexpr int kLagMargin = 2;

inline constexpr int kTuningNotes = 8;

inline constexpr float kMaxPartialHz = 9000.0f;

inline float hz_of(int note, float a4) { return a4 * std::exp2(static_cast<float>(note - kA4) / 12.0f); }

} // namespace pitch_detect_detail

using namespace pitch_detect_detail;

struct PitchDetector::Impl {
    enum class State : std::uint8_t { Idle, Acquiring, Sounding, Done };

    PitchDetectorConfig cfg;
    int states;    // notes low..high plus one unvoiced state
    float min_lag; // shortest period searched (highest note, a bit sharp)
    int max_lag;   // longest period searched (lowest note, a bit flat)
    float max_hz;  // partials above this are ignored

    std::vector<float> history = std::vector<float>(kHistory, 0.0f);
    std::int64_t written = 0;
    int since_hop = 0;

    dsp::FrameAnalyzer analyzer;
    dsp::RealFft onset_fft{kOnsetWindow};
    std::vector<float> onset_hann = std::vector<float>(kOnsetWindow);
    std::vector<float> onset_time = std::vector<float>(kOnsetWindow);
    std::vector<kiss_fft_cpx> onset_spec = std::vector<kiss_fft_cpx>(kOnsetWindow / 2 + 1);
    std::vector<float> onset_prev = std::vector<float>(kOnsetWindow / 2 + 1, 0.0f);
    std::vector<float> frame = std::vector<float>(kMaxFrame);
    // The frame with the previous note combed out: y[n] = x[n] - x[n - its period].
    dsp::FrameAnalyzer comb_analyzer;
    // The audio just before the current onset: what was already sounding, back to the onset
    // before it where that leaves enough (`before_available` samples). It is
    // compared with the audio after the onset over windows of the same length: a longer window
    // splits a note's detuned strings into smaller peaks, so the same sound would look as if it
    // grew. That is the whole frame while the audio before is as long; a longer frame is
    // compared through its latest `before_length` samples, analysed apart (`after_analyzer`).
    dsp::FrameAnalyzer before_analyzer;
    dsp::FrameAnalyzer after_analyzer;
    std::vector<float> before_audio = std::vector<float>(kBeforeSpectrum);
    int before_available = kBeforeSpectrum;
    int before_length = 0;
    std::vector<float> comb = std::vector<float>(kMaxFrame);
    ExpectedNoteChecker checker;

    // Gate.
    float noise_floor = 0.0f;
    bool calibrating = false;
    double calib_sum = 0.0;
    std::int64_t calib_hops = 0;
    bool gate_open = false;
    int quiet_hops = 0;
    std::array<double, kGateHops> hop_energy{};
    int hop_index = 0;

    // Onset.
    float flux_avg = 0.0f;
    float energy_avg = 0.0f;
    int spike_hops = 0;                    // hops left for a flux spike's energy rise to arrive
    float spike_energy = 0.0f;             // the loudest recent hop when the spike came
    std::array<float, 16> recent_energy{}; // 2048 samples: longer than the lowest period
    int recent_index = 0;
    std::int64_t last_onset = std::numeric_limits<std::int64_t>::min() / 2;

    // Note state.
    State state = State::Idle;
    std::int64_t onset = 0;
    std::vector<float> delta;
    std::vector<float> obs;
    std::vector<float> next;
    int stable_note = -1;
    int stable_count = 0;
    float stable_conf = 0.0f;
    int guess_note = -1;      // the likeliest note of the onset so far, for a "not sure" event
    float guess_score = 0.0f; // the raw strength of that note's candidate
    int current_note = -1;
    float current_period = 0.0f; // waveform period of the sounding note, in samples
    int prev_note = -1;          // the note sounding when the current onset came
    float prev_period = 0.0f;
    bool rang_before = false; // sound went on right up to the current onset
    float before_ms = 0.0f;   // mean square of the audio just before the current onset
    // The last note heard: the last confirmed one, or the best-supported pitch of a note that
    // ended unsure. Only for recognising it still ringing under a later onset, never reported.
    int last_note = -1;
    float last_period = 0.0f;
    int tentative_note = -1; // best-supported pitch of the note being acquired
    float tentative_period = 0.0f;
    float tentative_conf = 0.0f;
    int track_counter = 0;
    int track_misses = 0;

    // Tuning.
    float a4;
    float a4_base;
    std::array<float, kTuningNotes> tuning{};
    int tuning_count = 0;
    int tuning_next = 0;

    // Expected-note mode.
    std::array<int, kMaxChordNotes> expected{};
    int expected_count = 0;
    ExpectedVerdict last_verdict = ExpectedVerdict::NotSure;
    std::uint16_t last_present = 0;
    std::uint16_t last_absent = 0;
    int verdict_same = 0;

    // Output.
    PitchEvent* out = nullptr;
    int capacity = 0;
    int emitted = 0;
    std::int64_t dropped = 0;

    explicit Impl(const PitchDetectorConfig& c);

    float sample_at(std::int64_t index) const {
        return history[static_cast<size_t>(index & (kHistory - 1))];
    }

    // Copies history[end - length, end) into `dst`.
    void copy_recent(float* dst, std::int64_t end, int length) const {
        for (int i = 0; i < length; ++i) {
            dst[i] = sample_at(end - length + i);
        }
    }

    void emit(const PitchEvent& e) {
        if (emitted < capacity) {
            out[emitted++] = e;
        } else {
            ++dropped;
        }
    }

    float gate_threshold() const {
        return std::max(cfg.gate_min_rms, noise_floor * cfg.gate_ratio);
    }

    // RMS over the last kGateHops hops. One hop is too short: a bass partial's level in 128
    // samples swings with its phase, which would flap the gate open and shut.
    float gate_rms();

    // Mean positive change of log magnitude over the bins from 140 Hz to 6 kHz, against the
    // previous hop, and the energy in those bins. Magnitudes are floored near the gate level so
    // room noise adds little flux. The lowest bins are left out: a 512-sample frame cannot
    // resolve bass partials, and their level swings with phase from hop to hop.
    float spectral_flux(float* energy);

    // Places the onset within the last kOnsetLookBack samples: the first 32-sample block whose
    // energy rises well above the blocks before the attack.
    std::int64_t locate_onset() const;

    void reset_note_search();

    void end_current_note() {
        if (state == State::Sounding && current_note >= 0) {
            PitchEvent e;
            e.type = PitchEventType::NoteOff;
            e.note = current_note;
            e.onset_sample = onset;
            e.sample = written;
            emit(e);
        }
        current_note = -1;
    }

    void not_sure();

    // `refractory`: no further flux onset for kOnsetRefractoryMs.
    // A gate opening sets none, so a real attack right after noise crept over the gate counts.
    void start_note(std::int64_t at, bool refractory);

    // End of the audio that is surely from before the current onset. The onset may be placed a
    // little late, but the attack started at most one look-back window before it was detected.
    std::int64_t before_end() const {
        return std::min(onset, written - kOnsetLookBack) - kBeforeGuard;
    }

    // The note ringing under a new onset may never have been confirmed (its pitch unsure, or its
    // onset missed, so an older note still counts as sounding), or may have faded under the gate
    // and still ring faintly under a quiet new note. Take its pitch from the audio between the
    // last onset and this one, so it is filtered out and its subharmonics are treated with
    // suspicion. The confirmed note stays when that audio gives no clear pitch. `sounding`: the
    // gate was open up to this onset.
    void hear_ringing(std::int64_t last, bool sounding);

    void hop();

    int frame_length() const {
        return static_cast<int>(std::min<std::int64_t>(written - onset, kMaxFrame));
    }

    bool past_deadline() const { return written - onset >= kMaxFrame + kDeadlineHops * kHop; }

    struct Candidate {
        int note = -1;
        float hz = 0.0f;
        float confidence = 0.0f;
        float period = 0.0f; // waveform period in samples
        bool ready = false;  // enough periods in the window to rule out the octave below
        // The nearest key to the period, even when it is too far out of tune to count as that
        // note (note stays -1 then), and the period's raw strength: they feed a "not sure" event's
        // guess.
        int register_note = -1;
        float score = 0.0f;
    };

    // Whether the first three partials of a pitch at `hz` carry their share of the sound from half
    // the pitch up (see kFirstPartialsShare).
    bool first_partials_heard(const dsp::FrameAnalyzer& a, float hz) const {
        return a.band_energy(0.5f * hz, kFirstPartialsTop * hz) >=
               kFirstPartialsShare * a.band_energy(0.5f * hz, max_hz);
    }

    // Turns a period estimate into a note, correcting octave errors from the spectrum and
    // scaling its confidence by how clean and in-tune the evidence is. `clean` is the frame the
    // period came from: the raw frame, or the combed one with the previous note (fundamental
    // `notch_hz`) filtered out. Octave tests and pitch refinement read the raw spectrum, where no
    // partial is notched; harmonicity, completeness and sub-periods read the clean frame.
    // `ringing_hz` > 0: the old note still rings in the raw frame. Its partials are left out of
    // the octave tests there too, or they could pass for the odd partials of an octave below.
    // `under_ringing`: an old note rings under this onset, its pitch known or not; the checks
    // against blends of two notes apply.
    Candidate judge(const dsp::LagCandidate& lag, int length, const dsp::FrameAnalyzer& clean,
                    float notch_hz, float ringing_hz = 0.0f, bool under_ringing = false) const;

    // The spectrum after the onset that is compared with `before_analyzer`, over the same length.
    const dsp::FrameAnalyzer& growth_after() const {
        return analyzer.length() > before_length ? after_analyzer : analyzer;
    }

    // Every pitch, whatever path it came from, must be new sound: its partials grew at the onset.
    // A blend of notes already ringing (their common subharmonic, the old note itself, a note
    // between them) grows nowhere; a note an octave above the pitch grows only at its even
    // partials, and in general a pitch k times below the new note grows only at every k-th partial.
    // Where nothing was sounding at the pitch's partials there is nothing to tell apart: all of
    // it is new. That holds for a chord struck out of silence too, whose common subharmonic is
    // new sound as well; judge() has already turned such a pitch down by its empty lowest
    // partials, which needs no audio from before the onset. A frame that cannot separate the
    // partials shows only whether they grew as a whole, and whether the odd ones did.
    void check_fresh(Candidate& c) const;

    // Whether the note at this pitch was already sounding before the onset: its partials, the odd
    // ones included (a tone an octave up covers only the even ones), had sound (see check_fresh).
    bool old_sound_at(const Candidate& c) const {
        const auto e = dsp::fresh_evidence(before_analyzer, growth_after(), c.hz,
                                           dsp::piano_inharmonicity(c.note), max_hz);
        return e.before >= kOldSoundShare * e.after && e.before_odd >= kOldSoundShare * e.after_odd;
    }

    void acquire_step();

    // While a note sounds, check now and then that its period is still there.
    void track_step();

    void expected_step();

    void learn_tuning(int note, float hz);
};

} // namespace etude
