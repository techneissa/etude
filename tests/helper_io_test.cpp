// SPDX-License-Identifier: GPL-3.0-or-later
// The root update helper's file handling (helper/safe_io): what it will open in a folder the app can
// write to, and what it refuses. These run as the current user; the helper's own ownership checks
// compare with the user it runs as, which is root there.
#include "../helper/context.hpp"
#include "../helper/safe_io.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace etude::helper;
using namespace upload_test;

namespace etude::helper {
// The log of the helper's code under test: dropped.
void note(const std::string&) {}
} // namespace etude::helper

namespace {

FileLimits limits(std::uint64_t max = 1024) {
    FileLimits l;
    l.max_bytes = max;
    return l;
}

Fd dir_of(const fs::path& p) {
    std::string error;
    Fd fd = open_directory(p.string(), error);
    REQUIRE(fd.ok());
    return fd;
}

} // namespace

TEST_CASE("helper io: a plain file name has no slash, no dots and no control characters") {
    CHECK(plain_name("request"));
    CHECK(plain_name("release.tar.gz.minisig"));
    for (const char* bad : {"", ".", "..", "a/b", "/etc", "a\nb", "a\x7f"}) {
        CAPTURE(bad);
        CHECK_FALSE(plain_name(bad));
    }
}

TEST_CASE("helper io: a regular file opens, and is read from the open descriptor") {
    TempDir t;
    write_file(t.path / "a", "hello");
    const Fd dir = dir_of(t.path);
    PlainFile f = open_plain_file(dir.get(), "a", limits());
    REQUIRE(f.status == OpenStatus::Ok);
    CHECK(f.size == 5);
    std::string text, error;
    CHECK(read_all_text(f.fd.get(), 100, text, error));
    CHECK(text == "hello");
    CHECK(open_plain_file(dir.get(), "none", limits()).status == OpenStatus::Missing);
}

TEST_CASE("helper io: a link, a pipe, a folder and a name with a slash are refused") {
    TempDir t;
    write_file(t.path / "outside", "secret");
    fs::create_directories(t.path / "d");
    fs::create_symlink(t.path / "outside", t.path / "d" / "link");
    fs::create_symlink("/etc/passwd", t.path / "d" / "abs");
    REQUIRE(::mkfifo((t.path / "d" / "pipe").c_str(), 0600) == 0);
    fs::create_directories(t.path / "d" / "sub");
    const Fd dir = dir_of(t.path / "d");
    CHECK(open_plain_file(dir.get(), "link", limits()).status == OpenStatus::NotPlain);
    CHECK(open_plain_file(dir.get(), "abs", limits()).status == OpenStatus::NotPlain);
    CHECK(open_plain_file(dir.get(), "pipe", limits()).status == OpenStatus::NotPlain); // no wait on a writer
    CHECK(open_plain_file(dir.get(), "sub", limits()).status == OpenStatus::NotPlain);
    CHECK(open_plain_file(dir.get(), "../outside", limits()).status == OpenStatus::Failed);
    CHECK(open_plain_file(dir.get(), "sub/x", limits()).status == OpenStatus::Failed);
}

TEST_CASE("helper io: a file too large, with two names, not the app's or open to others is refused") {
    TempDir t;
    write_file(t.path / "big", std::string(100, 'x'));
    write_file(t.path / "one", "1");
    fs::create_hard_link(t.path / "one", t.path / "two");
    write_file(t.path / "open", "1");
    ::chmod((t.path / "open").c_str(), 0666);
    const Fd dir = dir_of(t.path);
    CHECK(open_plain_file(dir.get(), "big", limits(99)).status == OpenStatus::TooLarge);
    CHECK(open_plain_file(dir.get(), "big", limits(100)).status == OpenStatus::Ok);
    FileLimits mine = limits();
    mine.check_owner = true;
    mine.owner = ::geteuid();
    CHECK(open_plain_file(dir.get(), "big", mine).status == OpenStatus::Ok);
    CHECK(open_plain_file(dir.get(), "one", mine).status == OpenStatus::WrongOwner); // a second hard link
    mine.owner = ::geteuid() + 1;
    CHECK(open_plain_file(dir.get(), "big", mine).status == OpenStatus::WrongOwner);
    FileLimits closed = limits();
    closed.allow_group_other_write = false;
    CHECK(open_plain_file(dir.get(), "open", closed).status == OpenStatus::WrongOwner);
    CHECK(open_plain_file(dir.get(), "big", closed).status == OpenStatus::Ok);
}

TEST_CASE("helper io: reading stops at the limit") {
    TempDir t;
    write_file(t.path / "a", std::string(1000, 'x'));
    const Fd dir = dir_of(t.path);
    PlainFile f = open_plain_file(dir.get(), "a", limits(2000));
    std::vector<std::uint8_t> bytes;
    std::string error;
    CHECK_FALSE(read_all(f.fd.get(), 999, bytes, error));
    PlainFile again = open_plain_file(dir.get(), "a", limits(2000));
    CHECK(read_all(again.fd.get(), 1000, bytes, error));
    CHECK(bytes.size() == 1000);
}

TEST_CASE("helper io: a copy is a new file, never through a planted link") {
    TempDir t;
    write_file(t.path / "src", "payload");
    write_file(t.path / "victim", "keep");
    fs::create_directories(t.path / "to");
    const Fd to = dir_of(t.path / "to");
    const Fd from = dir_of(t.path);
    PlainFile src = open_plain_file(from.get(), "src", limits());
    std::string error;
    CHECK(copy_to_new_file(src.fd.get(), to.get(), "copy", 100, error));
    CHECK(read_file(t.path / "to" / "copy") == "payload");
    CHECK((fs::status(t.path / "to" / "copy").permissions() & fs::perms::others_read) == fs::perms::none);
    // An existing name, a link planted there: not written, not followed.
    PlainFile again = open_plain_file(from.get(), "src", limits());
    CHECK_FALSE(copy_to_new_file(again.fd.get(), to.get(), "copy", 100, error));
    fs::create_symlink(t.path / "victim", t.path / "to" / "planted");
    PlainFile third = open_plain_file(from.get(), "src", limits());
    CHECK_FALSE(copy_to_new_file(third.fd.get(), to.get(), "planted", 100, error));
    CHECK(read_file(t.path / "victim") == "keep");
    // Larger than allowed: no file is left.
    PlainFile fourth = open_plain_file(from.get(), "src", limits());
    CHECK_FALSE(copy_to_new_file(fourth.fd.get(), to.get(), "big", 3, error));
    CHECK_FALSE(fs::exists(t.path / "to" / "big"));
}

TEST_CASE("helper io: a file is written whole under another name and renamed over") {
    TempDir t;
    const Fd dir = dir_of(t.path);
    std::string error;
    CHECK(write_file_atomic(dir.get(), "status", "one\n", 0644, error));
    CHECK(read_file(t.path / "status") == "one\n");
    CHECK(write_file_atomic(dir.get(), "status", "two\n", 0644, error));
    CHECK(read_file(t.path / "status") == "two\n");
    CHECK(tree(t.path) == "status\n"); // no temporary left
    // A name that is a link is replaced, not written through.
    write_file(t.path / "victim", "keep");
    fs::remove(t.path / "status");
    fs::create_symlink(t.path / "victim", t.path / "status");
    CHECK(write_file_atomic(dir.get(), "status", "three\n", 0644, error));
    CHECK(read_file(t.path / "victim") == "keep");
    CHECK_FALSE(fs::is_symlink(t.path / "status"));
    // A name that is a folder is left alone, and says so.
    fs::remove(t.path / "status");
    fs::create_directories(t.path / "status");
    CHECK_FALSE(write_file_atomic(dir.get(), "status", "four\n", 0644, error));
    CHECK(fs::is_directory(t.path / "status"));
    CHECK(tree(t.path) == "status/\nvictim\n");
}

TEST_CASE("helper io: removing a tree does not follow a link out of it") {
    TempDir t;
    write_file(t.path / "keep" / "precious", "x");
    write_file(t.path / "doomed" / "a" / "b", "x");
    fs::create_symlink(t.path / "keep", t.path / "doomed" / "a" / "to-keep");
    fs::create_symlink(t.path / "keep", t.path / "link-to-keep");
    const Fd dir = dir_of(t.path);
    std::string error;
    CHECK(remove_tree_at(dir.get(), "doomed", error));
    CHECK(remove_tree_at(dir.get(), "link-to-keep", error));
    CHECK(remove_tree_at(dir.get(), "never-there", error));
    CHECK(tree(t.path) == "keep/\nkeep/precious\n");
}

TEST_CASE("helper io: a folder opens by path or by name, and a link in its place does not") {
    TempDir t;
    fs::create_directories(t.path / "real");
    fs::create_symlink(t.path / "real", t.path / "link");
    std::string error;
    CHECK(open_directory((t.path / "real").string(), error).ok());
    CHECK_FALSE(open_directory((t.path / "link").string(), error).ok());
    const Fd dir = dir_of(t.path);
    CHECK(open_directory_at(dir.get(), "real", error).ok());
    CHECK_FALSE(open_directory_at(dir.get(), "link", error).ok());
}
