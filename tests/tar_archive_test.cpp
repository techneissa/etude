// SPDX-License-Identifier: GPL-3.0-or-later
// What an update's tarball may hold: every kind of entry an update must refuse, and the ways one
// hides: a path that climbs out, a link that points out, a file below a link, a device.
#include "etude/tar_archive.hpp"

#include "tar_builder.hpp"
#include "update_archive_fixture.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace etude;
using tar_test::Tar;

namespace {

constexpr const char* kTop = "etude-1.2.3";

TarResult read(const Tar& t, const TarLimits& limits = {}) { return read_tar(t.done(), kTop, limits); }

Tar good() {
    Tar t;
    t.dir(kTop).dir(std::string(kTop) + "/bin").file(std::string(kTop) + "/bin/etude", "app", 0755);
    return t;
}

} // namespace

TEST_CASE("tar: a release's tarball is listed, below its top folder") {
    Tar t = good();
    t.dir(std::string(kTop) + "/share")
        .file(std::string(kTop) + "/share/a.txt", "hello")
        .symlink(std::string(kTop) + "/share/link", "a.txt")
        .file(std::string(kTop) + "/LICENSE", "gpl", 0600);
    const TarResult r = read(t);
    REQUIRE(r.ok());
    const auto& e = r.archive.entries;
    REQUIRE(e.size() == 7);
    CHECK(e[0].path.empty());
    CHECK(e[0].kind == TarKind::Directory);
    CHECK(e[1].path == "bin");
    CHECK(e[2].path == "bin/etude");
    CHECK(e[2].kind == TarKind::File);
    CHECK(e[2].mode == 0755);
    CHECK(e[2].size == 3);
    CHECK(e[4].path == "share/a.txt");
    CHECK(e[4].mode == 0644);
    CHECK(e[5].kind == TarKind::Symlink);
    CHECK(e[5].link == "a.txt");
    CHECK(e[6].mode == 0644); // owner-only read is still a plain file
}

TEST_CASE("tar: a file's bytes are where the entry says") {
    Tar t = good();
    t.file(std::string(kTop) + "/data", "0123456789abcdef");
    const std::vector<std::uint8_t> bytes = t.done();
    const TarResult r = read_tar(bytes, kTop);
    REQUIRE(r.ok());
    const TarEntry& e = r.archive.entries.back();
    CHECK(std::string(bytes.begin() + static_cast<long>(e.offset),
                      bytes.begin() + static_cast<long>(e.offset + e.size)) == "0123456789abcdef");
}

TEST_CASE("tar: set-uid, set-gid and sticky bits and group and other write never come through") {
    Tar t = good();
    t.file(std::string(kTop) + "/s", "x", 04777).file(std::string(kTop) + "/t", "x", 01666);
    const TarResult r = read(t);
    REQUIRE(r.ok());
    CHECK(r.archive.entries[3].mode == 0755);
    CHECK(r.archive.entries[4].mode == 0644); // sticky and no execute
}

TEST_CASE("tar: the real sample tarball, with its GNU long name and its link") {
    const GunzipResult unpacked = gunzip(update_fixture::sample_tar_gz(), 1 << 20);
    REQUIRE(unpacked.ok());
    const TarResult r = read_tar(unpacked.data, kTop);
    REQUIRE(r.ok());
    bool long_file = false;
    bool link = false;
    bool exec = false;
    for (const TarEntry& e : r.archive.entries) {
        long_file = long_file || e.path == std::string(update_fixture::kLongFolder) + "/font.txt";
        link = link || (e.kind == TarKind::Symlink && e.path == "share/etude/current-font");
        exec = exec || (e.path == "bin/etude" && e.mode == 0755);
    }
    CHECK(long_file);
    CHECK(link);
    CHECK(exec);
    // The same archive under another top folder is not this release's.
    CHECK(read_tar(unpacked.data, "etude-9.9.9").error == TarError::WrongTop);
}

TEST_CASE("tar: a ustar prefix and a pax path make the path, the way they do for tar") {
    Tar t;
    t.dir(kTop);
    t.add("deep/name.txt", '0', "x", "", 0644, std::string(kTop) + "/more", true);
    const TarResult prefixed = read(t);
    REQUIRE(prefixed.ok());
    CHECK(prefixed.archive.entries.back().path == "more/deep/name.txt");

    // A GNU tar's prefix field means something else and is left alone.
    Tar gnu;
    gnu.dir(kTop);
    gnu.add(std::string(kTop) + "/plain.txt", '0', "x", "", 0644, "ignored/", false);
    const TarResult plain = read(gnu);
    REQUIRE(plain.ok());
    CHECK(plain.archive.entries.back().path == "plain.txt");

    const std::string record = "30 path=" + std::string(kTop) + "/pax/x.txt\n";
    Tar px;
    px.dir(kTop);
    px.add("PaxHeaders/x", 'x', record);
    px.add("short", '0', "y");
    const TarResult pax = read(px);
    REQUIRE(pax.ok());
    CHECK(pax.archive.entries.back().path == "pax/x.txt");
}

TEST_CASE("tar: a path that is absolute, climbs, repeats a slash or has odd characters is refused") {
    for (const std::string& name : {std::string("/etc/passwd"), std::string(kTop) + "/../x",
                                   std::string(kTop) + "/a/../../x", std::string(kTop) + "//x",
                                   std::string(kTop) + "/./x", std::string(kTop) + "/a\\b",
                                   std::string(kTop) + "/a\tb", std::string(kTop) + "/..",
                                   std::string("../") + kTop + "/x"}) {
        Tar t = good();
        t.file(name);
        const TarResult r = read(t);
        CAPTURE(name);
        CHECK_FALSE(r.ok());
        CHECK((r.error == TarError::BadPath || r.error == TarError::WrongTop));
    }
}

TEST_CASE("tar: everything must lie inside the one top folder") {
    Tar t = good();
    t.file("other/x");
    CHECK(read(t).error == TarError::WrongTop);
    Tar two = good();
    two.dir("etude-1.2.4");
    CHECK(read(two).error == TarError::WrongTop);
    Tar near;
    near.dir("etude-1.2.30");
    CHECK(read(near).error == TarError::WrongTop);
    Tar none;
    CHECK(read(none).error == TarError::MissingTop);
}

TEST_CASE("tar: only files, folders and symbolic links; no hard links, devices or pipes") {
    for (const char type : {'1', '3', '4', '6', '7'}) {
        Tar t = good();
        t.add(std::string(kTop) + "/odd", type, "", "bin/etude");
        const TarResult r = read(t);
        CAPTURE(type);
        CHECK(r.error == TarError::Unsupported);
        CHECK(r.path == std::string(kTop) + "/odd");
    }
}

TEST_CASE("tar: a link may not be absolute, empty, or leave the top folder") {
    const auto refused = [](const std::string& at, const std::string& target) {
        Tar t = good();
        t.dir(std::string(kTop) + "/d").symlink(std::string(kTop) + "/" + at, target);
        return read(t).error == TarError::BadLink;
    };
    CHECK(refused("d/l", "/etc/passwd"));
    CHECK(refused("d/l", ""));
    CHECK(refused("d/l", "../../outside"));
    CHECK(refused("d/l", "../../../../.."));
    CHECK(refused("l", ".."));
    CHECK(refused("d/l", "a/../../../x"));
    CHECK(refused("d/l", "x\\y"));
    CHECK(refused("d/l", "a/../x")); // a climb after a name: the name could be a link
    CHECK_FALSE(refused("d/l", "../bin/etude"));
    CHECK_FALSE(refused("d/l", ".."));
    CHECK_FALSE(refused("l", "bin/etude"));
    CHECK_FALSE(refused("l", "./bin"));
}

TEST_CASE("tar: links that chain through each other cannot climb out") {
    const std::string top = std::string(kTop) + "/";
    // `a/up` is a link to the top folder, which reads as inside; `c` then goes through it and one
    // `..` past: lexically `a`, but on disk the folder above the top.
    Tar chained = good();
    chained.dir(top + "a").symlink(top + "a/up", "..").symlink(top + "c", "a/up/..");
    const TarResult r = read(chained);
    CHECK(r.error == TarError::BadLink);
    CHECK(r.path == top + "c");
    // The same through a longer chain, and with the climb after a `.` and an empty part.
    Tar longer = good();
    longer.dir(top + "a")
        .dir(top + "b")
        .symlink(top + "a/up", "..")
        .symlink(top + "b/up", "../a/up")
        .symlink(top + "c", "b/up/up/..");
    CHECK(read(longer).error == TarError::BadLink);
    Tar dotted = good();
    dotted.dir(top + "a").symlink(top + "a/up", "..").symlink(top + "d", "./a//up/../..");
    CHECK(read(dotted).error == TarError::BadLink);
    // Climbing first and then only going down is what releases do, and stays allowed.
    Tar fine = good();
    fine.dir(top + "share").dir(top + "share/x").dir(top + "lib").file(top + "lib/f", "f")
        .symlink(top + "share/x/f", "../../lib/f")
        .symlink(top + "latest", "share/x/f")
        .symlink(top + "up", ".");
    CHECK(read(fine).ok());
}

TEST_CASE("tar: nothing may be written below a link or a file") {
    Tar t = good();
    t.symlink(std::string(kTop) + "/l", "bin").file(std::string(kTop) + "/l/evil");
    CHECK(read(t).error == TarError::UnderFile);
    Tar f = good();
    f.file(std::string(kTop) + "/bin/etude/x");
    CHECK(read(f).error == TarError::UnderFile);
    // A folder named after a file that is later made, and a file whose name a folder already has.
    Tar later;
    later.dir(kTop).file(std::string(kTop) + "/d/x").file(std::string(kTop) + "/d");
    CHECK_FALSE(read(later).ok());
    Tar again = good();
    again.dir(std::string(kTop) + "/bin");
    CHECK(read(again).error == TarError::Duplicate);
    Tar twice = good();
    twice.file(std::string(kTop) + "/bin/etude");
    CHECK(read(twice).error == TarError::Duplicate);
    // A folder implied by a path, then declared: fine.
    Tar implied;
    implied.dir(kTop).file(std::string(kTop) + "/d/x").dir(std::string(kTop) + "/d");
    CHECK(read(implied).ok());
}

TEST_CASE("tar: damaged archives are refused") {
    Tar t = good();
    std::vector<std::uint8_t> bytes = t.done();
    std::vector<std::uint8_t> flipped = bytes;
    flipped[10] ^= 1; // in the first header's name: the checksum no longer matches
    CHECK(read_tar(flipped, kTop).error == TarError::BadHeader);
    // Cut off in the middle of a file's bytes.
    Tar big = good();
    big.file(std::string(kTop) + "/big", std::string(2000, 'x'));
    std::vector<std::uint8_t> cut = big.raw();
    cut.resize(cut.size() - 1200);
    CHECK(read_tar(cut, kTop).error == TarError::Truncated);
    // A size that is not an octal number.
    std::vector<std::uint8_t> bad = bytes;
    std::memcpy(bad.data() + 124, "99999999999\0", 12);
    CHECK_FALSE(read_tar(bad, kTop).ok());
    CHECK(read_tar({}, kTop).error == TarError::MissingTop);
}

TEST_CASE("tar: the limits on entries and on sizes") {
    Tar t = good();
    t.file(std::string(kTop) + "/a", std::string(100, 'x')).file(std::string(kTop) + "/b", std::string(100, 'y'));
    TarLimits limits;
    limits.max_entries = 4;
    CHECK(read(t, limits).error == TarError::TooBig);
    limits = {};
    limits.max_file_bytes = 99;
    CHECK(read(t, limits).error == TarError::TooBig);
    limits = {};
    limits.max_total_bytes = 202;
    CHECK(read(t, limits).error == TarError::TooBig);
    limits.max_total_bytes = 203;
    CHECK(read(t, limits).ok()); // 3 bytes of the app, 100 and 100
}

TEST_CASE("tar: the words for a refusal name the path") {
    Tar t = good();
    t.symlink(std::string(kTop) + "/l", "../..");
    const TarResult r = read(t);
    CHECK(tar_error_text(r) == "the archive has a link that leaves its folder: " + std::string(kTop) + "/l");
    CHECK(tar_error_text(TarResult{}).empty());
}
