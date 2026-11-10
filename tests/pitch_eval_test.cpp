// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/pitch_eval.hpp"
#include "etude/pitch_synth.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr int kRate = 48000;

PitchEvent on(int note, std::int64_t onset, std::int64_t at) {
    PitchEvent e;
    e.type = PitchEventType::NoteOn;
    e.note = note;
    e.onset_sample = onset;
    e.sample = at;
    return e;
}

} // namespace

TEST_CASE("pitch scoring: the first matching note-on is correct, with its latency") {
    const PitchEvent ev[] = {on(60, 1000, 2000)};
    const auto s = score_note(60, 1000, 20000, ev, 1, kRate);
    CHECK(s.outcome == NoteOutcome::Correct);
    CHECK(s.latency_samples == 1000);
}

TEST_CASE("pitch scoring: a wrong note-on after a correct one still makes the note wrong") {
    const PitchEvent ev[] = {on(60, 1000, 2000), on(62, 5000, 6000)};
    CHECK(score_note(60, 1000, 20000, ev, 2, kRate).outcome == NoteOutcome::Wrong);
}

TEST_CASE("pitch scoring: octaves off count as octave errors, other notes as wrong") {
    const PitchEvent up[] = {on(72, 1000, 2000)};
    const PitchEvent down2[] = {on(36, 1000, 2000)};
    const PitchEvent fifth[] = {on(67, 1000, 2000)};
    CHECK(score_note(60, 1000, 20000, up, 1, kRate).outcome == NoteOutcome::OctaveError);
    CHECK(score_note(60, 1000, 20000, down2, 1, kRate).outcome == NoteOutcome::OctaveError);
    CHECK(score_note(60, 1000, 20000, fifth, 1, kRate).outcome == NoteOutcome::Wrong);
}

TEST_CASE("pitch scoring: not sure, missed, and events of other notes") {
    PitchEvent unsure;
    unsure.type = PitchEventType::NotSure;
    unsure.onset_sample = 1000;
    CHECK(score_note(60, 1000, 20000, &unsure, 1, kRate).outcome == NoteOutcome::NotSure);
    CHECK(score_note(60, 1000, 20000, nullptr, 0, kRate).outcome == NoteOutcome::Missed);
    // An onset more than 20 ms early belongs to something before; one at `end` to the next note.
    const PitchEvent other[] = {on(62, 1000 - kRate / 40, 2000), on(64, 20000, 21000)};
    CHECK(score_note(60, 1000, 20000, other, 2, kRate).outcome == NoteOutcome::Missed);
}

TEST_CASE("pitch scoring: a chord is correct when every note-on is one of its keys") {
    const int chord[] = {60, 64, 67};
    const PitchEvent one[] = {on(64, 1000, 2000)};
    const PitchEvent two[] = {on(64, 1000, 2000), on(67, 9000, 9500)};
    auto s = score_chord(chord, 3, 1000, 20000, one, 1, kRate);
    CHECK(s.outcome == NoteOutcome::Correct);
    CHECK(s.reported_note == 64);
    CHECK(s.latency_samples == 1000);
    CHECK(score_chord(chord, 3, 1000, 20000, two, 2, kRate).outcome == NoteOutcome::Correct);
}

TEST_CASE("pitch scoring: a key outside the chord makes it wrong, an octave of one of its keys an "
          "octave error") {
    const int chord[] = {60, 64, 67};
    const PitchEvent subharmonic[] = {on(36, 1000, 2000)};
    const PitchEvent other[] = {on(64, 1000, 2000), on(62, 3000, 4000)};
    CHECK(score_chord(chord, 3, 1000, 20000, subharmonic, 1, kRate).outcome ==
          NoteOutcome::OctaveError);
    const auto s = score_chord(chord, 3, 1000, 20000, other, 2, kRate);
    CHECK(s.outcome == NoteOutcome::Wrong);
    CHECK(s.reported_note == 62);
}

TEST_CASE("pitch scoring: a chord with no note-on is not sure or missed") {
    const int chord[] = {48, 55};
    PitchEvent unsure;
    unsure.type = PitchEventType::NotSure;
    unsure.onset_sample = 1000;
    CHECK(score_chord(chord, 2, 1000, 20000, &unsure, 1, kRate).outcome == NoteOutcome::NotSure);
    CHECK(score_chord(chord, 2, 1000, 20000, nullptr, 0, kRate).outcome == NoteOutcome::Missed);
    // A note-on for the chord before it (more than 20 ms early) is not this chord's.
    const PitchEvent early[] = {on(40, 1000 - kRate / 40, 2000)};
    CHECK(score_chord(chord, 2, 1000, 20000, early, 1, kRate).outcome == NoteOutcome::Missed);
}

TEST_CASE("synthetic piano: same seed, same samples; the onset is silent before it") {
    SynthClipOptions o;
    o.noise_rms = 0.0f;
    o.interferer = false;
    const auto a = make_synth_clip(60, 0.7f, 42, o, kRate);
    const auto b = make_synth_clip(60, 0.7f, 42, o, kRate);
    REQUIRE(a.samples.size() == b.samples.size());
    CHECK(a.samples == b.samples);
    CHECK(a.samples[static_cast<size_t>(a.onset) - 1] == 0.0f);
    float peak = 0.0f;
    for (float v : a.samples) {
        peak = std::max(peak, std::fabs(v));
    }
    CHECK(peak > 0.05f);
    CHECK(peak < 1.0f);
    const auto c = make_synth_clip(60, 0.7f, 43, o, kRate);
    CHECK(c.samples != a.samples);
}

TEST_CASE("synthetic piano: the tone's pitch follows the note, the detune and A4") {
    for (float detune : {0.0f, 20.0f, -20.0f}) {
        for (float a4 : {440.0f, 442.0f}) {
            PianoToneOptions o;
            o.note = 69;
            o.detune_cents = detune;
            o.a4_hz = a4;
            o.hammer_noise = 0.0f;
            std::vector<float> out(kRate / 2, 0.0f);
            add_piano_tone(out.data(), static_cast<int>(out.size()), 4800, o, kRate);
            PitchDetectorConfig cfg;
            cfg.a4_hz = a4;
            PitchDetector d(cfg);
            const auto events = run_detector(d, out.data(), static_cast<int>(out.size()));
            const PitchEvent* on = nullptr;
            for (const auto& e : events) {
                if (e.type == PitchEventType::NoteOn) {
                    on = &e;
                    break;
                }
            }
            CAPTURE(detune);
            CAPTURE(a4);
            REQUIRE(on != nullptr);
            CHECK(on->note == 69);
            const double want = a4 * std::exp2(detune / 1200.0);
            CHECK(on->frequency_hz == doctest::Approx(want).epsilon(0.004));
        }
    }
}

namespace {

// A small, fast slice of the synthetic set: every octave of the keyboard, one velocity.
SynthEvalOptions small_set() {
    SynthEvalOptions o;
    o.low_note = 24;
    o.note_step = 12;
    o.velocities = {0.7f};
    o.variants = 1;
    o.seed = 3;
    o.clip.note_s = 0.2f;
    return o;
}

} // namespace

TEST_CASE("pitch evaluation: expected-note mode never gives a wrong verdict on a small set") {
    const ExpectedEvalReport r = evaluate_expected_synthetic(small_set());
    for (const VerdictStats* s : {&r.expected, &r.neighbor, &r.octave}) {
        CHECK(s->queries > 0);
        CHECK(s->wrong == 0);
        CHECK(s->false_accept == 0);
        CHECK(s->correct + s->wrong + s->not_sure == s->queries);
    }
    // Not a checker that stays safe by never answering.
    CHECK(rate(r.expected.correct, r.expected.queries) >= 0.75f);
    int by_register = 0;
    for (const auto& reg : r.registers) {
        by_register += reg.queries;
    }
    CHECK(by_register == r.expected.queries + r.neighbor.queries + r.octave.queries);
}

TEST_CASE("pitch evaluation: expected-chord mode never gives a wrong verdict on a small set") {
    SynthEvalOptions o = small_set();
    o.clip.note_s = 0.25f;
    const ChordEvalReport r = evaluate_chord_synthetic(o, 12);
    CHECK(r.notes == 36);
    CHECK(r.wrong == 0);
    CHECK(r.false_present == 0);
    CHECK(r.correct + r.wrong + r.not_sure == r.notes);
    CHECK(r.missing_caught <= r.missing_notes);
    CHECK(r.correct >= r.notes / 2);
}

TEST_CASE("pitch evaluation: a tiny melody run completes and its counts add up") {
    // Accuracy is not gated here: low-register legato is still being worked on.
    SynthEvalOptions o = small_set();
    const EvalReport r = evaluate_melody_synthetic(o, 2, 4);
    CHECK(r.total.notes == 8);
    CHECK(r.total.correct + r.total.wrong + r.total.not_sure + r.total.missed == r.total.notes);
    CHECK(r.total.octave <= r.total.wrong);
    CHECK(r.total.latency_ms.size() == static_cast<size_t>(r.total.correct));
    int notes = 0;
    for (const auto& reg : r.registers) {
        notes += reg.notes;
        CHECK(reg.correct + reg.wrong + reg.not_sure + reg.missed == reg.notes);
    }
    CHECK(notes == r.total.notes);
}
