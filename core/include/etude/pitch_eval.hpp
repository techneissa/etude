// SPDX-License-Identifier: GPL-3.0-or-later
// Scoring for the pitch detector against labelled notes, and drivers that run it over the
// synthetic piano set. Used by tools/pitch-eval and the CI
// accuracy test.
#pragma once

#include "etude/pitch_detect.hpp"
#include "etude/pitch_synth.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace etude {

enum class NoteOutcome : std::uint8_t {
    Correct,     // the first note-on for the onset has the labelled pitch, and no other did
    Wrong,       // a note-on with a different pitch (not an octave)
    OctaveError, // a note-on a whole number of octaves off
    NotSure,     // the detector said "not sure" and reported no note
    Missed,      // no event at all
};

struct NoteScore {
    NoteOutcome outcome = NoteOutcome::Missed;
    int reported_note = -1;
    std::int64_t latency_samples = -1; // onset to the correct note-on
};

// Events whose onset lies within 20 ms before `onset` or later, up to `end`, belong to the note.
// For a note followed by another, pass the next onset minus 20 ms as `end`.
// Any wrongly pitched note-on among them makes the outcome wrong, even after a correct one.
NoteScore score_note(int note, std::int64_t onset, std::int64_t end, const PitchEvent* events,
                     int count, int sample_rate);
// The same for a chord in open detection, which reports at most the keys it confirms: correct when
// there is at least one note-on and every one is a key of the chord, wrong (an octave error when
// it is a whole number of octaves from one of the chord's keys) when any other key is reported.
NoteScore score_chord(const int* notes, int count, std::int64_t onset, std::int64_t end,
                      const PitchEvent* events, int event_count, int sample_rate);

// Registers by octave: A0-B1, C2-B2, ... C6-B6, C7-C8.
inline constexpr int kRegisterCount = 7;
int register_of(int note);
const char* register_name(int index);

struct RegisterStats {
    int notes = 0;
    int correct = 0;
    int wrong = 0;  // every wrongly pitched report, octave errors included
    int octave = 0; // the octave errors among them
    int not_sure = 0;
    int missed = 0;
    std::vector<float> latency_ms; // of correct notes
    void add(const NoteScore& score, int sample_rate);
};

struct EvalReport {
    std::array<RegisterStats, kRegisterCount> registers;
    RegisterStats total;
    void add(int note, const NoteScore& score, int sample_rate);
};

// Fraction (0..1). Zero when there are no notes.
float rate(int count, int notes);
// Nearest-rank percentile, p in 0..100; 0 for an empty list.
float percentile(std::vector<float> values, float p);

// Runs the detector over `count` samples in `block`-sized pieces and collects its events.
std::vector<PitchEvent> run_detector(PitchDetector& detector, const float* samples, int count,
                                     int block = 256);

// One evaluated clip, for diagnosing failures.
struct NoteRecord {
    int note = 0;
    float velocity = 0.0f;
    int variant = 0;
    int interferer_note = -1;
    NoteScore score;
    std::vector<PitchEvent> events;
    std::int64_t onset = 0;
    std::vector<int> chord; // open chords: every key of the chord, `note` being the lowest
    int context = 0;        // open chords: the OpenChordContext
};

struct SynthEvalOptions {
    int low_note = kPianoLowNote;
    int high_note = kPianoHighNote;
    int note_step = 1;
    std::vector<float> velocities = {0.25f, 0.5f, 0.75f, 1.0f};
    int variants = 1; // seeds per note and velocity
    std::uint64_t seed = 1;
    int block = 256;
    SynthClipOptions clip;
    PitchDetectorConfig detector;
    std::vector<NoteRecord>* records = nullptr; // when set, every clip is appended here
    // Melodies: share of notes (0..1) that strike the same key again instead of stepping.
    float melody_repeats = 0.0f;
    // Melodies: each note draws its own velocity from `velocities` (loud and soft notes mixed)
    // instead of one velocity per melody.
    bool melody_note_velocities = false;
    // Melodies: the first one evaluated. Earlier ones are still drawn, so every later melody is
    // the one a longer run plays, but they are neither rendered nor scored. The detector has not
    // learned the tuning from them, so a melody can come out a little differently. Open chords:
    // the same, the earlier chords drawn but not scored.
    int melody_first = 0;
};

// Open detection over the synthetic set. The detector is calibrated once on room noise alone.
EvalReport evaluate_synthetic(const SynthEvalOptions& options);

// Melodies: random walks (steps up to a fifth) of `notes` notes, 140-400 ms apart (longer in the
// bass), each note released 40 ms after the next starts, so the previous note still rings under
// every onset. Room noise and the clip options' interferer run throughout. With melody_repeats
// above 0, that share of the notes strike the previous key again.
EvalReport evaluate_melody_synthetic(const SynthEvalOptions& options, int melodies, int notes);

// Expected-note mode: each clip is asked about the played note, its neighbours and its octaves.
struct VerdictStats {
    int queries = 0;
    int correct = 0;
    int wrong = 0; // a definite verdict that is not the right one
    int not_sure = 0;
    int false_accept = 0; // "Expected" when another note was played: the harmful error
    std::vector<float> latency_ms;
};

struct ExpectedEvalReport {
    VerdictStats expected;                              // the played note was asked for
    VerdictStats neighbor;                              // a semitone off
    VerdictStats octave;                                // an octave off
    std::array<VerdictStats, kRegisterCount> registers; // all queries, by the played note
};

ExpectedEvalReport evaluate_expected_synthetic(const SynthEvalOptions& options);

// Chord verification: random major and minor triads, rooted from C2 to C6, either complete or
// with one note left out. Every expected note is scored as present or absent.
struct ChordEvalReport {
    int notes = 0;
    int correct = 0;
    int wrong = 0;
    int not_sure = 0;
    int missing_notes = 0;  // left-out notes asked about
    int missing_caught = 0; // ... reported absent
    int false_present = 0;  // left-out notes reported present: the harmful error
    std::vector<float> latency_ms;
};

ChordEvalReport evaluate_chord_synthetic(const SynthEvalOptions& options, int chords);

// Chords in open detection (free play, Flow Mode): the detector must never report a key that is
// not in the chord, such as the chord's common subharmonic.
enum class OpenChordContext : std::uint8_t {
    Silence,     // struck together out of room noise
    Ringing,     // struck while another chord, 150-400 ms earlier, still rings
    NearSilence, // out of room noise, each note 5-30 ms after the one below it
    NearRinging, // the same, over a ringing chord
};
inline constexpr int kOpenChordContexts = 4;
const char* open_chord_context_name(int index);

struct OpenChordReport {
    EvalReport registers; // by the chord's lowest note
    std::array<RegisterStats, kOpenChordContexts> contexts;
};

// `chords` chords, the contexts and the registers taken in turn: dyads (a third to an octave) and
// triads in close and open voicings. Every note gets its own detune and inharmonicity, and with
// melody_note_velocities its own velocity. Room noise and the clip options' interferer run
// throughout.
OpenChordReport evaluate_open_chord_synthetic(const SynthEvalOptions& options, int chords);

} // namespace etude
