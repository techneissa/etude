// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: the metronome and the count-in.
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <cmath>
#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

TEST_CASE("metronome: clicks are handed over before they are due, on the beat grid") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now); // 120 BPM: a beat every 0.5 s, bars of 2 s
    c.set_options(click_options(true), now);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, t0 + 6 * kSec + 7 * kMs);
    REQUIRE(r.clicks.size() >= 12);
    for (std::size_t i = 0; i < 12; ++i) {
        CAPTURE(i);
        // Due on the beat to the nanosecond, handed over before then (or at it, for the first).
        CHECK(std::llabs(r.clicks[i].due_ns - (t0 + static_cast<std::int64_t>(i) * 500 * kMs)) <=
              2);
        CHECK(r.clicks[i].handed_ns <= r.clicks[i].due_ns);
        CHECK(r.clicks[i].due_ns - r.clicks[i].handed_ns <= kSongSoundLeadNs);
        // Beat one of a bar is the louder one.
        if (i % 4 == 0) {
            CHECK(r.clicks[i].velocity > r.clicks[1].velocity);
        } else {
            CHECK(r.clicks[i].velocity == r.clicks[1].velocity);
        }
    }
    // Every click note ends on its own: after the quit nothing is left sounding.
    shutdown_into(c, r.piano, now);
    CHECK(r.piano.silent());
}

TEST_CASE("metronome: off by default, and turning it off stops the clicks at once") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    ClickRun r;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, now + 2 * kSec);
    CHECK(r.clicks.empty());
    c.set_options(click_options(true), now);
    r.until(c, now, now + 2 * kSec);
    CHECK_FALSE(r.clicks.empty());
    c.set_options(click_options(false), now);
    r.piano.advance(now);
    r.clicks.clear();
    const std::int64_t off_at = now;
    r.until(c, now, now + 2 * kSec);
    // Clicks already queued past the switch were cancelled before they started.
    for (const HandedClick& h : r.clicks) {
        CAPTURE(h.due_ns - off_at);
        CHECK(h.due_ns < off_at); // none: nothing new is handed over
    }
    CHECK(r.clicks.empty());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("metronome: a speed change keeps the beats: none lost, none doubled, each on the grid") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(8), now); // beats every 0.5 s of song time
    c.set_options(click_options(true), now);
    ClickRun r;
    const std::int64_t t0 = now;
    // Where the song was, and when, at each change of speed: the wall time of a beat follows.
    struct Leg {
        std::int64_t wall_ns;
        double song_s;
        int percent;
    };
    std::vector<Leg> legs{{t0, 0.0, 100}};
    const auto change = [&](int percent) {
        c.set_speed(percent, now);
        legs.push_back({now, c.position_s(), percent});
        r.collect(c, now);
    };
    c.play(now);
    r.collect(c, now);
    r.until(c, now, now + 2 * kSec);
    change(200); // a beat every 250 ms of wall time
    r.until(c, now, now + 3 * kSec);
    change(50); // a beat every second
    r.until(c, now, now + 4 * kSec);
    change(150);
    r.until(c, now, now + 1 * kSec);
    const auto wall_of = [&](double beat_s) {
        std::size_t i = legs.size() - 1;
        while (i > 0 && legs[i].song_s > beat_s) {
            --i;
        }
        return legs[i].wall_ns +
               std::llround((beat_s - legs[i].song_s) / (legs[i].percent / 100.0) * 1e9);
    };
    REQUIRE(r.clicks.size() > 20);
    for (std::size_t i = 0; i < r.clicks.size(); ++i) {
        CAPTURE(i);
        // The i-th click is the i-th beat: none skipped, none twice, to the nanosecond.
        CHECK(std::llabs(r.clicks[i].due_ns - wall_of(0.5 * static_cast<double>(i))) <= 2);
    }
    // And the clicks reach as far as the song does, up to the look-ahead.
    CHECK(static_cast<double>(r.clicks.size()) * 0.5 >= c.position_s());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("metronome: across a loop wrap the grid starts again from the loop start") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_options(click_options(true), now);
    REQUIRE(c.set_loop(1.2, 2.7, now)); // 1.5 s: beats at 1.5, 2.0, 2.5
    c.seek(1.2, now);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, t0 + 3200 * kMs);
    REQUIRE(r.clicks.size() >= 6);
    // Each pass is 1.5 s of wall time: beats 0.3, 0.8, 1.3 s after the pass starts.
    for (std::size_t i = 0; i < 6; ++i) {
        CAPTURE(i);
        const std::int64_t pass = static_cast<std::int64_t>(i / 3) * 1500 * kMs;
        const std::int64_t within = (300 + static_cast<std::int64_t>(i % 3) * 500) * kMs;
        CHECK(std::llabs(r.clicks[i].due_ns - (t0 + pass + within)) <= 2);
    }
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("metronome: a pause cancels the clicks queued ahead") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_options(click_options(true), now);
    ClickRun r;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, now + 1 * kSec + 490 * kMs); // a beat is due at 1.5 s; it is queued already
    r.piano.advance(now);
    const int started = r.piano.note_ons;
    c.pause(now);
    drain(c, r.piano, now);
    r.piano.advance(now + 2 * kSec);
    CHECK(r.piano.note_ons == started); // the queued beat never started
    CHECK(r.piano.sounding[9].none());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: one bar of clicks, then the song starts exactly on the next beat") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(1);
    c.set_song_sound(SongSound::BothHands, now);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    CHECK(c.state() == TransportState::CountIn);
    CHECK(c.count_in_label() == 1);
    CHECK(c.count_in_bar_beats() == 4);
    CHECK(c.position_s() == 0.0);
    r.until(c, now, t0 + 2 * kSec + 100 * kMs);
    REQUIRE(r.clicks.size() >= 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(std::llabs(r.clicks[i].due_ns - (t0 + static_cast<std::int64_t>(i) * 500 * kMs)) <=
              2);
    }
    CHECK(r.clicks[0].velocity > r.clicks[1].velocity); // beat one is accented
    REQUIRE_FALSE(r.note_dues.empty());
    // The first note of the song is due when the count ends, and was handed over before.
    CHECK(std::llabs(r.note_dues[0] - (t0 + 2 * kSec)) <= 2);
    CHECK(c.state() == TransportState::Playing);
    // The count-in is not repeated at the next beat: no click once the song moves (metronome off).
    CHECK(r.clicks.size() == 4);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: two bars, the numbers count 1 2 3 4 1 2 3 4 and the song waits") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(2);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    std::vector<int> labels;
    while (now < t0 + 4 * kSec - 20 * kMs) {
        r.frame(c, now);
        const int label = c.count_in_label();
        if (labels.empty() || labels.back() != label) {
            labels.push_back(label);
        }
        CHECK(c.position_s() == 0.0);
        CHECK(c.state() == TransportState::CountIn);
    }
    CHECK(labels == std::vector<int>{1, 2, 3, 4, 1, 2, 3, 4});
    r.until(c, now, t0 + 4 * kSec + 100 * kMs);
    CHECK(c.state() == TransportState::Playing);
    CHECK(c.count_in_label() == 0);
    CHECK(r.clicks.size() == 8);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: the falling notes hold still until it ends, after play, a seek and a replay") {
    const auto same = [](const std::vector<VisibleNote>& a, const std::vector<VisibleNote>& b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].index != b[i].index || a[i].bottom != b[i].bottom || a[i].top != b[i].top) {
                return false;
            }
        }
        return true;
    };
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(1);
    ClickRun r;
    for (int start = 0; start < 3; ++start) {
        if (start == 1) {
            c.pause(now);
            c.seek(2.0, now); // a seek, then play
        }
        const std::int64_t t0 = now;
        if (start == 2) {
            c.replay(now); // Home, or a loop's restart
        } else {
            c.play(now);
        }
        r.collect(c, now);
        REQUIRE(c.count_in_label() > 0);
        const double held = c.position_s();
        CHECK(c.display_position_s() == held);
        std::vector<VisibleNote> first;
        c.visible_notes(4.0, first);
        CHECK_FALSE(first.empty());
        // Half way through the count and just before its end: the same notes in the same places.
        for (const std::int64_t at : {t0 + 1 * kSec, t0 + 2 * kSec - 20 * kMs}) {
            r.until(c, now, at);
            CAPTURE(start);
            REQUIRE(c.count_in_label() > 0);
            CHECK(c.display_position_s() == held);
            std::vector<VisibleNote> mid;
            c.visible_notes(4.0, mid);
            CHECK(same(first, mid));
        }
        // Once it has counted, they fall.
        r.until(c, now, t0 + 2 * kSec + 500 * kMs);
        CHECK(c.count_in_label() == 0);
        CHECK(c.display_position_s() > held);
    }
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}
TEST_CASE("count-in: every start counts in, the resume and the seek too, a loop wrap never") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(8), now);
    c.set_count_in_bars(1);
    ClickRun r;
    c.play(now); // from a stop
    r.collect(c, now);
    CHECK(c.state() == TransportState::CountIn);
    r.until(c, now, now + 3 * kSec);
    c.pause(now);
    CHECK(c.position_s() > 0.0);
    c.play(now); // a resume from the middle
    CHECK(c.state() == TransportState::CountIn);
    c.pause(now);
    c.seek(5.0, now);
    c.play(now); // a play after a seek
    CHECK(c.state() == TransportState::CountIn);
    c.pause(now);
    c.seek(0.01, now);
    c.play(now); // not at the start, still every start
    CHECK(c.state() == TransportState::CountIn);
    c.pause(now);
    // Off: never.
    c.set_count_in_bars(0);
    c.seek(0.0, now);
    c.play(now);
    CHECK(c.state() == TransportState::Playing);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: Home restarts with one, also while playing") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(8), now);
    c.set_count_in_bars(1);
    ClickRun r;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, now + 4 * kSec); // counted in, and playing now
    CHECK(c.state() == TransportState::Playing);
    CHECK(c.position_s() > 1.0);
    const std::size_t clicks_before = r.clicks.size();
    c.replay(now); // Home while the song plays
    r.collect(c, now);
    CHECK(c.state() == TransportState::CountIn);
    CHECK(c.position_s() == 0.0);
    CHECK(c.playing());
    const std::int64_t t0 = now;
    r.until(c, now, t0 + 2 * kSec + 100 * kMs);
    CHECK(c.state() == TransportState::Playing);
    CHECK(r.clicks.size() == clicks_before + 4); // the new count-in's four beats
    // A loop restart counts in too, from the loop start.
    REQUIRE(c.set_loop(4.0, 8.0, now));
    c.replay(now);
    CHECK(c.state() == TransportState::CountIn);
    CHECK(c.position_s() == doctest::Approx(4.0));
    {
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
}

TEST_CASE("count-in: a loop wrap does not count in") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(8), now);
    c.set_count_in_bars(1);
    ClickRun r;
    REQUIRE(c.set_loop(2.0, 4.0, now));
    c.seek(2.0, now);
    c.play(now);
    r.collect(c, now);
    CHECK(c.state() == TransportState::CountIn);
    r.until(c, now, now + 2 * kSec + 100 * kMs);
    CHECK(c.state() == TransportState::Playing);
    const std::size_t counted = r.clicks.size();
    CHECK(counted == 4);
    // Past the loop end it wraps, again and again: no count-in, no clicks (metronome off).
    int wraps = 0;
    double last = c.position_s();
    while (wraps < 3) {
        r.frame(c, now);
        CHECK(c.state() == TransportState::Playing);
        wraps += c.position_s() < last ? 1 : 0;
        last = c.position_s();
    }
    CHECK(r.clicks.size() == counted);
    {
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
}

TEST_CASE("count-in: in Wait Mode only when the song starts from its beginning") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(8), now);
    c.set_mode(PracticeMode::Wait, now);
    c.set_count_in_bars(1);
    c.play(now); // from the start
    CHECK(c.state() == TransportState::CountIn);
    c.pause(now);
    c.play(now); // the resume: the count-in began at the start; the position is still there
    CHECK(c.state() == TransportState::CountIn);
    c.pause(now);
    c.seek(5.0, now);
    c.play(now);
    CHECK(c.state() != TransportState::CountIn);
    c.pause(now);
    c.seek(0.0, now);
    c.play(now); // back at the start
    CHECK(c.state() == TransportState::CountIn);
    c.pause(now);
    c.seek(0.01, now);
    c.play(now); // a hair past the start is not the start
    CHECK(c.state() != TransportState::CountIn);
    c.pause(now);
    // Home from the middle goes to the start and counts in.
    c.seek(5.0, now);
    c.replay(now);
    CHECK(c.state() == TransportState::CountIn);
    c.pause(now);
    // Home with a loop goes to the loop start, which is not the song's start: no count-in.
    REQUIRE(c.set_loop(4.0, 8.0, now));
    c.replay(now);
    CHECK(c.state() != TransportState::CountIn);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: it is the one countdown after the settings close") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(8), now);
    c.set_count_in_bars(1);
    ClickRun r;
    c.seek(4.0, now);
    c.play(now);
    r.collect(c, now);
    r.until(c, now, now + 500 * kMs);
    c.menu_opened(now);
    CHECK(c.state() == TransportState::Paused);
    c.menu_closed(now);
    // The count-in's clicks and numbers, not the seconds: no silent countdown runs.
    CHECK(c.state() == TransportState::CountIn);
    CHECK_FALSE(c.counting_down());
    CHECK(c.countdown() == 0);
    CHECK(c.count_in_label() == 1);
    const std::int64_t t0 = now;
    const std::size_t before = r.clicks.size();
    r.until(c, now, t0 + 2 * kSec + 100 * kMs);
    CHECK(c.state() == TransportState::Playing);
    CHECK(r.clicks.size() == before + 4);

    // With the count-in off, the seconds countdown is what runs.
    c.pause(now);
    c.set_count_in_bars(0);
    c.play(now);
    c.menu_opened(now);
    c.menu_closed(now);
    CHECK(c.counting_down());
    CHECK(c.state() == TransportState::Paused);

    // In Wait Mode the seconds countdown too.
    c.pause(now);
    c.set_count_in_bars(1);
    c.set_mode(PracticeMode::Wait, now);
    c.seek(4.0, now);
    c.play(now);
    c.menu_opened(now);
    c.menu_closed(now);
    CHECK(c.counting_down());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: a pause or a seek ends it and cancels the clicks queued") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(2);
    ClickRun r;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, now + 700 * kMs);
    const int started = r.piano.note_ons;
    c.pause(now);
    drain(c, r.piano, now);
    r.piano.advance(now + 5 * kSec);
    CHECK(r.piano.note_ons == started); // the clicks queued ahead never started
    CHECK(c.state() == TransportState::Paused);
    CHECK(c.count_in_label() == 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: a speed change mid-count moves the rest of the clicks, none lost or doubled") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(1);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, t0 + 1 * kSec); // two beats counted
    c.set_speed(50, now);           // the other two now take a second each
    r.until(c, now, t0 + 4 * kSec);
    std::vector<std::int64_t> due;
    for (const HandedClick& h : r.clicks) {
        due.push_back(h.due_ns);
    }
    REQUIRE(due.size() == 4);
    CHECK(std::llabs(due[0] - t0) <= 2);
    CHECK(std::llabs(due[1] - (t0 + 500 * kMs)) <= 2);
    CHECK(std::llabs(due[2] - (t0 + 1000 * kMs)) <= 2); // already due when the speed changed
    // The speed changed at 1.008 s, 2.016 beats in: the last beat is 0.984 beats on, a second a
    // beat at half speed.
    CHECK(std::llabs(due[3] - (t0 + 1992 * kMs)) <= 2);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}
