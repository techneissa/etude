// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/session_restore.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <string>

using namespace etude;
using Outcome = RestoreOutcome;

namespace {

Song song_of(double duration_s) {
    Song s;
    s.duration_s = duration_s;
    return s;
}

SessionState saved_in(double position_s = 30.0) {
    SessionState s;
    s.song_path = "Songs/a.mid";
    s.song_hash = 0x1234;
    s.position_s = position_s;
    s.speed_percent = 70;
    s.practice_hands = HandFilter::Left;
    s.piano_plays = PianoPlays::Both;
    s.transpose_semitones = 3;
    s.section_id = 4;
    s.plan_item = 2;
    s.loop_start_s = 10.0;
    s.loop_end_s = 20.0;
    s.loop_enabled = true;
    s.auto_speed_enabled = true;
    s.auto_speed_start_percent = 50;
    s.count_in_bars = 2;
    s.mode = PracticeMode::Flow;
    s.keyboard = KeyboardChoice::Manual;
    s.keyboard_lowest = 28;
    s.keyboard_highest = 103;
    return s.clamped();
}

// The part of the state that belongs to the player and the device, not the song.
void check_preferences_kept(const SessionState& state, const SessionState& saved) {
    CHECK(state.mode == saved.mode);
    CHECK(state.auto_speed_enabled == saved.auto_speed_enabled);
    CHECK(state.auto_speed_start_percent == saved.auto_speed_start_percent);
    CHECK(state.auto_speed_step_percent == saved.auto_speed_step_percent);
    CHECK(state.auto_speed_max_percent == saved.auto_speed_max_percent);
    CHECK(state.keyboard == saved.keyboard);
    CHECK(state.keyboard_lowest == saved.keyboard_lowest);
    CHECK(state.keyboard_highest == saved.keyboard_highest);
}

void check_song_state_reset(const SessionState& state) {
    CHECK(state.position_s == 0.0);
    CHECK(state.speed_percent == 100);
    CHECK(state.count_in_bars == 1);
    CHECK(state.practice_hands == HandFilter::Both);
    CHECK(state.piano_plays == PianoPlays::Nothing);
    CHECK(state.transpose_semitones == 0);
    CHECK(state.section_id == 0);
    CHECK(state.plan_item == 0);
    CHECK_FALSE(state.has_loop());
    CHECK_FALSE(state.loop_enabled);
    CHECK(state.song_hash == 0);
}

} // namespace

TEST_CASE("a found, unchanged song restores everything as saved") {
    const SessionState saved = saved_in();
    Song song = song_of(120.0);
    RestorePlan plan = plan_restore(saved, true, true, &song);
    CHECK(plan.outcome == Outcome::Restored);
    CHECK(plan.load_song);
    CHECK_FALSE(plan.was_reset());
    CHECK_FALSE(plan.position_clamped);
    CHECK_FALSE(plan.loop_adjusted);
    CHECK_FALSE(plan.loop_dropped);
    CHECK(plan.state == saved);
}

TEST_CASE("the section and the plan item come back with the song") {
    const SessionState saved = saved_in();
    Song song = song_of(120.0);
    RestorePlan plan = plan_restore(saved, true, true, &song);
    CHECK(plan.state.section_id == 4);
    CHECK(plan.state.plan_item == 2);
}

TEST_CASE("a position past the song's end moves to the end") {
    Song song = song_of(25.0);
    RestorePlan plan = plan_restore(saved_in(40.0), true, true, &song);
    CHECK(plan.outcome == Outcome::Restored);
    CHECK(plan.state.position_s == 25.0);
    CHECK(plan.position_clamped);
    CHECK(plan.state.speed_percent == 70); // the rest is untouched

    plan = plan_restore(saved_in(25.0), true, true, &song);
    CHECK_FALSE(plan.position_clamped);
    CHECK(plan.state.position_s == 25.0);
}

TEST_CASE("a loop that reaches past the song's end is cut to fit") {
    Song song = song_of(15.0);
    RestorePlan plan = plan_restore(saved_in(5.0), true, true, &song); // loop 10..20
    CHECK(plan.state.loop_start_s == 10.0);
    CHECK(plan.state.loop_end_s == 15.0);
    CHECK(plan.state.loop_enabled);
    CHECK(plan.loop_adjusted);
    CHECK_FALSE(plan.loop_dropped);
}

TEST_CASE("a loop outside the song is dropped and switched off") {
    Song song = song_of(8.0);
    RestorePlan plan = plan_restore(saved_in(5.0), true, true, &song); // loop 10..20
    CHECK_FALSE(plan.state.has_loop());
    CHECK(plan.state.loop_start_s == 0.0);
    CHECK(plan.state.loop_end_s == 0.0);
    CHECK_FALSE(plan.state.loop_enabled);
    CHECK(plan.loop_dropped);
    CHECK(plan.outcome == Outcome::Restored);
}

TEST_CASE("a loop left too short by the song's end is dropped") {
    Song song = song_of(10.05); // 10..10.05 is under kMinLoopSeconds
    RestorePlan plan = plan_restore(saved_in(5.0), true, true, &song);
    CHECK(plan.loop_dropped);
    CHECK_FALSE(plan.state.has_loop());
}

TEST_CASE("a switched-off loop outside the song is dropped too") {
    SessionState saved = saved_in(5.0);
    saved.loop_enabled = false;
    Song song = song_of(8.0);
    RestorePlan plan = plan_restore(saved, true, true, &song);
    CHECK(plan.loop_dropped);
    CHECK_FALSE(plan.state.has_loop());
}

TEST_CASE("a loop inside the song is left alone") {
    Song song = song_of(20.0); // loop 10..20 ends exactly at the end
    RestorePlan plan = plan_restore(saved_in(5.0), true, true, &song);
    CHECK_FALSE(plan.loop_adjusted);
    CHECK_FALSE(plan.loop_dropped);
    CHECK(plan.state.loop_start_s == 10.0);
    CHECK(plan.state.loop_end_s == 20.0);
    CHECK(plan.state.loop_enabled);
}

TEST_CASE("a session with no loop reports no loop change") {
    SessionState saved = saved_in(5.0);
    saved.loop_start_s = saved.loop_end_s = 0.0;
    saved.loop_enabled = false;
    Song song = song_of(8.0);
    RestorePlan plan = plan_restore(saved, true, true, &song);
    CHECK_FALSE(plan.loop_dropped);
    CHECK_FALSE(plan.loop_adjusted);
}

TEST_CASE("a missing song resets the song state and keeps the player's preferences") {
    const SessionState saved = saved_in();
    RestorePlan plan = plan_restore(saved, false, false, nullptr);
    CHECK(plan.outcome == Outcome::SongMissing);
    CHECK_FALSE(plan.load_song);
    CHECK(plan.was_reset());
    CHECK(plan.state.song_path.empty());
    check_song_state_reset(plan.state);
    check_preferences_kept(plan.state, saved);
}

TEST_CASE("a missing song wins over a hash that happens to say it matches") {
    Song song = song_of(100.0);
    RestorePlan plan = plan_restore(saved_in(), false, true, &song);
    CHECK(plan.outcome == Outcome::SongMissing);
    CHECK_FALSE(plan.load_song);
}

TEST_CASE("a song that was found but did not load is unreadable") {
    const SessionState saved = saved_in();
    RestorePlan plan = plan_restore(saved, true, true, nullptr);
    CHECK(plan.outcome == Outcome::SongUnreadable);
    CHECK_FALSE(plan.load_song);
    CHECK(plan.state.song_path.empty());
    check_song_state_reset(plan.state);
    check_preferences_kept(plan.state, saved);
    // Unreadable is reported before changed: there is no content to compare.
    CHECK(plan_restore(saved, true, false, nullptr).outcome == Outcome::SongUnreadable);
}

TEST_CASE("a changed song loads from the start with the song state reset") {
    const SessionState saved = saved_in();
    Song song = song_of(120.0);
    RestorePlan plan = plan_restore(saved, true, false, &song);
    CHECK(plan.outcome == Outcome::SongChanged);
    CHECK(plan.load_song);
    CHECK(plan.was_reset());
    CHECK(plan.state.song_path == saved.song_path); // the file is still the player's pick
    check_song_state_reset(plan.state);             // including the stale hash
    check_preferences_kept(plan.state, saved);
}

TEST_CASE("a session saved with no hash restores whatever the hash check says") {
    SessionState saved = saved_in();
    saved.song_hash = 0;
    Song song = song_of(120.0);
    RestorePlan plan = plan_restore(saved, true, false, &song);
    CHECK(plan.outcome == Outcome::Restored);
    CHECK(plan.state.position_s == 30.0);
}

TEST_CASE("a session with no song restores the preferences only") {
    SessionState saved = saved_in();
    saved.song_path.clear();
    saved = saved.clamped();
    RestorePlan plan = plan_restore(saved, false, false, nullptr);
    CHECK(plan.outcome == Outcome::NoSong);
    CHECK_FALSE(plan.load_song);
    CHECK_FALSE(plan.was_reset());
    check_song_state_reset(plan.state);
    check_preferences_kept(plan.state, saved);
}

TEST_CASE("an empty song restores at zero and has no loop") {
    Song song = song_of(0.0);
    RestorePlan plan = plan_restore(saved_in(), true, true, &song);
    CHECK(plan.outcome == Outcome::Restored);
    CHECK(plan.state.position_s == 0.0);
    CHECK_FALSE(plan.state.has_loop());
}

TEST_CASE("a song with a nonsense duration is treated as empty") {
    Song song = song_of(std::numeric_limits<double>::quiet_NaN());
    RestorePlan plan = plan_restore(saved_in(), true, true, &song);
    CHECK(plan.state.position_s == 0.0);
    CHECK_FALSE(plan.state.has_loop());
    song.duration_s = -4.0;
    plan = plan_restore(saved_in(), true, true, &song);
    CHECK(plan.state.position_s == 0.0);
}

TEST_CASE("a damaged saved state still produces a valid plan in every outcome") {
    SessionState wild;
    wild.song_path = "../../etc/passwd";
    wild.position_s = std::numeric_limits<double>::quiet_NaN();
    wild.speed_percent = -50;
    wild.loop_start_s = 5e9;
    wild.loop_end_s = -5e9;
    wild.loop_enabled = true;
    wild.transpose_semitones = 99;
    Song song = song_of(60.0);
    for (bool found : {false, true}) {
        for (bool matches : {false, true}) {
            for (const Song* s :
                 {static_cast<const Song*>(nullptr), static_cast<const Song*>(&song)}) {
                RestorePlan plan = plan_restore(wild, found, matches, s);
                CHECK(plan.state == plan.state.clamped());
            }
        }
    }
    wild.song_path = "ok.mid";
    for (bool found : {false, true}) {
        for (bool matches : {false, true}) {
            for (const Song* s :
                 {static_cast<const Song*>(nullptr), static_cast<const Song*>(&song)}) {
                RestorePlan plan = plan_restore(wild, found, matches, s);
                CHECK(plan.state == plan.state.clamped());
                CHECK(plan.state.position_s <= 60.0);
                CHECK(plan.state.loop_end_s <= 60.0);
            }
        }
    }
}

TEST_CASE("the library path is tried first, then the absolute one, each by content hash") {
    SessionState saved = saved_in();
    saved.song_abs_path = "/elsewhere/a.mid";
    const SongProbe good{true, true, true};
    const SongProbe changed{true, false, true};
    const SongProbe missing{};
    const SongProbe unreadable{true, true, false};

    // Both are good: the library wins.
    CHECK(choose_song_source(saved, good, good) == SongSource::Library);
    // The library file is gone, moved or changed: the absolute path takes over.
    CHECK(choose_song_source(saved, missing, good) == SongSource::Absolute);
    CHECK(choose_song_source(saved, changed, good) == SongSource::Absolute);
    CHECK(choose_song_source(saved, unreadable, good) == SongSource::Absolute);
    // Neither is good: the library answers when it found a file, else the absolute path does.
    CHECK(choose_song_source(saved, changed, changed) == SongSource::Library);
    CHECK(choose_song_source(saved, missing, changed) == SongSource::Absolute);
    CHECK(choose_song_source(saved, missing, missing) == SongSource::Library);
    // A path that was not saved is not tried, whatever its probe says.
    SessionState only_lib = saved;
    only_lib.song_abs_path.clear();
    CHECK(choose_song_source(only_lib, missing, good) == SongSource::Library);
    SessionState only_abs = saved;
    only_abs.song_path.clear();
    CHECK(choose_song_source(only_abs, good, good) == SongSource::Absolute);
    CHECK(choose_song_source(only_abs, good, missing) == SongSource::Absolute);
}

TEST_CASE("a session with only an absolute path restores from it and forgets it when gone") {
    SessionState saved = saved_in();
    saved.song_path.clear();
    saved.song_abs_path = "/elsewhere/a.mid";
    Song song = song_of(120.0);
    RestorePlan ok = plan_restore(saved, true, true, &song);
    CHECK(ok.outcome == Outcome::Restored);
    CHECK(ok.load_song);
    CHECK(ok.state.song_abs_path == saved.song_abs_path);
    CHECK(ok.state.position_s == doctest::Approx(30.0));

    RestorePlan gone = plan_restore(saved, false, false, nullptr);
    CHECK(gone.outcome == Outcome::SongMissing);
    CHECK(gone.state.song_abs_path.empty());
    CHECK(gone.state.song_path.empty());
    CHECK_FALSE(gone.state.has_song());
}

TEST_CASE("what the player changed while the song was read wins over the saved session") {
    SessionState saved = saved_in(12.0);
    saved.mode = PracticeMode::Wait;
    saved.practice_hands = HandFilter::Right;
    saved.piano_plays = PianoPlays::Right;
    saved.speed_percent = 80;
    saved.count_in_bars = 2;
    saved.transpose_semitones = 3;
    const SessionState at_start = saved; // the controller had taken the saved choices in

    // Nothing changed: the planned state stands, song-bound parts included.
    CHECK(keep_player_changes(saved, at_start, at_start) == saved);

    SessionState now = at_start;
    now.practice_hands = HandFilter::Left;
    now.transpose_semitones = -2;
    const SessionState kept = keep_player_changes(saved, at_start, now);
    CHECK(kept.practice_hands == HandFilter::Left);
    CHECK(kept.transpose_semitones == -2);
    // The rest of the saved session is untouched.
    CHECK(kept.mode == PracticeMode::Wait);
    CHECK(kept.speed_percent == 80);
    CHECK(kept.count_in_bars == 2);
    CHECK(kept.piano_plays == PianoPlays::Right);
    CHECK(kept.position_s == saved.position_s);
    CHECK(kept.song_hash == saved.song_hash);

    // A plan that reset the song-bound choices (the song changed) still gives way to the player's
    // own change, and only to that.
    SessionState reset = saved;
    reset.speed_percent = 100;
    reset.practice_hands = HandFilter::Both;
    reset.transpose_semitones = 0;
    SessionState tweaked = at_start;
    tweaked.speed_percent = 120;
    const SessionState over = keep_player_changes(reset, at_start, tweaked);
    CHECK(over.speed_percent == 120);
    CHECK(over.practice_hands == HandFilter::Both);
    CHECK(over.transpose_semitones == 0);

    // Every field the function looks at is one the controller can change without a song.
    SessionState flipped = at_start;
    flipped.mode = PracticeMode::Flow;
    flipped.piano_plays = PianoPlays::Nothing;
    flipped.count_in_bars = 0;
    flipped.speed_percent = 50;
    const SessionState all = keep_player_changes(saved, at_start, flipped);
    CHECK(all.mode == PracticeMode::Flow);
    CHECK(all.piano_plays == PianoPlays::Nothing);
    CHECK(all.count_in_bars == 0);
    CHECK(all.speed_percent == 50);
}
