// SPDX-License-Identifier: GPL-3.0-or-later
// An update's tarball written into a folder: the real sample release, and what is left behind when
// something is refused (nothing: the archive is read and checked before the first file is made).
#include "../helper/unpack.hpp"
#include "update_archive_fixture.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>

using namespace etude;
using namespace etude::helper;
using namespace upload_test;

namespace {

const std::string kTop = "etude-1.2.3";

struct Sample {
    std::vector<std::uint8_t> tar;
    TarResult read;
    Sample() {
        const GunzipResult g = gunzip(update_fixture::sample_tar_gz(), 1 << 20);
        REQUIRE(g.ok());
        tar = g.data;
        read = read_tar(tar, kTop);
        REQUIRE(read.ok());
    }
};

mode_t mode_of(const fs::path& p) {
    struct stat st {};
    REQUIRE(::lstat(p.c_str(), &st) == 0);
    return st.st_mode & 07777;
}

} // namespace

TEST_CASE("unpack: the sample release is written as it was packed") {
    TempDir temp;
    const Sample sample;
    const fs::path dest = temp.path / "versions" / "1.2.3";
    const UnpackResult r = unpack_tar(sample.read.archive, sample.tar, dest);
    INFO(r.reason);
    REQUIRE(r.ok);
    CHECK(read_file(dest / "share/etude/piano.sf2") == "samples");
    CHECK(read_file(dest / "LICENSE") == "GPL");
    CHECK(read_file(dest / "bin/etude") == "#!/bin/sh\necho etude 1.2.3\n");
    CHECK(read_file(fs::path(dest) / update_fixture::kLongFolder / "font.txt") == "long name");
    CHECK(mode_of(dest / "bin/etude") == 0755);
    CHECK(mode_of(dest / "LICENSE") == 0644);
    CHECK(mode_of(dest) == 0755);
    CHECK(mode_of(dest / "bin") == 0755);
    // The link is a link, with the target the archive gave, and it leads to the long folder.
    const fs::path link = dest / "share/etude/current-font";
    REQUIRE(fs::is_symlink(link));
    CHECK(fs::read_symlink(link).string().rfind("verovio-data/aaaa", 0) == 0);
    CHECK(fs::is_directory(link));
}

TEST_CASE("unpack: a folder that is already there is not written into") {
    TempDir temp;
    const Sample sample;
    const fs::path dest = temp.path / "taken";
    fs::create_directories(dest);
    write_file(dest / "keep", "mine");
    const UnpackResult r = unpack_tar(sample.read.archive, sample.tar, dest);
    CHECK_FALSE(r.ok);
    CHECK(r.reason.find("already there") != std::string::npos);
    CHECK(read_file(dest / "keep") == "mine");
    CHECK_FALSE(fs::exists(dest / "bin"));
    // A link in its place is not followed either.
    fs::create_symlink(temp.path / "keep-away", temp.path / "linked");
    CHECK_FALSE(unpack_tar(sample.read.archive, sample.tar, temp.path / "linked").ok);
    CHECK_FALSE(fs::exists(temp.path / "keep-away"));
}

TEST_CASE("unpack: a destination that cannot be made says so") {
    TempDir temp;
    const Sample sample;
    write_file(temp.path / "file", "x");
    const UnpackResult r = unpack_tar(sample.read.archive, sample.tar, temp.path / "file" / "inside");
    CHECK_FALSE(r.ok);
    CHECK(r.reason.rfind("Could not write ", 0) == 0);
}
