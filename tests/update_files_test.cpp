// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/update_files.hpp"

#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <cerrno>

using namespace etude::app;
using namespace upload_test;

#if !defined(_WIN32)

namespace {

// Each step can be made to fail; the others are the system's own.
ssize_t write_fails(int, const void*, std::size_t) {
    errno = ENOSPC;
    return -1;
}
ssize_t write_half(int fd, const void* data, std::size_t size) {
    return ::write(fd, data, size / 2 == 0 ? size : size / 2); // a short write, then the rest
}
int fsync_fails(int) {
    errno = EIO;
    return -1;
}
int rename_fails(const char*, const char*) {
    errno = EXDEV;
    return -1;
}

std::string folder_listing(const fs::path& dir) {
    std::string out;
    for (const auto& entry : fs::directory_iterator(dir)) {
        out += entry.path().filename().string() + ";";
    }
    return out;
}

} // namespace

TEST_CASE("atomic write: a failed write, fsync or rename leaves the old file whole") {
    struct Case {
        const char* name;
        FileSteps steps;
        int error;
    };
    FileSteps no_write;
    no_write.write = write_fails;
    FileSteps no_fsync;
    no_fsync.fsync = fsync_fails;
    FileSteps no_rename;
    no_rename.rename = rename_fails;
    for (const Case& c : {Case{"write", no_write, ENOSPC}, Case{"fsync", no_fsync, EIO},
                          Case{"rename", no_rename, EXDEV}}) {
        CAPTURE(c.name);
        TempDir dir;
        const fs::path file = dir.path / "state.txt";
        write_file(file, "the old answer\n");
        int error = 0;
        CHECK_FALSE(write_file_atomic_with(c.steps, file, "the new answer\n", &error));
        CHECK(error == c.error);
        CHECK(read_file(file) == "the old answer\n");
        CHECK(folder_listing(dir.path) == "state.txt;"); // no .part left behind
    }
}

TEST_CASE("atomic write: a short write is finished, and the file is replaced whole") {
    TempDir dir;
    const fs::path file = dir.path / "sub/state.txt";
    FileSteps steps;
    steps.write = write_half;
    int error = -1;
    CHECK(write_file_atomic_with(steps, file, "0123456789", &error));
    CHECK(error == 0);
    CHECK(read_file(file) == "0123456789");
    CHECK(write_file_atomic(file, "second"));
    CHECK(read_file(file) == "second");
    CHECK(folder_listing(file.parent_path()) == "state.txt;");
}

TEST_CASE("atomic write: a folder that cannot be written to fails with its reason") {
    TempDir dir;
    const fs::path file = dir.path / "state.txt";
    fs::create_directories(fs::path(file).concat(".part")); // the part's name is taken by a folder
    write_file(file, "kept");
    int error = 0;
    CHECK_FALSE(write_file_atomic(file, "new", &error));
    CHECK(error != 0);
    CHECK(read_file(file) == "kept");
}

#endif
