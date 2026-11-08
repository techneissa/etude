// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: slider drags that rebuild once, and microphone notes decided before a seek or a switch.
#include "mic_inbox.hpp"
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

namespace {

// A run in `mode` with one right-hand note played and judged, the song still playing.
struct OpenRun {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;

    explicit OpenRun(PracticeMode mode) {
        c.load(two_hand_song(), now);
        c.set_practice_hands(HandFilter::Right, now);
        c.set_mode(mode, now);
        c.play(now);
        run(c, piano, now, now + 16 * kMs);
        if (mode == PracticeMode::Wait) {
            REQUIRE(c.waiting());
            const std::array<MidiEvent, 1> first{
                press_with(c.waiting_keys()[0].key, 80, now + 16 * kMs)};
            run(c, piano, now, now + 16 * kMs, first);
        } else {
            const std::array<MidiEvent, 1> first{note_on(72, now + 16 * kMs)};
            run(c, piano, now, now + 16 * kMs, first);
        }
        run(c, piano, now, now + 300 * kMs);
        REQUIRE(c.summary() != nullptr);
        REQUIRE(c.summary()->all.graded > 0);
        REQUIRE(c.take_finished_runs().empty());
    }
    void finish() {
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
};

} // namespace

TEST_CASE("changing the look-ahead mid-run keeps the run and rebuilds nothing, in both modes") {
    for (const PracticeMode mode : {PracticeMode::Flow, PracticeMode::Wait}) {
        CAPTURE(static_cast<int>(mode));
        OpenRun r(mode);
        const int graded = r.c.summary()->all.graded;
        const int hit = r.c.summary()->all.hit;
        const int rebuilds = r.c.evaluator_rebuilds();
        SongOptions o = r.c.options();
        // Dragging "Notes ahead": a new value every frame, with the slider held and not.
        for (int frame = 0; frame < 30; ++frame) {
            o.lookahead_s = 2.0 + 0.1 * frame;
            r.c.set_options(o, r.now, frame < 29);
            run(r.c, r.piano, r.now, r.now + 16 * kMs);
        }
        CHECK(r.c.options().lookahead_s == doctest::Approx(4.9));
        CHECK(r.c.evaluator_rebuilds() == rebuilds);
        CHECK(r.c.take_finished_runs().empty());
        CHECK(r.c.summary()->all.graded >= graded);
        CHECK(r.c.summary()->all.hit >= hit);
        CHECK(r.c.playing());
        run(r.c, r.piano, r.now, r.now + 7 * kSec);
        r.c.set_practice_hands(HandFilter::Left, r.now); // ends the run
        const auto runs = r.c.take_finished_runs();
        REQUIRE(runs.size() == 1); // one run, with what was played before and after the changes
        CHECK(runs[0].record.all.hit >= hit);
        r.finish();
    }
}

TEST_CASE("a drag of the hit window, latency or input delay rebuilds once, when the slider lifts") {
    for (const PracticeMode mode : {PracticeMode::Flow, PracticeMode::Wait}) {
        CAPTURE(static_cast<int>(mode));
        OpenRun r(mode);
        const int rebuilds = r.c.evaluator_rebuilds();
        const double window = r.c.options().hit_window_ms;
        SongOptions o = r.c.options();
        for (int frame = 0; frame < 30; ++frame) {
            o.hit_window_ms = 80.0 + frame;
            o.latency_ms = 2.0 * frame;
            o.input_delay_ms = frame;
            r.c.set_options(o, r.now, true); // the slider is held
            run(r.c, r.piano, r.now, r.now + 16 * kMs);
            // The run goes on at the values in force.
            CHECK(r.c.evaluator_rebuilds() == rebuilds);
            CHECK(r.c.options().hit_window_ms == window);
            CHECK(r.c.take_finished_runs().empty());
        }
        r.c.set_options(o, r.now, false); // the finger lifts on 109 ms
        CHECK(r.c.evaluator_rebuilds() == rebuilds + 1);
        CHECK(r.c.options().hit_window_ms == 109.0);
        CHECK(r.c.options().latency_ms == 58.0);
        CHECK(r.c.options().input_delay_ms == 29);
        const auto runs = r.c.take_finished_runs();
        CHECK(runs.size() == 1); // the run before the change, once
        // The same values again, or a held slider that moved nothing, change nothing.
        r.c.set_options(o, r.now, false);
        r.c.set_options(o, r.now, true);
        CHECK(r.c.evaluator_rebuilds() == rebuilds + 1);
        r.finish();
    }
}

TEST_CASE("a hit-window preset tap applies at once, once") {
    OpenRun r(PracticeMode::Flow);
    const int rebuilds = r.c.evaluator_rebuilds();
    SongOptions o = r.c.options();
    o.hit_window_ms = 40.0;
    r.c.set_options(o, r.now); // not settling: a tap, not a drag
    CHECK(r.c.evaluator_rebuilds() == rebuilds + 1);
    CHECK(r.c.options().hit_window_ms == 40.0);
    CHECK(r.c.take_finished_runs().size() == 1);
    r.finish();
}

TEST_CASE("a drag that ends where it began rebuilds nothing") {
    OpenRun r(PracticeMode::Flow);
    const int rebuilds = r.c.evaluator_rebuilds();
    SongOptions o = r.c.options();
    const double window = o.hit_window_ms;
    for (int frame = 0; frame < 10; ++frame) {
        o.hit_window_ms = window + frame;
        r.c.set_options(o, r.now, true);
    }
    o.hit_window_ms = window;
    r.c.set_options(o, r.now, false);
    CHECK(r.c.evaluator_rebuilds() == rebuilds);
    CHECK(r.c.take_finished_runs().empty());
    r.finish();
}

// ---------------------------------------------------------------------------------------------
// The microphone's notes are pulled with the rest, so an action in the frame does not judge a note
// the analysis thread had decided before it against the state it changed.

namespace {

// A note-on the detector decided: stamped at its onset, decided `latency` later.
MicEventIn mic_onset(int note, std::int64_t onset, std::int64_t latency = 40 * kMs) {
    MicEventIn in;
    in.event.type = PitchEventType::NoteOn;
    in.event.note = note;
    in.onset_ns = onset;
    in.sample_ns = onset + latency;
    return in;
}

// A song with a chord at 1 s and another at 8 s, waiting at the first.
struct MicWait {
    SongController c;
    Host host{c};
    std::int64_t now = kSec;

    explicit MicWait(const std::vector<std::pair<double, int>>& notes) {
        c.load(notes_song(notes), now);
        c.set_mode(PracticeMode::Wait, now);
        c.play(now);
        host.frames(now, now + 2500 * kMs);
        REQUIRE(c.waiting());
    }
    // A few frames on, so everything queued has been judged.
    void settle() {
        now += 16 * kMs;
        host.finish_frame(now);
        host.frames(now, now + 200 * kMs);
    }
};

} // namespace

TEST_CASE("host: a microphone note decided before a seek is not marked wrong") {
    MicWait w({{1.0, 60}, {8.0, 64}});
    w.host.mic_queue.push_back(mic_onset(60, w.now - 46 * kMs)); // decided 6 ms ago
    w.c.seek(7.0, w.now); // a tap on the bar, before the frame's drain
    w.settle();
    const EvaluationSummary* s = w.c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 0);
    CHECK(s->counts.played == 1);
}

TEST_CASE("host: the old wiring (the microphone taken only at the frame's drain) judged it wrong") {
    MicWait w({{1.0, 60}, {8.0, 64}});
    w.host.mic_in_pull = false;
    w.host.mic_queue.push_back(mic_onset(60, w.now - 46 * kMs));
    w.c.seek(7.0, w.now);
    w.settle();
    const EvaluationSummary* s = w.c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 1); // judged against the chord after the seek
}

TEST_CASE("host: a microphone note decided before a song switch is not judged against the new song") {
    MicWait w({{1.0, 60}, {8.0, 62}});
    w.host.mic_queue.push_back(mic_onset(60, w.now - 46 * kMs));
    w.c.load(notes_song({{1.0, 64}, {8.0, 65}}, 0x78), w.now);
    w.settle();
    const EvaluationSummary* s = w.c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 0);
    CHECK(s->counts.played == 0);
    const std::vector<FinishedRun> runs = w.c.take_finished_runs();
    REQUIRE(runs.size() == 1);
}

TEST_CASE("host: a microphone note decided 40 ms before the pump that wraps the loop is lap one's") {
    SongController c;
    Host host(c);
    std::int64_t now = kSec;
    c.load(notes_song({{0.0, 60}, {1.0, 62}, {3.0, 65}}), now);
    c.set_mode(PracticeMode::Wait, now);
    REQUIRE(c.set_loop(0.0, 2.0, now));
    c.play(now);
    host.frames(now, now + 100 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(60, now));
    host.frames(now, now + 1500 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(62, now));
    std::vector<LoopPass> passes;
    while (c.position_s() <= 1.984 || c.waiting()) {
        host.frames(now, now + 16 * kMs);
        for (const LoopPass& p : c.take_loop_passes()) {
            passes.push_back(p);
        }
        REQUIRE(now < 20 * kSec);
    }
    REQUIRE(passes.empty()); // the next pump crosses the loop's end
    // A wrong note, decided and waiting in the microphone's queue; a speed change (a pump outside
    // the frame's drain) wraps the loop. The note's onset is 40 ms before that pump, and so
    // before the wrap, which the clock reached at or after `now`.
    now += 16 * kMs;
    host.mic_queue.push_back(mic_onset(61, now - 40 * kMs, 20 * kMs));
    host.mic_now = now;
    c.set_speed(95, now);
    REQUIRE(c.position_s() < 0.5); // wrapped
    host.frames(now, now + 200 * kMs);
    for (const LoopPass& p : c.take_loop_passes()) {
        passes.push_back(p);
    }
    REQUIRE(!passes.empty());
    CHECK(passes[0].wrong == 1);
}

TEST_CASE("host: Both mode, a keyboard press and the microphone's onset of the same strike are one press") {
    MicWait w({{1.0, 60}, {8.0, 64}});
    w.host.both = true;
    w.host.queue.push_back(midi_note_on(60, w.now - 30 * kMs));
    w.host.mic_queue.push_back(mic_onset(60, w.now - 20 * kMs));
    w.settle();
    CHECK(w.host.presses_of(60) == 1);
    const EvaluationSummary* s = w.c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 0);
    CHECK(s->counts.played == 1);
}

TEST_CASE("host: Both mode, taking the microphone above the MIDI drain presses the strike twice") {
    MicWait w({{1.0, 60}, {8.0, 64}});
    w.host.both = true;
    w.host.mic_before_midi = true; // the merge has not heard of the keyboard's press yet
    w.host.queue.push_back(midi_note_on(60, w.now - 30 * kMs));
    w.host.mic_queue.push_back(mic_onset(60, w.now - 20 * kMs));
    w.settle();
    CHECK(w.host.presses_of(60) == 2);
}

TEST_CASE("host: each \"not sure\" marker reaches the song once, and leaves its note ungraded") {
    SongController c;
    Host host(c);
    std::int64_t now = kSec;
    c.load(notes_song({{1.0, 60}}), now);
    c.set_mode(PracticeMode::Flow, now);
    c.play(now);
    const std::int64_t note_ns = now + kSec;
    host.frames(now, note_ns - 30 * kMs);
    MicEventIn unsure;
    unsure.event.type = PitchEventType::NotSure;
    unsure.onset_ns = note_ns;
    unsure.sample_ns = note_ns + 100 * kMs;
    host.mic_queue.push_back(unsure);
    c.set_speed(100, now); // an action in the frame: the pull takes it
    host.frames(now, note_ns + 450 * kMs); // and the drains after it find nothing more
    CHECK(host.unsure_given == std::vector<std::int64_t>{note_ns});
    const EvaluationSummary* s = c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->unjudged == 1);
    CHECK(s->counts.missed == 0);
}

// ---- Tables for another hand choice or key of a long song, built on a worker
