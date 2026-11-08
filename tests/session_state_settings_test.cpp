// SPDX-License-Identifier: GPL-3.0-or-later
// session_state: settings (split from session_state_test.cpp).
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

TEST_CASE("skip back and skip forward seconds round-trip separately and clamp to 1..30") {
    for (int back = 1; back <= 30; back += 7) {
        AppSettings a;
        a.skip_back_seconds = back;
        a.skip_forward_seconds = 31 - back;
        SettingsDocument doc = SettingsDocument::parse("");
        write_app_settings(doc, a);
        const AppSettings b = read_app_settings(SettingsDocument::parse(doc.serialize()));
        CHECK(b.skip_back_seconds == back);
        CHECK(b.skip_forward_seconds == 31 - back);
    }
    const AppSettings c = read_app_settings(
        SettingsDocument::parse("[app]\nskip_back_seconds = 0\nskip_forward_seconds = 45\n"));
    CHECK(c.skip_back_seconds == 1);
    CHECK(c.skip_forward_seconds == 30);
    const AppSettings d = read_app_settings(
        SettingsDocument::parse("[app]\nskip_back_seconds = far\nskip_forward_seconds = 2.5\n"));
    CHECK(d.skip_back_seconds == 5);
    CHECK(d.skip_forward_seconds == 5);
}

TEST_CASE("the resume countdown round-trips through the file and falls back when unreadable") {
    for (int seconds = kMinResumeCountdownS; seconds <= kMaxResumeCountdownS; ++seconds) {
        AppSettings a;
        a.resume_countdown_s = seconds;
        SettingsDocument doc = SettingsDocument::parse("");
        write_app_settings(doc, a);
        CHECK(read_app_settings(SettingsDocument::parse(doc.serialize())).resume_countdown_s ==
              seconds);
    }
    std::vector<SettingsIssue> issues;
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nresume_countdown_s = soon\n"), &issues)
              .resume_countdown_s == 3);
    CHECK_FALSE(issues.empty());
    // The range is whole seconds from 1 to 10: a saved value outside it is clamped on load.
    CHECK(kMinResumeCountdownS == 1);
    CHECK(kMaxResumeCountdownS == 10);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nresume_countdown_s = 9\n"))
              .resume_countdown_s == 9);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nresume_countdown_s = 10\n"))
              .resume_countdown_s == 10);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nresume_countdown_s = 11\n"))
              .resume_countdown_s == 10);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nresume_countdown_s = 600\n"))
              .resume_countdown_s == 10);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nresume_countdown_s = 0\n"))
              .resume_countdown_s == 1);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nresume_countdown_s = -3\n"))
              .resume_countdown_s == 1);
}

TEST_CASE("the resume countdown, skip steps and bar style have their defaults and ranges") {
    const AppSettings d;
    CHECK(d.resume_countdown_s == 3);
    CHECK(d.skip_back_seconds == 5);
    CHECK(d.skip_forward_seconds == 5);
    CHECK(d.bar_style == BarStyle::Strip);
    AppSettings a;
    a.resume_countdown_s = 0;
    a.skip_back_seconds = 0;
    a.skip_forward_seconds = -4;
    a.bar_style = static_cast<BarStyle>(9);
    AppSettings c = a.clamped();
    CHECK(c.resume_countdown_s == 1);
    CHECK(c.skip_back_seconds == 1);
    CHECK(c.skip_forward_seconds == 1);
    CHECK(c.bar_style == BarStyle::Strip);
    a.resume_countdown_s = 11;
    a.skip_back_seconds = 31;
    a.skip_forward_seconds = 300;
    c = a.clamped();
    CHECK(c.resume_countdown_s == 10);
    CHECK(c.skip_back_seconds == 30);
    CHECK(c.skip_forward_seconds == 30);
}

TEST_CASE("the skip steps and bar style are saved under their own keys") {
    AppSettings a;
    a.skip_back_seconds = 1;
    a.skip_forward_seconds = 30;
    a.resume_countdown_s = 6;
    a.bar_style = BarStyle::Dock;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("skip_back_seconds = 1\n") != std::string::npos);
    CHECK(text.find("skip_forward_seconds = 30\n") != std::string::npos);
    CHECK(text.find("resume_countdown_s = 6\n") != std::string::npos);
    CHECK(text.find("bar_style = dock\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app_settings(SettingsDocument::parse(text), &issues) == a);
    CHECK(issues.empty());
    a.bar_style = BarStyle::Groups;
    write_app_settings(doc, a);
    CHECK(read_app_settings(SettingsDocument::parse(doc.serialize())).bar_style ==
          BarStyle::Groups);
}

TEST_CASE("the microphone settings default to MIDI only, clamp, and round-trip") {
    const AppSettings d;
    CHECK(d.input_mode == InputMode::Midi);
    CHECK(d.mic_device.empty());
    CHECK_FALSE(d.mic_learn_tuning);
    CHECK(d.mic_a4_tenths == 4400);
    CHECK(d.mic_noise_floor_ppm == 0);
    AppSettings a;
    a.input_mode = static_cast<InputMode>(9);
    a.mic_a4_tenths = 100;
    a.mic_noise_floor_ppm = -5;
    AppSettings c = a.clamped();
    CHECK(c.input_mode == InputMode::Midi);
    CHECK(c.mic_a4_tenths == kMinMicA4Tenths);
    CHECK(c.mic_noise_floor_ppm == 0);
    a.mic_a4_tenths = 9000;
    a.mic_noise_floor_ppm = 9'000'000;
    c = a.clamped();
    CHECK(c.mic_a4_tenths == kMaxMicA4Tenths);
    CHECK(c.mic_noise_floor_ppm == kMaxMicNoiseFloorPpm);

    a = AppSettings{};
    a.input_mode = InputMode::Both;
    a.mic_device = "USB Audio Device";
    a.mic_learn_tuning = true;
    a.mic_a4_tenths = 4423;
    a.mic_noise_floor_ppm = 1200;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("input_source = both\n") != std::string::npos);
    CHECK(text.find("mic_device = USB Audio Device\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app_settings(SettingsDocument::parse(text), &issues) == a);
    CHECK(issues.empty());
}

TEST_CASE("ABC chords as a left hand is off by default, saved as abc_chords, and a bad value keeps "
          "it off") {
    CHECK_FALSE(AppSettings{}.abc_chords);
    AppSettings a;
    a.abc_chords = true;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("abc_chords = true\n") != std::string::npos);
    CHECK(read_app_settings(SettingsDocument::parse(text)).abc_chords);
    std::vector<SettingsIssue> issues;
    CHECK_FALSE(read_app_settings(SettingsDocument::parse("[app]\nabc_chords = maybe\n"), &issues)
                    .abc_chords);
    CHECK_FALSE(issues.empty());
    // A settings file from before the key existed keeps the default.
    CHECK_FALSE(
        read_app_settings(SettingsDocument::parse("[app]\nbar_pinned = true\n")).abc_chords);
}

TEST_CASE("the screen size is saved as screen_width_mm, 0 by default, kept in range, and a bad "
          "value keeps the display's own") {
    CHECK(AppSettings{}.screen_width_mm == 0);
    AppSettings a;
    a.screen_width_mm = 344;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("screen_width_mm = 344\n") != std::string::npos);
    CHECK(read_app_settings(SettingsDocument::parse(text)).screen_width_mm == 344);
    // Too wide is clamped, a word keeps the default, a file from before the key keeps it too.
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscreen_width_mm = 99999\n"))
              .screen_width_mm == kMaxScreenWidthMm);
    std::vector<SettingsIssue> issues;
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscreen_width_mm = wide\n"), &issues)
              .screen_width_mm == 0);
    CHECK_FALSE(issues.empty());
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nbar_pinned = true\n"))
              .screen_width_mm == 0);
    // 0 is Auto, otherwise 60..1500: 1..59 is not a size (the layout ignores it) and reads as Auto.
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscreen_width_mm = 59\n"))
              .screen_width_mm == 0);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscreen_width_mm = 1\n"))
              .screen_width_mm == 0);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscreen_width_mm = -5\n"))
              .screen_width_mm == 0);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscreen_width_mm = 60\n"))
              .screen_width_mm == kMinScreenWidthMm);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscreen_width_mm = 1500\n"))
              .screen_width_mm == kMaxScreenWidthMm);
    AppSettings low;
    low.screen_width_mm = 30;
    CHECK(low.clamped().screen_width_mm == 0);
}

TEST_CASE("the device check's memory is saved as device_check_done and device_check_seen") {
    CHECK_FALSE(AppSettings{}.device_check_done);
    CHECK(AppSettings{}.device_check_seen.empty());
    AppSettings a;
    a.device_check_done = true;
    a.device_check_seen = "Yamaha PSR-E383|Casio CT-S1";
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("device_check_done = true\n") != std::string::npos);
    const AppSettings back = read_app_settings(SettingsDocument::parse(text));
    CHECK(back.device_check_done);
    CHECK(back.device_check_seen == "Yamaha PSR-E383|Casio CT-S1");
    // A file from before the keys existed keeps the defaults.
    CHECK_FALSE(read_app_settings(SettingsDocument::parse("[app]\nbar_pinned = true\n"))
                    .device_check_done);
}

TEST_CASE("the difficulty-weighted score is off by default, saved as weighted_score") {
    CHECK_FALSE(AppSettings{}.weighted_score);
    AppSettings a;
    a.weighted_score = true;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("weighted_score = true\n") != std::string::npos);
    CHECK(read_app_settings(SettingsDocument::parse(text)).weighted_score);
    CHECK_FALSE(
        read_app_settings(SettingsDocument::parse("[app]\nbar_pinned = true\n")).weighted_score);
    std::vector<SettingsIssue> issues;
    CHECK_FALSE(read_app_settings(SettingsDocument::parse("[app]\nweighted_score = maybe\n"),
                                  &issues)
                    .weighted_score);
    CHECK_FALSE(issues.empty());
}

TEST_CASE("a song keeps the played notes of its newest 20 runs by default, saved as run_events_kept") {
    CHECK(AppSettings{}.run_events_kept == 20);
    AppSettings a;
    a.run_events_kept = 5;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("run_events_kept = 5\n") != std::string::npos);
    CHECK(read_app_settings(SettingsDocument::parse(text)).run_events_kept == 5);
    // A file from before the key keeps the default; 0 is allowed (keep none); a number out of range
    // is held to it.
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nbar_pinned = true\n")).run_events_kept ==
          20);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nrun_events_kept = 0\n")).run_events_kept ==
          0);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nrun_events_kept = 100000\n"))
              .run_events_kept <= kMaxRunEventsKept);
    // The cap is 200: a player who set it that high before keeps it, and one above it is held to it.
    CHECK(kMaxRunEventsKept == 200);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nrun_events_kept = 200\n"))
              .run_events_kept == 200);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nrun_events_kept = 201\n"))
              .run_events_kept == 200);
}

TEST_CASE("a replays-kept value out of range in the file is held to 0 .. 200 on reading") {
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nrun_events_kept = 500\n"))
              .run_events_kept == kMaxRunEventsKept);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nrun_events_kept = -3\n"))
              .run_events_kept == 0);
}

TEST_CASE("the replays kept per song round-trips at both ends of its range and in between") {
    // The settings row's slider runs 0 to kMaxRunEventsKept: every value it can make is saved and
    // read back as it was.
    for (const int kept : {0, 1, 20, 137, kMaxRunEventsKept}) {
        AppSettings a;
        a.run_events_kept = kept;
        SettingsDocument doc = SettingsDocument::parse("");
        write_app_settings(doc, a);
        CHECK(read_app_settings(SettingsDocument::parse(doc.serialize())).run_events_kept == kept);
    }
}

TEST_CASE("the section band's difficulty tint is off by default, saved as band_difficulty") {
    CHECK_FALSE(AppSettings{}.band_difficulty);
    AppSettings a;
    a.band_difficulty = true;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("band_difficulty = true\n") != std::string::npos);
    CHECK(read_app_settings(SettingsDocument::parse(text)).band_difficulty);
    // A file from before it, and a bad value, keep it off.
    CHECK_FALSE(
        read_app_settings(SettingsDocument::parse("[app]\nbar_pinned = true\n")).band_difficulty);
    std::vector<SettingsIssue> issues;
    CHECK_FALSE(
        read_app_settings(SettingsDocument::parse("[app]\nband_difficulty = maybe\n"), &issues)
            .band_difficulty);
    CHECK_FALSE(issues.empty());
}

TEST_CASE(
    "keeping the bar shown is off by default, saved as bar_pinned, and a bad value keeps it off") {
    CHECK_FALSE(AppSettings{}.bar_pinned);
    AppSettings a;
    a.bar_pinned = true;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("bar_pinned = true\n") != std::string::npos);
    CHECK(read_app_settings(SettingsDocument::parse(text)).bar_pinned);
    std::vector<SettingsIssue> issues;
    CHECK_FALSE(read_app_settings(SettingsDocument::parse("[app]\nbar_pinned = maybe\n"), &issues)
                    .bar_pinned);
    CHECK_FALSE(issues.empty());
    // A file from before the setting existed keeps the bar auto-hiding.
    issues.clear();
    CHECK_FALSE(read_app_settings(SettingsDocument::parse("[app]\nbar_style = dock\n"), &issues)
                    .bar_pinned);
    CHECK(issues.empty());
}

TEST_CASE("an unknown bar style in the file keeps the strip and reports the line") {
    std::vector<SettingsIssue> issues;
    AppSettings got = read_app_settings(
        SettingsDocument::parse("[app]\nbar_style = floating\nskip_back_seconds = 9\n"), &issues);
    CHECK(got.bar_style == BarStyle::Strip);
    CHECK(got.skip_back_seconds == 9);
    CHECK_FALSE(issues.empty());
}
