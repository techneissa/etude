// SPDX-License-Identifier: GPL-3.0-or-later
// Chords in open detection (microphone free play, Flow Mode): the detector must never name a key
// that is not in the chord, such as the period the chord's notes share (their common
// subharmonic) or the octave below its lowest note. It names one of the chord's keys, each
// confirmed by its own partials, or says "not sure". The gate runs a few chords of the set
// tools/pitch-eval runs with `--open-chords N`; the tool runs thousands.
#include "etude/pitch_detect.hpp"
#include "etude/pitch_eval.hpp"
#include "etude/pitch_synth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

// One chord for each register in each context.
constexpr int kRounds = 1;

constexpr int kRate = 48000;

void check_gate(PitchMethod method) {
    SynthEvalOptions options;
    options.detector.method = method;
    const int chords = kRounds * kOpenChordContexts * kRegisterCount;
    const OpenChordReport r = evaluate_open_chord_synthetic(options, chords);
    REQUIRE(r.registers.total.notes == chords);
    CHECK(r.registers.total.wrong == 0);
    CHECK(r.registers.total.octave == 0);
    // Not a detector that stays safe by never answering: some chords are named by one of their
    // keys, and the chords struck out of silence are all answered.
    CHECK(rate(r.registers.total.correct, r.registers.total.notes) > 0.1f);
    const RegisterStats& silence = r.contexts[static_cast<size_t>(OpenChordContext::Silence)];
    CHECK(silence.missed == 0);
}

// The events for `notes` struck at `starts_ms` (from the end of 150 ms of room noise, -60 dBFS)
// and left ringing for 400 ms, the detector calibrated on that noise. `clip` > 0 cuts the
// waveform at that level, as an overloaded microphone input does.
std::vector<PitchEvent> play(const std::vector<int>& notes, const std::vector<int>& starts_ms,
                             float velocity, PitchMethod method, float clip = 0.0f,
                             std::uint64_t seed = 1) {
    const int lead = kRate * 15 / 100;
    const int last = lead + *std::max_element(starts_ms.begin(), starts_ms.end()) * kRate / 1000;
    const int length = last + kRate * 4 / 10;
    std::vector<float> audio(static_cast<size_t>(length), 0.0f);
    for (size_t i = 0; i < notes.size(); ++i) {
        PianoToneOptions tone;
        tone.note = notes[i];
        tone.velocity = velocity;
        tone.seed = seed * 31 + i;
        add_piano_tone(audio.data(), length, lead + starts_ms[i] * kRate / 1000, tone, kRate);
    }
    add_room_noise(audio.data(), length, 1e-3f, seed);
    if (clip > 0.0f) {
        for (auto& v : audio) {
            v = std::clamp(v, -clip, clip);
        }
    }
    PitchDetectorConfig config;
    config.method = method;
    PitchDetector detector(config);
    std::vector<float> noise(kRate / 2, 0.0f);
    add_room_noise(noise.data(), kRate / 2, 1e-3f, 99);
    detector.begin_noise_calibration();
    run_detector(detector, noise.data(), kRate / 2);
    detector.end_noise_calibration();
    detector.reset();
    return run_detector(detector, audio.data(), length);
}

// Every note-on is a key of the chord, and the chord is answered: by one of its keys, or by
// "not sure".
void check_chord(const std::vector<int>& notes, const std::vector<int>& starts_ms, float velocity,
                 float clip = 0.0f) {
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        CAPTURE(static_cast<int>(method));
        const auto events = play(notes, starts_ms, velocity, method, clip);
        bool answered = false;
        for (const auto& e : events) {
            if (e.type == PitchEventType::NoteOn) {
                CAPTURE(e.note);
                CHECK(std::find(notes.begin(), notes.end(), e.note) != notes.end());
                answered = true;
            }
            answered = answered || e.type == PitchEventType::NotSure;
        }
        CHECK(answered);
    }
}

void check_together(const std::vector<int>& notes, float velocity, float clip = 0.0f) {
    check_chord(notes, std::vector<int>(notes.size(), 0), velocity, clip);
}

// Wrong and octave-off keys for one chord, which must give an answer (correct or not sure) of a `pitch-eval --open-chords` run: chord `index` of
// seed `seed`, played alone.
int wrong_in_chord(std::uint64_t seed, int index, PitchMethod method, bool interferer = true) {
    SynthEvalOptions options;
    options.seed = seed;
    options.clip.interferer = interferer;
    options.detector.method = method;
    options.melody_first = index;
    const OpenChordReport r = evaluate_open_chord_synthetic(options, index + 1);
    REQUIRE(r.registers.total.notes == 1);
    // A chord that gives no event at all must not count as "no wrong key".
    CHECK(r.registers.total.missed == 0);
    return r.registers.total.wrong;
}

} // namespace

TEST_CASE("pitch chords: McLeod names no key outside the chord, struck together or nearly") {
    check_gate(PitchMethod::Mpm);
}

TEST_CASE("pitch chords: a loud triad out of silence is not its common subharmonic") {
    // Each used to be reported two or three octaves below its root (C1 or C2 for C3 and C4).
    for (int root : {36, 48, 60, 72, 84}) {
        CAPTURE(root);
        check_together({root, root + 4, root + 7}, 1.0f);
        check_together({root, root + 3, root + 7}, 0.75f);
    }
}

TEST_CASE("pitch chords: a chord is not the octave below its lowest note") {
    // F6 C7 A7 are partials 2, 3 and 5 of F5, which was reported: only its fundamental is empty.
    check_together({89, 96, 105}, 0.25f);
    // A#3 F4 D5 and B4 E5 gave A#2 and E3.
    check_together({58, 65, 74}, 0.75f);
    check_together({71, 76}, 0.75f);
}

TEST_CASE("pitch chords: a dyad is not a pitch far below it with none of its own partials") {
    // A#4 F#5 are partials 10 and 16 of F#1: nothing at F#1's first partials.
    check_together({70, 78}, 1.0f);
    // D#3 C4 gave G#1, whose third and fifth partials they are.
    check_together({51, 60}, 0.25f);
}

TEST_CASE("pitch chords: two notes 15 ms apart are not their common subharmonic") {
    for (const auto& pair : std::vector<std::vector<int>>{{48, 52},
                                                          {48, 55},
                                                          {52, 55},
                                                          {55, 60},
                                                          {60, 64},
                                                          {60, 67},
                                                          {64, 67},
                                                          {67, 72},
                                                          {72, 76},
                                                          {76, 84}}) {
        CAPTURE(pair[0]);
        CAPTURE(pair[1]);
        check_chord(pair, {0, 15}, 0.75f);
    }
}

TEST_CASE("pitch chords: the ends of the keyboard, very soft and clipped chords") {
    check_together({21, 28}, 0.75f);       // A0 E1
    check_together({21, 33, 40}, 1.0f);    // A0 A1 E2
    check_together({100, 104, 108}, 0.5f); // E7 G#7 C8
    check_together({96, 108}, 1.0f);       // C7 C8
    check_together({60, 64, 67}, 0.0f);    // C4 E4 G4 at the softest
    check_together({48, 52, 55}, 1.0f, 0.1f);
    check_together({72, 76, 79}, 1.0f, 0.1f);
}

TEST_CASE("pitch chords: chords that once gave wrong keys") {
    // From the seed sweeps of tools/pitch-eval --open-chords, each chord played alone with
    // the method that went wrong.
    // C#4 G#4 F5, soft: C#3. Sound before the onset gave a 28.5 Hz period, a comb so low that
    // nearly every partial fell in a notch, and C#3's empty fundamental went unmeasured.
    // C#2 G#2 over a ringing chord: C#1, whose "fundamental" was the old chord's sound, which
    // did not grow at the onset.
    // A1 A2 C#3 over a ringing chord: A0, the same way.
    CHECK(wrong_in_chord(15, 1121, PitchMethod::Mpm) == 0);
    // D#1 A#1, 5-30 ms apart, over a ringing chord and a steady G#1 tone: G#1, which seemed to
    // grow when A#1 spread into its partials in a frame too short to separate them.
    // A1 A2 C#3 and G#1 C#2 E2, soft, out of silence: A0. In a frame of 2.2 periods the
    // search around A0's fundamental reached into A1's main lobe.
    // A5 D6 F#6, loud, 5-30 ms apart, over a ringing chord: D4, whose "fundamental" was hammer
    // noise, as loud beside it.
    CHECK(wrong_in_chord(4, 1839, PitchMethod::Mpm) == 0);
    // G#6 D#7 C8 over a ringing chord: C7, already sounding and quieter than before the onset.
    CHECK(wrong_in_chord(6, 83, PitchMethod::Mpm, false) == 0);
}
