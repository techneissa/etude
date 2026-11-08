// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: the events of a run, and the changes that make a run not complete.
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

TEST_CASE("a Flow run records each press with its velocity, the note it matched and its error, "
          "each missed note and each wrong press") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    const std::int64_t start = now;
    // The eight right-hand notes at 0.5 s steps; the third (E5, 1.0 s) is not played, the fourth is
    // 30 ms late, and a C#4 that is in no bar is pressed at 1.2 s.
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int i = 0; i < 8; ++i) {
        if (i == 2) {
            continue;
        }
        const std::int64_t t = start + i * 500 * kMs + (i == 3 ? 30 * kMs : 0);
        const int key = 72 + (i % 4) * 2;
        script.push_back({t, press_with(key, 60 + i, t)});
        script.push_back({t + 200 * kMs, note_off(key, t + 200 * kMs)});
    }
    script.push_back({start + 1200 * kMs, press_with(61, 33, start + 1200 * kMs)});
    std::sort(script.begin(), script.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    FakePiano piano;
    std::size_t next = 0;
    std::vector<MidiEvent> frame;
    while (c.state() != TransportState::Ended && now < start + 10 * kSec) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        feed(c, piano, now);
    }
    REQUIRE(c.state() == TransportState::Ended);
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    RunEvents ev = runs[0].events;
    CHECK(ev.unordered); // the frame thread does not sort: the history thread does
    finish_ordering(ev);
    CHECK(ev.complete);
    CHECK(ev.expected_notes == 8);
    CHECK(ev.dropped == 0);

    int presses = 0;
    int missed = 0;
    int wrong = 0;
    std::int32_t last_ms = -1;
    for (const RunEvent& e : ev.events) {
        CHECK(e.song_ms >= last_ms); // song-time order
        last_ms = e.song_ms;
        switch (e.kind) {
        case RunEventKind::Press:
            ++presses;
            CHECK(e.hand == Hand::Right);
            CHECK(e.grade != NoteGrade::Missed);
            CHECK(e.velocity >= 60);
            CHECK(e.velocity <= 67);
            // The press is at its note's start (a multiple of 500 ms), give or take the 30 ms we
            // made one late.
            CHECK(std::abs(e.song_ms - (e.song_ms + 250) / 500 * 500) <= 40);
            if (e.velocity == 63) { // the fourth note: late by 30 ms
                CHECK(e.error_ms >= 20);
                CHECK(e.error_ms <= 40);
            } else {
                CHECK(std::abs(e.error_ms) <= 20);
            }
            break;
        case RunEventKind::Missed:
            ++missed;
            CHECK(e.key == 76);
            CHECK(e.velocity == 0);
            CHECK(e.song_ms == 1000);
            CHECK(e.hand == Hand::Right);
            break;
        case RunEventKind::Wrong:
            ++wrong;
            CHECK(e.key == 61);
            CHECK(e.velocity == 33);
            CHECK(e.note_index == -1);
            CHECK(std::abs(e.song_ms - 1200) <= 40);
            break;
        }
    }
    CHECK(presses == 7);
    CHECK(missed == 1);
    CHECK(wrong == runs[0].record.wrong);
    CHECK(wrong == 1);
    // The record and the events agree on what was played.
    CHECK(presses + missed == runs[0].record.right.graded);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a run stopped half way keeps what was played and is not a complete pass") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    FakePiano piano;
    const std::int64_t start = now;
    // Four notes on time, then the hands change: the run ends there.
    std::size_t played = 0;
    while (now < start + 2200 * kMs) {
        now += 16 * kMs;
        std::vector<MidiEvent> frame;
        while (played < 4 && start + static_cast<std::int64_t>(played) * 500 * kMs <= now) {
            const std::int64_t t = start + static_cast<std::int64_t>(played) * 500 * kMs;
            frame.push_back(press_with(72 + static_cast<int>(played % 4) * 2, 80, t));
            ++played;
        }
        c.update(now, frame);
        feed(c, piano, now);
    }
    c.set_practice_hands(HandFilter::Left, now);
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].events.expected_notes == 8);
    RunEvents ordered = runs[0].events;
    finish_ordering(ordered);
    CHECK_FALSE(ordered.complete);
    CHECK(runs[0].events.events.size() >= 4);
    CHECK(runs[0].events.events.size() < 8);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a Wait run records the press that played each note, early by how much it was") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    FakePiano piano;
    int pressed = 0;
    for (int frames = 0; frames < 2000 && c.state() != TransportState::Ended; ++frames) {
        now += 16 * kMs;
        std::vector<MidiEvent> frame;
        if (c.waiting() && frames % 10 == 0) {
            for (const auto& k : c.waiting_keys()) {
                frame.push_back(press_with(k.key, 70 + pressed, now));
                ++pressed;
            }
        }
        c.update(now, frame);
        feed(c, piano, now);
    }
    REQUIRE(c.state() == TransportState::Ended);
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    RunEvents ev = runs[0].events;
    CHECK(ev.unordered); // the frame thread does not sort: the history thread does
    finish_ordering(ev);
    CHECK(ev.complete);
    CHECK(ev.expected_notes == 8);
    int presses = 0;
    for (const RunEvent& e : ev.events) {
        if (e.kind != RunEventKind::Press) {
            continue;
        }
        ++presses;
        CHECK(e.velocity >= 70);
        CHECK(e.hand == Hand::Right);
        CHECK(e.error_ms <= 0); // the song waited for the key: never late
    }
    CHECK(presses == 8);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a run that is not an attempt leaves nothing behind for the next one") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 6 * kSec);
    REQUIRE(c.take_finished_runs().empty());
    // Play it again, right this time: the misses of the first pass are not in this run's events.
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    const std::int64_t start = now;
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int i = 0; i < 8; ++i) {
        const std::int64_t t = start + i * 500 * kMs;
        script.push_back({t, press_with(72 + (i % 4) * 2, 90, t)});
    }
    std::size_t next = 0;
    std::vector<MidiEvent> frame;
    while (c.state() != TransportState::Ended && now < start + 10 * kSec) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        feed(c, piano, now);
    }
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    RunEvents ordered = runs[0].events;
    finish_ordering(ordered);
    CHECK(ordered.events.size() == 8);
    CHECK(ordered.complete);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

// ---- A run that changes under the player is not a complete pass

namespace {

// Plays the right hand's eight notes on time, runs `change` at 1.2 s, and returns the first run
// finished. Without a change this is a complete run (the Flow test above).
RunEvents run_with_change(const std::function<void(SongController&, std::int64_t)>& change) {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    const std::int64_t start = now;
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int i = 0; i < 8; ++i) {
        const std::int64_t t = start + i * 500 * kMs;
        script.push_back({t, press_with(72 + (i % 4) * 2, 90, t)});
    }
    FakePiano piano;
    std::size_t next = 0;
    bool changed = false;
    std::vector<MidiEvent> frame;
    while (c.state() != TransportState::Ended && now < start + 15 * kSec) {
        now += 16 * kMs;
        if (!changed && now >= start + 1200 * kMs) {
            change(c, now);
            changed = true;
        }
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        feed(c, piano, now);
    }
    auto runs = c.take_finished_runs();
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    REQUIRE_FALSE(runs.empty());
    RunEvents ordered = runs[0].events;
    finish_ordering(ordered); // as the history thread does before it writes them
    return ordered;
}

} // namespace

TEST_CASE("a run with nothing changed in it is complete (the control for the changes below)") {
    const RunEvents events = run_with_change([](SongController&, std::int64_t) {});
    CHECK(events.complete);
}

TEST_CASE("a speed change during a run makes it not complete, whatever the new speed") {
    CHECK_FALSE(run_with_change([](SongController& c, std::int64_t now) { c.set_speed(80, now); })
                    .complete);
    CHECK_FALSE(run_with_change([](SongController& c, std::int64_t now) { c.set_speed(120, now); })
                    .complete);
    // Slowed and put back is still two speeds in one run.
    CHECK_FALSE(run_with_change([](SongController& c, std::int64_t now) {
                    c.set_speed(80, now);
                    c.set_speed(100, now);
                }).complete);
}

TEST_CASE("a transposition change during a run makes it not complete") {
    CHECK_FALSE(
        run_with_change([](SongController& c, std::int64_t now) { c.set_transpose(2, now); })
            .complete);
    CHECK_FALSE(run_with_change([](SongController& c, std::int64_t now) {
                    c.set_transpose(2, now);
                    c.set_transpose(0, now);
                }).complete);
}

TEST_CASE("a look-ahead change during a run makes it not complete, and does not end it") {
    CHECK_FALSE(run_with_change([](SongController& c, std::int64_t now) {
                    SongOptions o = c.options();
                    o.lookahead_s = 2.0;
                    c.set_options(o, now);
                }).complete);
    // Moved and put back is still two reaches in one run, and a drag of it is one run.
    const RunEvents events = run_with_change([](SongController& c, std::int64_t now) {
        SongOptions o = c.options();
        const double before = o.lookahead_s;
        for (int frame = 0; frame < 20; ++frame) {
            o.lookahead_s = before + 0.1 * frame;
            c.set_options(o, now, true);
        }
        o.lookahead_s = before;
        c.set_options(o, now);
    });
    CHECK_FALSE(events.complete);
}

TEST_CASE("a hands or mode change ends the run, and what was played of it is not complete") {
    CHECK_FALSE(run_with_change([](SongController& c, std::int64_t now) {
                    c.set_practice_hands(HandFilter::Left, now);
                }).complete);
    CHECK_FALSE(run_with_change([](SongController& c, std::int64_t now) {
                    c.set_mode(PracticeMode::Wait, now);
                }).complete);
}

TEST_CASE("wrong presses past what the evaluator keeps in a frame are counted as dropped, in both "
          "modes, and the run is not complete") {
    for (const PracticeMode mode : {PracticeMode::Flow, PracticeMode::Wait}) {
        CAPTURE(static_cast<int>(mode));
        SongController c;
        std::int64_t now = kSec;
        c.load(two_hand_song(), now);
        c.set_practice_hands(HandFilter::Right, now);
        c.set_mode(mode, now);
        c.play(now);
        FakePiano piano;
        run(c, piano, now, now + 500 * kMs);
        if (mode == PracticeMode::Wait) {
            // The first chord is played, so the run has a note graded and is an attempt.
            REQUIRE(c.waiting());
            const std::array<MidiEvent, 1> first{press_with(c.waiting_keys()[0].key, 80, now + 16 * kMs)};
            run(c, piano, now, now + 16 * kMs, first);
            run(c, piano, now, now + 100 * kMs);
        }
        // Forty keys no note of the song uses, all in one frame.
        std::vector<MidiEvent> burst;
        for (int key = 20; key < 60; ++key) {
            burst.push_back(press_with(key, 50, now + 16 * kMs));
        }
        run(c, piano, now, now + 16 * kMs, burst);
        run(c, piano, now, now + 600 * kMs);
        c.set_practice_hands(HandFilter::Left, now); // ends the run
        const auto runs = c.take_finished_runs();
        REQUIRE(runs.size() == 1);
        CHECK(runs[0].record.wrong == 40);
        int wrong = 0;
        for (const RunEvent& e : runs[0].events.events) {
            wrong += e.kind == RunEventKind::Wrong ? 1 : 0;
        }
        CHECK(wrong == static_cast<int>(kMaxKeptWrongPresses));
        CHECK(runs[0].events.dropped == 40 - static_cast<int>(kMaxKeptWrongPresses));
        CHECK(wrong + runs[0].events.dropped == runs[0].record.wrong);
        CHECK_FALSE(runs[0].events.complete);
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
}

// ---------------------------------------------------------------------------------------------
// A press stamped before an action is judged under the state it was played under, also when the
// host hands it over in the same frame as the action (the frame's ops come first, then the drain).
