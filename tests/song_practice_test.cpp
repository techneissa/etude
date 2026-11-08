// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_practice.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace etude;

TEST_CASE("song practice: the defaults are a plain start") {
    const SongPractice p;
    CHECK(p.speed_percent == 100);
    CHECK(p.count_in_bars == kDefaultCountInBars);
    CHECK(p.hands == HandFilter::Both);
    CHECK(p.transpose_semitones == 0);
    CHECK(p.split_point == kAutoSplit); // an unseen song splits as before
}

TEST_CASE("song practice: sanitized forces every field into its range") {
    SongPractice p;
    p.speed_percent = 5000;
    p.count_in_bars = -3;
    p.hands = static_cast<HandFilter>(9);
    p.transpose_semitones = -40;
    const SongPractice c = sanitized(p);
    CHECK(c.speed_percent == kMaxSpeedPercent);
    CHECK(c.count_in_bars == 0);
    CHECK(c.hands == HandFilter::Both);
    CHECK(c.transpose_semitones == -kMaxTransposeSemitones);
    CHECK(sanitized(c) == c);

    SongPractice q;
    q.speed_percent = 1;
    q.count_in_bars = 99;
    q.hands = HandFilter::Right;
    q.transpose_semitones = 99;
    const SongPractice d = sanitized(q);
    CHECK(d.speed_percent == kMinSpeedPercent);
    CHECK(d.count_in_bars == kMaxCountInBars);
    CHECK(d.hands == HandFilter::Right);
    CHECK(d.transpose_semitones == kMaxTransposeSemitones);
}

TEST_CASE("song practice: stored choices arrive late and the player's changes win, field by field") {
    SongPractice stored;
    stored.speed_percent = 70;
    stored.count_in_bars = 2;
    stored.hands = HandFilter::Left;
    stored.transpose_semitones = 3;

    const SongPractice at_open; // the defaults the song was opened on

    // Nothing changed since: everything stored comes in.
    CHECK(with_player_changes(stored, at_open, at_open) == stored);

    // The player moved the speed and the hands: those stay theirs, the rest is the stored.
    SongPractice now = at_open;
    now.speed_percent = 120;
    now.hands = HandFilter::Right;
    const SongPractice merged = with_player_changes(stored, at_open, now);
    CHECK(merged.speed_percent == 120);
    CHECK(merged.hands == HandFilter::Right);
    CHECK(merged.count_in_bars == 2);
    CHECK(merged.transpose_semitones == 3);

    // A change back to exactly what the song opened on is no change.
    now.speed_percent = at_open.speed_percent;
    CHECK(with_player_changes(stored, at_open, now).speed_percent == 70);
}

TEST_CASE("song practice: the muted parts and the loop are kept in range") {
    SongPractice p;
    p.parts_set = true;
    p.muted_parts = {9, 3, 9, 5};
    p.loop_start_s = 2.0004;
    p.loop_end_s = 6.0;
    p.loop_enabled = true;
    p.section_id = -4;
    p.plan_item = 9999;
    const SongPractice c = sanitized(p);
    CHECK(c.muted_parts == std::vector<std::uint32_t>{3, 5, 9});
    CHECK(c.loop_start_s == doctest::Approx(2.0));  // whole milliseconds
    CHECK(c.loop_enabled);
    CHECK(c.section_id == 0);
    CHECK(c.plan_item == kMaxSessionPlanItems);
    CHECK(sanitized(c) == c);

    // Parts the player never chose are none; a loop too short, or not a number, is no loop.
    SongPractice q;
    q.muted_parts = {1, 2};
    q.loop_start_s = 4.0;
    q.loop_end_s = 4.01;
    q.loop_enabled = true;
    const SongPractice d = sanitized(q);
    CHECK(d.muted_parts.empty());
    CHECK_FALSE(d.has_loop());
    CHECK_FALSE(d.loop_enabled);
    CHECK(d.loop_end_s == 0.0);
    SongPractice r;
    r.loop_start_s = std::nan("");
    r.loop_end_s = std::nan("");
    r.loop_enabled = true;
    CHECK(sanitized(r).loop_start_s == 0.0);
    CHECK_FALSE(sanitized(r).has_loop());
    CHECK_FALSE(sanitized(r).loop_enabled);
    r.loop_start_s = 1.0;
    r.loop_end_s = 1e12; // a loop to the end of time is cut to a day
    CHECK(sanitized(r).loop_end_s == kMaxSessionSeconds);
}

TEST_CASE("song practice: the parts and the loop each come in late as one piece unless changed") {
    SongPractice stored;
    stored.parts_set = true;
    stored.muted_parts = {7};
    stored.loop_start_s = 10.0;
    stored.loop_end_s = 20.0;
    stored.loop_enabled = true;
    stored.section_id = 2;
    const SongPractice at_open;
    CHECK(with_player_changes(stored, at_open, at_open) == stored);

    SongPractice now = at_open;
    now.loop_start_s = 1.0;
    now.loop_end_s = 5.0;
    now.loop_enabled = true;
    const SongPractice merged = with_player_changes(stored, at_open, now);
    CHECK(merged.loop_start_s == 1.0); // the player's loop, whole, with no section
    CHECK(merged.loop_end_s == 5.0);
    CHECK(merged.section_id == 0);
    CHECK(merged.muted_parts == std::vector<std::uint32_t>{7}); // the stored parts

    now = at_open;
    now.parts_set = true;
    now.muted_parts = {1, 2};
    const SongPractice other = with_player_changes(stored, at_open, now);
    CHECK(other.muted_parts == std::vector<std::uint32_t>{1, 2});
    CHECK(other.loop_start_s == 10.0);
    CHECK(note_changes({}, at_open, at_open).any() == false);
}

TEST_CASE("song practice: the hand split is Auto or a point in 1..127, anything else is forced") {
    SongPractice p;
    p.split_point = 64;
    CHECK(sanitized(p).split_point == 64);
    p.split_point = -3;
    CHECK(sanitized(p).split_point == kAutoSplit);
    p.split_point = 500;
    CHECK(sanitized(p).split_point == kMaxSplitPoint);
    p.split_point = 127;
    CHECK(sanitized(p).split_point == 127);
    p.split_point = 0;
    CHECK(sanitized(p).split_point == kAutoSplit);
}

TEST_CASE("song practice: a changed hand split is one of the player's changes, and wins late") {
    SongPractice stored;
    stored.split_point = 67;
    stored.speed_percent = 80;

    const SongPractice at_open; // Auto, 100%
    SongPractice now = at_open;
    now.split_point = 55;
    const PracticeChanges changes = note_changes({}, at_open, now);
    CHECK(changes.split);
    CHECK(changes.any());
    CHECK_FALSE(changes.speed);
    CHECK(changes.value.split_point == 55);

    const SongPractice merged = apply_changes(stored, changes);
    CHECK(merged.split_point == 55);   // the player's, over the stored 67
    CHECK(merged.speed_percent == 80); // everything else from the history

    // Not changed since the song was opened: the stored one arrives.
    CHECK(with_player_changes(stored, at_open, at_open).split_point == 67);
    // Back to Auto is a change too.
    SongPractice opened = at_open;
    opened.split_point = 60;
    SongPractice auto_now = opened;
    auto_now.split_point = kAutoSplit;
    CHECK(with_player_changes(stored, opened, auto_now).split_point == kAutoSplit);
}

TEST_CASE("song practice: turning the octave-shift offer off is a choice like the others") {
    const SongPractice at_open;
    CHECK_FALSE(at_open.no_shift_offer);
    SongPractice now = at_open;
    now.no_shift_offer = true;
    const PracticeChanges changes = note_changes({}, at_open, now);
    CHECK(changes.shift_offer);
    CHECK(changes.any());
    CHECK_FALSE(changes.speed);
    CHECK(apply_changes(at_open, changes).no_shift_offer);

    // Coming in late: the stored choice stands unless the player changed it since the song opened.
    SongPractice stored;
    stored.no_shift_offer = true;
    CHECK(with_player_changes(stored, at_open, at_open).no_shift_offer);
    SongPractice undone = stored;
    undone.no_shift_offer = false;
    CHECK_FALSE(with_player_changes(stored, stored, undone).no_shift_offer);
    CHECK(sanitized(stored) == stored);
}
