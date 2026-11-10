// SPDX-License-Identifier: GPL-3.0-or-later
// Pitch detection: halfway between keys is "not sure" and presses nothing, octave errors are
// reported as such, chords and legato name no wrong key, and the queues keep order.
#include "ear_session.hpp"
#include "etude/evaluator.hpp"
#include "etude/mic_input.hpp"
#include "etude/midi_event.hpp"
#include "etude/pitch.hpp"
#include "etude/pitch_detect.hpp"
#include "etude/pitch_eval.hpp"
#include "etude/pitch_synth.hpp"
#include "etude/sample_ring.hpp"
#include "etude/spsc_queue.hpp"
#include "etude/transport.hpp"
#include "mic_inbox.hpp"
#include "pitch_tones.hpp"
#include "song_controller_test_support.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <doctest/doctest.h>
#include <functional>
#include <thread>

namespace {

using namespace etude;
using namespace etude::app;
using namespace pitch_test;

constexpr std::int64_t kMs = 1'000'000;
constexpr std::int64_t sec(double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); }
constexpr std::int64_t ms(double m) { return static_cast<std::int64_t>(std::llround(m * 1e6)); }

std::vector<PitchEvent> detect(const std::vector<float>& audio, PitchMethod method) {
    PitchDetectorConfig cfg;
    cfg.method = method;
    PitchDetector d(cfg);
    return run_detector(d, audio.data(), static_cast<int>(audio.size()));
}

std::vector<PitchEvent> detect(const std::vector<float>& audio) {
    PitchDetector d;
    return run_detector(d, audio.data(), static_cast<int>(audio.size()));
}

int count_type(const std::vector<PitchEvent>& ev, PitchEventType type) {
    return static_cast<int>(
        std::count_if(ev.begin(), ev.end(), [&](const PitchEvent& e) { return e.type == type; }));
}

const PitchEvent* first_of(const std::vector<PitchEvent>& ev, PitchEventType type) {
    const auto it =
        std::find_if(ev.begin(), ev.end(), [&](const PitchEvent& e) { return e.type == type; });
    return it == ev.end() ? nullptr : &*it;
}

// A note that ends as "not sure": exactly one NotSure event with no note and no frequency, and
// no NoteOn at all.
void check_only_not_sure(const std::vector<PitchEvent>& ev, std::int64_t onset) {
    CHECK(count_type(ev, PitchEventType::NoteOn) == 0);
    REQUIRE(count_type(ev, PitchEventType::NotSure) == 1);
    const PitchEvent& e = *first_of(ev, PitchEventType::NotSure);
    CHECK(e.note == -1);
    CHECK(e.frequency_hz == 0.0f);
    CHECK(std::abs(e.onset_sample - onset) <= 256);
    CHECK(e.sample > e.onset_sample);
}

TEST_CASE("pitch detector: a steady A4 is reported once as note 69, by both methods") {
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        CAPTURE(static_cast<int>(method));
        const auto ev = detect(tone(440.0, rich(0.3), kRate / 2, 4800), method);
        REQUIRE(count_type(ev, PitchEventType::NoteOn) == 1);
        const auto* on = first_of(ev, PitchEventType::NoteOn);
        CHECK(on->note == 69);
        CHECK(on->frequency_hz == doctest::Approx(440.0).epsilon(0.01));
        CHECK(on->confidence >= 0.8f);
        CHECK(std::abs(on->onset_sample - 4800) <= 256);
        CHECK(count_type(ev, PitchEventType::NotSure) == 0);
    }
}

TEST_CASE("pitch detector: silence and noise under the gate produce no events") {
    CHECK(detect(std::vector<float>(kRate, 0.0f)).empty());
    CHECK(detect(noise(kRate, 1e-4f, 12345)).empty()); // about -80 dBFS
}

TEST_CASE("pitch detector: a bass note or a strong second partial is not reported an octave off") {
    // A1 (55 Hz) shaped like a piano bass string: the fundamental is far weaker than partials 2-5.
    auto ev = detect(
        tone(55.0, {0.02, 0.10, 0.15, 0.15, 0.10, 0.08, 0.05, 0.04, 0.03, 0.02}, kRate, 4800));
    REQUIRE(count_type(ev, PitchEventType::NoteOn) == 1);
    CHECK(first_of(ev, PitchEventType::NoteOn)->note == 33);
    ev = detect(tone(440.0, {0.08, 0.3, 0.05, 0.05, 0.04, 0.03, 0.02}, kRate / 2, 4800));
    REQUIRE(count_type(ev, PitchEventType::NoteOn) == 1);
    CHECK(first_of(ev, PitchEventType::NoteOn)->note == 69);
}

TEST_CASE("pitch detector: a note needs its own fundamental, at most 20 dB under its partials") {
    // A3 with partials 2-8 as a piano's: at -17 dB the fundamental is weak but there, and the
    // note is A3. At -24 dB the tone is what several notes sharing A3's period sound like, and
    // A3 is never named.
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        for (double fundamental_db : {-17.0, -20.0, -21.0, -24.0}) {
            CAPTURE(static_cast<int>(method));
            CAPTURE(fundamental_db);
            const std::vector<double> amps = {
                0.3 * std::pow(10.0, fundamental_db / 20.0), 0.3, 0.2, 0.15, 0.1, 0.08, 0.05, 0.04};
            const auto ev = detect(tone(220.0, amps, kRate / 2, 4800), method);
            const auto* on = first_of(ev, PitchEventType::NoteOn);
            if (fundamental_db >= -20.0) {
                REQUIRE(on != nullptr);
                CHECK(on->note == 57);
            } else {
                for (const auto& e : ev) {
                    CHECK(!(e.type == PitchEventType::NoteOn && e.note == 57));
                }
            }
        }
    }
}

TEST_CASE("pitch detector: 30 cents from a key is the hard limit; halfway is not sure") {
    for (const int note : {60, 69}) {
        for (const double cents : {29.5, 30.5, 45.0, -29.5, -30.5, -45.0}) { // read to a fraction
            CAPTURE(note);
            CAPTURE(cents);
            const auto ev = detect(tone(grid_hz(note, cents), rich(0.3), kRate / 2, 4800));
            if (std::abs(cents) < 30.0) {
                REQUIRE(count_type(ev, PitchEventType::NoteOn) == 1);
                CHECK(first_of(ev, PitchEventType::NoteOn)->note == note);
                CHECK(count_type(ev, PitchEventType::NotSure) == 0);
            } else {
                check_only_not_sure(ev, 4800);
            }
        }
    }
    const auto ev = detect(tone(grid_hz(69, 50.0), rich(0.3), kRate / 2, 4800)); // a quarter tone
    CHECK(count_type(ev, PitchEventType::NoteOn) == 0);
    CHECK(count_type(ev, PitchEventType::NotSure) == 1);
}

TEST_CASE("pitch detector: an octave the check cannot decide is not sure, never a guess") {
    // Odd partials at about a tenth of their natural level: too many to be the octave above,
    // too few to be the note itself.
    for (int note : {60, 69, 72}) {
        for (double odd : {0.08, 0.1}) {
            CAPTURE(note);
            CAPTURE(odd);
            check_only_not_sure(detect(tone(grid_hz(note), rich(0.3, odd), kRate / 2, 4800)), 4800);
        }
    }
    // The same tone with its odd partials back at full level is just the note.
    const auto ev = detect(tone(grid_hz(60), rich(0.3), kRate / 2, 4800));
    CHECK(first_of(ev, PitchEventType::NoteOn)->note == 60);
}

// The first Expected event for the onset, run through the streaming detector. A detector that
// emits none fails the test: "no answer" is not a verdict.
PitchEvent ask_samples(const std::vector<float>& samples, std::int64_t onset,
                       const std::vector<int>& expected) {
    PitchDetector d;
    d.set_expected_notes(expected.data(), static_cast<int>(expected.size()));
    for (const auto& e : run_detector(d, samples.data(), static_cast<int>(samples.size()))) {
        if (e.type == PitchEventType::Expected && e.onset_sample >= onset - kRate / 50) {
            return e;
        }
    }
    FAIL("the detector emitted no Expected event for the onset");
    return {};
}

PitchEvent ask(int note, const std::vector<int>& expected) {
    SynthClipOptions o;
    o.note_s = 0.2f;
    const SynthClip c = make_synth_clip(note, 0.7f, 5, o, kRate);
    return ask_samples(c.samples, c.onset, expected);
}

// The frame a direct check of `expected` sees: three windows of an exact tone, at most the
// longest frame the checker takes.
int long_frame(int expected) {
    return std::min(ExpectedNoteChecker::kMaxFrame,
                    3 * ExpectedNoteChecker::window_samples(expected, kRate));
}

void check_not_sure(const ExpectedNoteResult& r) {
    CHECK(r.verdict == ExpectedVerdict::NotSure);
    CHECK(r.heard_note == -1);
    CHECK(r.confidence == 0.0f);
}

TEST_CASE("expected note: the played note is confirmed, a semitone or octave off is named") {
    for (int note : {33, 48, 60, 72, 96}) {
        CAPTURE(note);
        const auto e = ask(note, {note});
        CHECK(e.type == PitchEventType::Expected);
        CHECK(e.verdict == ExpectedVerdict::Expected);
        CHECK(e.note == note);
    }
    // Named from the expected note: asked for C4, played B3, the player is one key below.
    CHECK(ask(59, {60}).verdict == ExpectedVerdict::NeighborBelow);
    CHECK(ask(61, {60}).verdict == ExpectedVerdict::NeighborAbove);
    CHECK(ask(83, {84}).verdict == ExpectedVerdict::NeighborBelow);
    CHECK(ask(72, {60}).verdict == ExpectedVerdict::OctaveAbove);
    CHECK(ask(48, {60}).verdict == ExpectedVerdict::OctaveBelow);
    // A major third off is not a question this mode answers: never confirmed.
    CHECK(ask(64, {60}).verdict != ExpectedVerdict::Expected);
}

TEST_CASE("expected chord: a complete triad is all present; a left-out note is absent") {
    SynthClipOptions o;
    o.interferer = false;
    o.note_s = 0.25f;
    const int chord[] = {60, 64, 67};
    const auto full = make_synth_chord_clip(chord, 3, 0.7f, 3, o, kRate);
    const auto e = ask_samples(full.samples, full.onset, {60, 64, 67});
    CHECK(e.type == PitchEventType::Expected);
    CHECK(e.chord_present == 0b111);
    CHECK(e.chord_absent == 0);
    CHECK(e.verdict == ExpectedVerdict::Expected);

    const int partial[] = {60, 67}; // E4 left out
    const auto missing = make_synth_chord_clip(partial, 2, 0.7f, 3, o, kRate);
    const auto m = ask_samples(missing.samples, missing.onset, {60, 64, 67});
    CHECK((m.chord_present & 0b010) == 0); // never claims the missing E4
    CHECK(m.verdict != ExpectedVerdict::Expected);
}

TEST_CASE("expected note: a wrong, unclear or missing answer is an explicit not sure") {
    // 45 cents off, and unpitched noise.
    CHECK(ask_samples(tone(grid_hz(60, 45.0), rich(0.3), kRate / 2, 4800), 4800, {60}).verdict ==
          ExpectedVerdict::NotSure);
    const auto burst = ask_samples(noise(kRate / 2, 0.3f, 9, 4800), 4800, {60});
    CHECK(burst.type == PitchEventType::Expected);
    CHECK(burst.verdict == ExpectedVerdict::NotSure);
    CHECK(burst.note == -1);
    // A0 needs 4096 samples; the tone fades after 2300, the gate closes first, and the answer is
    // "not sure" rather than silence.
    std::vector<float> fading = tone(grid_hz(21), rich(0.3), kRate / 2, 4800);
    fade_out(fading, 4800 + 2300, 600);
    const auto gone = ask_samples(fading, 4800, {21});
    CHECK(gone.type == PitchEventType::Expected);
    CHECK(gone.verdict == ExpectedVerdict::NotSure);
    CHECK(gone.note == -1);
    CHECK(gone.sample - gone.onset_sample < ExpectedNoteChecker::window_samples(21, kRate));
}

TEST_CASE("expected note: two keys away, two keys at once or the octave below") {
    ExpectedNoteChecker checker(kRate);
    check_not_sure(checker.check(tone(grid_hz(62), rich(0.3), 8192).data(), long_frame(60), 60));
    // C4 and C#4 together: neither can be told from the other.
    auto cluster = tone(grid_hz(60), rich(0.3), 8192);
    const auto upper = tone(grid_hz(61), rich(0.3), 8192);
    std::transform(cluster.begin(), cluster.end(), upper.begin(), cluster.begin(), std::plus<>());
    check_not_sure(checker.check(cluster.data(), long_frame(60), 60));
    // The octave below is named when it explains the spectrum.
    auto audio = tone(grid_hz(60), rich(0.3), 8192);
    add_sub_octave(audio, grid_hz(60), 1.0, 0);
    const auto r = checker.check(audio.data(), long_frame(60), 60);
    CHECK(r.verdict == ExpectedVerdict::OctaveBelow);
    CHECK(r.heard_note == 48);
}

// Chords in open detection (microphone free play, Flow Mode): the detector must never name a key
// that is not in the chord, such as the period the chord's notes share (their common
// subharmonic) or the octave below its lowest note. It names one of the chord's keys, each
// confirmed by its own partials, or says "not sure".

// 150 ms of room noise (-60 dBFS), then `audio` is run through a detector calibrated on that noise.
std::vector<PitchEvent> run_calibrated(std::vector<float> audio, PitchMethod method,
                                       std::uint64_t seed) {
    add_room_noise(audio.data(), static_cast<int>(audio.size()), 1e-3f, seed);
    PitchDetectorConfig config;
    config.method = method;
    PitchDetector detector(config);
    std::vector<float> calm(kRate / 2, 0.0f);
    add_room_noise(calm.data(), kRate / 2, 1e-3f, 99);
    detector.begin_noise_calibration();
    run_detector(detector, calm.data(), kRate / 2);
    detector.end_noise_calibration();
    detector.reset();
    return run_detector(detector, audio.data(), static_cast<int>(audio.size()));
}

// `notes` struck at `starts_ms` (after the room noise) and left ringing for 400 ms.
std::vector<PitchEvent> play(const std::vector<int>& notes, const std::vector<int>& starts_ms,
                             float velocity, PitchMethod method) {
    const int lead = kRate * 15 / 100;
    const int length = lead + *std::max_element(starts_ms.begin(), starts_ms.end()) * kRate / 1000 +
                       kRate * 4 / 10;
    std::vector<float> audio(static_cast<size_t>(length), 0.0f);
    for (size_t i = 0; i < notes.size(); ++i) {
        PianoToneOptions tone;
        tone.note = notes[i];
        tone.velocity = velocity;
        tone.seed = 31 + i;
        add_piano_tone(audio.data(), length, lead + starts_ms[i] * kRate / 1000, tone, kRate);
    }
    return run_calibrated(audio, method, 1);
}

// Every note-on is a key of the chord, and the chord is answered: by one of its keys, or by
// "not sure".
void check_chord(const std::vector<int>& notes, float velocity) {
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        CAPTURE(static_cast<int>(method));
        bool answered = false;
        for (const auto& e : play(notes, std::vector<int>(notes.size(), 0), velocity, method)) {
            if (e.type == PitchEventType::NoteOn) {
                CAPTURE(e.note);
                CHECK(std::find(notes.begin(), notes.end(), e.note) != notes.end());
            }
            answered =
                answered || e.type == PitchEventType::NoteOn || e.type == PitchEventType::NotSure;
        }
        CHECK(answered);
    }
}

// Wrong and octave-off keys for chord `index` of seed `seed` of a `pitch-eval --open-chords` run,
// played alone.
int wrong_in_chord(std::uint64_t seed, int index, bool interferer = true) {
    SynthEvalOptions options;
    options.seed = seed;
    options.clip.interferer = interferer;
    options.detector.method = PitchMethod::Mpm;
    options.melody_first = index;
    const OpenChordReport r = evaluate_open_chord_synthetic(options, index + 1);
    REQUIRE(r.registers.total.notes == 1);
    CHECK(r.registers.total.missed == 0); // no answer at all is not "no wrong key"
    return r.registers.total.wrong;
}

TEST_CASE("pitch chords: McLeod names no key outside the chord, struck together or nearly") {
    SynthEvalOptions options; // one chord for each register in each context
    options.detector.method = PitchMethod::Mpm;
    const int chords = kOpenChordContexts * kRegisterCount;
    const OpenChordReport r = evaluate_open_chord_synthetic(options, chords);
    REQUIRE(r.registers.total.notes == chords);
    CHECK(r.registers.total.wrong == 0);
    CHECK(r.registers.total.octave == 0);
    // Not a detector that stays safe by never answering: some chords are named by one of their
    // keys, and the chords struck out of silence are all answered.
    CHECK(rate(r.registers.total.correct, r.registers.total.notes) > 0.1f);
    CHECK(r.contexts[static_cast<size_t>(OpenChordContext::Silence)].missed == 0);
}

TEST_CASE("pitch chords: a chord is not the octave below its lowest note") {
    check_chord({89, 96, 105},
                0.25f); // F6 C7 A7 are partials 2, 3, 5 of F5: only its fundamental is empty
    check_chord({58, 65, 74}, 0.75f); // A#3 F4 D5 gave A#2
    check_chord({71, 76}, 0.75f);     // B4 E5 gave E3
}

TEST_CASE("pitch chords: chords that once gave wrong keys name none now") {
    CHECK(wrong_in_chord(15, 1121) ==
          0); // C#4 G#4 F5, soft: C#3, a comb so low all partials fell in a notch
    CHECK(wrong_in_chord(4, 1839) == 0);      // A5 D6 F#6 over a ringing chord: D4, hammer noise
    CHECK(wrong_in_chord(6, 83, false) == 0); // G#6 D#7 C8 over a ringing chord: C7
}

// Legato: the previous note still rings under each onset. The detector never names a wrong key
// there; when the blend of the notes leaves it unsure it says "not sure".
TEST_CASE("pitch legato: McLeod names no wrong key in melodies of loud, soft and repeated notes") {
    SynthEvalOptions options;
    options.seed = 1;
    options.detector.method = PitchMethod::Mpm;
    options.melody_note_velocities = true;
    options.melody_repeats = 0.25f;
    const EvalReport r = evaluate_melody_synthetic(options, 2, 12);
    CHECK(r.total.notes == 24);
    CHECK(r.total.wrong + r.total.octave == 0);
    CHECK(rate(r.total.correct, r.total.notes) > 0.5f); // not safe by never answering
}

// Notes struck `gap_ms` apart after 150 ms of room noise, each damper falling 40 ms after the next
// note starts, as in the melody evaluation. The outcome for the last note.
NoteOutcome last_of(const std::vector<int>& notes, int gap_ms, float velocity, std::uint64_t seed,
                    PitchMethod method) {
    std::vector<int> onsets;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        onsets.push_back(kRate * 15 / 100 + static_cast<int>(i) * gap_ms * kRate / 1000);
    }
    const int length = onsets.back() + kRate * 35 / 100;
    std::vector<float> audio(static_cast<std::size_t>(length), 0.0f);
    for (std::size_t i = 0; i < notes.size(); ++i) {
        std::vector<float> one(static_cast<std::size_t>(length), 0.0f);
        PianoToneOptions tone;
        tone.note = notes[i];
        tone.velocity = velocity;
        tone.seed = seed * 31 + i;
        add_piano_tone(one.data(), length, onsets[i], tone, kRate);
        const int release = i + 1 < notes.size() ? onsets[i + 1] + kRate / 25 : length;
        for (int k = 0; k < length; ++k) {
            const float fade =
                k < release ? 1.0f : std::exp(-static_cast<float>(k - release) / (0.03f * kRate));
            audio[static_cast<std::size_t>(k)] += one[static_cast<std::size_t>(k)] * fade;
        }
    }
    const auto events = run_calibrated(audio, method, seed);
    return score_note(notes.back(), onsets.back(), length, events.data(),
                      static_cast<int>(events.size()), kRate)
        .outcome;
}

TEST_CASE("pitch legato: notes under a ringing one that once gave wrong keys are named right") {
    struct Row {
        std::vector<int> notes;
        int gap_ms;
        float velocity;
        std::uint64_t seed;
    };
    const Row rows[] = {
        {{66, 59, 52}, 235, 1.0f, 5},   // B3 over E3: not their common subharmonic E2
        {{86, 79, 73}, 156, 0.5f, 6},   // G5 over C#5: a tritone blend is not a deep bass note
        {{53, 58, 53}, 151, 0.25f, 49}, // F3 again after A#3: not A#1
        {{59, 57, 59}, 247, 0.5f, 25},  // B3 over a missed A3: not A3
    };
    for (const Row& r : rows) {
        for (const PitchMethod method : {PitchMethod::Mpm, PitchMethod::Yin}) {
            CAPTURE(r.notes.back());
            CAPTURE(static_cast<int>(method));
            CHECK(last_of(r.notes, r.gap_ms, r.velocity, r.seed, method) == NoteOutcome::Correct);
        }
    }
}

TEST_CASE("pitch legato: a held bass note is reported once, not struck again") {
    for (const PitchMethod method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        for (const int note : {28, 36, 43}) {
            CAPTURE(static_cast<int>(method));
            CAPTURE(note);
            const int length = kRate * 6 / 5;
            std::vector<float> audio(static_cast<std::size_t>(length), 0.0f);
            PianoToneOptions tone;
            tone.note = note;
            tone.velocity = 0.5f;
            tone.seed = 100 + static_cast<std::uint64_t>(note);
            add_piano_tone(audio.data(), length, kRate / 10, tone, kRate);
            add_room_noise(audio.data(), length, 1e-3f, 1);
            const auto ev = detect(audio, method);
            REQUIRE(count_type(ev, PitchEventType::NoteOn) == 1);
            CHECK(first_of(ev, PitchEventType::NoteOn)->note == note);
        }
    }
}

// Scoring a note or chord from its events.

PitchEvent on(int note, std::int64_t onset, std::int64_t at) {
    PitchEvent e;
    e.type = PitchEventType::NoteOn;
    e.note = note;
    e.onset_sample = onset;
    e.sample = at;
    return e;
}

TEST_CASE("pitch scoring: the first matching note-on is correct, a later wrong one spoils it") {
    const PitchEvent ev[] = {on(60, 1000, 2000)};
    const auto s = score_note(60, 1000, 20000, ev, 1, kRate);
    CHECK(s.outcome == NoteOutcome::Correct);
    CHECK(s.latency_samples == 1000);
    const PitchEvent then_wrong[] = {on(60, 1000, 2000), on(62, 5000, 6000)};
    CHECK(score_note(60, 1000, 20000, then_wrong, 2, kRate).outcome == NoteOutcome::Wrong);
}

TEST_CASE("pitch scoring: octaves off count as octave errors, other notes as wrong") {
    for (const auto& [key, outcome] :
         {std::pair{72, NoteOutcome::OctaveError}, std::pair{36, NoteOutcome::OctaveError},
          std::pair{67, NoteOutcome::Wrong}}) {
        const PitchEvent ev[] = {on(key, 1000, 2000)};
        CHECK(score_note(60, 1000, 20000, ev, 1, kRate).outcome == outcome);
    }
}

TEST_CASE("pitch scoring: not sure, missed, and events of other notes") {
    PitchEvent unsure;
    unsure.type = PitchEventType::NotSure;
    unsure.onset_sample = 1000;
    CHECK(score_note(60, 1000, 20000, &unsure, 1, kRate).outcome == NoteOutcome::NotSure);
    CHECK(score_note(60, 1000, 20000, nullptr, 0, kRate).outcome == NoteOutcome::Missed);
    // An onset 20 ms early still belongs to the note, one sample more belongs to something
    // before; an onset at `end` is the next note's.
    const PitchEvent at_edge[] = {on(60, 1000 - kRate / 50, 2000)};
    const PitchEvent past_edge[] = {on(60, 1000 - kRate / 50 - 1, 2000)};
    const PitchEvent next_note[] = {on(60, 20000, 21000)};
    CHECK(score_note(60, 1000, 20000, at_edge, 1, kRate).outcome == NoteOutcome::Correct);
    CHECK(score_note(60, 1000, 20000, past_edge, 1, kRate).outcome == NoteOutcome::Missed);
    CHECK(score_note(60, 1000, 20000, next_note, 1, kRate).outcome == NoteOutcome::Missed);
}

TEST_CASE("pitch scoring: a chord is correct when every note-on is one of its keys") {
    const int chord[] = {60, 64, 67};
    const PitchEvent one[] = {on(64, 1000, 2000)};
    const PitchEvent two[] = {on(64, 1000, 2000), on(67, 9000, 9500)};
    const auto s = score_chord(chord, 3, 1000, 20000, one, 1, kRate);
    CHECK(s.outcome == NoteOutcome::Correct);
    CHECK(s.reported_note == 64);
    CHECK(s.latency_samples == 1000);
    CHECK(score_chord(chord, 3, 1000, 20000, two, 2, kRate).outcome == NoteOutcome::Correct);
}

TEST_CASE(
    "pitch scoring: a key outside the chord makes it wrong, an octave of one an octave error") {
    const int chord[] = {60, 64, 67};
    const PitchEvent subharmonic[] = {on(36, 1000, 2000)};
    const PitchEvent other[] = {on(64, 1000, 2000), on(62, 3000, 4000)};
    CHECK(score_chord(chord, 3, 1000, 20000, subharmonic, 1, kRate).outcome ==
          NoteOutcome::OctaveError);
    const auto s = score_chord(chord, 3, 1000, 20000, other, 2, kRate);
    CHECK(s.outcome == NoteOutcome::Wrong);
    CHECK(s.reported_note == 62);
}

// The microphone's queue of detected notes: what comes out of it, and that taking at any point of a
// frame loses and repeats nothing.

MicEventIn mic_in(PitchEventType type, int note, std::int64_t onset_ns, std::int64_t sample_ns) {
    MicEventIn in;
    in.event.type = type;
    in.event.note = note;
    in.onset_ns = onset_ns;
    in.sample_ns = sample_ns;
    return in;
}

MicEventIn mic_on(int note, std::int64_t onset_ns) {
    return mic_in(PitchEventType::NoteOn, note, onset_ns, onset_ns + ms(40));
}

MicEventIn mic_off(int note, std::int64_t at_ns) {
    return mic_in(PitchEventType::NoteOff, note, at_ns - ms(300), at_ns);
}

MicEventIn mic_not_sure(std::int64_t onset_ns, int guess = -1) {
    MicEventIn in = mic_in(PitchEventType::NotSure, -1, onset_ns, onset_ns + ms(100));
    in.event.guess_note = guess;
    return in;
}

// A MicInbox with a queue of events standing in for the analysis thread's.
struct Mic {
    MicInbox inbox;
    std::deque<MicEventIn> queue;
    std::vector<MidiEvent> events;
    std::vector<UnsureMark> marks;

    void take(std::int64_t now_ns) {
        inbox.take(
            [this](MicEventIn& in) {
                if (queue.empty()) {
                    return false;
                }
                in = queue.front();
                queue.pop_front();
                return true;
            },
            now_ns, events, marks);
    }
};

TEST_CASE("mic inbox: a note comes out once, stamped at its onset, tagged as the microphone") {
    Mic m;
    m.queue.push_back(mic_on(60, 100 * kMs));
    m.take(300 * kMs);
    REQUIRE(m.events.size() == 1);
    CHECK(m.events[0].type == MidiEventType::NoteOn);
    CHECK(m.events[0].data1 == 60);
    CHECK(m.events[0].time_ns == 100 * kMs);
    CHECK(m.events[0].source == InputSource::Microphone);
    CHECK(m.inbox.last_note() == 60);
    // The Ear card never takes a microphone note as an answer, a MIDI one it does.
    CHECK_FALSE(ear_takes_answer(m.events[0]));
    m.events[0].source = InputSource::Midi;
    CHECK(ear_takes_answer(m.events[0]));
    // A second take, later in the same frame, finds nothing new.
    m.events.clear();
    m.take(310 * kMs);
    CHECK(m.events.empty());
}

TEST_CASE("mic inbox: a not-sure event is a marker with its onset, and presses nothing") {
    Mic m;
    m.queue.push_back(mic_on(60, 100 * kMs));
    m.queue.push_back(mic_not_sure(400 * kMs));
    m.take(600 * kMs);
    CHECK(m.events.size() == 1);
    REQUIRE(m.marks.size() == 1);
    CHECK(m.marks[0].onset_ns == 400 * kMs);
    CHECK(m.marks[0].last_heard == 60);
    CHECK(m.marks[0].at_ns == 600 * kMs);
    CHECK(m.inbox.unsure_seen() == 1);
}

TEST_CASE("mic inbox: closing the microphone releases the keys it holds, stamped at the close") {
    Mic m;
    m.queue.push_back(mic_on(60, 100 * kMs));
    m.queue.push_back(mic_on(64, 110 * kMs));
    m.take(200 * kMs); // taken, then the input switches to MIDI
    m.inbox.release_all(250 * kMs);
    // The next take reads nothing (the pipeline is closed), only hands the releases out.
    m.inbox.flush(m.events);
    int down[128] = {};
    for (const MidiEvent& ev : m.events) {
        down[ev.data1] += ev.type == MidiEventType::NoteOn ? 1 : -1;
    }
    CHECK(down[60] == 0);
    CHECK(down[64] == 0);
    REQUIRE(m.events.size() == 4);
    CHECK(m.events[2].time_ns == 250 * kMs);
    CHECK(m.events[3].time_ns == 250 * kMs);
    m.inbox.flush(m.events); // handed out once
    CHECK(m.events.size() == 4);
    m.inbox.release_all(300 * kMs); // nothing is held any more
    m.take(310 * kMs);
    CHECK(m.events.size() == 4);
}

// What the microphone's pitch events do: presses and releases, the "not sure" marker, how it
// shares the keyboard with MIDI, and (the key rule) that an unsure event neither hits nor misses.

std::vector<MicOut> apply(MicNoteBridge& b, const MicEventIn& in) {
    MicOut out[MicNoteBridge::kMaxOut];
    const int n = b.apply(in, out);
    return {out, out + n};
}

MicEventIn verdict_event(ExpectedVerdict v, int heard, int expected, std::int64_t onset_ns) {
    MicEventIn in = mic_in(PitchEventType::Expected, heard, onset_ns, onset_ns + ms(60));
    in.event.verdict = v;
    in.expected_count = 1;
    in.expected[0] = static_cast<std::int8_t>(expected);
    return in;
}

TEST_CASE("mic bridge: not sure never presses or releases a key, it only marks the register") {
    MicNoteBridge b;
    apply(b, mic_on(64, sec(1)));
    const auto out = apply(b, mic_not_sure(sec(2), 71));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::Unsure);
    CHECK(out[0].note == 71);
    CHECK(out[0].time_ns == sec(2));
    // The key that was down stays down.
    MicOut rel[MicNoteBridge::kMaxOut];
    CHECK(b.release_due(sec(10), rel, MicNoteBridge::kMaxOut) == 0);
}

TEST_CASE("mic bridge: a neighbour or octave verdict presses the key that was heard") {
    MicNoteBridge b;
    const auto below = apply(b, verdict_event(ExpectedVerdict::NeighborBelow, 59, 60, sec(1)));
    REQUIRE(below.size() == 1);
    CHECK(below[0].note == 59);
    const auto octave = apply(b, verdict_event(ExpectedVerdict::OctaveAbove, 72, 60, sec(2)));
    REQUIRE(octave.size() == 1);
    CHECK(octave[0].note == 72);
}

TEST_CASE("mic bridge: a note-on is a press at its onset, a release for a note it did not press is "
          "dropped") {
    MicNoteBridge b;
    const auto on = apply(b, mic_on(60, sec(1)));
    REQUIRE(on.size() == 1);
    CHECK(on[0].kind == MicOutKind::NoteOn);
    CHECK(on[0].time_ns == sec(1)); // the onset, not when the pitch was confirmed
    CHECK(apply(b, mic_off(60, sec(2))).size() == 1);
    CHECK(apply(b, mic_off(60, sec(3))).empty()); // pressed once, released once
    CHECK(apply(b, mic_off(64, sec(3))).empty());
}

TEST_CASE(
    "mic bridge: the keyboard and the microphone hearing one strike are one press (Both mode)") {
    const std::pair<std::int64_t, bool> rows[] = {{ms(60), true}, // mic onset after the keyboard's
                                                  {kMicMergeNs, true},
                                                  {kMicMergeNs + 1, false},
                                                  {-kMicMergeBeforeNs, true},
                                                  {-kMicMergeBeforeNs - 1, false}};
    for (const auto& [after_ns, merged] : rows) {
        CAPTURE(after_ns);
        MicNoteBridge b;
        b.note_midi_press(60, sec(5));
        CHECK(apply(b, mic_on(60, sec(5) + after_ns)).empty() == merged);
    }
    MicNoteBridge b;
    b.note_midi_press(60, sec(1));
    CHECK(apply(b, mic_on(60, sec(1) + ms(60))).empty());
    CHECK(apply(b, mic_off(60, sec(2))).empty());             // and so is its release
    CHECK(apply(b, mic_on(62, sec(1) + ms(60))).size() == 1); // another key is its own
}

TEST_CASE("flow: an unsure event is not a press, so it is never a wrong note") {
    const Song song = song_test::notes_song({{1.0, 60}}).song;
    Transport tr;
    tr.set_song(song);
    FlowEvaluator eval(song);
    tr.play(sec(1));
    std::vector<TransportOp> ops;
    const auto apply_ops = [&] {
        tr.take_ops(ops);
        for (const TransportOp& op : ops) {
            eval.apply(op);
        }
        ops.clear();
    };
    apply_ops();
    eval.apply_unsure(sec(2) + ms(10));
    eval.apply_unsure(sec(3));
    for (std::int64_t t = sec(1); t <= sec(4); t += 16'666'667) {
        tr.update(t);
        apply_ops();
        eval.update(t);
    }
    CHECK(eval.summary().wrong == 0);
    CHECK(eval.summary().combo == 0);
}

TEST_CASE("wait: an unsure event handed to the song leaves it waiting at the chord") {
    using namespace song_test;
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    c.load(notes_song({{1.0, 60}, {3.0, 64}}), now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    run(c, piano, now, now + 1500 * kMs);
    REQUIRE(c.waiting());
    const double at = c.position_s();
    c.input_unsure(now);
    run(c, piano, now, now + 1000 * kMs);
    c.input_unsure(now);
    run(c, piano, now, now + 500 * kMs);
    CHECK(c.waiting());
    CHECK(c.position_s() == doctest::Approx(at));
    REQUIRE(c.summary() != nullptr);
    CHECK(c.summary()->counts.played == 0);
    CHECK(c.summary()->wrong == 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("sample ring: the capacity is a power of two and samples come out in order") {
    SampleRing ring(100);
    CHECK(ring.capacity() == 128);
    std::vector<float> in(60);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<float>(i);
    }
    CHECK(ring.write(in.data(), in.size()));
    CHECK(ring.available() == 60);
    std::vector<float> out(100, -1.0f);
    CHECK(ring.read(out.data(), 40) == 40);
    for (std::size_t i = 0; i < 40; ++i) {
        CHECK(out[i] == static_cast<float>(i));
    }
    CHECK(ring.read(out.data(), 100) == 20);
    CHECK(out[0] == 40.0f);
    CHECK(ring.read(out.data(), 100) == 0);
}

TEST_CASE("sample ring: a block that does not fit is dropped whole and counted") {
    SampleRing ring(64);
    std::vector<float> a(50, 1.0f);
    std::vector<float> b(20, 2.0f);
    CHECK(ring.write(a.data(), a.size()));
    CHECK_FALSE(ring.write(b.data(), b.size())); // 14 free
    CHECK(ring.dropped_samples() == 20);
    CHECK(ring.dropped_blocks() == 1);
    CHECK(ring.available() == 50); // nothing of b went in
    std::vector<float> out(64);
    CHECK(ring.read(out.data(), 64) == 50);
    CHECK(ring.write(b.data(), b.size())); // room again
}

TEST_CASE("sample ring: a producer thread and a consumer thread lose nothing and keep order") {
    // The producer writes 1,2,3,... in blocks of varying size and waits when the ring is full;
    // the consumer checks the sequence. Run under ThreadSanitizer this also checks the hand-off.
    constexpr std::size_t kTotal = 400'000;
    SampleRing ring(1024);
    std::atomic<bool> bad{false};
    std::thread producer([&] {
        std::vector<float> block;
        std::size_t next = 0;
        std::size_t size = 1;
        while (next < kTotal) {
            const std::size_t n = std::min(size, kTotal - next);
            block.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                block[i] = static_cast<float>(next + i);
            }
            while (!ring.write(block.data(), n)) {
                std::this_thread::yield();
            }
            next += n;
            size = size % 700 + 3;
        }
    });
    std::thread consumer([&] {
        std::vector<float> out(300);
        std::size_t expect = 0;
        while (expect < kTotal) {
            const std::size_t n = ring.read(out.data(), out.size());
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                bad = bad || out[i] != static_cast<float>(expect + i);
            }
            expect += n;
        }
    });
    producer.join();
    consumer.join();
    CHECK_FALSE(bad.load());
}

TEST_CASE("spsc queue: items pop in push order, and a full queue drops new items and counts them") {
    SpscQueue<int, 4> q;
    for (int i = 0; i < 4; ++i) {
        CHECK(q.push(i));
    }
    CHECK_FALSE(q.push(100));
    CHECK_FALSE(q.push(101));
    CHECK(q.dropped() == 2);
    int out = -1;
    for (int i = 0; i < 4; ++i) { // the items already queued are intact; the dropped never appear
        REQUIRE(q.pop(out));
        CHECK(out == i);
    }
    CHECK_FALSE(q.pop(out));
}

TEST_CASE(
    "spsc queue: two threads, every item arrives once and in order, or is counted as dropped") {
    // A small queue so the producer overruns the consumer and both paths run under contention.
    SpscQueue<MidiEvent, 64> q;
    constexpr std::int64_t kItems = 200000;
    std::thread producer([&q] {
        for (std::int64_t i = 0; i < kItems; ++i) {
            // Every field derives from i, so a torn read shows up as a mismatch below.
            const auto low = static_cast<std::uint8_t>(i & 0x7F);
            q.push(MidiEvent{i, MidiEventType::NoteOn, static_cast<std::uint8_t>(i & 0x0F), low,
                             static_cast<std::uint8_t>(127 - low)});
        }
    });
    std::int64_t received = 0;
    std::int64_t last = -1;
    bool ordered = true;
    bool intact = true;
    // Spin until everything pushed is accounted for as received or dropped.
    while (true) {
        MidiEvent ev;
        if (q.pop(ev)) {
            ordered = ordered && ev.time_ns > last;
            const auto low = static_cast<std::uint8_t>(ev.time_ns & 0x7F);
            intact = intact && ev.data1 == low && ev.data2 == 127 - low &&
                     ev.channel == (ev.time_ns & 0x0F);
            last = ev.time_ns;
            ++received;
        } else if (received + static_cast<std::int64_t>(q.dropped()) == kItems) {
            break;
        }
    }
    producer.join();
    CHECK(ordered);
    CHECK(intact);
    CHECK(received + static_cast<std::int64_t>(q.dropped()) == kItems);
    CHECK(received > 0);
    MidiEvent ev;
    CHECK_FALSE(q.pop(ev));
}

TEST_CASE("pitch: single notes across the keyboard are never a wrong key or an octave off") {
    SynthEvalOptions options; // every third key A0 to C8 at two velocities, room noise, a tone
    options.velocities = {0.4f, 0.9f};
    options.variants = 1;
    options.note_step = 3;
    options.seed = 11;
    options.clip.note_s = 0.2f;
    options.detector.method = PitchMethod::Mpm;
    const EvalReport r = evaluate_synthetic(options);
    REQUIRE(r.total.notes == 60);
    CHECK(r.total.wrong == 0);
    CHECK(r.total.octave == 0);
    CHECK(r.total.notes - r.total.correct <= 2); // "not sure" is allowed, but rarely
}

} // namespace
