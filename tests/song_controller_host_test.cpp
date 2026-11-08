// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: presses and events stamped before a seek, a transposition, a loop wrap or a song switch.
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

TEST_CASE("host: a press stamped before a seek in the same frame is not marked wrong") {
    SongController c;
    Host host(c);
    std::int64_t now = kSec;
    c.load(notes_song({{1.0, 60}, {8.0, 64}}), now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    host.frames(now, now + 2500 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(60, now - 6 * kMs));
    c.seek(7.0, now); // a tap on the bar in the frame the press was drained in
    host.finish_frame(now + 16 * kMs);
    now += 16 * kMs;
    host.frames(now, now + 200 * kMs);
    const EvaluationSummary* s = c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 0);
    CHECK(s->counts.played == 1);
}

TEST_CASE("host: a press stamped before a transposition in the same frame is not marked wrong") {
    SongController c;
    Host host(c);
    std::int64_t now = kSec;
    c.load(notes_song({{1.0, 60}, {1.0, 64}, {8.0, 67}}), now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    host.frames(now, now + 2500 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(60, now - 14 * kMs)); // both keys of the chord, before the tap
    host.queue.push_back(note_on(64, now - 6 * kMs));
    c.set_transpose(2, now);
    host.finish_frame(now + 16 * kMs);
    now += 16 * kMs;
    host.frames(now, now + 200 * kMs);
    const EvaluationSummary* s = c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 0);
    CHECK(s->counts.played == 2);
}

TEST_CASE("host: a press stamped before a loop wrap, handed over in the wrap's frame, is lap one's") {
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
    REQUIRE(passes.empty()); // the next frame crosses the loop's end
    // The frame in which the loop wraps: the press is handed to update() with the frame's events,
    // which apply it before the clock's ops, not after.
    const std::array<MidiEvent, 1> stray{note_on(61, now)}; // wrong, stamped before the wrap
    now += 16 * kMs;
    c.update(now, stray);
    REQUIRE(c.position_s() < 0.5); // wrapped in that frame
    host.frames(now, now + 200 * kMs);
    for (const LoopPass& p : c.take_loop_passes()) {
        passes.push_back(p);
    }
    REQUIRE(!passes.empty());
    CHECK(passes[0].wrong == 1);
    CHECK_FALSE(passes[0].clean);
}

TEST_CASE("host: a press stamped before a song switch is not judged against the new song") {
    SongController c;
    Host host(c);
    std::int64_t now = kSec;
    c.load(notes_song({{1.0, 60}, {8.0, 62}}), now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    host.frames(now, now + 2500 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(60, now - 6 * kMs)); // right for the song being left
    c.load(notes_song({{1.0, 64}, {8.0, 65}}, 0x78), now);
    host.finish_frame(now + 16 * kMs);
    now += 16 * kMs;
    host.frames(now, now + 200 * kMs);
    const EvaluationSummary* s = c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 0);
    CHECK(s->counts.played == 0);
    // The first run is over and kept, with the press in it.
    const std::vector<FinishedRun> runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].record.wrong == 0);
    CHECK(runs[0].record.all.hit >= 1);
}

TEST_CASE("host: without the pull, a press handed over after the seek is judged after it") {
    // What the pull is for: a host that gives no pull hands the press over at the frame's end,
    // after the seek, and the press (right for the place it was played at) is marked wrong.
    SongController c;
    Host host(c, /*pull_events=*/false);
    std::int64_t now = kSec;
    c.load(notes_song({{1.0, 60}, {8.0, 64}}), now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    host.frames(now, now + 2500 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(60, now - 6 * kMs));
    c.seek(7.0, now);
    host.finish_frame(now + 16 * kMs);
    now += 16 * kMs;
    host.frames(now, now + 200 * kMs);
    const EvaluationSummary* s = c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 1);
    CHECK(s->counts.played == 0);
}

TEST_CASE("host: without the pull, a press handed over after the song switch is in neither run") {
    SongController c;
    Host host(c, /*pull_events=*/false);
    std::int64_t now = kSec;
    c.load(notes_song({{1.0, 60}, {8.0, 62}}), now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    host.frames(now, now + 2500 * kMs);
    REQUIRE(c.waiting());
    host.queue.push_back(note_on(60, now - 6 * kMs));
    c.load(notes_song({{1.0, 64}, {8.0, 65}}, 0x78), now);
    host.finish_frame(now + 16 * kMs);
    now += 16 * kMs;
    host.frames(now, now + 200 * kMs);
    // The first run ended with no press in it (so it is no attempt and is not kept), and the new
    // song is not playing, so the late press counts for nothing.
    CHECK(c.take_finished_runs().empty());
    const EvaluationSummary* s = c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->counts.played == 0);
}

TEST_CASE("host: an event pulled during the clock's pump reaches the octave detector") {
    // The keyboard sends every note an octave high; the presses come only through the pull (update()
    // is given an empty list), as the ones that arrive after the host's drain do.
    SongController c;
    Host host(c);
    FakePiano piano;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(), now);
    c.play(now);
    int played = 0;
    for (int bar = 0; bar < 2 && !c.shift_offer().has_value(); ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const std::int64_t at = kSec + (bar * 4 + beat) * 500 * kMs;
            host.frames(now, at);
            host.queue.push_back(midi_note_on(72 + beat * 2 + 12, at));
            if (beat == 0) {
                host.queue.push_back(midi_note_on(48 + 12, at));
            }
            played += static_cast<int>(host.queue.size());
            now += 16 * kMs;
            c.update(now, {}); // the pump pulls the queue; the list given is the frame's own, empty
            host.frame.clear();
            feed(c, piano, now);
        }
    }
    REQUIRE(c.shift_offer().has_value());
    CHECK(played >= 8);
    CHECK(c.shift_offer()->source == OfferSource::Keys);
    CHECK(c.shift_offer()->target == 12);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("host: more events than the frame's list had room for, pulled during the pump, are all taken") {
    SongController c;
    Host host(c);
    std::int64_t now = kSec;
    c.load(notes_song({{1.0, 60}, {8.0, 64}}), now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    host.frames(now, now + 2500 * kMs);
    REQUIRE(c.waiting());
    host.frame.reserve(64);
    // Ten events are in the list update() is given; 200 more are pulled while it pumps, which
    // moves the list. Wrong keys, so each shows in the count.
    for (int i = 0; i < 10; ++i) {
        host.frame.push_back(note_on(61, now + i * kMs / 2));
    }
    for (int i = 0; i < 200; ++i) {
        host.queue.push_back(note_on(62, now + 5 * kMs + i * kMs / 100));
    }
    const std::span<const MidiEvent> given(host.frame.data(), host.frame.size());
    now += 16 * kMs;
    c.update(now, given);
    host.frame.clear();
    host.frames(now, now + 200 * kMs);
    const EvaluationSummary* s = c.summary();
    REQUIRE(s != nullptr);
    CHECK(s->wrong == 210);
}

// ---- Options that change while a slider is dragged (#261)
