// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/desktop_entry.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <string>

using namespace etude::app;
namespace fs = std::filesystem;

namespace {

const std::string kSystemEntry =
    "[Desktop Entry]\nType=Application\nName=etude\nExec=/opt/etude/bin/etude\n"
    "Icon=etude\nTerminal=false\nCategories=AudioVideo;Audio;Music;Education;\n";

// A scratch folder with a system folder and a user folder in it, removed at the end.
struct Scratch {
    fs::path root;
    DesktopFolders folders;

    Scratch() {
        root = fs::temp_directory_path() /
               ("etude-desktop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "system" / "applications");
        fs::create_directories(root / "home");
        folders.data_home = root / "home";
        folders.data_dirs = {root / "system"};
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void put_system_entry(const std::string& text = kSystemEntry) const {
        std::ofstream(root / "system" / "applications" / "etude.desktop") << text;
    }
    void put_system_icon(const std::string& icon) const {
        const fs::path dir = root / "system" / "icons" / "hicolor" / "256x256" / "apps";
        fs::create_directories(dir);
        std::ofstream(dir / (icon + ".png")) << "png";
    }
    std::string user_entry() const { return read(folders.data_home / "applications" / "etude.desktop"); }
    fs::path user_entry_path() const {
        return folders.data_home / "applications" / "etude.desktop";
    }
    // What this module writes for `icon`: the marker line, then the system's entry with that icon.
    static std::string copy_for(const std::string& icon, const std::string& base = kSystemEntry) {
        return std::string(kDesktopCopyMarker) + "\n" + desktop_entry_with_icon(base, icon);
    }
    fs::path user_icon(const std::string& icon) const {
        return folders.data_home / "icons" / "hicolor" / "256x256" / "apps" / (icon + ".png");
    }
    static std::string read(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
};

const unsigned char kPng[] = {0x89, 'P', 'N', 'G'};

} // namespace

TEST_CASE("desktop folders: the XDG defaults and the environment's values") {
    const DesktopFolders d = desktop_folders("", "", "/home/pi");
    CHECK(d.data_home == fs::path("/home/pi/.local/share"));
    REQUIRE(d.data_dirs.size() == 2);
    CHECK(d.data_dirs[0] == fs::path("/usr/local/share"));
    CHECK(d.data_dirs[1] == fs::path("/usr/share"));
    const DesktopFolders e = desktop_folders("/data", "/a::/b", "/home/pi");
    CHECK(e.data_home == fs::path("/data"));
    REQUIRE(e.data_dirs.size() == 2);
    CHECK(e.data_dirs[0] == fs::path("/a"));
    CHECK(e.data_dirs[1] == fs::path("/b"));
}

TEST_CASE("desktop folders: a relative path is ignored, and no home means no user folder") {
    const DesktopFolders a = desktop_folders("relative/data", "", "/home/pi");
    CHECK(a.data_home == fs::path("/home/pi/.local/share"));
    const DesktopFolders b = desktop_folders("", "share:/a:../b:/c", "/home/pi");
    REQUIRE(b.data_dirs.size() == 2);
    CHECK(b.data_dirs[0] == fs::path("/a"));
    CHECK(b.data_dirs[1] == fs::path("/c"));
    CHECK(desktop_folders("", "", "").data_home.empty());
    CHECK(desktop_folders(".local/share", "", "home").data_home.empty());
    const DesktopFolders none = desktop_folders("", "", "");
    CHECK(apply_desktop_icon(none, "etude-teal", kPng) == DesktopResult::NoUserFolder);
}

TEST_CASE("desktop entry: the Icon= line is set, added or kept in its own group") {
    CHECK(desktop_entry_with_icon(kSystemEntry, "etude-teal") ==
          "[Desktop Entry]\nType=Application\nName=etude\nExec=/opt/etude/bin/etude\n"
          "Icon=etude-teal\nTerminal=false\nCategories=AudioVideo;Audio;Music;Education;\n");
    // No Icon= line: added at the group's end, before the next group.
    CHECK(desktop_entry_with_icon("[Desktop Entry]\nName=etude\n[Desktop Action x]\nIcon=other\n",
                                  "etude-black") ==
          "[Desktop Entry]\nName=etude\nIcon=etude-black\n[Desktop Action x]\nIcon=other\n");
    CHECK(desktop_entry_with_icon("[Desktop Entry]\nName=etude\n", "etude-black") ==
          "[Desktop Entry]\nName=etude\nIcon=etude-black\n");
    // Done twice it is the same.
    const std::string once = desktop_entry_with_icon(kSystemEntry, "etude-teal");
    CHECK(desktop_entry_with_icon(once, "etude-teal") == once);
}

TEST_CASE("desktop icon: the user's entry is a copy of the system's with the colour's icon") {
    Scratch s;
    s.put_system_entry();
    s.put_system_icon("etude-teal");
    CHECK(apply_desktop_icon(s.folders, "etude-teal", kPng) == DesktopResult::Done);
    CHECK(s.user_entry() == Scratch::copy_for("etude-teal"));
    CHECK_FALSE(fs::exists(s.user_icon("etude-teal"))); // the system has it: nothing is copied
    CHECK(Scratch::read(s.root / "system" / "applications" / "etude.desktop") == kSystemEntry);
    CHECK(apply_desktop_icon(s.folders, "etude-teal", kPng) == DesktopResult::Unchanged);
    s.put_system_icon("etude-black");
    CHECK(apply_desktop_icon(s.folders, "etude-black", kPng) == DesktopResult::Done);
    CHECK(s.user_entry() == Scratch::copy_for("etude-black"));
}

TEST_CASE("desktop icon: a colour the system does not hold gets the built-in 256 px icon") {
    Scratch s;
    s.put_system_entry();
    CHECK(apply_desktop_icon(s.folders, "etude-wine", kPng) == DesktopResult::Done);
    REQUIRE(fs::exists(s.user_icon("etude-wine")));
    CHECK(Scratch::read(s.user_icon("etude-wine")) == std::string(reinterpret_cast<const char*>(kPng), sizeof(kPng)));
}

TEST_CASE("desktop icon: no launcher entry anywhere changes nothing") {
    Scratch s;
    CHECK(apply_desktop_icon(s.folders, "etude-teal", kPng) == DesktopResult::NotInstalled);
    CHECK_FALSE(fs::exists(s.folders.data_home / "applications"));
    CHECK_FALSE(fs::exists(s.folders.data_home / "icons"));
}

TEST_CASE("desktop icon: a reinstalled system entry is taken again") {
    Scratch s;
    s.put_system_entry();
    s.put_system_icon("etude-teal");
    CHECK(apply_desktop_icon(s.folders, "etude-teal", kPng) == DesktopResult::Done);
    s.put_system_entry("[Desktop Entry]\nName=etude\nExec=/usr/local/bin/etude\nIcon=etude\n");
    CHECK(apply_desktop_icon(s.folders, "etude-teal", kPng) == DesktopResult::Done);
    CHECK(s.user_entry() ==
          Scratch::copy_for(
              "etude-teal",
              "[Desktop Entry]\nName=etude\nExec=/usr/local/bin/etude\nIcon=etude\n"));
}

TEST_CASE(
    "launcher: wine is the system's own icon, so no copy is written, and one made earlier goes") {
    Scratch s;
    s.put_system_entry(); // Icon=etude
    s.put_system_icon("etude-teal");
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Wine, kPng) ==
          DesktopResult::Unchanged);
    CHECK_FALSE(fs::exists(s.user_entry_path()));
    CHECK_FALSE(fs::exists(s.user_icon("etude-wine"))); // and no icon is copied for it
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Teal, kPng) == DesktopResult::Done);
    CHECK(fs::exists(s.user_entry_path()));
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Wine, kPng) == DesktopResult::Done);
    CHECK_FALSE(fs::exists(s.user_entry_path()));
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Wine, kPng) ==
          DesktopResult::Unchanged);
}

TEST_CASE("launcher: a copy the player made is never touched") {
    Scratch s;
    s.put_system_entry();
    s.put_system_icon("etude-teal");
    fs::create_directories(s.user_entry_path().parent_path());
    const std::string theirs =
        "[Desktop Entry]\nName=My etude\nExec=/home/me/etude\nIcon=mine\n";
    std::ofstream(s.user_entry_path()) << theirs;
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Teal, kPng) ==
          DesktopResult::Unchanged);
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Wine, kPng) ==
          DesktopResult::Unchanged);
    CHECK(s.user_entry() == theirs);
}

TEST_CASE("launcher: a folder that cannot be written is a Failed result, and the next try works") {
    Scratch s;
    s.put_system_entry();
    s.put_system_icon("etude-teal");
    // A file where the user's applications folder should be: the entry cannot be written there.
    std::ofstream(s.folders.data_home / "applications") << "in the way";
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Teal, kPng) ==
          DesktopResult::Failed);
    CHECK(fs::is_regular_file(s.folders.data_home / "applications")); // nothing was damaged
    // The icon folder blocked the same way, for a colour the system does not hold.
    fs::remove(s.folders.data_home / "applications");
    std::ofstream(s.folders.data_home / "icons") << "in the way";
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Cobalt, kPng) ==
          DesktopResult::Failed);
    // Once the way is clear the same call goes through: a failure left nothing behind.
    fs::remove(s.folders.data_home / "icons");
    CHECK(apply_launcher_colour(s.folders, etude::IconColour::Cobalt, kPng) ==
          DesktopResult::Done);
    CHECK(s.user_entry() == Scratch::copy_for("etude-cobalt"));
}
