// SPDX-License-Identifier: GPL-3.0-or-later
// What the microphone's pitch events do: presses and releases, the "not sure" marker, how it
// shares the keyboard with MIDI, and (the key rule) that an unsure event neither hits nor misses.
#include "etude/evaluator.hpp"
#include "etude/mic_input.hpp"
#include "etude/transport.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <vector>

using namespace etude;

namespace {

constexpr std::int64_t sec(double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); }
constexpr std::int64_t ms(double m) { return static_cast<std::int64_t>(std::llround(m * 1e6)); }

MicEventIn note_on_event(int note, std::int64_t onset_ns) {
    MicEventIn in;
    in.event.type = PitchEventType::NoteOn;
    in.event.note = note;
    in.onset_ns = onset_ns;
    in.sample_ns = onset_ns + ms(40);
    return in;
}

MicEventIn note_off_event(int note, std::int64_t at_ns) {
    MicEventIn in;
    in.event.type = PitchEventType::NoteOff;
    in.event.note = note;
    in.onset_ns = at_ns - ms(300);
    in.sample_ns = at_ns;
    return in;
}

MicEventIn not_sure_event(std::int64_t onset_ns, int guess = -1) {
    MicEventIn in;
    in.event.type = PitchEventType::NotSure;
    in.event.guess_note = guess;
    in.onset_ns = onset_ns;
    in.sample_ns = onset_ns + ms(100);
    return in;
}

MicEventIn verdict_event(ExpectedVerdict v, int heard, std::initializer_list<int> expected,
                         std::int64_t onset_ns) {
    MicEventIn in;
    in.event.type = PitchEventType::Expected;
    in.event.verdict = v;
    in.event.note = heard;
    in.onset_ns = onset_ns;
    in.sample_ns = onset_ns + ms(60);
    in.expected_count = static_cast<int>(expected.size());
    int i = 0;
    for (const int n : expected) {
        in.expected[static_cast<std::size_t>(i++)] = static_cast<std::int8_t>(n);
    }
    return in;
}

std::vector<MicOut> apply(MicNoteBridge& b, const MicEventIn& in) {
    MicOut out[MicNoteBridge::kMaxOut];
    const int n = b.apply(in, out);
    return {out, out + n};
}

} // namespace

// ---- Marker, meter

TEST_CASE("a not-sure marker is fully shown at first, then fades to nothing and stays gone") {
    CHECK(unsure_marker_alpha(0) == 1.0f);
    CHECK(unsure_marker_alpha(kUnsureHoldNs) == 1.0f);
    float last = 1.0f;
    for (std::int64_t t = kUnsureHoldNs; t <= kUnsureHoldNs + kUnsureFadeNs; t += ms(50)) {
        const float a = unsure_marker_alpha(t);
        CHECK(a <= last);
        CHECK(a >= 0.0f);
        last = a;
    }
    CHECK(unsure_marker_alpha(kUnsureHoldNs + kUnsureFadeNs) == 0.0f);
    CHECK(unsure_marker_alpha(kUnsureHoldNs + kUnsureFadeNs + sec(5)) == 0.0f);
}

TEST_CASE("the not-sure marker sits over the guess, else the last note heard, else mid-keyboard") {
    const KeyRange range = kRange61; // 36..96
    CHECK(unsure_marker_note(72, 50, range) == 72);
    CHECK(unsure_marker_note(-1, 50, range) == 50);
    CHECK(unsure_marker_note(-1, -1, range) == (36 + 96) / 2);
    CHECK(unsure_marker_note(21, -1, range) == 36); // never off the keyboard
    CHECK(unsure_marker_note(108, -1, range) == 96);
}

TEST_CASE("the input meter is 0 at -60 dBFS, 1 at -6 dBFS and linear in dB between") {
    CHECK(mic_meter_fraction(0.0f) == 0.0f);
    CHECK(mic_meter_fraction(-1.0f) == 0.0f);
    CHECK(mic_meter_fraction(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(mic_meter_fraction(1e-4f) == 0.0f); // -80 dBFS
    CHECK(mic_meter_fraction(1.0f) == 1.0f);
    CHECK(mic_meter_fraction(0.5012f) == doctest::Approx(1.0f).epsilon(0.01));  // -6 dBFS
    CHECK(mic_meter_fraction(0.02239f) == doctest::Approx(0.5f).epsilon(0.02)); // -33 dBFS
}

// ---- The bridge

TEST_CASE("mic bridge: a note-on becomes a key press at the onset, a note-off a release") {
    MicNoteBridge b;
    const auto on = apply(b, note_on_event(60, sec(1)));
    REQUIRE(on.size() == 1);
    CHECK(on[0].kind == MicOutKind::NoteOn);
    CHECK(on[0].note == 60);
    CHECK(on[0].time_ns == sec(1)); // the onset, not when the pitch was confirmed
    const auto off = apply(b, note_off_event(60, sec(2)));
    REQUIRE(off.size() == 1);
    CHECK(off[0].kind == MicOutKind::NoteOff);
    CHECK(off[0].time_ns == sec(2));
}

TEST_CASE("mic bridge: a release for a note this source did not press is dropped") {
    MicNoteBridge b;
    CHECK(apply(b, note_off_event(60, sec(2))).empty());
}

TEST_CASE("mic bridge: not sure never presses or releases a key, it only marks the register") {
    MicNoteBridge b;
    apply(b, note_on_event(64, sec(1)));
    const auto out = apply(b, not_sure_event(sec(2), 71));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::Unsure);
    CHECK(out[0].note == 71);
    CHECK(out[0].time_ns == sec(2));
    // The key that was down stays down.
    MicOut rel[MicNoteBridge::kMaxOut];
    CHECK(b.release_due(sec(10), rel, MicNoteBridge::kMaxOut) == 0);
}

TEST_CASE(
    "mic bridge: expected-note verdict Expected presses the expected note and releases it later") {
    MicNoteBridge b;
    const auto out = apply(b, verdict_event(ExpectedVerdict::Expected, 60, {60}, sec(1)));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::NoteOn);
    CHECK(out[0].note == 60);
    MicOut rel[MicNoteBridge::kMaxOut];
    CHECK(b.release_due(sec(1) + kMicExpectedHoldNs - 1, rel, MicNoteBridge::kMaxOut) == 0);
    REQUIRE(b.release_due(sec(1) + kMicExpectedHoldNs, rel, MicNoteBridge::kMaxOut) == 1);
    CHECK(rel[0].kind == MicOutKind::NoteOff);
    CHECK(rel[0].note == 60);
    CHECK(b.release_due(sec(9), rel, MicNoteBridge::kMaxOut) == 0);
}

TEST_CASE("mic bridge: a neighbour or octave verdict presses the key that was heard") {
    MicNoteBridge b;
    const auto below = apply(b, verdict_event(ExpectedVerdict::NeighborBelow, 59, {60}, sec(1)));
    REQUIRE(below.size() == 1);
    CHECK(below[0].note == 59);
    const auto octave = apply(b, verdict_event(ExpectedVerdict::OctaveAbove, 72, {60}, sec(2)));
    REQUIRE(octave.size() == 1);
    CHECK(octave[0].note == 72);
}

TEST_CASE("mic bridge: an unsure verdict only marks") {
    MicNoteBridge b;
    const auto out = apply(b, verdict_event(ExpectedVerdict::NotSure, -1, {60}, sec(1)));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::Unsure);
}

TEST_CASE("mic bridge: a chord presses the notes found present and marks what it could not judge") {
    MicNoteBridge b;
    MicEventIn in = verdict_event(ExpectedVerdict::Expected, -1, {60, 64, 67}, sec(1));
    in.event.chord_present = 0b111;
    auto out = apply(b, in);
    REQUIRE(out.size() == 3);
    for (const MicOut& o : out) {
        CHECK(o.kind == MicOutKind::NoteOn);
    }

    MicNoteBridge c;
    MicEventIn part = verdict_event(ExpectedVerdict::NotSure, -1, {60, 64, 67}, sec(1));
    part.event.chord_present = 0b001;
    part.event.chord_absent = 0b010; // 67 neither present nor absent
    out = apply(c, part);
    REQUIRE(out.size() == 2);
    CHECK(out[0].kind == MicOutKind::NoteOn);
    CHECK(out[0].note == 60);
    CHECK(out[1].kind == MicOutKind::Unsure);

    MicNoteBridge d;
    MicEventIn missing = verdict_event(ExpectedVerdict::NotSure, -1, {60, 64}, sec(1));
    missing.event.chord_present = 0b01;
    missing.event.chord_absent = 0b10; // clearly missing: nothing to mark, nothing pressed for it
    out = apply(d, missing);
    REQUIRE(out.size() == 1);
    CHECK(out[0].note == 60);
}

TEST_CASE("mic bridge: pressing a held key again releases it first") {
    MicNoteBridge b;
    apply(b, note_on_event(60, sec(1)));
    const auto out = apply(b, note_on_event(60, sec(2)));
    REQUIRE(out.size() == 2);
    CHECK(out[0].kind == MicOutKind::NoteOff);
    CHECK(out[1].kind == MicOutKind::NoteOn);
}

TEST_CASE("mic bridge: a note the keyboard just played is not played twice (Both mode)") {
    MicNoteBridge b;
    b.note_midi_press(60, sec(1));
    CHECK(apply(b, note_on_event(60, sec(1) + ms(60))).empty());
    CHECK(apply(b, note_off_event(60, sec(2))).empty()); // and neither is its release
    // The microphone hearing it just before the keyboard's stamp is the same note.
    b.note_midi_press(64, sec(5));
    CHECK(apply(b, note_on_event(64, sec(5) - ms(50))).empty());
    // A different note, or the same one much later, is the microphone's own.
    CHECK(apply(b, note_on_event(62, sec(1) + ms(60))).size() == 1);
    CHECK(apply(b, note_on_event(60, sec(1) + kMicMergeNs + ms(1))).size() == 1);
}

TEST_CASE("mic bridge: a new onset of a note the microphone holds, with no keyboard near it, is "
          "its own press") {
    // The keyboard played 60 long before; the microphone holds it and hears it struck again. The
    // old press ends and the new one starts, and the release comes out when the note ends, with
    // nothing stray after it.
    MicNoteBridge b;
    b.note_midi_press(60, sec(0));
    REQUIRE(apply(b, note_on_event(60, sec(1) + ms(900))).size() == 1);
    const auto again = apply(b, note_on_event(60, sec(3)));
    REQUIRE(again.size() == 2);
    CHECK(again[0].kind == MicOutKind::NoteOff);
    CHECK(again[1].kind == MicOutKind::NoteOn);
    const auto off = apply(b, note_off_event(60, sec(4)));
    REQUIRE(off.size() == 1);
    CHECK(off[0].kind == MicOutKind::NoteOff);
    CHECK(off[0].note == 60);
    CHECK(apply(b, note_off_event(60, sec(5))).empty()); // no second, stray note-off
    MicOut rel[MicNoteBridge::kMaxOut];
    CHECK(b.release_all(sec(6), rel, MicNoteBridge::kMaxOut) == 0);
}

TEST_CASE("mic bridge: the keyboard keeps a note the microphone was holding (Both mode)") {
    // The microphone holds 60 from an earlier strike; the player presses 60 on the keyboard and the
    // microphone hears the new strike about 60 ms later, as the detector reports it: the old note
    // ends, then the new one starts. None of it may reach the keys: a release would end the
    // keyboard's key and the new press would strike it a second time.
    MicNoteBridge b;
    REQUIRE(apply(b, note_on_event(60, sec(1))).size() == 1);
    b.note_midi_press(60, sec(2));
    CHECK(apply(b, note_off_event(60, sec(2) + ms(55))).empty()); // the old note ends
    CHECK(apply(b, note_on_event(60, sec(2) + ms(60))).empty());  // the same strike, heard
    CHECK(apply(b, note_off_event(60, sec(3))).empty());          // and its end
    CHECK(apply(b, note_off_event(60, sec(4))).empty());          // nothing stray later
    MicOut rel[MicNoteBridge::kMaxOut];
    CHECK(b.release_all(sec(5), rel, MicNoteBridge::kMaxOut) == 0);
    // Only the microphone's own strikes come out again afterwards.
    CHECK(apply(b, note_on_event(60, sec(10))).size() == 1);
}

TEST_CASE("mic bridge: a re-onset heard just before the keyboard's stamp is merged too") {
    MicNoteBridge b;
    REQUIRE(apply(b, note_on_event(60, sec(1))).size() == 1);
    b.note_midi_press(60, sec(2));
    CHECK(apply(b, note_on_event(60, sec(2) - ms(50))).empty());
}

TEST_CASE(
    "mic bridge: an expected-note press is not released over a keyboard press of the same note") {
    MicNoteBridge b;
    REQUIRE(apply(b, verdict_event(ExpectedVerdict::Expected, 60, {60}, sec(1))).size() == 1);
    b.note_midi_press(60, sec(1) + ms(100)); // the keyboard presses it inside the hold
    MicOut rel[MicNoteBridge::kMaxOut];
    CHECK(b.release_due(sec(2), rel, MicNoteBridge::kMaxOut) == 0);
}

TEST_CASE("mic bridge: a new detector starts the tuning gate over, whatever the setting was") {
    MicNoteBridge b;
    b.set_learning_tuning(true);
    for (int i = 0; i < kTuningSettledNotes; ++i) {
        MicEventIn in = note_on_event(60 + i, sec(1 + i));
        in.event.confidence = 0.95f;
        apply(b, in);
    }
    REQUIRE(b.tuning_settled());
    // The microphone is closed and opened again (MIDI then Microphone, or another device) with
    // learning still on: the setting did not change, but the detector is new.
    b.set_learning_tuning(true);
    CHECK(b.tuning_settled()); // the edge rule alone would leave it settled
    b.reset_tuning(true);
    CHECK_FALSE(b.tuning_settled());
    const auto out = apply(b, verdict_event(ExpectedVerdict::NeighborAbove, 61, {60}, sec(30)));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::Unsure);
    // With learning off the tuning is fixed, so it is settled however it was reset.
    b.reset_tuning(false);
    CHECK(b.tuning_settled());
}

TEST_CASE("mic bridge: neighbour and octave verdicts wait for the tuning to settle") {
    MicNoteBridge b;
    b.set_learning_tuning(true);
    CHECK_FALSE(b.tuning_settled());
    // Not settled: a "neighbour" could be a right note under a wrong A4, so it only marks.
    auto out = apply(b, verdict_event(ExpectedVerdict::NeighborBelow, 59, {60}, sec(1)));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::Unsure);
    CHECK(out[0].note == 59); // the register it was heard in
    out = apply(b, verdict_event(ExpectedVerdict::OctaveAbove, 72, {60}, sec(2)));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::Unsure);
    // The note the player was asked for is believed whatever the tuning.
    out = apply(b, verdict_event(ExpectedVerdict::Expected, 60, {60}, sec(3)));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::NoteOn);

    // Confident notes count only from open detection at kLearnMinConfidence and above.
    for (int i = 0; i < kTuningSettledNotes - 1; ++i) {
        MicEventIn in = note_on_event(60 + i, sec(10 + i));
        in.event.confidence = 0.95f;
        apply(b, in);
    }
    CHECK_FALSE(b.tuning_settled());
    MicEventIn shaky = note_on_event(70, sec(20));
    shaky.event.confidence = 0.85f;
    apply(b, shaky);
    CHECK_FALSE(b.tuning_settled());
    MicEventIn last = note_on_event(71, sec(21));
    last.event.confidence = 0.95f;
    apply(b, last);
    CHECK(b.tuning_settled());
    out = apply(b, verdict_event(ExpectedVerdict::NeighborBelow, 59, {60}, sec(30)));
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MicOutKind::NoteOn);
    CHECK(out[0].note == 59);

    // Switching learning off fixes the tuning: settled at once. Switching it on starts over.
    b.set_learning_tuning(false);
    CHECK(b.tuning_settled());
    b.set_learning_tuning(true);
    CHECK_FALSE(b.tuning_settled());
}

TEST_CASE("mic bridge: release_all lets go of every key the microphone holds") {
    MicNoteBridge b;
    apply(b, note_on_event(60, sec(1)));
    apply(b, note_on_event(64, sec(1)));
    MicOut out[MicNoteBridge::kMaxOut];
    CHECK(b.release_all(sec(2), out, MicNoteBridge::kMaxOut) == 2);
    CHECK(b.release_all(sec(3), out, MicNoteBridge::kMaxOut) == 0);
}

// ---- Grading: an unsure event neither hits nor misses

namespace {

Song one_note_song(double start, int pitch) {
    Song s;
    Note n;
    n.start_s = start;
    n.duration_s = 0.25;
    n.pitch = static_cast<std::uint8_t>(pitch);
    n.hand = Hand::Right;
    n.measure = 1;
    n.start_tick = static_cast<std::int64_t>(start * 960.0);
    n.end_tick = n.start_tick + 240;
    s.notes.push_back(n);
    s.duration_s = start + 3.0;
    Measure m;
    s.measures.push_back(m);
    return s;
}

MidiEvent mic_press(std::int64_t at_ns, int key) {
    MidiEvent e;
    e.time_ns = at_ns;
    e.type = MidiEventType::NoteOn;
    e.data1 = static_cast<std::uint8_t>(key);
    e.data2 = static_cast<std::uint8_t>(kMicVelocity);
    e.source = InputSource::Microphone;
    return e;
}

// Plays the song at wall time 1 s (so the note at song time 1 s arrives at 2 s) and runs the
// evaluator in 60 FPS frames to wall time `until_s`.
void run_flow(FlowEvaluator& eval, Transport& tr, double until_s, const std::vector<MidiEvent>& in,
              const std::vector<std::int64_t>& unsure) {
    std::vector<TransportOp> ops;
    tr.take_ops(ops);
    for (const TransportOp& op : ops) {
        eval.apply(op);
    }
    for (const MidiEvent& e : in) {
        eval.apply(e);
    }
    for (const std::int64_t u : unsure) {
        eval.apply_unsure(u);
    }
    for (std::int64_t t = sec(1); t <= sec(until_s); t += 16'666'667) {
        tr.update(t);
        tr.take_ops(ops);
        for (const TransportOp& op : ops) {
            eval.apply(op);
        }
        ops.clear();
        eval.update(t);
    }
}

} // namespace

TEST_CASE("flow: an unsure event is not a press, so it is never a wrong note") {
    const Song song = one_note_song(1.0, 60);
    Transport tr;
    tr.set_song(song);
    FlowEvaluator eval(song);
    tr.play(sec(1));
    run_flow(eval, tr, 4.0, {}, {sec(2) + ms(10), sec(3)});
    CHECK(eval.summary().wrong == 0);
    CHECK(eval.summary().combo == 0);
}

TEST_CASE(
    "flow: a note whose window holds an unsure event and no press is neither hit nor missed") {
    const Song song = one_note_song(1.0, 60);
    Transport tr;
    tr.set_song(song);
    FlowEvaluator eval(song);
    tr.play(sec(1));
    run_flow(eval, tr, 4.0, {}, {sec(2) + ms(20)});
    const EvaluationSummary& s = eval.summary();
    CHECK(s.counts.missed == 0);
    CHECK(s.counts.perfect + s.counts.good + s.counts.early + s.counts.late == 0);
    CHECK(s.unjudged == 1);
    CHECK(s.wrong == 0);
    CHECK(s.all.graded == 0);
}

TEST_CASE("flow: the same note with nothing heard at all is missed") {
    const Song song = one_note_song(1.0, 60);
    Transport tr;
    tr.set_song(song);
    FlowEvaluator eval(song);
    tr.play(sec(1));
    run_flow(eval, tr, 4.0, {}, {});
    CHECK(eval.summary().counts.missed == 1);
    CHECK(eval.summary().unjudged == 0);
}

TEST_CASE("flow: an unsure event far from the note does not excuse it") {
    const Song song = one_note_song(1.0, 60);
    Transport tr;
    tr.set_song(song);
    FlowEvaluator eval(song);
    tr.play(sec(1));
    run_flow(eval, tr, 4.0, {}, {sec(2) + ms(500)});
    CHECK(eval.summary().counts.missed == 1);
    CHECK(eval.summary().unjudged == 0);
}

TEST_CASE("flow: a confident press still hits a note an unsure event also covers") {
    const Song song = one_note_song(1.0, 60);
    Transport tr;
    tr.set_song(song);
    FlowEvaluator eval(song);
    tr.play(sec(1));
    run_flow(eval, tr, 4.0, {mic_press(sec(2) + ms(5), 60)}, {sec(2) + ms(5)});
    const EvaluationSummary& s = eval.summary();
    CHECK(s.counts.perfect == 1);
    CHECK(s.counts.missed == 0);
    CHECK(s.unjudged == 0);
}

TEST_CASE("wait: an unsure event leaves the song waiting at the chord") {
    const Song song = one_note_song(1.0, 60);
    Transport tr;
    tr.set_song(song);
    WaitEvaluator eval(song);
    tr.play(sec(1));
    // An unsure event sends the evaluators nothing, so the chord is never played.
    for (std::int64_t t = sec(1); t <= sec(4); t += 16'666'667) {
        tr.update(t);
        std::vector<TransportOp> ops;
        tr.take_ops(ops);
        for (const TransportOp& op : ops) {
            eval.apply(op);
        }
        const WaitDirective d = eval.update(t);
        if (d.hold_now) {
            tr.hold(t);
        }
        if (d.arm_hold) {
            tr.hold_at(t, d.hold_at_s);
        }
    }
    CHECK(tr.held());
    CHECK(eval.waiting_for_s() == doctest::Approx(1.0));
    CHECK(eval.summary().counts.played == 0);
    CHECK(eval.summary().wrong == 0);
}
