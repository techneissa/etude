// SPDX-License-Identifier: GPL-3.0-or-later
// Copying songs from a stick into the Inbox, with a temporary folder as the stick: what arrives,
// what is skipped as already there, the stick pulled out in the middle, and that the stick is only
// ever read.
#include "../app/usb_import.hpp"
#include "../app/usb_scan.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <future>

using namespace etude::app;
using namespace upload_test;

namespace {

struct Rig {
    TempDir stick;
    TempDir songs;
    ImportJob job() const {
        ImportJob j;
        j.stick = stick.path;
        j.songs_folder = songs.path;
        for (const StickSong& s : scan_stick(stick.path).songs) {
            j.songs.push_back(s.path);
        }
        return j;
    }
};

std::string song(const std::string& tag) { return midi_bytes(tag); }

} // namespace

TEST_CASE("usb import: songs from the stick land in the Inbox, names kept") {
    Rig rig;
    write_file(rig.stick.path / "one.mid", song("1"));
    write_file(rig.stick.path / "Lessons/two.mid", song("2"));
    std::vector<std::string> seen;
    ImportHooks hooks;
    hooks.on_song = [&seen](std::size_t i, ui::SongMark mark) {
        seen.push_back(std::to_string(i) + (mark == ui::SongMark::None ? "start" : "done"));
    };
    const ImportResult r = import_songs(rig.job(), hooks);
    CHECK(r.end == ui::ImportEnd::Done);
    CHECK(r.copied == 2);
    CHECK(r.duplicates == 0);
    CHECK(r.failed == 0);
    CHECK(read_file(rig.songs.path / "Inbox/one.mid") == song("1"));
    CHECK(read_file(rig.songs.path / "Inbox/two.mid") == song("2")); // the Inbox is flat
    CHECK(seen == std::vector<std::string>{"0start", "0done", "1start", "1done"});
    CHECK(tree(rig.songs.path / "Inbox/.incoming").empty()); // no temporary file left
}

TEST_CASE("usb import: a song already in the library, by content, is skipped") {
    Rig rig;
    write_file(rig.stick.path / "same.mid", song("A"));
    write_file(rig.stick.path / "renamed.mid", song("B"));
    write_file(rig.stick.path / "new.mid", song("C"));
    // The library has A under another name in a collection, and B's size with other bytes.
    write_file(rig.songs.path / "Collection/old name.mid", song("A"));
    write_file(rig.songs.path / "Collection/other.mid", song("Z"));
    const ImportResult r = import_songs(rig.job(), {});
    CHECK(r.copied == 2);
    CHECK(r.duplicates == 1);
    CHECK(r.marks[2] == ui::SongMark::Duplicate); // same.mid: new.mid, renamed.mid, same.mid
    CHECK(tree(rig.songs.path / "Inbox") == ".incoming/\nnew.mid\nrenamed.mid\n");
}

TEST_CASE("usb import: the same song twice on the stick comes in once") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("Q"));
    write_file(rig.stick.path / "copy of a.mid", song("Q"));
    const ImportResult r = import_songs(rig.job(), {});
    CHECK(r.copied == 1);
    CHECK(r.duplicates == 1);
}

TEST_CASE("usb import: importing again copies nothing") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("1"));
    write_file(rig.stick.path / "b.mid", song("2"));
    CHECK(import_songs(rig.job(), {}).copied == 2);
    const ImportResult again = import_songs(rig.job(), {});
    CHECK(again.copied == 0);
    CHECK(again.duplicates == 2);
}

TEST_CASE("usb import: a taken name becomes name (2)") {
    Rig rig;
    write_file(rig.stick.path / "tune.mid", song("new"));
    write_file(rig.songs.path / "Inbox/tune.mid", song("old")); // another song, same name
    const ImportResult r = import_songs(rig.job(), {});
    CHECK(r.copied == 1);
    CHECK(read_file(rig.songs.path / "Inbox/tune.mid") == song("old"));
    CHECK(read_file(rig.songs.path / "Inbox/tune (2).mid") == song("new"));
}

TEST_CASE("usb import: a file that is not a song fails alone") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("1"));
    write_file(rig.stick.path / "b.mid", "this is not a midi file at all");
    write_file(rig.stick.path / "c.mid", song("3"));
    const ImportResult r = import_songs(rig.job(), {});
    CHECK(r.end == ui::ImportEnd::Done);
    CHECK(r.copied == 2);
    CHECK(r.failed == 1);
    CHECK(r.marks[1] == ui::SongMark::Failed);
    CHECK_FALSE(fs::exists(rig.songs.path / "Inbox/b.mid"));
}

TEST_CASE("usb import: the stick pulled out mid-copy stops cleanly") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("1"));
    write_file(rig.stick.path / "b.mid", song(std::string(40'000, 'b'))); // many chunks
    write_file(rig.stick.path / "c.mid", song("3"));
    ImportJob job = rig.job();
    job.chunk = 4096;
    int chunks = 0;
    ImportHooks hooks;
    hooks.on_chunk = [&](bool copying) {
        // In the pass that writes the Inbox (not the one that hashes): a.mid takes two reads (its
        // data, then the end), so the fourth read is b.mid's second chunk: pull the stick.
        if (copying && ++chunks == 4) {
            std::error_code ec;
            fs::remove_all(rig.stick.path, ec);
        }
    };
    const ImportResult r = import_songs(job, hooks);
    CHECK(r.end == ui::ImportEnd::Removed);
    CHECK(r.copied == 1);
    CHECK(r.marks[1] == ui::SongMark::None);
    CHECK(r.marks[2] == ui::SongMark::None);
    CHECK(read_file(rig.songs.path / "Inbox/a.mid") == song("1"));
    // Nothing half done is left: not b.mid, not a temporary file.
    CHECK(tree(rig.songs.path / "Inbox") == ".incoming/\na.mid\n");
}

TEST_CASE("usb import: stop ends the job and is not a removal") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("1"));
    write_file(rig.stick.path / "b.mid", song("2"));
    bool stop = false;
    ImportHooks hooks;
    hooks.stop = [&stop] { return stop; };
    hooks.on_song = [&stop](std::size_t, ui::SongMark mark) {
        stop = stop || mark != ui::SongMark::None;
    };
    const ImportResult r = import_songs(rig.job(), hooks);
    CHECK(r.end == ui::ImportEnd::None);
    CHECK(r.copied == 1);
    CHECK_FALSE(fs::exists(rig.songs.path / "Inbox/b.mid"));
}

TEST_CASE("usb import: the stick is only read") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("1"));
    write_file(rig.stick.path / "sub/b.mid", song("2"));
    const std::string before = tree(rig.stick.path);
    struct stat a{};
    struct stat sub{};
    REQUIRE(::stat((rig.stick.path / "a.mid").c_str(), &a) == 0);
    REQUIRE(::stat((rig.stick.path / "sub").c_str(), &sub) == 0);
    // A read-only stick: any attempt to write or make a file on it would fail the copy.
    fs::permissions(rig.stick.path / "sub", fs::perms::owner_read | fs::perms::owner_exec);
    fs::permissions(rig.stick.path, fs::perms::owner_read | fs::perms::owner_exec);
    fs::permissions(rig.stick.path / "a.mid", fs::perms::owner_read);
    const ImportResult r = import_songs(rig.job(), {});
    fs::permissions(rig.stick.path, fs::perms::owner_all);
    fs::permissions(rig.stick.path / "sub", fs::perms::owner_all);
    CHECK(r.copied == 2);
    CHECK(tree(rig.stick.path) == before);
    struct stat a2{};
    REQUIRE(::stat((rig.stick.path / "a.mid").c_str(), &a2) == 0);
    CHECK(a2.st_mtime == a.st_mtime);
    CHECK(read_file(rig.stick.path / "a.mid") == song("1"));
}

TEST_CASE("usb import: a link on the stick is not followed") {
    Rig rig;
    TempDir secret;
    write_file(secret.path / "secret.mid", song("S"));
    fs::create_symlink(secret.path / "secret.mid", rig.stick.path / "linked.mid");
    ImportJob job;
    job.stick = rig.stick.path;
    job.songs_folder = rig.songs.path;
    job.songs = {"linked.mid"};
    const ImportResult r = import_songs(job, {});
    CHECK(r.end == ui::ImportEnd::Done); // the stick is there: the song alone failed
    CHECK(r.copied == 0);
    CHECK(r.failed == 1);
    CHECK_FALSE(fs::exists(rig.songs.path / "Inbox/linked.mid"));
}

TEST_CASE("usb import: a folder swapped for a link after the scan does not lead out of the stick") {
    Rig rig;
    TempDir outside;
    write_file(outside.path / "secret.mid", song("S"));
    write_file(rig.stick.path / "Lessons/secret.mid", song("L"));
    write_file(rig.stick.path / "a/b/deep.mid", song("D"));
    const ImportJob job = rig.job(); // scanned: Lessons/secret.mid, a/b/deep.mid
    fs::remove_all(rig.stick.path / "Lessons");
    fs::create_symlink(outside.path, rig.stick.path / "Lessons"); // a link now, same name
    fs::remove_all(rig.stick.path / "a/b");
    fs::create_symlink(outside.path, rig.stick.path / "a/b"); // a middle folder too
    write_file(outside.path / "deep.mid", song("X"));
    const ImportResult r = import_songs(job, {});
    CHECK(r.end == ui::ImportEnd::Done);
    CHECK(r.copied == 0);
    CHECK(r.failed == 2);
    CHECK(tree(rig.songs.path / "Inbox").find(".mid") == std::string::npos);
}

TEST_CASE("usb import: a FIFO where a song was does not hold the copy") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("A"));
    write_file(rig.stick.path / "b.mid", song("B"));
    const ImportJob job = rig.job();
    fs::remove(rig.stick.path / "a.mid");
    REQUIRE(::mkfifo((rig.stick.path / "a.mid").c_str(), 0600) == 0);
    auto done = std::async(std::launch::async, [&] { return import_songs(job, {}); });
    REQUIRE(done.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    const ImportResult r = done.get();
    CHECK(r.failed == 1);
    CHECK(r.copied == 1); // b.mid still comes in
    CHECK(r.marks[0] == ui::SongMark::Failed);
}

TEST_CASE("usb import: a file past the size cap is refused without reading it") {
    Rig rig;
    write_file(rig.stick.path / "big.mid", song("B"));
    fs::resize_file(rig.stick.path / "big.mid", 40ull * 1024 * 1024); // a hole: no bytes on disk
    write_file(rig.stick.path / "small.mid", song("S"));
    int reads = 0;
    ImportHooks hooks;
    hooks.on_chunk = [&reads](bool) { ++reads; };
    const ImportResult r = import_songs(rig.job(), hooks);
    CHECK(r.marks[0] == ui::SongMark::Failed);
    CHECK(r.copied == 1);
    CHECK(reads < 10); // only small.mid was read
}

TEST_CASE("usb import: two songs that the hash cannot tell apart are told apart by their bytes") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("1234"));
    write_file(rig.songs.path / "Collection/other.mid", song("5678")); // same size, other bytes
    write_file(rig.songs.path / "Collection/same.mid", song("1234"));
    write_file(rig.stick.path / "b.mid", song("5679")); // same size as both, differs from both
    ImportHooks hooks;
    hooks.hash = [](std::uint64_t, const char*, std::size_t) { return std::uint64_t{7}; };
    const ImportResult r = import_songs(rig.job(), hooks);
    CHECK(r.marks[0] == ui::SongMark::Duplicate); // a.mid is in the library
    CHECK(r.marks[1] == ui::SongMark::Copied);    // b.mid is not, though every hash matches
    CHECK(read_file(rig.songs.path / "Inbox/b.mid") == song("5679"));
}

TEST_CASE("usb import: a song copied earlier in the job is compared too") {
    Rig rig;
    write_file(rig.stick.path / "a.mid", song("1234"));
    write_file(rig.stick.path / "b.mid", song("1235"));
    write_file(rig.stick.path / "c.mid", song("1234"));
    ImportHooks hooks;
    hooks.hash = [](std::uint64_t, const char*, std::size_t) { return std::uint64_t{7}; };
    const ImportResult r = import_songs(rig.job(), hooks);
    CHECK(r.copied == 2);
    CHECK(r.marks[2] == ui::SongMark::Duplicate); // c.mid is a.mid again
}
