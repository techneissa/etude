// SPDX-License-Identifier: GPL-3.0-or-later
// The resume point (core): written and read whole, every start resumes, the 10 s guard, an old or damaged
// file ignored, and when the running app writes one.
#include "etude/resume_point.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

// Where a song may be reopened from.
const std::vector<std::string> kRoots{"/home/pat/Music", "/data/Songs"};

ResumePoint a_point() {
    ResumePoint p;
    p.ran_ms = 123'456;
    p.clean = false;
    p.screen = "learn-chords";
    p.session.song_path = "Lessons/Für Elise.mid";
    p.session.song_abs_path = "/home/pat/Music/Für Elise.mid";
    p.session.song_hash = 0xFEDCBA9876543210ULL;
    p.session.position_s = 63.25;
    p.session.speed_percent = 75;
    p.session.practice_hands = HandFilter::Left;
    p.session.piano_plays = PianoPlays::Right;
    p.session.loop_start_s = 12.5;
    p.session.loop_end_s = 20.0;
    p.session.loop_enabled = true;
    p.session.section_id = 7;
    p.session.plan_item = 3;
    p.session.count_in_bars = 2;
    p.session.transpose_semitones = -3;
    return p;
}

ResumeParse parsed(const ResumePoint& p) { return parse_resume_point(format_resume_point(p)); }

} // namespace

TEST_CASE("resume point: written and read whole, every field") {
    const ResumePoint p = a_point();
    const std::string text = format_resume_point(p);
    CHECK(text.size() <= kMaxResumeBytes);
    CHECK(text.rfind("etude-resume 2\n", 0) == 0);
    CHECK(text.substr(text.size() - 4) == "end\n");
    const ResumeParse r = parse_resume_point(text);
    REQUIRE(r.ok);
    CHECK(r.point.ran_ms == p.ran_ms);
    CHECK(r.point.clean == p.clean);
    CHECK(r.point.screen == "learn-chords");
    CHECK(r.point.session.song_path == p.session.song_path);
    CHECK(r.point.session.song_abs_path == p.session.song_abs_path);
    CHECK(r.point.session.song_hash == p.session.song_hash);
    CHECK(r.point.session.position_s == doctest::Approx(63.25));
    CHECK(r.point.session.speed_percent == 75);
    CHECK(r.point.session.practice_hands == HandFilter::Left);
    CHECK(r.point.session.piano_plays == PianoPlays::Right);
    CHECK(r.point.session.loop_start_s == doctest::Approx(12.5));
    CHECK(r.point.session.loop_end_s == doctest::Approx(20.0));
    CHECK(r.point.session.loop_enabled);
    CHECK(r.point.session.section_id == 7);
    CHECK(r.point.session.plan_item == 3);
    CHECK(r.point.session.count_in_bars == 2);
    CHECK(r.point.session.transpose_semitones == -3);
    // And a second round gives the same text: nothing drifts.
    CHECK(format_resume_point(r.point) == text);
}

TEST_CASE("resume point: free play, a clean close and every screen survive") {
    ResumePoint p;
    p.clean = true;
    CHECK(parsed(p).ok);
    CHECK(parsed(p).point.clean);
    CHECK(parsed(p).point.screen == "play");
    CHECK_FALSE(parsed(p).point.session.has_song());
    for (const char* screen : {"play", "library", "learn", "learn-scales", "learn-chords", "learn-ear",
                               "learn-melodies", "notes", "effects", "view", "practice", "input", "presets",
                               "add-songs", "about"}) {
        CAPTURE(screen);
        p.screen = screen;
        REQUIRE(parsed(p).ok);
        CHECK(parsed(p).point.screen == screen);
        CHECK(resume_screen_known(screen));
    }
}

TEST_CASE("resume point: a damaged file is damage, not a guess") {
    const std::string good = format_resume_point(a_point());
    CHECK_FALSE(parse_resume_point("").ok);
    CHECK_FALSE(parse_resume_point("junk\n").ok);
    CHECK_FALSE(parse_resume_point(good.substr(0, good.size() / 2)).ok);             // cut in the middle
    CHECK_FALSE(parse_resume_point(good.substr(0, good.size() - 4)).ok);              // the end line gone
    CHECK_FALSE(parse_resume_point(good.substr(0, good.size() - 1)).ok);              // no final newline
    CHECK_FALSE(parse_resume_point(good + "speed=100\n").ok);                         // text after the end
    CHECK_FALSE(parse_resume_point(std::string(kMaxResumeBytes + 1, 'x')).ok);
    const auto with = [&](const std::string& from, const std::string& to) {
        std::string text = good;
        text.replace(text.find(from), from.size(), to);
        return parse_resume_point(text);
    };
    CHECK_FALSE(with("speed=75", "speed=900").ok);
    CHECK_FALSE(with("speed=75", "speed=fast").ok);
    CHECK_FALSE(with("hands=left", "hands=none").ok);
    CHECK_FALSE(with("plays=right", "plays=loud").ok);
    CHECK_FALSE(with("clean=0", "clean=2").ok);
    CHECK_FALSE(with("screen=learn-chords", "screen=medal-sheet").ok); // a screen that is not the player's
    CHECK_FALSE(with("transpose=-3", "transpose=40").ok);
    CHECK_FALSE(with("position_ms=63250", "position_ms=-5").ok);
    CHECK_FALSE(with("position_ms=63250", "position_ms=999999999999").ok);
    CHECK_FALSE(with("section=7", "section=-1").ok);
    CHECK_FALSE(with("count_in=2", "count_in=3").ok);
    CHECK_FALSE(with("hash=", "hash=zz").ok);
    CHECK_FALSE(with("end\n", "bonus=1\nend\n").ok); // an unknown key
    CHECK_FALSE(with("song=Lessons", "song=Les\tsons").ok); // a control character in a path
    CHECK(parse_resume_point(good).error.empty());
    CHECK_FALSE(with("speed=75", "speed=900").error.empty());
}

TEST_CASE("resume point: a file of another version is ignored") {
    std::string text = format_resume_point(a_point());
    text.replace(text.find("etude-resume 2"), 16, "etude-resume 3");
    const ResumeParse r = parse_resume_point(text);
    CHECK_FALSE(r.ok);
    CHECK(plan_resume(&r, kRoots) == ResumeAction::Fresh);
    std::string older = format_resume_point(a_point());
    older.replace(older.find("etude-resume 2"), 16, "etude-resume 1");
    const ResumeParse zero = parse_resume_point(older);
    CHECK(plan_resume(&zero, kRoots) == ResumeAction::Fresh);
}

TEST_CASE("resume plan: every start resumes, after a crash or a normal close, at any time later") {
    ResumePoint p = a_point();
    p.ran_ms = 600'000; // ran ten minutes
    for (const bool clean : {false, true}) {
        p.clean = clean;
        const ResumeParse r = parsed(p);
        CHECK(plan_resume(&r, kRoots) == ResumeAction::Resume);
    }
    // A clean close of a short run (the player opened it and shut it at once) resumes too.
    p.clean = true;
    p.ran_ms = 2000;
    const ResumeParse brief = parsed(p);
    CHECK(plan_resume(&brief, kRoots) == ResumeAction::Resume);
}

TEST_CASE("resume plan: a crash within 10 s of the start opens the song list; later it resumes") {
    ResumePoint p = a_point();
    p.clean = false;
    for (const std::int64_t ran : {0, 1000, 5000, 9999}) {
        CAPTURE(ran);
        p.ran_ms = ran;
        const ResumeParse r = parsed(p);
        CHECK(plan_resume(&r, kRoots) == ResumeAction::OpenSongList);
    }
    for (const std::int64_t ran : {10000, 11000, 3'600'000}) {
        CAPTURE(ran);
        p.ran_ms = ran;
        const ResumeParse r = parsed(p);
        CHECK(plan_resume(&r, kRoots) == ResumeAction::Resume);
    }
}

TEST_CASE("resume plan: no file, or a damaged one, starts as the settings say") {
    CHECK(plan_resume(nullptr, kRoots) == ResumeAction::Fresh);
    const ResumeParse damaged = parse_resume_point("etude-resume 2\nspeed=oops\nend\n");
    CHECK(plan_resume(&damaged, kRoots) == ResumeAction::Fresh);
}

TEST_CASE("resume session: the song-bound part comes from the point, the player's and the device's stay") {
    SessionState settings;
    settings.song_path = "other.mid";
    settings.speed_percent = 100;
    settings.mode = PracticeMode::Flow;
    settings.auto_speed_enabled = true;
    settings.auto_speed_step_percent = 10;
    settings.keyboard = KeyboardChoice::Profile;
    settings.keyboard_name = "Yamaha PSR-E383";
    const SessionState s = session_with_resume(settings, a_point());
    CHECK(s.song_path == "Lessons/Für Elise.mid");
    CHECK(s.position_s == doctest::Approx(63.25));
    CHECK(s.speed_percent == 75);
    CHECK(s.practice_hands == HandFilter::Left);
    CHECK(s.section_id == 7);
    CHECK(s.mode == PracticeMode::Flow);
    CHECK(s.auto_speed_enabled);
    CHECK(s.auto_speed_step_percent == 10);
    CHECK(s.keyboard == KeyboardChoice::Profile);
    CHECK(s.keyboard_name == "Yamaha PSR-E383");
}

TEST_CASE("resume session: the guard takes the song and what belongs to it, and keeps the settings") {
    SessionState settings = session_with_resume({}, a_point());
    settings.mode = PracticeMode::Wait;
    settings.auto_speed_enabled = true;
    const SessionState s = session_without_song(settings);
    CHECK_FALSE(s.has_song());
    CHECK(s.song_hash == 0);
    CHECK(s.position_s == 0.0);
    CHECK(s.speed_percent == 100);
    CHECK(s.practice_hands == HandFilter::Both);
    CHECK_FALSE(s.loop_enabled);
    CHECK(s.section_id == 0);
    CHECK(s.plan_item == 0);
    CHECK(s.transpose_semitones == 0);
    CHECK(s.mode == PracticeMode::Wait);
    CHECK(s.auto_speed_enabled);
}

TEST_CASE("resume pacer: at once at the start, at every change of screen, and every few seconds while it changes") {
    ResumePacer pacer;
    CHECK(pacer.due(0, false, false));      // the start writes
    CHECK_FALSE(pacer.due(100, true, false)); // not again a moment later
    CHECK(pacer.due(400, false, true));     // a screen change writes (past the gap)
    CHECK_FALSE(pacer.due(450, false, true)); // but a screen that flips every frame does not write every frame
    // The first 15 s beat every 2 s, changed or not: the point says the run lasted past the guard.
    CHECK_FALSE(pacer.due(1500, false, false));
    CHECK(pacer.due(2400, false, false));
    CHECK(pacer.due(4400, false, false));
    CHECK(pacer.due(14300, false, false));
    // After that only a change writes, and not more often than every 3 s.
    CHECK_FALSE(pacer.due(16400, false, false));
    CHECK(pacer.due(17500, true, false));
    CHECK_FALSE(pacer.due(19000, true, false));
    CHECK(pacer.due(20600, true, false));
    CHECK_FALSE(pacer.due(40000, false, false)); // nothing changed for a long time: nothing written
    CHECK(pacer.due(40100, true, true));
}

TEST_CASE("resume plan: a song is reopened only from the library or the data folder, by a path with no `..`") {
    ResumePoint p = a_point();
    p.clean = true;
    p.ran_ms = 60'000;
    const auto plan = [&](const char* relative, const char* absolute) {
        p.session.song_path = relative;
        p.session.song_abs_path = absolute;
        const ResumeParse r = parsed(p);
        REQUIRE(r.ok);
        return plan_resume(&r, kRoots);
    };
    CHECK(plan("Lessons/a.mid", "") == ResumeAction::Resume);
    CHECK(plan("", "/home/pat/Music/Lessons/a.mid") == ResumeAction::Resume);
    CHECK(plan("Lessons/a.mid", "/data/Songs/Lessons/a.mid") == ResumeAction::Resume);
    CHECK(plan("", "") == ResumeAction::Resume); // no song: nothing to refuse
    // Outside the folders, beside them, a root itself, and every way to climb out.
    CHECK(plan("", "/etc/shadow") == ResumeAction::SongRefused);
    CHECK(plan("", "/home/pat/Music") == ResumeAction::SongRefused);
    CHECK(plan("", "/home/pat/MusicBox/a.mid") == ResumeAction::SongRefused);
    CHECK(plan("", "/home/pat/Music/../../../etc/passwd") == ResumeAction::SongRefused);
    CHECK(plan("", "/home/pat/Music/./a.mid") == ResumeAction::SongRefused);
    CHECK(plan("", "home/pat/Music/a.mid") == ResumeAction::SongRefused);   // not absolute
    CHECK(plan("../a.mid", "") == ResumeAction::SongRefused);
    CHECK(plan("Lessons/../../a.mid", "") == ResumeAction::SongRefused);
    CHECK(plan("/etc/passwd", "") == ResumeAction::SongRefused);             // absolute where a relative one belongs
    CHECK(plan("Lessons\\a.mid", "") == ResumeAction::SongRefused);
    // One good path does not excuse a bad one.
    CHECK(plan("Lessons/a.mid", "/etc/shadow") == ResumeAction::SongRefused);
    CHECK(plan("../a.mid", "/home/pat/Music/a.mid") == ResumeAction::SongRefused);
    // No folders known: no absolute path is let through.
    p.session.song_path.clear();
    p.session.song_abs_path = "/home/pat/Music/a.mid";
    const ResumeParse r = parsed(p);
    CHECK(plan_resume(&r, {}) == ResumeAction::SongRefused);
    // The guard still comes first.
    p.clean = false;
    p.ran_ms = 2000;
    p.session.song_abs_path = "/etc/shadow";
    const ResumeParse quick = parsed(p);
    CHECK(plan_resume(&quick, kRoots) == ResumeAction::OpenSongList);
}
