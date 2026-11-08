// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/library_files.hpp"
#include "../app/song_library.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

using namespace etude::app;
namespace fs = std::filesystem;

namespace {

void touch(const fs::path& p, const char* text = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Polls until the rescan is in (each poll stands for a frame).
void settle(SongLibrary& lib) {
    for (int i = 0; i < 2000 && (lib.rescanning() || lib.state().pending() > 0); ++i) {
        lib.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    lib.poll();
}

} // namespace

TEST_CASE("move_into keeps the name, or numbers it when the folder has one already") {
    const fs::path dir = fs::temp_directory_path() / "etude_move_into";
    fs::remove_all(dir);
    touch(dir / "a" / "song.mid", "one");
    touch(dir / "b" / "song.mid", "two");
    std::string error;
    const MoveDone moved = move_into(dir / "a" / "song.mid", dir / "b", &error);
    CHECK(error.empty());
    CHECK(moved.outcome == MoveOutcome::Moved);
    CHECK(moved.now == dir / "b" / "song (2).mid");
    CHECK_FALSE(fs::exists(dir / "a" / "song.mid"));
    // The file that was there is untouched.
    CHECK(read(dir / "b" / "song.mid") == "two");
    CHECK(read(dir / "b" / "song (2).mid") == "one");
    // A folder that is not there is made.
    CHECK(move_into(dir / "b" / "song.mid", dir / "c" / "d", &error).now ==
          dir / "c" / "d" / "song.mid");
    // A file that is gone is a failure with a reason, and nothing changes.
    const MoveDone gone = move_into(dir / "nothing.mid", dir / "b", &error);
    CHECK(gone.outcome == MoveOutcome::Failed);
    CHECK(gone.now.empty());
    CHECK_FALSE(error.empty());
    // A file already in the folder stays where it is, and is not a move.
    const MoveDone there = move_into(dir / "b" / "song (2).mid", dir / "b", &error);
    CHECK(there.outcome == MoveOutcome::AlreadyThere);
    CHECK(there.now == dir / "b" / "song (2).mid");
    fs::remove_all(dir);
}

TEST_CASE("move_into never writes over a file, whichever names are taken") {
    const fs::path dir = fs::temp_directory_path() / "etude_move_into_taken";
    fs::remove_all(dir);
    touch(dir / "a" / "x.mid", "new");
    touch(dir / "b" / "x.mid", "1");
    touch(dir / "b" / "x (2).mid", "2");
    touch(dir / "b" / "x (3).mid", "3");
    std::string error;
    const MoveDone done = move_into(dir / "a" / "x.mid", dir / "b", &error);
    REQUIRE(done.outcome == MoveOutcome::Moved);
    CHECK(done.now == dir / "b" / "x (4).mid");
    CHECK(read(dir / "b" / "x.mid") == "1");
    CHECK(read(dir / "b" / "x (2).mid") == "2");
    CHECK(read(dir / "b" / "x (3).mid") == "3");
    CHECK(read(dir / "b" / "x (4).mid") == "new");
    fs::remove_all(dir);
}

TEST_CASE("the file mover runs its jobs off the caller's thread and reports each once") {
    const fs::path dir = fs::temp_directory_path() / "etude_library_files";
    fs::remove_all(dir);
    touch(dir / "Songs" / "x.mid");
    touch(dir / "Songs" / "y.mid");
    LibraryFiles files;
    CHECK_FALSE(files.busy());
    FileJob job;
    job.kind = FileJob::Kind::Remove;
    job.files = {{dir / "Songs" / "x.mid", dir / "Removed songs"},
                 {dir / "Songs" / "gone.mid", dir / "Removed songs"}};
    files.request(job);
    CHECK(files.busy());
    files.wait_idle();
    const std::optional<FileResult> r = files.take();
    REQUIRE(r.has_value());
    CHECK(r->kind == FileJob::Kind::Remove);
    CHECK(r->moved == 1);
    CHECK(r->failed == 1);
    CHECK(r->skipped == 0);
    CHECK_FALSE(r->error.empty());
    CHECK(fs::exists(dir / "Removed songs" / "x.mid"));
    CHECK_FALSE(files.take().has_value());
    CHECK_FALSE(files.busy());
    // A song already in the folder it is moved to is passed over, not counted as moved.
    FileJob again;
    again.files = {{dir / "Songs" / "y.mid", dir / "Songs"}};
    files.request(again);
    files.wait_idle();
    const std::optional<FileResult> same = files.take();
    REQUIRE(same.has_value());
    CHECK(same->moved == 0);
    CHECK(same->skipped == 1);
    fs::remove_all(dir);
}

TEST_CASE("the file mover finishes the jobs still queued when it is destroyed") {
    const fs::path dir = fs::temp_directory_path() / "etude_library_files_quit";
    fs::remove_all(dir);
    for (int i = 0; i < 20; ++i) {
        touch(dir / "Songs" / ("s" + std::to_string(i) + ".mid"));
    }
    {
        LibraryFiles files;
        for (int i = 0; i < 20; ++i) {
            FileJob job;
            job.kind = FileJob::Kind::Remove;
            job.files = {{dir / "Songs" / ("s" + std::to_string(i) + ".mid"), dir / "Removed"}};
            files.request(job);
        }
    } // destroyed at once, every job still queued or running
    for (int i = 0; i < 20; ++i) {
        CHECK(fs::exists(dir / "Removed" / ("s" + std::to_string(i) + ".mid")));
    }
    fs::remove_all(dir);
}

TEST_CASE("a rescan lists the folders on its own thread and the moved song comes in at a poll") {
    const fs::path dir = fs::temp_directory_path() / "etude_lib_rescan";
    fs::remove_all(dir);
    fs::create_directories(dir / "Classical");
    const fs::path src = fs::path(ETUDE_TEST_DATA_DIR) / "satie-gymnopedie-1.mid";
    fs::copy_file(src, dir / "a.mid");
    fs::copy_file(fs::path(ETUDE_TEST_DATA_DIR) / "bach-bwv846-prelude.mid", dir / "b.mid");
    SongLibrary lib({dir});
    CHECK(lib.scan() == 2);
    lib.load_all_now();
    CHECK(lib.state().loaded() == 2);
    // a.mid goes into Classical; until the poll that brings the listing in, nothing changes.
    std::string error;
    REQUIRE(move_into(dir / "a.mid", dir / "Classical", &error).outcome == MoveOutcome::Moved);
    lib.rescan();
    CHECK(lib.state().items[0].relative == "a.mid");
    settle(lib);
    CHECK_FALSE(lib.rescanning());
    REQUIRE(lib.size() == 2);
    CHECK(lib.index_of(dir / "Classical" / "a.mid").has_value());
    CHECK_FALSE(lib.index_of(dir / "a.mid").has_value());
    // The unchanged song kept its result; the moved one was read again.
    CHECK(lib.state().loaded() == 2);
    // A removed song leaves the list; two rescans asked at once list again after the first.
    fs::remove(dir / "b.mid");
    lib.rescan();
    lib.rescan();
    settle(lib);
    CHECK(lib.size() == 1);
    fs::remove_all(dir);
}
