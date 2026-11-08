// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: starting, the sound modes, replay and loops, Wait Mode and Flow Mode, the settings menu and the kept session.
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

TEST_CASE("a loaded song starts paused at 0 in Flow Mode, muted, and plays when asked") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    CHECK(c.has_song());
    CHECK(c.state() == TransportState::Paused);
    CHECK(c.position_s() == 0.0);
    CHECK(c.mode() == PracticeMode::Flow);
    CHECK(c.song_sound() == SongSound::Mute);
    FakePiano piano;
    c.toggle_play(now);
    run(c, piano, now, now + 1 * kSec);
    CHECK(c.position_s() == doctest::Approx(1.0).epsilon(0.02));
    CHECK(piano.note_ons == 0); // Mute: nothing of the song sounds
    c.toggle_play(now);
    CHECK(c.state() == TransportState::Paused);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("song sound: Mute plays nothing, Other hand the hand not practised, Both hands all") {
    const auto ons_by_channel = [](SongSound sound, HandFilter hands) {
        SongController c;
        std::int64_t now = kSec;
        c.load(two_hand_song(), now);
        c.set_practice_hands(hands, now);
        c.set_song_sound(sound, now);
        FakePiano piano;
        std::array<int, 2> ons{};
        c.play(now);
        std::vector<SongSoundEvent> out;
        for (int i = 0; i < 120; ++i) {
            now += 16 * kMs;
            c.update(now, {});
            out.clear();
            c.take_sound_events(out);
            for (const auto& e : out) {
                if (e.kind == SongSoundEvent::Kind::Message &&
                    e.message.kind == PlaybackMessage::Kind::NoteOn && e.message.channel < 2) {
                    ++ons[e.message.channel];
                }
                piano.event(e);
            }
            piano.advance(now);
        }
        shutdown_into(c, piano, now);
        CHECK(piano.silent());
        return ons; // channel 0 is the right hand, 1 the left
    };
    CHECK(ons_by_channel(SongSound::Mute, HandFilter::Both) == std::array<int, 2>{0, 0});
    CHECK(ons_by_channel(SongSound::OtherHand, HandFilter::Both) == std::array<int, 2>{0, 0});
    const auto other_of_right = ons_by_channel(SongSound::OtherHand, HandFilter::Right);
    CHECK(other_of_right[0] == 0);
    CHECK(other_of_right[1] > 0);
    const auto other_of_left = ons_by_channel(SongSound::OtherHand, HandFilter::Left);
    CHECK(other_of_left[0] > 0);
    CHECK(other_of_left[1] == 0);
    const auto both = ons_by_channel(SongSound::BothHands, HandFilter::Right);
    CHECK(both[0] > 0);
    CHECK(both[1] > 0);
}

TEST_CASE(
    "skip back and forward step by their own seconds, stopping at 0, the loop start, the end") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(8), now); // bars of 2 s, 16 s long
    CHECK(c.skip_back_seconds() == 5);
    CHECK(c.skip_forward_seconds() == 5);
    c.skip_forward(now);
    CHECK(c.position_s() == doctest::Approx(5.0));
    CHECK(c.bar() == 3);
    SongOptions o;
    o.skip_back_seconds = 2;
    o.skip_forward_seconds = 7;
    c.set_options(o, now);
    CHECK(c.skip_back_seconds() == 2);
    CHECK(c.skip_forward_seconds() == 7);
    c.skip_forward(now);
    CHECK(c.position_s() == doctest::Approx(12.0));
    c.skip_back(now);
    CHECK(c.position_s() == doctest::Approx(10.0));
    c.skip_forward(now); // 17 is past the end: stops at it
    CHECK(c.position_s() == doctest::Approx(c.duration_s()));
    c.seek(1.0, now);
    c.skip_back(now); // stops at 0
    CHECK(c.position_s() == 0.0);
    // Inside a loop, back stops at the loop start.
    REQUIRE(c.set_loop(4.0, 10.0, now));
    c.seek(5.0, now);
    c.skip_back(now);
    CHECK(c.position_s() == doctest::Approx(4.0));
    c.skip_back(now);
    CHECK(c.position_s() == doctest::Approx(4.0));
    // Before the loop, back goes on to 0.
    c.seek(1.5, now);
    c.skip_back(now);
    CHECK(c.position_s() == 0.0);
    c.clear_loop(now);

    // Every jump while the song sounds leaves the piano silent.
    c.set_song_sound(SongSound::BothHands, now);
    FakePiano piano;
    c.seek(2.0, now);
    c.play(now);
    run(c, piano, now, now + 700 * kMs);
    REQUIRE(piano.count() > 0);
    c.skip_forward(now);
    drain(c, piano, now);
    CHECK(piano.count() == 0);
    run(c, piano, now, now + 700 * kMs);
    REQUIRE(piano.count() > 0);
    c.skip_back(now);
    drain(c, piano, now);
    CHECK(piano.count() == 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("replay from the start or the loop start, loop at bars, and speed in steps of 5") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now); // bars of 2 s

    c.change_speed(5, now);
    CHECK(c.speed_percent() == 105);
    for (int i = 0; i < 40; ++i) {
        c.change_speed(-5, now);
    }
    CHECK(c.speed_percent() == 25);
    for (int i = 0; i < 40; ++i) {
        c.change_speed(5, now);
    }
    CHECK(c.speed_percent() == 200);

    c.seek(4.5, now);
    c.set_loop_start_here(now); // bar 3: 4..6 s
    CHECK(c.pending_loop_start() == doctest::Approx(4.0));
    CHECK_FALSE(c.loop_enabled());
    c.seek(5.2, now);
    c.set_loop_end_here(now);
    CHECK(c.loop_enabled());
    CHECK(c.loop_start_s() == doctest::Approx(4.0));
    CHECK(c.loop_end_s() == doctest::Approx(6.0));
    c.seek(5.9, now);
    c.replay(now);
    CHECK(c.position_s() == doctest::Approx(4.0));
    CHECK(c.playing());
    c.clear_loop(now);
    CHECK_FALSE(c.loop_enabled());
    c.replay(now);
    CHECK(c.position_s() == 0.0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("loop_bar loops one bar of the song and plays it from its start (the results card)") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now); // bars of 2 s, numbered 1 to 4
    REQUIRE(c.song().song.measures.size() == 4);

    CHECK(c.loop_bar(3, now)); // bar 3: 4..6 s
    CHECK(c.loop_enabled());
    CHECK(c.loop_start_s() == doctest::Approx(4.0));
    CHECK(c.loop_end_s() == doctest::Approx(6.0));
    CHECK(c.position_s() == doctest::Approx(4.0));
    CHECK(c.playing());

    CHECK(c.loop_bar(4, now)); // the last bar ends at the song's end
    CHECK(c.loop_start_s() == doctest::Approx(6.0));
    CHECK(c.loop_end_s() == doctest::Approx(c.duration_s())); // a song ends with its last note

    // A bar the song does not have changes nothing.
    CHECK_FALSE(c.loop_bar(9, now));
    CHECK_FALSE(c.loop_bar(-1, now));
    CHECK(c.loop_start_s() == doctest::Approx(6.0));
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("Wait Mode holds at a chord, shows its keys, and moves on when they are played") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_mode(PracticeMode::Wait, now);
    CHECK(c.mode() == PracticeMode::Wait);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 500 * kMs);
    REQUIRE(c.waiting());
    CHECK(c.position_s() == doctest::Approx(0.0).epsilon(0.001));
    auto keys = c.waiting_keys();
    REQUIRE(keys.size() == 2);
    CHECK(keys[0].key == 48);
    CHECK(keys[0].hand == Hand::Left);
    CHECK(keys[1].key == 72);
    CHECK(keys[1].hand == Hand::Right);
    // Both keys down: the song goes on and waits at the next note, D5 at 0.5 s.
    const std::array<MidiEvent, 2> chord{note_on(48, now), note_on(72, now)};
    run(c, piano, now, now + 300 * kMs, chord);
    CHECK(c.position_s() > 0.0);
    run(c, piano, now, now + 800 * kMs);
    REQUIRE(c.waiting());
    CHECK(c.position_s() == doctest::Approx(0.5).epsilon(0.001));
    keys = c.waiting_keys();
    REQUIRE(keys.size() == 1);
    CHECK(keys[0].key == 74);
    // Practising the left hand: only its notes stop the song.
    c.set_practice_hands(HandFilter::Left, now);
    run(c, piano, now, now + 200 * kMs);
    const std::array<MidiEvent, 2> up{note_off(48, now), note_off(72, now)};
    run(c, piano, now, now + 2 * kSec, up);
    REQUIRE(c.waiting());
    CHECK(c.position_s() == doctest::Approx(2.0).epsilon(0.001));
    keys = c.waiting_keys();
    REQUIRE(keys.size() == 1);
    CHECK(keys[0].key == 48);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("Flow Mode grades notes as they pass, counts the combo, and a finished song is a run") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    const std::int64_t start = now;
    // Press each right-hand note on time except the third (E5 at 1.0 s).
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int i = 0; i < 8; ++i) {
        if (i == 2) {
            continue;
        }
        const std::int64_t t = start + i * 500 * kMs;
        const int key = 72 + (i % 4) * 2;
        script.push_back({t, note_on(key, t)});
        script.push_back({t + 200 * kMs, note_off(key, t + 200 * kMs)});
    }
    std::size_t next = 0;
    std::vector<MidiEvent> frame;
    std::vector<SongSoundEvent> out;
    int max_grades_seen = 0;
    while (c.state() != TransportState::Ended && now < start + 10 * kSec) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        max_grades_seen = std::max(max_grades_seen, static_cast<int>(c.recent_grades().size()));
        out.clear();
        c.take_sound_events(out);
    }
    CHECK(c.state() == TransportState::Ended);
    CHECK(max_grades_seen > 0);
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    const RunRecord& r = runs[0].record;
    CHECK(r.mode == RunMode::Flow);
    CHECK(r.hands == HandFilter::Right);
    CHECK(r.song_hash == 0x1234);
    CHECK(r.right.graded == 8);
    CHECK(r.right.hit == 7);
    CHECK(r.counts.missed == 1);
    CHECK(r.longest_combo == 5);
    CHECK(r.left.graded == 0);
    CHECK(r.duration_ms > 3500);
    // The results line names the hand and the combo.
    const std::string line = results_line(r, compare_with_last_attempt({}, r));
    CHECK(line.find("right 88%") != std::string::npos);
    CHECK(line.find("longest combo 5") != std::string::npos);
    c.shutdown(now, out);
}

// Plays the right hand of the two-hand song in `mode`, pressing every note but the third on time,
// and returns the most graded presses the game-feel state held (a finished run clears it).
static int most_timing_presses(PracticeMode mode) {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.set_mode(mode, now);
    c.play(now);
    const std::int64_t start = now;
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int i = 0; i < 8; ++i) {
        if (i == 2) {
            continue;
        }
        const std::int64_t t = start + i * 500 * kMs;
        const int key = 72 + (i % 4) * 2;
        script.push_back({t, note_on(key, t)});
        script.push_back({t + 200 * kMs, note_off(key, t + 200 * kMs)});
    }
    std::size_t next = 0;
    std::vector<MidiEvent> frame;
    std::vector<SongSoundEvent> out;
    int most = 0;
    while (c.state() != TransportState::Ended && now < start + 14 * kSec) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        most = std::max(most, c.game_feel().presses());
        out.clear();
        c.take_sound_events(out);
    }
    c.shutdown(now, out);
    return most;
}

TEST_CASE("game feel counts Flow Mode's graded presses, and Wait Mode's none") {
    // Seven of the eight right-hand notes are pressed; the skipped one is a Miss, not a sample.
    CHECK(most_timing_presses(PracticeMode::Flow) == 7);
    CHECK(most_timing_presses(PracticeMode::Wait) == 0);
}

TEST_CASE("opening the settings pauses and silences; closing counts down with ticks, then plays") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_song_sound(SongSound::BothHands, now);
    SongOptions o;
    o.resume_countdown_s = 3;
    c.set_options(o, now);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 600 * kMs);
    CHECK(piano.count() > 0);
    c.menu_opened(now);
    drain(c, piano, now);
    CHECK(c.state() == TransportState::Paused);
    CHECK(piano.silent());
    const double paused_at = c.position_s();
    // A seek while the menu is open still resumes through the countdown.
    c.seek(2.0, now);
    run(c, piano, now, now + 1 * kSec);
    c.menu_closed(now);
    CHECK(c.counting_down());
    CHECK(c.countdown() == 3);
    int ticks = 0;
    std::vector<SongSoundEvent> out;
    const std::int64_t closed = now;
    while (now < closed + 2900 * kMs) {
        now += 16 * kMs;
        c.update(now, {});
        out.clear();
        c.take_sound_events(out);
        for (const auto& e : out) {
            if (e.kind == SongSoundEvent::Kind::Message &&
                e.message.kind == PlaybackMessage::Kind::NoteOn && e.message.click) {
                ++ticks;
            }
            piano.event(e);
        }
        piano.advance(now);
        CHECK_FALSE(c.playing());
    }
    CHECK(ticks == 3);
    CHECK(c.countdown() == 1);
    CHECK(c.position_s() == doctest::Approx(2.0)); // the seek, not where it paused
    CHECK(paused_at < 2.0);
    run(c, piano, now, now + 300 * kMs);
    CHECK(c.playing());
    CHECK_FALSE(c.counting_down());

    // Closing a menu opened while paused does not start anything.
    c.pause(now);
    c.menu_opened(now);
    c.menu_closed(now);
    CHECK_FALSE(c.counting_down());
    out.clear();
    c.shutdown(now, out);
}

TEST_CASE("a song opened while a menu is up waits for Play when the menu closes") {
    // The library opened over a playing song: the song picked in it must not start by itself.
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 300 * kMs);
    REQUIRE(c.state() == TransportState::Playing);
    c.menu_opened(now);
    c.load(two_hand_song(2), now);
    c.menu_closed(now);
    CHECK_FALSE(c.counting_down());
    run(c, piano, now, now + 2 * kSec);
    CHECK(c.state() != TransportState::Playing);
    CHECK(c.position_s() == doctest::Approx(0.0));
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("the session keeps the song's place and choices, and a restored one comes back") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_mode(PracticeMode::Wait, now);
    c.set_practice_hands(HandFilter::Left, now);
    c.set_song_sound(SongSound::OtherHand, now);
    c.set_speed(70, now);
    c.set_transpose(-2, now);
    c.seek(2.5, now);
    c.set_loop_start_here(now);
    c.set_loop_end_here(now);
    c.seek(3.0, now);
    const SessionState s = c.session_state(SessionState{});
    CHECK(s.song_path == "test.mid");
    CHECK(s.song_hash == 0x1234);
    CHECK(s.position_s == doctest::Approx(3.0));
    CHECK(s.mode == PracticeMode::Wait);
    CHECK(s.practice_hands == HandFilter::Left);
    CHECK(s.piano_plays == PianoPlays::OtherHand);
    CHECK(s.speed_percent == 70);
    CHECK(s.transpose_semitones == -2);
    CHECK(s.loop_enabled);
    CHECK(s.loop_start_s == doctest::Approx(2.0));
    CHECK(s.loop_end_s == doctest::Approx(4.0));
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);

    SongController d;
    d.load(two_hand_song(4), now);
    d.apply_session(s, now);
    CHECK(d.session_state(SessionState{}) == s);
    CHECK(d.state() == TransportState::Paused);
    d.shutdown(now, out);

    // A song from outside the library is played but not saved.
    LoadedSong outside = two_hand_song();
    outside.library_path.clear();
    SongController e;
    e.load(std::move(outside), now);
    CHECK(e.session_state(s).song_path.empty());
    CHECK(e.session_state(s).song_abs_path.empty());
    e.shutdown(now, out);

    // ...unless it knows where it was opened from: that absolute path is saved, and a song in the
    // library saves both.
    LoadedSong opened = two_hand_song();
    opened.library_path.clear();
    opened.absolute_path = "/elsewhere/x.mid";
    SongController f;
    f.load(std::move(opened), now);
    const SessionState fs = f.session_state(SessionState{});
    CHECK(fs.song_path.empty());
    CHECK(fs.song_abs_path == "/elsewhere/x.mid");
    CHECK(fs.has_song());
    f.shutdown(now, out);

    LoadedSong both = two_hand_song();
    both.absolute_path = "/lib/test.mid";
    SongController g;
    g.load(std::move(both), now);
    CHECK(g.session_state(SessionState{}).song_path == "test.mid");
    CHECK(g.session_state(SessionState{}).song_abs_path == "/lib/test.mid");
    g.shutdown(now, out);
}
