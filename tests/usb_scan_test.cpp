// SPDX-License-Identifier: GPL-3.0-or-later
// Finding sticks and the songs on them: the caps, what is left out, and where volumes are.
#include "../app/usb_scan.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

using namespace etude::app;
using namespace upload_test;

namespace {

std::string names(const StickScan& scan) {
    std::string out;
    for (const StickSong& s : scan.songs) {
        out += s.path + ";";
    }
    return out;
}

} // namespace

TEST_CASE("stick scan: songs in name order, sizes, other files left out") {
    TempDir stick;
    write_file(stick.path / "b.mid", midi_bytes("12345"));
    write_file(stick.path / "a.mid", midi_bytes());
    write_file(stick.path / "Lessons/c.abc", "X:1\nK:C\nCDEF|\n");
    write_file(stick.path / "notes.txt", "hello");
    write_file(stick.path / "scan.pdf", "%PDF");
    write_file(stick.path / "photo.png", "png");
    const StickScan scan = scan_stick(stick.path);
    CHECK(names(scan) == "Lessons/c.abc;a.mid;b.mid;");
    CHECK(scan.songs[2].size == 19);
    CHECK_FALSE(scan.truncated);
}

TEST_CASE("stick scan: hidden files and folders, system folders and links are not entered") {
    TempDir stick;
    write_file(stick.path / ".hidden.mid", midi_bytes());
    write_file(stick.path / "._a.mid", midi_bytes());
    write_file(stick.path / ".Trashes/x.mid", midi_bytes());
    write_file(stick.path / "System Volume Information/x.mid", midi_bytes());
    write_file(stick.path / "real.mid", midi_bytes());
    TempDir outside;
    write_file(outside.path / "out.mid", midi_bytes());
    fs::create_symlink(outside.path, stick.path / "link");
    fs::create_symlink(outside.path / "out.mid", stick.path / "linked.mid");
    const StickScan scan = scan_stick(stick.path);
    CHECK(names(scan) == "real.mid;");
}

TEST_CASE("stick scan: the song cap stops the scan and says so") {
    TempDir stick;
    for (int i = 0; i < 12; ++i) {
        write_file(stick.path / ("s" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ".mid"),
                   midi_bytes());
    }
    const StickScan scan = scan_stick(stick.path, 5);
    CHECK(scan.songs.size() == 5);
    CHECK(scan.truncated);
    CHECK(names(scan) == "s00.mid;s01.mid;s02.mid;s03.mid;s04.mid;"); // the same five every time
    CHECK_FALSE(scan_stick(stick.path, 12).truncated);
}

TEST_CASE("stick scan: the depth cap leaves the deep folders alone") {
    TempDir stick;
    write_file(stick.path / "a/b/c/deep.mid", midi_bytes());
    write_file(stick.path / "top.mid", midi_bytes());
    const StickScan shallow = scan_stick(stick.path, 500, 2);
    CHECK(names(shallow) == "top.mid;");
    CHECK(shallow.truncated);
    const StickScan deep = scan_stick(stick.path, 500, 3);
    CHECK(names(deep) == "a/b/c/deep.mid;top.mid;");
    CHECK_FALSE(deep.truncated);
}

TEST_CASE("stick scan: the folder cap bounds a stick of many folders") {
    TempDir stick;
    for (int i = 0; i < 30; ++i) {
        fs::create_directories(stick.path / ("d" + std::to_string(i)));
    }
    write_file(stick.path / "d29/last.mid", midi_bytes());
    const StickScan scan = scan_stick(stick.path, 500, 6, 10);
    CHECK(scan.truncated);
    CHECK(scan.songs.empty());
}

TEST_CASE("stick scan: a folder of too many entries is cut short, and says so") {
    TempDir stick;
    for (int i = 0; i < 30; ++i) {
        write_file(stick.path / ("s" + std::to_string(100 + i) + ".mid"), midi_bytes());
    }
    const StickScan scan = scan_stick(stick.path, 500, 6, 3000, 10);
    CHECK(scan.truncated);
    CHECK(scan.songs.size() <= 10);
    CHECK_FALSE(scan_stick(stick.path, 500, 6, 3000, 30).truncated);
}

TEST_CASE("stick scan: a folder that is not there lists nothing") {
    TempDir stick;
    const StickScan scan = scan_stick(stick.path / "gone");
    CHECK(scan.songs.empty());
    CHECK_FALSE(scan.truncated);
}

TEST_CASE("volumes: /media/<user>/<label> and /run/media, each a stick") {
    TempDir root;
    fs::create_directories(root.path / "media/pi/STICK");
    fs::create_directories(root.path / "media/pi/.hidden");
    fs::create_directories(root.path / "run/media/ann/MUSIC");
    write_file(root.path / "media/pi/a-file", "x");
    VolumeRoots roots;
    roots.shared = {root.path / "media", root.path / "run/media", root.path / "missing"};
    const auto volumes = list_volumes(roots);
    REQUIRE(volumes.size() == 2);
    CHECK(volumes[0].label == "STICK");
    CHECK(volumes[1].label == "MUSIC");
    CHECK(volumes[0].path == root.path / "media/pi/STICK");
}

TEST_CASE("volumes: /Volumes without the startup volume") {
    TempDir root;
    fs::create_directories(root.path / "Volumes/KEYS");
    fs::create_directories(root.path / "Volumes/Data copy");
    fs::create_symlink("/", root.path / "Volumes/Macintosh HD"); // the startup volume is a link
    VolumeRoots roots;
    roots.direct = {root.path / "Volumes"};
    const auto volumes = list_volumes(roots);
    REQUIRE(volumes.size() == 2);
    CHECK(volumes[0].label == "Data copy");
    CHECK(volumes[1].label == "KEYS");
    // A folder on the machine's own disk (the same device as a `system` path) is not a stick.
    roots.system = {root.path};
    CHECK(list_volumes(roots).empty());
}

TEST_CASE("volumes: a pretend volume is the only one, while its folder exists") {
    TempDir root;
    VolumeRoots roots;
    roots.shared = {root.path / "media"};
    fs::create_directories(root.path / "media/u/REAL");
    roots.pretend = root.path / "fake-stick";
    CHECK(list_volumes(roots).empty());
    fs::create_directories(roots.pretend);
    const auto volumes = list_volumes(roots);
    REQUIRE(volumes.size() == 1);
    CHECK(volumes[0].label == "fake-stick");
}
