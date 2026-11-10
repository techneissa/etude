// SPDX-License-Identifier: GPL-3.0-or-later
// Legato: the previous note still rings under each onset. The detector must never name a wrong
// key there; when the blend of the two notes leaves it unsure it says "not sure". The gate runs a
// few of the melodies tools/pitch-eval uses (`--melodies N`); the tool runs thousands of notes.
#include "etude/pitch_detect.hpp"
#include "etude/pitch_eval.hpp"
#include "etude/pitch_synth.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

// Two 12-note melodies for the first seed.
constexpr int kSeeds = 1;
constexpr int kMelodies = 2;
constexpr int kMelodyNotes = 12;

constexpr int kRate = 48000;
constexpr int kToolMelodyNotes = 24; // notes per melody in tools/pitch-eval

// Melodies with every note at its own velocity (loud next to soft) and a quarter of the notes
// striking the same key again. With seeds 1 and 7 the detector used to name wrong keys for both
// methods here.
void check_melodies(PitchMethod method) {
    int notes = 0;
    int wrong = 0;
    int octave = 0;
    int correct = 0;
    for (std::uint64_t seed : {1u, 7u}) {
        if (notes >= kSeeds * kMelodies * kMelodyNotes) {
            break;
        }
        SynthEvalOptions options;
        options.seed = seed;
        options.detector.method = method;
        options.melody_note_velocities = true;
        options.melody_repeats = 0.25f;
        const EvalReport r = evaluate_melody_synthetic(options, kMelodies, kMelodyNotes);
        notes += r.total.notes;
        wrong += r.total.wrong;
        octave += r.total.octave;
        correct += r.total.correct;
    }
    REQUIRE(notes == kSeeds * kMelodies * kMelodyNotes);
    CHECK(wrong == 0);
    CHECK(octave == 0);
    // Not a detector that stays safe by never answering.
    CHECK(rate(correct, notes) > 0.5f);
}

// Notes struck `gap_ms` apart after 150 ms of room noise (-60 dBFS), each damper falling 40 ms
// after the next note starts, as in the melody evaluation. The outcome for the last note.
NoteOutcome last_of(const std::vector<int>& notes, int gap_ms, const std::vector<float>& velocities,
                    std::uint64_t seed, PitchMethod method) {
    std::vector<int> onsets;
    int t = kRate * 15 / 100;
    for (size_t i = 0; i < notes.size(); ++i) {
        onsets.push_back(t);
        t += gap_ms * kRate / 1000;
    }
    const int length = onsets.back() + kRate * 35 / 100;
    std::vector<float> audio(static_cast<size_t>(length), 0.0f);
    std::vector<float> one(static_cast<size_t>(length));
    for (size_t i = 0; i < notes.size(); ++i) {
        std::fill(one.begin(), one.end(), 0.0f);
        PianoToneOptions tone;
        tone.note = notes[i];
        tone.velocity = velocities[i];
        tone.seed = seed * 31 + i;
        add_piano_tone(one.data(), length, onsets[i], tone, kRate);
        const int release = i + 1 < notes.size() ? onsets[i + 1] + kRate / 25 : length;
        for (int k = 0; k < length; ++k) {
            const float fade =
                k < release ? 1.0f : std::exp(-static_cast<float>(k - release) / (0.03f * kRate));
            audio[static_cast<size_t>(k)] += one[static_cast<size_t>(k)] * fade;
        }
    }
    add_room_noise(audio.data(), length, 1e-3f, seed);
    PitchDetectorConfig config;
    config.method = method;
    PitchDetector detector(config);
    std::vector<float> noise(kRate / 2, 0.0f);
    add_room_noise(noise.data(), kRate / 2, 1e-3f, 99);
    detector.begin_noise_calibration();
    run_detector(detector, noise.data(), kRate / 2);
    detector.end_noise_calibration();
    detector.reset();
    const auto events = run_detector(detector, audio.data(), length);
    return score_note(notes.back(), onsets.back(), length, events.data(),
                      static_cast<int>(events.size()), kRate)
        .outcome;
}

void check_last_correct(const std::vector<int>& notes, int gap_ms, float velocity,
                        std::uint64_t seed) {
    const std::vector<float> velocities(notes.size(), velocity);
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        CAPTURE(static_cast<int>(method));
        CHECK(last_of(notes, gap_ms, velocities, seed, method) == NoteOutcome::Correct);
    }
}

// The last note is the right key or none (not sure, or missed).
void check_last_not_wrong(const std::vector<int>& notes, int gap_ms,
                          const std::vector<float>& velocities, std::uint64_t seed) {
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        CAPTURE(static_cast<int>(method));
        const NoteOutcome o = last_of(notes, gap_ms, velocities, seed, method);
        CHECK(o != NoteOutcome::Wrong);
        CHECK(o != NoteOutcome::OctaveError);
    }
}

// Wrong and octave-off keys in one melody of a `pitch-eval --melodies` run with every note at
// its own velocity: melody `melody` of seed `seed`, played alone.
int wrong_in_melody(std::uint64_t seed, int melody, float repeats, PitchMethod method) {
    SynthEvalOptions options;
    options.seed = seed;
    options.detector.method = method;
    options.melody_note_velocities = true;
    options.melody_repeats = repeats;
    options.melody_first = melody;
    const EvalReport r = evaluate_melody_synthetic(options, melody + 1, kToolMelodyNotes);
    REQUIRE(r.total.notes == kToolMelodyNotes);
    return r.total.wrong + r.total.octave;
}

} // namespace

TEST_CASE("pitch legato: McLeod names no wrong key in melodies of loud, soft and repeated notes") {
    check_melodies(PitchMethod::Mpm);
}

TEST_CASE("pitch evaluation: melody_repeats strikes the same key again") {
    SynthEvalOptions options;
    options.low_note = 60;
    options.high_note = 72;
    options.velocities = {0.7f};
    options.melody_repeats = 1.0f;
    std::vector<NoteRecord> records;
    options.records = &records;
    const EvalReport r = evaluate_melody_synthetic(options, 1, 4);
    REQUIRE(records.size() == 4);
    for (const auto& rec : records) {
        CHECK(rec.note == records.front().note);
    }
    CHECK(r.total.notes == 4);
    CHECK(r.total.wrong == 0);
}

// Each case below used to end in a wrong key with both methods.

TEST_CASE("pitch legato: a note a fifth below a ringing one is not their common subharmonic") {
    // B3 rings over E3: both repeat at E2's period (B3 is its third partial, E3 its second).
    check_last_correct({66, 59, 52}, 235, 1.0f, 5);
}

TEST_CASE("pitch legato: a ringing note whose onset was missed is still heard under the next") {
    // C3's onset goes unnoticed, so it was never confirmed; F2 under it gave F1, the common
    // subharmonic, until the audio before the onset was used to find what still rang.
    check_last_correct({43, 48, 41}, 311, 0.5f, 26);
}

TEST_CASE("pitch legato: a note a step from a ringing unconfirmed one is not the old note") {
    // A3 is missed; B3 struck over it was reported as A3.
    check_last_correct({59, 57, 59}, 247, 0.5f, 25);
    // A2 is missed; G#2, a semitone below, was reported as A2.
    check_last_correct({41, 45, 44}, 246, 0.75f, 5);
}

TEST_CASE("pitch legato: a tritone blend is not reported as a deep bass note") {
    // G5 rings over C#5 (about 7:5): the mix repeats near A2's period.
    check_last_correct({86, 79, 73}, 156, 0.5f, 6);
}

TEST_CASE("pitch legato: a quick return to a key is that key, not a bass note under it") {
    // F3, A#3, F3 at 151 ms: the second F3 was reported as A#1.
    check_last_correct({53, 58, 53}, 151, 0.25f, 49);
}

TEST_CASE("pitch legato: a soft bass note under a ringing one gets its own onset") {
    // Both used to pass without an onset, so the new note was never heard.
    check_last_correct({49, 43}, 294, 0.5f, 44);  // C#3, then G2
    check_last_correct({37, 40}, 318, 0.25f, 18); // C#2, then E2, softly
}

TEST_CASE("pitch legato: a held bass note is reported once, not struck again") {
    // A bass note's level swings with its phase in a short frame; the onset test must not take
    // that for new attacks.
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        for (int note : {28, 36, 43}) {
            CAPTURE(static_cast<int>(method));
            CAPTURE(note);
            const int length = kRate * 6 / 5;
            std::vector<float> audio(static_cast<size_t>(length), 0.0f);
            PianoToneOptions tone;
            tone.note = note;
            tone.velocity = 0.5f;
            tone.seed = 100 + static_cast<std::uint64_t>(note);
            add_piano_tone(audio.data(), length, kRate / 10, tone, kRate);
            add_room_noise(audio.data(), length, 1e-3f, 1);
            PitchDetectorConfig config;
            config.method = method;
            PitchDetector detector(config);
            const auto events = run_detector(detector, audio.data(), length);
            int note_ons = 0;
            for (const auto& e : events) {
                note_ons += e.type == PitchEventType::NoteOn ? 1 : 0;
                if (e.type == PitchEventType::NoteOn) {
                    CHECK(e.note == note);
                }
            }
            CHECK(note_ons == 1);
        }
    }
}

namespace {

// Two piano notes, both left ringing, over room noise; the events from the detector.
std::vector<PitchEvent> two_notes(int first, float first_velocity, int second,
                                  float second_velocity, int gap_ms, PitchMethod method,
                                  std::uint64_t seed = 1) {
    const int length = kRate;
    std::vector<float> audio(static_cast<size_t>(length), 0.0f);
    PianoToneOptions tone;
    tone.note = first;
    tone.velocity = first_velocity;
    tone.seed = seed;
    add_piano_tone(audio.data(), length, kRate / 10, tone, kRate);
    tone.note = second;
    tone.velocity = second_velocity;
    tone.seed = seed + 1;
    add_piano_tone(audio.data(), length, kRate / 10 + gap_ms * kRate / 1000, tone, kRate);
    add_room_noise(audio.data(), length, 1e-3f, 3);
    PitchDetectorConfig config;
    config.method = method;
    PitchDetector detector(config);
    return run_detector(detector, audio.data(), length);
}

} // namespace

namespace {

// Every note-on from the second onset on is the second key, and the second onset is answered:
// with that key or with "not sure", never with silence.
void check_second_not_wrong(int first, float first_velocity, int second, float second_velocity,
                            int gap_ms, std::uint64_t seed = 1) {
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        CAPTURE(static_cast<int>(method));
        const auto events =
            two_notes(first, first_velocity, second, second_velocity, gap_ms, method, seed);
        const std::int64_t at = kRate / 10 + static_cast<std::int64_t>(gap_ms) * kRate / 1000;
        bool answered = false;
        for (const auto& e : events) {
            if (e.onset_sample < at - kRate / 50) {
                continue;
            }
            if (e.type == PitchEventType::NoteOn) {
                CHECK(e.note == second);
                answered = answered || e.note == second;
            }
            answered = answered || e.type == PitchEventType::NotSure;
        }
        CHECK(answered);
    }
}

} // namespace

TEST_CASE("pitch legato: a note an octave above a louder ringing one is not the old key again") {
    // E3 loud, E4 softer 250 ms later: every partial of E4 lies on E3's series. The detector
    // used to report E3 again for the second onset.
    check_second_not_wrong(52, 1.0f, 64, 0.8f, 250);
}

TEST_CASE("pitch legato: a soft step next to a loud ringing top note is not that note again") {
    // The old note's high partials beat and grow on their own; only the sound a step away shows
    // that another key was struck. Each used to be reported as the old key.
    check_second_not_wrong(103, 1.0f, 104, 0.25f, 230, 3); // G7, then G#7
    check_second_not_wrong(106, 1.0f, 104, 0.25f, 300);    // A#7, then G#7 a tone below
}

TEST_CASE("pitch legato: a soft note over a ringing one is not a note a third or more below") {
    // C#4, G4, C#5: C#5 is F#3's third partial and the sixth grows with it; F#3 was reported.
    check_last_not_wrong({61, 67, 73}, 150, {1.0f, 0.25f, 0.25f}, 5);
}

TEST_CASE("pitch legato: melodies that once gave wrong keys") {
    // From the seed sweep of tools/pitch-eval --melodies 100 --note-velocities, each melody
    // played alone with the method that went wrong (and still goes wrong alone).
    CHECK(wrong_in_melody(10, 25, 0.0f, PitchMethod::Mpm) == 0); // F#7 under F7: F7
    CHECK(wrong_in_melody(12, 13, 0.3f, PitchMethod::Mpm) == 0); // C#5 under G4: D#2
}

TEST_CASE("pitch legato: a note 60 ms after a confirmed one is still heard") {
    // E3, then G3 60 ms later: an onset that soon after a confirmed note is a new note, not the
    // first attack still building.
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        CAPTURE(static_cast<int>(method));
        const auto events = two_notes(52, 0.7f, 55, 0.7f, 60, method);
        std::vector<int> heard;
        for (const auto& e : events) {
            if (e.type == PitchEventType::NoteOn) {
                heard.push_back(e.note);
            }
        }
        CHECK(heard == std::vector<int>{52, 55});
    }
}
