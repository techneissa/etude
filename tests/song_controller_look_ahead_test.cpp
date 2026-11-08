// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: the look-ahead, and pauses, seeks and menus that cancel notes not yet started.
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

TEST_CASE(
    "song notes are handed over before they are due, each due when its note reaches the hit line") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_song_sound(SongSound::BothHands, now);
    c.play(now);
    const std::int64_t played = now;
    std::vector<SongSoundEvent> out;
    int note_ons = 0;
    int handed_at_play = 0;
    // The notes of the first look-ahead are handed over by the play itself.
    c.take_sound_events(out);
    for (const SongSoundEvent& e : out) {
        handed_at_play += starts_note(e.message) ? 1 : 0;
    }
    CHECK(handed_at_play == 2); // the notes at 0.000 s of both hands, due now
    out.clear();
    while (now < played + 3 * kSec) {
        now += 16 * kMs;
        c.update(now, {});
        c.take_sound_events(out);
        for (const SongSoundEvent& e : out) {
            if (e.kind != SongSoundEvent::Kind::Message || !starts_note(e.message)) {
                continue;
            }
            ++note_ons;
            // Due when the clock reaches the note: its song time after the play, to rounding...
            CHECK(std::llabs(e.message.at_ns - (played + std::llround(e.message.song_s * 1e9))) <=
                  3);
            // ...and in hand before then: this frame is shorter than the look-ahead.
            CHECK(e.message.at_ns >= now);
            CHECK(e.message.at_ns - now <= kSongSoundLeadNs);
        }
        out.clear();
    }
    CHECK(note_ons >= 6);
    c.shutdown(now, out);
}

TEST_CASE("the look-ahead is set in the options; 0 hands each note over when the frame that passes "
          "it runs") {
    const auto first = [](std::int64_t lead_ns) {
        SongController c;
        std::int64_t now = kSec;
        SongOptions o;
        o.sound_lead_ns = lead_ns;
        c.set_options(o, now);
        c.load(two_hand_song(), now);
        c.set_song_sound(SongSound::BothHands, now);
        c.play(now);
        const std::int64_t played = now;
        // {due, when it was handed over}, both after the play.
        std::pair<std::int64_t, std::int64_t> notes[2] = {{-1, -1}, {-1, -1}};
        int n = 0;
        std::vector<SongSoundEvent> out;
        for (int frame = 0; frame < 40 && n < 2; ++frame) {
            c.take_sound_events(out);
            for (const SongSoundEvent& e : out) {
                if (e.kind == SongSoundEvent::Kind::Message && starts_note(e.message) &&
                    e.message.song_s > 0.4 && n < 2) {
                    notes[n++] = {e.message.at_ns - played, now - played};
                }
            }
            out.clear();
            now += 16 * kMs;
            c.update(now, {});
        }
        return notes[0]; // the note at 0.5 s
    };
    const auto ahead = first(kSongSoundLeadNs);
    CHECK(ahead.first == 500 * kMs);
    CHECK(ahead.second <= ahead.first);
    CHECK(ahead.first - ahead.second <= kSongSoundLeadNs);
    const auto wide = first(50 * kMs);
    CHECK(wide.first == 500 * kMs);
    CHECK(wide.first - wide.second <= 50 * kMs);
    CHECK(wide.first - wide.second > kSongSoundLeadNs - 16 * kMs); // handed over earlier
    const auto none = first(0);
    CHECK(none.first == 500 * kMs);
    CHECK(none.second >= none.first); // handed over by the frame that passed it, late
    CHECK(none.second - none.first <= 16 * kMs);
}

// Issue #27: a pause in the frame where a note starts used to send its note-on and, at once, its
// note-off. A note that has not started when the pause comes never starts; one that did start
// ends with the pause.
TEST_CASE("a pause up to the start of a note sends no note-on for it") {
    // The right hand's second note is D5 (key 74), at 0.5 s.
    for (const std::int64_t before_start_ms : {20, 10, 3, 1, 0}) {
        INFO("pause " << before_start_ms << " ms before the note's start");
        SongController c;
        std::int64_t now = kSec;
        c.load(two_hand_song(), now);
        c.set_song_sound(SongSound::BothHands, now);
        FakePiano piano;
        c.play(now);
        const std::int64_t played = now;
        run(c, piano, now, played + 470 * kMs); // the note is handed over, due in 30 ms
        REQUIRE(piano.ever_started.test(72));   // the first note sounded
        REQUIRE_FALSE(piano.ever_started.test(74));
        REQUIRE_FALSE(piano.queue.empty());
        now = played + 500 * kMs - before_start_ms * kMs;
        c.pause(now);
        feed(c, piano, now);
        feed(c, piano, now + 1 * kSec);
        CHECK_FALSE(piano.ever_started.test(74));
        CHECK(piano.silent());
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
}

TEST_CASE("a pause after a note started ends it; with no look-ahead the same pause sends the "
          "note-on and note-off together") {
    for (const bool ahead : {true, false}) {
        INFO("look-ahead " << ahead);
        SongController c;
        std::int64_t now = kSec;
        SongOptions o;
        o.sound_lead_ns = ahead ? kSongSoundLeadNs : 0;
        c.set_options(o, now);
        c.load(two_hand_song(), now);
        c.set_song_sound(SongSound::BothHands, now);
        FakePiano piano;
        c.play(now);
        const std::int64_t played = now;
        run(c, piano, now, played + 480 * kMs);
        REQUIRE_FALSE(piano.ever_started.test(74));
        now = played + 503 * kMs;
        c.pause(now);
        drain(c, piano, now);
        drain(c, piano, now + 1 * kSec);
        // With the look-ahead the note started on time, 3 ms before the pause. Without it the
        // frame that passed the note is the pause itself: the note-on and the note-off together.
        CHECK(piano.ever_started.test(74));
        CHECK(piano.silent());
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
}

TEST_CASE("seek, skip, a new song and the settings menu cancel notes that had not started") {
    // After each action the piano must hear none of the note at 0.5 s (key 74), 5 ms from starting.
    const auto cancels = [](auto action, bool plays_on) {
        SongController c;
        std::int64_t now = kSec;
        c.load(two_hand_song(4), now);
        c.set_song_sound(SongSound::BothHands, now);
        FakePiano piano;
        c.play(now);
        const std::int64_t played = now;
        run(c, piano, now, played + 480 * kMs);
        now = played + 495 * kMs;
        c.update(now, {});
        action(c, now);
        feed(c, piano, now);
        feed(c, piano, now + 1 * kSec);
        CHECK_FALSE(piano.ever_started.test(74));
        if (!plays_on) {
            CHECK(piano.silent());
        }
        CHECK(piano.queue.empty());
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    };
    cancels([](SongController& c, std::int64_t now) { c.seek(3.0, now); }, true);
    cancels([](SongController& c, std::int64_t now) { c.skip_forward(now); }, true);
    cancels([](SongController& c, std::int64_t now) { c.skip_back(now); }, true);
    cancels([](SongController& c, std::int64_t now) { c.load(two_hand_song(), now); }, false);
    cancels([](SongController& c, std::int64_t now) { c.menu_opened(now); }, false);
    cancels([](SongController& c, std::int64_t now) { c.lost_focus(now); }, false);
    cancels([](SongController& c, std::int64_t now) { c.unload(now); }, false);
}

// The plan changes under the song while it plays: cancel what was handed over, go on from the
// position. No note is missed or sounds twice, and the notes that sound go on.
TEST_CASE(
    "a new speed, a mute or a transposition while playing cancels and goes on without a gap") {
    const auto notes_started = [](auto change, int speed_after) {
        SongController c;
        std::int64_t now = kSec;
        c.load(two_hand_song(6), now);
        c.set_song_sound(SongSound::BothHands, now);
        FakePiano piano;
        c.play(now);
        const std::int64_t played = now;
        run(c, piano, now, played + 1230 * kMs);
        change(c, now);
        CHECK(c.speed_percent() == speed_after);
        run(c, piano, now, now + 4 * kSec);
        drain(c, piano, now + kSongSoundLeadNs);
        std::vector<SongSoundEvent> out;
        shutdown_into(c, piano, now);
        return piano.note_ons;
    };
    // The whole song is 12 beats a bar pair: 6 bars of 4 right notes and 1 left note.
    const int baseline = notes_started([](SongController&, std::int64_t) {}, 100);
    // At 150% from 1.23 s the song is 4 s further on than at 100%.
    const int faster =
        notes_started([](SongController& c, std::int64_t now) { c.set_speed(150, now); }, 150);
    CHECK(faster > baseline);
    const int muted = notes_started(
        [](SongController& c, std::int64_t now) { c.set_song_sound(SongSound::OtherHand, now); },
        100);
    CHECK(muted < baseline); // the hands not practised... Both hands practised: nothing sounds
    const int moved =
        notes_started([](SongController& c, std::int64_t now) { c.set_transpose(2, now); }, 100);
    CHECK(moved == baseline); // the same notes, a tone up
}

// A pedal press that had not started is cancelled with the notes, or it would hold the pedal after
// the pause.
TEST_CASE("a pause just before the pedal goes down leaves the pedal up") {
    const auto pedal_after_pause_at = [](std::int64_t pause_ms) {
        SongController c;
        std::int64_t now = kSec;
        c.load(two_hand_song(2, true), now);
        c.set_song_sound(SongSound::BothHands, now);
        FakePiano piano;
        c.play(now);
        const std::int64_t played = now;
        run(c, piano, now, played + 470 * kMs);
        REQUIRE(piano.pedal.none());
        now = played + pause_ms * kMs;
        c.pause(now);
        feed(c, piano, now);
        feed(c, piano, now + 1 * kSec);
        const bool down = piano.pedal.any();
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
        return down;
    };
    CHECK_FALSE(pedal_after_pause_at(480)); // the press at 0.5 s is 20 ms away
    CHECK_FALSE(pedal_after_pause_at(499));
    CHECK_FALSE(pedal_after_pause_at(500)); // at its exact time
    // Left to play, the pedal does go down; a pause then lifts it.
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(2, true), now);
    c.set_song_sound(SongSound::BothHands, now);
    FakePiano piano;
    c.play(now);
    const std::int64_t played = now;
    run(c, piano, now, played + 700 * kMs);
    CHECK(piano.pedal.test(0));
    c.pause(now);
    feed(c, piano, now);
    CHECK(piano.silent());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE(
    "an armed hold stops the plan: nothing past the chord is handed over, and the chord waits") {
    // Wait Mode on a two-hand song: the clock holds at each chord; the sound must not run ahead.
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(2), now);
    c.set_song_sound(SongSound::OtherHand, now);
    c.set_mode(PracticeMode::Wait, now);
    c.set_practice_hands(HandFilter::Right, now);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 2 * kSec);
    // Held at the first chord that needs the player: the left hand's notes sound (the other
    // hand), and the clock does not move on, nor does the sound.
    const int before = piano.note_ons;
    run(c, piano, now, now + 1 * kSec);
    CHECK(piano.note_ons == before);
    std::vector<SongSoundEvent> out;
    shutdown_into(c, piano, now);
    CHECK(piano.silent());
}

// ---- Metronome, count-in and tempo --------------------------------------------------------------
