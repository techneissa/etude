// SPDX-License-Identifier: GPL-3.0-or-later
// session_state: file (split from session_state_test.cpp).
#include "etude/session_state.hpp"
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <limits>
#include <string>
#include <vector>

#include "session_state_test_support.hpp"

using namespace etude;
using Kind = SettingsIssueKind;
using namespace session_state_test_support;

namespace {

SessionState busy_session() {
    SessionState s;
    s.song_path = "Songs/F\xC3\xBCr Elise #2.mid";
    s.song_hash = 0xdeadbeefULL;
    s.position_s = 12.5;
    s.speed_percent = 75;
    s.practice_hands = HandFilter::Right;
    s.piano_plays = PianoPlays::OtherHand;
    s.loop_start_s = 8.0;
    s.loop_end_s = 16.25;
    s.loop_enabled = true;
    s.auto_speed_enabled = true;
    s.count_in_bars = 2;
    s.mode = PracticeMode::Wait;
    s.transpose_semitones = -2;
    s.section_id = 3;
    s.plan_item = 2;
    s.keyboard = KeyboardChoice::Profile;
    s.keyboard_name = "PSR-E383";
    return s;
}

} // namespace

constexpr const char* kDefaultDocumentApp = "# etude settings\n"
                                            "format = 1.0\n"
                                            "\n"
                                            "[app]\n"
                                            "hit_window_ms = 80\n"
                                            "latency_correction_ms = 0\n"
                                            "midi_in_port =\n"
                                            "midi_out_port =\n"
                                            "daily_goal_minutes = 15\n"
                                            "resume_countdown_s = 3\n"
                                            "skip_back_seconds = 5\n"
                                            "skip_forward_seconds = 5\n"
                                            "bar_style = strip\n"
                                            "metronome = false\n"
                                            "metronome_sound = wood\n"
                                            "metronome_volume = 70\n"
                                            "metronome_accent = true\n"
                                            "metronome_subdivision = off\n"
                                            "metronome_pulse = false\n"
                                            "accompaniment = true\n"
                                            "accompaniment_volume = 100\n"
                                            "bar_pinned = false\n"
                                            "band_difficulty = false\n"
                                            "weighted_score = false\n"
                                            "run_events_kept = 20\n"
                                            "view_mode = falling\nrenderer = basic\n"
                                            "input_source = midi\n"
                                            "mic_device =\n"
                                            "mic_learn_tuning = false\n"
                                            "mic_a4_tenths = 4400\n"
                                            "mic_noise_floor_ppm = 0\n"
                                            "abc_chords = false\n"
                                            "screen_width_mm = 0\n"
                                            "scale_overlay = false\n"
                                            "scale_root = 0\n"
                                            "scale_type = major\n"
                                            "scale_fingers = false\n"
                                            "device_check_done = false\n"
                                            "device_check_seen =\n"
                                            "update_auto_check = false\n"
                                            "icon_colour = wine\n"
                                            "active_profile = 1\n"
                                            "recent_colors =\n"
                                            "library_sort = name\n"
                                            "library_sort_reversed = false\n"
                                            "library_group = none\n"
                                            "library_filter = all\n"
                                            "library_levels = 0\n"
                                            "library_hands = any\n"
                                            "library_view = grid\n"
                                            "library_scroll = 0\n"
                                            "get_songs_online = false\n"
                                            "group_versions = true\n"
                                            "learn_picker = ring\n"
                                            "learn_root = 0\n"
                                            "learn_scale = major\n"
                                            "learn_hands = both\n"
                                            "learn_tempo = 80\n"
                                            "learn_direction = up\n"
                                            "learn_octaves = 1\n"
                                            "learn_arpeggio = false\n"
                                            "learn_ear_mode = note\n"
                                            "learn_ear_octave = 4\n"
                                            "learn_ear_any_octave = false\n"
                                            "learn_ear_max = 12\n"
                                            "learn_chord_root = 0\n"
                                            "learn_chord = major\n"
                                            "learn_inversion = 0\n"
                                            "melody_added =\n"
                                            "badge_wall_view = category\n"
                                            "runs_chart_view = bars\n"
                                            "\n";

TEST_CASE("a default state writes this exact document") {
    SettingsDocument doc = SettingsDocument::parse("");
    doc.ensure_format();
    write_app_settings(doc, AppSettings{});
    write_session(doc, SessionState{});
    CHECK(doc.serialize() == std::string(kDefaultDocumentApp) + "[session]\n"
                                                                "song =\n"
                                                                "song_abs =\n"
                                                                "song_hash = 0000000000000000\n"
                                                                "position_s = 0\n"
                                                                "speed_percent = 100\n"
                                                                "hands = both\n"
                                                                "piano_plays = nothing\n"
                                                                "loop_start_s = 0\n"
                                                                "loop_end_s = 0\n"
                                                                "loop_enabled = false\n"
                                                                "auto_speed_enabled = false\n"
                                                                "auto_speed_start_percent = 60\n"
                                                                "auto_speed_step_percent = 5\n"
                                                                "auto_speed_max_percent = 100\n"
                                                                "count_in_bars = 1\n"
                                                                "mode = free_play\n"
                                                                "transpose_semitones = 0\n"
                                                                "section_id = 0\n"
                                                                "plan_item = 0\n"
                                                                "keyboard = auto\n"
                                                                "keyboard_name =\n"
                                                                "keyboard_lowest = 36\n"
                                                                "keyboard_highest = 96\n");
}

TEST_CASE("a busy state writes this exact session section") {
    SettingsDocument doc = SettingsDocument::parse("");
    write_session(doc, busy_session());
    CHECK(doc.serialize() == "[session]\n"
                             "song = Songs/F\xC3\xBCr Elise #2.mid\n"
                             "song_abs =\n"
                             "song_hash = 00000000deadbeef\n"
                             "position_s = 12.5\n"
                             "speed_percent = 75\n"
                             "hands = right\n"
                             "piano_plays = other_hand\n"
                             "loop_start_s = 8\n"
                             "loop_end_s = 16.25\n"
                             "loop_enabled = true\n"
                             "auto_speed_enabled = true\n"
                             "auto_speed_start_percent = 60\n"
                             "auto_speed_step_percent = 5\n"
                             "auto_speed_max_percent = 100\n"
                             "count_in_bars = 2\n"
                             "mode = wait\n"
                             "transpose_semitones = -2\n"
                             "section_id = 3\n"
                             "plan_item = 2\n"
                             "keyboard = profile\n"
                             "keyboard_name = PSR-E383\n"
                             "keyboard_lowest = 36\n"
                             "keyboard_highest = 96\n");
}

TEST_CASE("every field survives write then read") {
    SessionState s = busy_session();
    s.song_hash = 0xffffffffffffffffULL;
    s.piano_plays = PianoPlays::Nothing;
    s.keyboard = KeyboardChoice::Manual;
    s.keyboard_name = "  padded \"name\"  ";
    s.keyboard_lowest = 28;
    s.keyboard_highest = 103;
    s.position_s = 0.1 + 0.2; // not exactly representable in decimal
    s.loop_start_s = 1.0 / 3.0;
    s.loop_end_s = 100.0 / 7.0;
    s.auto_speed_start_percent = 40;
    s.auto_speed_step_percent = 10;
    s.auto_speed_max_percent = 120;
    s = s.clamped();
    REQUIRE(s.piano_plays == PianoPlays::Nothing);

    SettingsDocument doc = SettingsDocument::parse("");
    write_session(doc, s);
    std::vector<SettingsIssue> issues;
    SessionState back = read_session(SettingsDocument::parse(doc.serialize()), &issues);
    CHECK(issues.empty());
    CHECK(back == s);

    for (HandFilter h : {HandFilter::Both, HandFilter::Left, HandFilter::Right}) {
        for (PianoPlays p : {PianoPlays::OtherHand, PianoPlays::Left, PianoPlays::Right,
                             PianoPlays::Both, PianoPlays::Nothing}) {
            for (PracticeMode m :
                 {PracticeMode::FreePlay, PracticeMode::Wait, PracticeMode::Flow}) {
                SessionState e = s;
                e.practice_hands = h;
                e.piano_plays = p;
                e.mode = m;
                SettingsDocument d = SettingsDocument::parse("");
                write_session(d, e);
                CHECK(read_session(SettingsDocument::parse(d.serialize())) == e);
            }
        }
    }
}

TEST_CASE("app settings survive write then read") {
    AppSettings a;
    a.hit_window_ms = 120;
    a.latency_correction_ms = -35;
    a.midi_in_port = "PSR-E383 MIDI 1";
    a.midi_out_port = " Port with \"quotes\" # and = signs ";
    a.daily_goal_minutes = 0;
    a.resume_countdown_s = 2;
    a.skip_back_seconds = 12;
    a.skip_forward_seconds = 7;
    a.bar_style = BarStyle::Groups;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    std::vector<SettingsIssue> issues;
    CHECK(read_app_settings(SettingsDocument::parse(doc.serialize()), &issues) == a);
    CHECK(issues.empty());
}

TEST_CASE("missing sections and keys keep the defaults without any issue") {
    std::vector<SettingsIssue> issues;
    CHECK(read("", &issues) == SessionState{});
    CHECK(read_app("format = 1.0\n", &issues) == AppSettings{});
    CHECK(read("[session]\nspeed_percent = 80\n", &issues).speed_percent == 80);
    CHECK(read("[session]\nspeed_percent = 80\n", &issues).mode == PracticeMode::FreePlay);
    CHECK(issues.empty());
}

TEST_CASE("a value that does not parse keeps the default and is reported") {
    std::vector<SettingsIssue> issues;
    SessionState s = read("[session]\n"
                          "speed_percent = fast\n"
                          "position_s = 1.5x\n"
                          "loop_enabled = maybe\n"
                          "hands = both hands\n"
                          "song_hash = xyz\n"
                          "count_in_bars = 99999999999999999999\n"
                          "song = \"unterminated\n"
                          "position_s = nan\n"
                          "transpose_semitones = 3\n",
                          &issues);
    CHECK(s.speed_percent == 100);
    CHECK(s.position_s == 0.0);
    CHECK_FALSE(s.loop_enabled);
    CHECK(s.practice_hands == HandFilter::Both);
    CHECK(s.song_hash == 0);
    CHECK(s.count_in_bars == 1);
    CHECK(s.song_path.empty());
    CHECK(s.transpose_semitones == 3); // good lines still count
    for (const char* key : {"speed_percent", "position_s", "loop_enabled", "hands", "song_hash",
                            "count_in_bars", "song"}) {
        CAPTURE(key);
        CHECK(has_issue(issues, Kind::BadValue, key));
    }
}

TEST_CASE("a value outside its range is clamped and reported") {
    std::vector<SettingsIssue> issues;
    SessionState s =
        read("[session]\ntranspose_semitones = 30\nspeed_percent = 7\nposition_s = -4\n", &issues);
    CHECK(s.transpose_semitones == 12);
    CHECK(s.speed_percent == 25);
    CHECK(s.position_s == 0.0);
    CHECK(has_issue(issues, Kind::OutOfRange, "transpose_semitones"));
    CHECK(has_issue(issues, Kind::OutOfRange, "speed_percent"));
    CHECK(has_issue(issues, Kind::OutOfRange, "position_s"));

    issues.clear();
    AppSettings a = read_app("[app]\nhit_window_ms = 5000\n", &issues);
    CHECK(a.hit_window_ms == 250);
    CHECK(has_issue(issues, Kind::OutOfRange, "hit_window_ms"));
}

TEST_CASE("a speed between the steps is kept, one out of range is clamped and reported") {
    std::vector<SettingsIssue> issues;
    CHECK(read("[session]\nspeed_percent = 62\n", &issues).speed_percent == 62);
    CHECK(issues.empty());
    CHECK(read("[session]\nspeed_percent = 260\n", &issues).speed_percent == 200);
    CHECK(has_issue(issues, Kind::OutOfRange, "speed_percent"));
}

TEST_CASE("fields that clash with each other are fixed and the clash is reported") {
    std::vector<SettingsIssue> issues;
    SessionState s =
        read("[session]\nsong = a.mid\nloop_start_s = 20\nloop_end_s = 10\nloop_enabled = true\n"
             "keyboard = manual\nkeyboard_lowest = 60\nkeyboard_highest = 62\n",
             &issues);
    CHECK(s.loop_start_s == 10.0);
    CHECK(s.loop_end_s == 20.0);
    CHECK(s.keyboard == KeyboardChoice::Auto);
    CHECK(has_issue(issues, Kind::OutOfRange));
}

TEST_CASE("an absolute or climbing song path in a file is refused") {
    std::vector<SettingsIssue> issues;
    SessionState s =
        read("[session]\nsong = ../../etc/hosts\nsong_hash = 00000000000000ff\n", &issues);
    CHECK(s.song_path.empty());
    CHECK(s.song_hash == 0);
    CHECK(has_issue(issues, Kind::OutOfRange, "song"));
}

TEST_CASE("a repeated key reports DuplicateKey and the last one counts") {
    std::vector<SettingsIssue> issues;
    SessionState s = read("[session]\nspeed_percent = 80\nspeed_percent = 90\n", &issues);
    CHECK(s.speed_percent == 90);
    CHECK(has_issue(issues, Kind::DuplicateKey, "speed_percent"));
}

TEST_CASE("unknown keys and malformed lines are reported and survive a save") {
    const std::string text = "format = 1.0\n"
                             "[session]\n"
                             "speed_percent = 80\n"
                             "future_key = something   odd\n"
                             "# a comment\n"
                             "garbage line\n";
    std::vector<SettingsIssue> issues;
    SettingsDocument doc = SettingsDocument::parse(text);
    SessionState s = read_session(doc, &issues);
    CHECK(s.speed_percent == 80);
    CHECK(has_issue(issues, Kind::UnknownKey, "future_key"));
    CHECK(has_issue(issues, Kind::MalformedLine));
    for (const SettingsIssue& i : issues) {
        if (i.kind == Kind::UnknownKey) {
            CHECK(i.section == "session");
            CHECK(i.line == 4);
        }
    }
    s.speed_percent = 85;
    write_session(doc, s);
    std::string out = doc.serialize();
    CHECK(out.find("future_key = something   odd\n") != std::string::npos);
    CHECK(out.find("# a comment\n") != std::string::npos);
    CHECK(out.find("garbage line\n") != std::string::npos);
    CHECK(out.find("speed_percent = 85\n") != std::string::npos);
}

TEST_CASE("reading a document and writing the same values back changes no byte") {
    const std::string text = "# my settings\r\n"
                             "format=1.0\r\n"
                             "\r\n"
                             "[app]\r\n"
                             "hit_window_ms   =   80\r\n"
                             "\r\n"
                             "[session]\r\n"
                             "speed_percent=100\r\n"
                             "song = Songs/a.mid\r\n"
                             "future = 1\r\n";
    LoadedSettings loaded = load_settings(text);
    CHECK(loaded.serialize().find("hit_window_ms   =   80\r\n") != std::string::npos);
    SettingsDocument doc = SettingsDocument::parse(text);
    write_app_settings(doc, read_app_settings(doc));
    write_session(doc, read_session(doc));
    // Only keys that were missing are added; the lines that were there stay as typed.
    std::string out = doc.serialize();
    CHECK(out.rfind("# my settings\r\nformat=1.0\r\n\r\n[app]\r\nhit_window_ms   =   80\r\n", 0) ==
          0);
    CHECK(out.find("speed_percent=100\r\nsong = Songs/a.mid\r\nfuture = 1\r\n") !=
          std::string::npos);
}

TEST_CASE("load_settings reads both sections and reports sections nobody owns") {
    const std::string text = "format = 1.0\n"
                             "[app]\nhit_window_ms = 100\n"
                             "[session]\nspeed_percent = 50\n"
                             "[mystery]\nx = 1\n";
    LoadedSettings loaded = load_settings(text);
    CHECK(loaded.app.hit_window_ms == 100);
    CHECK(loaded.session.speed_percent == 50);
    CHECK_FALSE(loaded.read_only_recommended());
    int unknown = 0;
    for (const SettingsIssue& i : loaded.issues) {
        if (i.kind == Kind::UnknownSection) {
            ++unknown;
            CHECK(i.section == "mystery");
            CHECK(i.line == 6);
        }
    }
    CHECK(unknown == 1);
}

TEST_CASE("sections owned by another reader are not reported, and every unknown one survives") {
    const std::string future =
        "[from_the_future]\n# written by a newer build\nweird   =  1\n\nno equals\n";
    const std::string look = "[look]\nversion = 1\nstyle = neon\n";
    const std::string text =
        "format = 1.0\n" + look + "\n" + future + "\n[session]\nspeed_percent = 50\n";
    const std::array<std::string_view, 2> owned{"look", "user_presets"};
    LoadedSettings loaded = load_settings(text, owned);
    for (const SettingsIssue& i : loaded.issues) {
        CHECK(i.section != "look");
    }
    CHECK(loaded.document.section_text("look") == "version = 1\nstyle = neon\n");

    loaded.session.speed_percent = 70;
    loaded.app.hit_window_ms = 90;
    loaded.document.set_section_text("look", "version = 1\nstyle = glass\n");
    std::string out = loaded.serialize();
    CHECK(out.find(future) != std::string::npos); // byte for byte
    CHECK(out.find("[look]\nversion = 1\nstyle = glass\n") != std::string::npos);
    CHECK(out.find("speed_percent = 70\n") != std::string::npos);
    CHECK(out.find("[app]\nhit_window_ms = 90\n") != std::string::npos);

    // And the result loads again to the same values.
    LoadedSettings again = load_settings(out, owned);
    CHECK(again.session.speed_percent == 70);
    CHECK(again.app.hit_window_ms == 90);
    CHECK(again.serialize() == out);
}

TEST_CASE("a newer major version loads what it can and recommends read-only") {
    LoadedSettings loaded = load_settings(
        "format = 2.0\n[app]\nhit_window_ms = 100\nnew_thing = 1\n[session]\nspeed_percent = 50\n");
    CHECK(loaded.read_only_recommended());
    CHECK(loaded.app.hit_window_ms == 100);
    CHECK(loaded.session.speed_percent == 50);
    CHECK(has_issue(loaded.issues, Kind::NewerVersion));
}

TEST_CASE(
    "a hash, enum and bool are read in the forms the writer produces and a few friendlier ones") {
    SessionState s = read("[session]\nsong = a.mid\nsong_hash = ABC\nloop_enabled = yes\n"
                          "hands = left\nmode = flow\npiano_plays = nothing\nkeyboard = manual\n");
    CHECK(s.song_hash == 0xabc);
    CHECK(s.mode == PracticeMode::Flow);
    CHECK(s.practice_hands == HandFilter::Left);
    CHECK(s.piano_plays == PianoPlays::Nothing);
    CHECK(s.keyboard == KeyboardChoice::Manual);
}

TEST_CASE("a song path with quotes, blanks and hashes round trips through the file") {
    for (const char* name :
         {"  lead.mid", "trail.mid  ", "\"quoted\".mid", "a # b = c.mid", "dir/\xE6\x9B\xB2.mid"}) {
        CAPTURE(name);
        SessionState s;
        s.song_path = name;
        s.song_hash = 9;
        SettingsDocument doc = SettingsDocument::parse("");
        write_session(doc, s);
        CHECK(read_session(SettingsDocument::parse(doc.serialize())) == s);
    }
}

TEST_CASE("a default LoadedSettings can be edited and saved") {
    LoadedSettings loaded;
    loaded.session.speed_percent = 80;
    std::string text = loaded.serialize();
    CHECK(text.rfind("# etude settings\nformat = 1.0\n", 0) == 0);
    CHECK(load_settings(text).session.speed_percent == 80);
}

TEST_CASE("the absolute fallback path round-trips next to the library path") {
    SessionState s;
    s.song_path = "Songs/a.mid";
    s.song_abs_path = "/home/me/My Music/a # b.mid";
    s.song_hash = 9;
    SettingsDocument doc = SettingsDocument::parse("");
    write_session(doc, s);
    CHECK(read_session(SettingsDocument::parse(doc.serialize())) == s);

    // A song from outside the library has only the absolute path, and it is a saved song.
    SessionState outside;
    outside.song_abs_path = "C:/Songs/x.mid";
    outside.song_hash = 5;
    outside.position_s = 7.0;
    CHECK(outside.has_song());
    CHECK(outside.clamped() == outside);
    SettingsDocument doc2 = SettingsDocument::parse("");
    write_session(doc2, outside);
    CHECK(read_session(SettingsDocument::parse(doc2.serialize())) == outside);
}

TEST_CASE("an absolute song path that is relative, climbs out or is not text is dropped") {
    for (const char* bad : {"rel/a.mid", "a.mid", "/x/../etc/passwd", "C:x.mid", "/a\nb.mid"}) {
        CAPTURE(bad);
        SessionState s;
        s.song_path = "a.mid";
        s.song_abs_path = bad;
        CHECK(s.clamped().song_abs_path.empty());
        CHECK(s.clamped().song_path == "a.mid");
    }
    SessionState gone;
    gone.song_abs_path = "relative.mid";
    gone.song_hash = 4;
    gone.position_s = 3.0;
    CHECK_FALSE(gone.clamped().has_song());
    CHECK(gone.clamped().position_s == 0.0); // no song left: the song fields reset
    SessionState long_path;
    long_path.song_abs_path = "/" + std::string(kMaxSongPathBytes, 'a');
    CHECK(long_path.clamped().song_abs_path.empty());

    std::vector<SettingsIssue> issues;
    const SessionState read = read_session(
        SettingsDocument::parse("[session]\nsong = \"a.mid\"\nsong_abs = \"rel/a.mid\"\n"),
        &issues);
    CHECK(read.song_abs_path.empty());
    CHECK(read.song_path == "a.mid");
    CHECK_FALSE(issues.empty());
}
