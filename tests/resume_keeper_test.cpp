// SPDX-License-Identifier: GPL-3.0-or-later
// The resume point on disk: written whole by a thread of its own, read back whole, every start picking it
// up, the guard after a crash within 10 s, and an old, damaged or planted file ignored.
#include "resume_keeper.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

// Where a song may be reopened from.
const std::vector<std::string> kRoots{"/home/pat/Music", "/data/Songs"};

ResumePoint at_bar(double seconds, const char* screen = "play") {
    ResumePoint p;
    p.screen = screen;
    p.session.song_path = "Lessons/a.mid";
    p.session.song_hash = 42;
    p.session.position_s = seconds;
    p.session.speed_percent = 80;
    p.session.practice_hands = HandFilter::Right;
    p.session.section_id = 2;
    return p;
}

// What a start would do with the file as it stands.
ResumeAction plan_of(const fs::path& file) {
    const std::optional<ResumeParse> r = read_resume_file(file);
    return plan_resume(r ? &*r : nullptr, kRoots);
}

ResumeParse read_whole(const fs::path& file) {
    const std::optional<ResumeParse> r = read_resume_file(file);
    REQUIRE(r.has_value());
    return *r;
}

} // namespace

TEST_CASE("resume keeper: the start writes at once, unclean; the point is read back whole, with how long the run had run") {
    TempDir t;
    const fs::path file = t.path / kResumeFile;
    {
        ResumeKeeper keeper;
        keeper.begin(file, true, at_bar(12.5, "learn-ear"), 7000);
        // The thread writes it; close() waits for what is queued.
        keeper.close(at_bar(12.5, "learn-ear").session, "learn-ear", 10000);
    }
    const ResumeParse r = read_whole(file);
    REQUIRE(r.ok);
    CHECK(r.point.ran_ms == 3000); // by the steady clock handed in, whatever the date is
    CHECK(r.point.clean);
    CHECK(r.point.screen == "learn-ear");
    CHECK(r.point.session.position_s == doctest::Approx(12.5));
    CHECK(r.point.session.practice_hands == HandFilter::Right);
    CHECK_FALSE(fs::exists(file.string() + ".part")); // no half-written copy is left
}

TEST_CASE("resume keeper: the first write is on disk before the run has done anything, unclean") {
    TempDir t;
    const fs::path file = t.path / kResumeFile;
    ResumeKeeper keeper;
    keeper.begin(file, true, at_bar(3.0), 500);
    for (int i = 0; i < 200 && !fs::exists(file); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const ResumeParse r = read_whole(file);
    REQUIRE(r.ok);
    CHECK_FALSE(r.point.clean);
    CHECK(r.point.ran_ms == 0);
    // A crash now (the keeper is never closed) leaves a point the next start guards against.
    CHECK(plan_resume(&r, kRoots) == ResumeAction::OpenSongList);
}

TEST_CASE("resume keeper: changes are written every few seconds and a change of screen at once, never when nothing changed late") {
    TempDir t;
    const fs::path file = t.path / kResumeFile;
    ResumeKeeper keeper;
    keeper.begin(file, true, at_bar(0.0), 0);
    const auto settle = [&](const auto& want) {
        for (int i = 0; i < 300; ++i) {
            const std::optional<ResumeParse> r = read_resume_file(file);
            if (r && r->ok && want(r->point)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    // Past the beat (15 s): only a change writes.
    keeper.note(at_bar(20.0).session, "play", 20000);
    CHECK(settle([](const ResumePoint& p) { return p.ran_ms == 20000 && p.session.position_s == 20.0; }));
    keeper.note(at_bar(21.0).session, "play", 21000); // less than 3 s later: not yet
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(read_whole(file).point.session.position_s == doctest::Approx(20.0));
    keeper.note(at_bar(24.0).session, "play", 24000);
    CHECK(settle([](const ResumePoint& p) { return p.session.position_s == 24.0; }));
    keeper.note(at_bar(24.0).session, "library", 24300); // a change of screen: at once
    CHECK(settle([](const ResumePoint& p) { return p.screen == "library"; }));
    keeper.note(at_bar(24.0).session, "library", 60000); // nothing changed: nothing written
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(read_whole(file).point.ran_ms == 24300);
    keeper.close(at_bar(24.0).session, "library", 61000);
    CHECK(read_whole(file).point.clean);
}

TEST_CASE("resume keeper: every start picks up the last point, a crash or a close, and the guard needs both a crash and 10 s") {
    TempDir t;
    const fs::path file = t.path / kResumeFile;
    // Run 1 plays for a minute and crashes (never closed): the next start resumes it.
    {
        ResumeKeeper keeper;
        keeper.begin(file, true, at_bar(0.0), 0);
        keeper.note(at_bar(50.0, "library").session, "library", 20000);
        keeper.note(at_bar(61.0, "library").session, "library", 30000);
        // Closing the keeper's thread without a clean point is a crash for the reader's purposes: the
        // destructor writes nothing more.
    }
    std::optional<ResumeParse> read = read_resume_file(file);
    REQUIRE(read.has_value());
    REQUIRE(read->ok);
    CHECK_FALSE(read->point.clean);
    CHECK(plan_resume(&*read, kRoots) == ResumeAction::Resume);
    // Run 2 resumes it, writes its own start at once, and crashes 4 s later: the guard.
    {
        ResumeKeeper keeper;
        keeper.begin(file, true, read->point, 0);
        keeper.note(read->point.session, read->point.screen, 4000);
    }
    read = read_resume_file(file);
    REQUIRE(read.has_value());
    REQUIRE(read->ok);
    CHECK(plan_resume(&*read, kRoots) == ResumeAction::OpenSongList);
    // Run 3 (the song list, the guard's start) closes normally after 2 s: the next start resumes again.
    {
        ResumeKeeper keeper;
        ResumePoint start = read->point;
        start.screen = "library";
        keeper.begin(file, true, start, 0);
        keeper.close(start.session, "library", 2000);
    }
    read = read_resume_file(file);
    REQUIRE(read.has_value());
    CHECK(plan_resume(&*read, kRoots) == ResumeAction::Resume);
    CHECK(read->point.screen == "library");
}

TEST_CASE("resume keeper: no file, an old or damaged one, a link, a folder and a huge one are ignored") {
    TempDir t;
    const fs::path file = t.path / kResumeFile;
    CHECK_FALSE(read_resume_file(file).has_value()); // none: start as the settings say
    write_file(file, "etude-resume 0\nend\n");      // an older format
    CHECK(plan_of(file) == ResumeAction::Fresh);
    write_file(file, "etude-resume 2\nspeed=\n");   // damaged
    write_file(file, "etude-resume 1\nstarted=1\nwritten=2\nclean=0\nscreen=play\nend\n"); // the format before the steady clock
    CHECK(plan_of(file) == ResumeAction::Fresh);
    write_file(file, std::string(10000, 'x'));        // too big
    CHECK(plan_of(file) == ResumeAction::Fresh);
    fs::remove(file);
    fs::create_directories(file); // a folder where the file belongs
    CHECK(plan_of(file) == ResumeAction::Fresh);
    fs::remove(file);
    write_file(t.path / "elsewhere", format_resume_point(at_bar(5.0)));
    fs::create_symlink(t.path / "elsewhere", file); // a link is never followed
    CHECK(plan_of(file) == ResumeAction::Fresh);
}

TEST_CASE("resume keeper: a developer run keeps nothing, and a write that cannot happen leaves the old file") {
    TempDir t;
    const fs::path file = t.path / kResumeFile;
    {
        ResumeKeeper off;
        off.begin(file, false, at_bar(1.0), 0);
        off.note(at_bar(2.0).session, "play", 100000);
        off.close(at_bar(2.0).session, "play", 101000);
        CHECK_FALSE(off.active());
    }
    CHECK_FALSE(fs::exists(file));
    write_file(file, format_resume_point(at_bar(7.0)));
    {
        ResumeKeeper keeper;
        write_file(t.path / "blocked", "a file where the folder should be");
        keeper.begin(t.path / "blocked" / kResumeFile, true, at_bar(1.0), 0); // cannot be written
        keeper.close(at_bar(2.0).session, "play", 2000);
    }
    CHECK(read_whole(file).point.session.position_s == doctest::Approx(7.0)); // the other file is untouched
    CHECK(read_file(t.path / "blocked") == "a file where the folder should be");
}

TEST_CASE("resume keeper: how long a run ran is its own steady clock's, so a jump of the date between two runs cannot make a crash look late or early") {
    TempDir t;
    const fs::path file = t.path / kResumeFile;
    // The steady clock's origin is whatever it is (a Pi's is its boot); only differences count. A run 4 s
    // long on it is within the guard, one of 40 s is not, whatever the wall clock did in between (it is
    // never read: the keeper takes no date at all).
    for (const std::int64_t length : {4000, 40000}) {
        CAPTURE(length);
        {
            ResumeKeeper keeper;
            keeper.begin(file, true, at_bar(1.0), 987'654'321);
            keeper.note(at_bar(2.0).session, "play", 987'654'321 + length);
        } // never closed: a crash
        const std::optional<ResumeParse> r = read_resume_file(file);
        REQUIRE(r.has_value());
        REQUIRE(r->ok);
        CHECK(r->point.ran_ms == length);
        CHECK(plan_resume(&*r, kRoots) == (length < 10000 ? ResumeAction::OpenSongList : ResumeAction::Resume));
    }
    // A clock that reads earlier than the start (it cannot, but a bad caller) counts as no time at all.
    {
        ResumeKeeper keeper;
        keeper.begin(file, true, at_bar(1.0), 50'000);
        keeper.note(at_bar(2.0).session, "play", 2'000);
    }
    CHECK(read_whole(file).point.ran_ms == 0);
}

TEST_CASE("resume roots: a folder given with `.`, `..`, as a relative path or through a link still holds its songs") {
    TempDir t;
    const fs::path base = fs::canonical(t.path); // the temp folder itself may sit behind a link (macOS /var)
    const fs::path songs = base / "Songs";
    fs::create_directories(songs);
    fs::create_directories(base / "other");
    std::error_code ec;
    fs::create_symlink(songs, base / "link", ec);
    const std::string song = (songs / "a.mid").lexically_normal().generic_string(); // as a song's path is kept
    const auto plan = [&](const fs::path& folder, const std::string& abs) {
        ResumePoint p;
        p.clean = true;
        p.ran_ms = 60'000;
        p.session.song_abs_path = abs;
        const ResumeParse r = parse_resume_point(format_resume_point(p));
        REQUIRE(r.ok);
        return plan_resume(&r, resume_song_roots({folder}));
    };
    CHECK(plan(songs, song) == ResumeAction::Resume);
    CHECK(plan(base / "other" / ".." / "Songs", song) == ResumeAction::Resume);
    CHECK(plan(base / "." / "Songs" / ".", song) == ResumeAction::Resume);
    CHECK(plan(fs::relative(songs, fs::current_path(), ec), song) == ResumeAction::Resume);
    if (!ec && fs::is_symlink(base / "link")) {
        CHECK(plan(base / "link", song) == ResumeAction::Resume);
    }
    // What the folder does not hold stays refused, whatever the form it was given in.
    CHECK(plan(base / "other" / ".." / "Songs", (base / "other" / "a.mid").generic_string()) == ResumeAction::SongRefused);
    CHECK(plan(songs, (base / "Songsy" / "a.mid").generic_string()) == ResumeAction::SongRefused);
    CHECK(plan(songs, song + "/../../x.mid") == ResumeAction::SongRefused);
    CHECK(resume_song_roots({fs::path()}).empty()); // an empty folder names nothing
}
