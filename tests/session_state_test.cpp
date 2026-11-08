// SPDX-License-Identifier: GPL-3.0-or-later
// session_state: the first topics (split from session_state_test.cpp).
#include "etude/session_state.hpp"
#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "session_state_test_support.hpp"

using namespace etude;
using Kind = SettingsIssueKind;
using namespace session_state_test_support;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

constexpr double kInf = std::numeric_limits<double>::infinity();

} // namespace

TEST_CASE("the defaults are what the documentation says") {
    SessionState s;
    CHECK(s.song_path.empty());
    CHECK(s.song_hash == 0);
    CHECK(s.position_s == 0.0);
    CHECK(s.speed_percent == 100);
    CHECK(s.practice_hands == HandFilter::Both);
    CHECK(s.piano_plays == PianoPlays::Nothing); // song sound is Mute by default
    CHECK_FALSE(s.has_loop());
    CHECK_FALSE(s.loop_enabled);
    CHECK_FALSE(s.auto_speed_enabled);
    CHECK(s.auto_speed_start_percent == 60);
    CHECK(s.auto_speed_step_percent == 5);
    CHECK(s.auto_speed_max_percent == 100);
    CHECK(s.count_in_bars == 1);
    CHECK(s.mode == PracticeMode::FreePlay);
    CHECK(s.transpose_semitones == 0);
    CHECK(s.section_id == 0);
    CHECK(s.plan_item == 0);
    CHECK(s.keyboard == KeyboardChoice::Auto);
    CHECK(s.keyboard_range().lowest == 36);
    CHECK(s.keyboard_range().highest == 96);
    CHECK(s.clamped() == s);

    AppSettings a;
    CHECK(a.hit_window_ms == 80);
    CHECK(a.latency_correction_ms == 0);
    CHECK(a.midi_in_port.empty());
    CHECK(a.midi_out_port.empty());
    CHECK(a.daily_goal_minutes == 15);
    CHECK(a.resume_countdown_s == 3);
    CHECK(a.skip_back_seconds == 5);
    CHECK(a.skip_forward_seconds == 5);
    CHECK(a.clamped() == a);
}

TEST_CASE("clamped() holds each number to its documented range") {
    SessionState s;
    s.song_path = "a.mid";
    s.position_s = -3.0;
    s.speed_percent = 5;
    s.count_in_bars = 99;
    s.transpose_semitones = 40;
    s.auto_speed_start_percent = 1000;
    s.auto_speed_step_percent = 0;
    SessionState c = s.clamped();
    CHECK(c.position_s == 0.0);
    CHECK(c.speed_percent == 25);
    CHECK(c.count_in_bars == 2);
    CHECK(c.transpose_semitones == 12);
    CHECK(c.auto_speed_start_percent == 200);
    CHECK(c.auto_speed_step_percent == 5);

    s.position_s = 1e12;
    s.speed_percent = 500;
    s.count_in_bars = -1;
    s.transpose_semitones = -13;
    s.auto_speed_step_percent = 90;
    c = s.clamped();
    CHECK(c.position_s == kMaxSessionSeconds);
    CHECK(c.speed_percent == 200);
    CHECK(c.count_in_bars == 0);
    CHECK(c.transpose_semitones == -12);
    CHECK(c.auto_speed_step_percent == 25);
    CHECK(c.clamped() == c);
}

TEST_CASE("clamped() holds the section and the plan item to their ranges") {
    SessionState s;
    s.song_path = "a.mid";
    s.section_id = 7;
    s.plan_item = 3;
    CHECK(s.clamped() == s);
    s.section_id = -4;
    s.plan_item = -1;
    SessionState c = s.clamped();
    CHECK(c.section_id == 0);
    CHECK(c.plan_item == 0);
    s.section_id = 2'100'000'000;
    s.plan_item = 1000;
    c = s.clamped();
    CHECK(c.section_id == kMaxSessionSectionId);
    CHECK(c.plan_item == kMaxSessionPlanItems);
    CHECK(c.clamped() == c);
}

TEST_CASE("clamped() without a song forgets the section and the plan item") {
    SessionState s;
    s.section_id = 7;
    s.plan_item = 3;
    SessionState c = s.clamped();
    CHECK(c.section_id == 0);
    CHECK(c.plan_item == 0);
}

TEST_CASE("the section and the plan item are read from and written to the session") {
    SessionState s = read("[session]\nsong = a.mid\nsection_id = 12\nplan_item = 2\n");
    CHECK(s.section_id == 12);
    CHECK(s.plan_item == 2);
    std::vector<SettingsIssue> issues;
    s = read("[session]\nsong = a.mid\nsection_id = x\nplan_item = 99\n", &issues);
    CHECK(s.section_id == 0);
    CHECK(s.plan_item == kMaxSessionPlanItems);
    CHECK(has_issue(issues, Kind::BadValue, "section_id"));
    CHECK(has_issue(issues, Kind::OutOfRange, "plan_item"));
}

TEST_CASE("clamped() keeps the speed a whole percent and snaps the speed-up policy to fives") {
    SessionState s;
    s.speed_percent = 62;
    s.auto_speed_start_percent = 58;
    s.auto_speed_max_percent = 143;
    s.auto_speed_step_percent = 7;
    SessionState c = s.clamped();
    CHECK(c.speed_percent == 62);
    CHECK(c.auto_speed_start_percent == 60);
    CHECK(c.auto_speed_max_percent == 145);
    CHECK(c.auto_speed_step_percent == 5);
    s.speed_percent = 63;
    CHECK(s.clamped().speed_percent == 63);
    s.speed_percent = 250;
    CHECK(s.clamped().speed_percent == 200);
    s.speed_percent = 3;
    CHECK(s.clamped().speed_percent == 25);
}

TEST_CASE("clamped() turns NaN and infinity into valid times") {
    SessionState s;
    s.song_path = "a.mid";
    s.position_s = kNaN;
    s.loop_start_s = kNaN;
    s.loop_end_s = kNaN;
    s.loop_enabled = true;
    SessionState c = s.clamped();
    CHECK(c.position_s == 0.0);
    CHECK_FALSE(c.has_loop());
    CHECK_FALSE(c.loop_enabled);
    s.position_s = kInf;
    CHECK(s.clamped().position_s == kMaxSessionSeconds);
    s.position_s = -kInf;
    CHECK(s.clamped().position_s == 0.0);
}

TEST_CASE(
    "clamped() orders the loop, drops one that is too short, and switches off a missing one") {
    SessionState s;
    s.song_path = "a.mid";
    s.loop_start_s = 20.0;
    s.loop_end_s = 10.0;
    s.loop_enabled = true;
    SessionState c = s.clamped();
    CHECK(c.loop_start_s == 10.0);
    CHECK(c.loop_end_s == 20.0);
    CHECK(c.loop_enabled);

    s.loop_start_s = 10.0;
    s.loop_end_s = 10.05; // under kMinLoopSeconds
    c = s.clamped();
    CHECK(c.loop_start_s == 0.0);
    CHECK(c.loop_end_s == 0.0);
    CHECK_FALSE(c.loop_enabled);

    s.loop_start_s = 10.0;
    s.loop_end_s = 10.5;
    c = s.clamped();
    CHECK(c.has_loop());

    s.loop_start_s = 0.0;
    s.loop_end_s = 0.0;
    s.loop_enabled = true;
    CHECK_FALSE(s.clamped().loop_enabled);

    s.loop_start_s = 5.0;
    s.loop_end_s = 9.0;
    s.loop_enabled = false;
    c = s.clamped();
    CHECK(c.has_loop()); // a loop that is set but switched off stays
    CHECK_FALSE(c.loop_enabled);
}

TEST_CASE("clamped() keeps the auto speed-up maximum at or above its start") {
    SessionState s;
    s.auto_speed_start_percent = 90;
    s.auto_speed_max_percent = 70;
    SessionState c = s.clamped();
    CHECK(c.auto_speed_start_percent == 90);
    CHECK(c.auto_speed_max_percent == 90);
    AutoSpeedUp policy = c.auto_speed_up();
    CHECK(policy.start_percent == 90);
    CHECK(policy.max_percent == 90);
}

TEST_CASE("clamped() accepts only library-relative song paths") {
    auto path = [](std::string p) {
        SessionState s;
        s.song_path = std::move(p);
        s.song_hash = 5;
        return s.clamped();
    };
    CHECK(path("Songs/a.mid").song_path == "Songs/a.mid");
    CHECK(path("a b/c d.mid").song_path == "a b/c d.mid");
    CHECK(path("a..b/c.mid").song_path == "a..b/c.mid"); // dots inside a name are fine
    CHECK(path("\xF0\x9F\x8E\xB9.mid").song_path == "\xF0\x9F\x8E\xB9.mid"); // 4-byte character
    CHECK(path("Songs/F\xC3\xBCr Elise.mid").song_path == "Songs/F\xC3\xBCr Elise.mid");
    for (const char* bad : {"/etc/passwd", "../x.mid", "a/../../x.mid", "a/..", "..", "C:/x.mid",
                            "c:\\x.mid", "a\nb.mid",
                            "a\x01"
                            "b.mid",
                            "\x7f", "\xFF\xFE.mid", "\xC3", "a\\..\\b.mid", "\xC0\x80",
                            "\xED\xA0\x80", "\xF4\x90\x80\x80"}) {
        CAPTURE(bad);
        SessionState c = path(bad);
        CHECK(c.song_path.empty());
        CHECK(c.song_hash == 0); // the hash went with the path
    }
    CHECK(path(std::string(kMaxSongPathBytes, 'a')).song_path.size() == kMaxSongPathBytes);
    CHECK(path(std::string(kMaxSongPathBytes + 1, 'a')).song_path.empty());
}

TEST_CASE("clamped() without a song clears the song-bound position and loop") {
    SessionState s;
    s.position_s = 40.0;
    s.loop_start_s = 1.0;
    s.loop_end_s = 5.0;
    s.loop_enabled = true;
    s.song_hash = 77;
    SessionState c = s.clamped();
    CHECK(c.song_hash == 0);
    CHECK(c.position_s == 0.0);
    CHECK_FALSE(c.has_loop());
    CHECK_FALSE(c.loop_enabled);
}

TEST_CASE("clamped() repairs the manual keyboard range or falls back to Auto") {
    SessionState s;
    s.keyboard = KeyboardChoice::Manual;
    s.keyboard_lowest = 36;
    s.keyboard_highest = 96;
    CHECK(s.clamped() == s);

    s.keyboard_lowest = 37;  // C#: widens down to C
    s.keyboard_highest = 97; // C#: widens up to D
    SessionState c = s.clamped();
    CHECK(c.keyboard == KeyboardChoice::Manual);
    CHECK(c.keyboard_lowest == 36);
    CHECK(c.keyboard_highest == 98);

    s.keyboard_lowest = 96;
    s.keyboard_highest = 36; // reversed
    c = s.clamped();
    CHECK(c.keyboard_lowest == 36);
    CHECK(c.keyboard_highest == 96);

    s.keyboard_lowest = -5;
    s.keyboard_highest = 400;
    c = s.clamped();
    CHECK(c.keyboard_lowest == 0);
    CHECK(c.keyboard_highest == 127);
    CHECK(KeyRange{c.keyboard_lowest, c.keyboard_highest}.valid());

    s.keyboard_lowest = 60;
    s.keyboard_highest = 64; // five keys: not a keyboard
    c = s.clamped();
    CHECK(c.keyboard == KeyboardChoice::Auto);
    CHECK(c.keyboard_lowest == 36);
    CHECK(c.keyboard_highest == 96);
}

TEST_CASE("clamped() makes a Profile choice without a name Auto, and cleans the names") {
    SessionState s;
    s.keyboard = KeyboardChoice::Profile;
    CHECK(s.clamped().keyboard == KeyboardChoice::Auto);
    s.keyboard_name = "Some Future Board";
    CHECK(s.clamped().keyboard == KeyboardChoice::Profile); // this build need not know the name
    s.keyboard_name = std::string(kMaxNameBytes + 1, 'k');
    CHECK(s.clamped().keyboard_name.empty());
    s.keyboard_name = "bad\xFF";
    CHECK(s.clamped().keyboard_name.empty());

    SessionState bad_enum;
    bad_enum.practice_hands = static_cast<HandFilter>(77);
    bad_enum.piano_plays = static_cast<PianoPlays>(77);
    bad_enum.mode = static_cast<PracticeMode>(77);
    bad_enum.keyboard = static_cast<KeyboardChoice>(77);
    SessionState c = bad_enum.clamped();
    CHECK(c.practice_hands == HandFilter::Both);
    CHECK(c.piano_plays == PianoPlays::Nothing);
    CHECK(c.mode == PracticeMode::FreePlay);
    CHECK(c.keyboard == KeyboardChoice::Auto);
}

TEST_CASE("AppSettings::clamped() holds each field to its range") {
    AppSettings a;
    a.hit_window_ms = 0;
    a.latency_correction_ms = -9000;
    a.daily_goal_minutes = 5000;
    a.resume_countdown_s = 0;
    a.skip_back_seconds = 0;
    a.skip_forward_seconds = -7;
    a.midi_in_port = std::string(kMaxNameBytes + 1, 'p');
    a.midi_out_port = "ok port";
    AppSettings c = a.clamped();
    CHECK(c.hit_window_ms == 20);
    CHECK(c.latency_correction_ms == -250);
    CHECK(c.daily_goal_minutes == 720);
    CHECK(c.resume_countdown_s == 1);
    CHECK(c.skip_back_seconds == 1);
    CHECK(c.skip_forward_seconds == 1);
    CHECK(c.midi_in_port.empty());
    CHECK(c.midi_out_port == "ok port");
    a.hit_window_ms = 9999;
    a.latency_correction_ms = 9000;
    a.daily_goal_minutes = -1;
    a.resume_countdown_s = 60;
    a.skip_back_seconds = 31;
    a.skip_forward_seconds = 900;
    c = a.clamped();
    CHECK(c.hit_window_ms == 250);
    CHECK(c.latency_correction_ms == 250);
    CHECK(c.daily_goal_minutes == 0);
    CHECK(c.resume_countdown_s == 10);
    CHECK(c.skip_back_seconds == 30);
    CHECK(c.skip_forward_seconds == 30);
}
