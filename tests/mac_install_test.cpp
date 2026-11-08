// SPDX-License-Identifier: GPL-3.0-or-later
// What a Mac decides before it replaces its own app bundle: which bundle runs, where it cannot be
// replaced (App Translocation, a disk image, a folder it cannot write), and the names an update
// leaves beside it. Pure text, so no file system here (tests/mac_update_test.cpp has the steps).
#include "etude/mac_install.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("mac install: the bundle is the .app a program inside Contents/MacOS belongs to") {
    CHECK(mac_bundle_of_executable("/Applications/etude.app/Contents/MacOS/etude") ==
          "/Applications/etude.app");
    CHECK(mac_bundle_of_executable("/Applications/My Apps/etude 2.app/Contents/MacOS/etude") ==
          "/Applications/My Apps/etude 2.app");
    CHECK(mac_bundle_of_executable("/opt/build/app/etude").empty());
    CHECK(mac_bundle_of_executable("etude.app/Contents/MacOS/etude").empty()); // not absolute
    CHECK(mac_bundle_of_executable("/Applications/etude.app/Contents/Resources/etude").empty());
    CHECK(mac_bundle_of_executable("/Applications/etude.app/Contents/MacOS/").empty());
    CHECK(mac_bundle_of_executable("/Applications/etude.app/Contents/MacOS/a/b").empty());
    CHECK(mac_bundle_of_executable("/.app/Contents/MacOS/etude").empty());
}

TEST_CASE("mac install: a bundle in a writable folder on a writable volume can be replaced") {
    MacInstallFacts facts;
    facts.bundle = "/Applications/etude.app";
    facts.parent_writable = true;
    const MacInstallVerdict verdict = check_mac_install(facts);
    CHECK(verdict.can_install());
    CHECK(verdict.block == MacInstallBlock::None);
    CHECK(verdict.reason.empty());
}

TEST_CASE("mac install: a program that is not in a bundle installs nothing, and says so") {
    MacInstallFacts facts;
    facts.parent_writable = true;
    const MacInstallVerdict verdict = check_mac_install(facts);
    CHECK_FALSE(verdict.can_install());
    CHECK(verdict.block == MacInstallBlock::NotABundle);
    CHECK_FALSE(verdict.reason.empty());
}

TEST_CASE("mac install: App Translocation is named before anything else about the folder") {
    MacInstallFacts facts;
    facts.bundle = "/private/var/folders/ab/cd/T/AppTranslocation/7B1E-42/d/etude.app";
    facts.volume_read_only = true; // the translocated copy is a read-only mount
    const MacInstallVerdict verdict = check_mac_install(facts);
    CHECK(verdict.block == MacInstallBlock::Translocated);
    CHECK(verdict.reason.find("Translocation") != std::string::npos);
}

TEST_CASE("mac install: a disk image says so") {
    MacInstallFacts facts;
    facts.bundle = "/Volumes/etude 0.9.1/etude.app";
    facts.volume_read_only = true;
    facts.volume_is_disk_image = true;
    const MacInstallVerdict verdict = check_mac_install(facts);
    CHECK_FALSE(verdict.can_install());
    CHECK(verdict.block == MacInstallBlock::DiskImage);
    CHECK(verdict.reason == "it runs from a disk image");
    CHECK(verdict.reason.size() <= 44); // it sits on one status line at 800 px
}

TEST_CASE("mac install: a read-only volume that is not a disk image gets a reason of its own") {
    MacInstallFacts facts;
    facts.bundle = "/Volumes/Backup/etude.app";
    facts.volume_read_only = true;
    const MacInstallVerdict verdict = check_mac_install(facts);
    CHECK(verdict.block == MacInstallBlock::ReadOnlyVolume);
    CHECK(verdict.reason == "it runs from a read-only volume");
    CHECK(verdict.reason.size() <= 44);
    // A disk image that is writable is no obstacle by itself: the folder decides.
    facts.volume_read_only = false;
    facts.volume_is_disk_image = true;
    facts.parent_writable = true;
    CHECK(check_mac_install(facts).can_install());
}

TEST_CASE("mac install: a mount point is a disk image when hdiutil lists it") {
    const std::string plist =
        "<plist><dict><key>images</key><array><dict><key>system-entities</key><array><dict>"
        "<key>mount-point</key><string>/Volumes/etude 0.9.1</string></dict></array></dict></array></dict></plist>";
    CHECK(mac_mount_is_disk_image(plist, "/Volumes/etude 0.9.1"));
    CHECK_FALSE(mac_mount_is_disk_image(plist, "/Volumes/etude"));      // a longer name is not a match
    CHECK_FALSE(mac_mount_is_disk_image(plist, "/Volumes/Backup"));
    CHECK_FALSE(mac_mount_is_disk_image(plist, ""));
    CHECK_FALSE(mac_mount_is_disk_image("", "/Volumes/etude 0.9.1"));
}

TEST_CASE("mac install: a folder this user cannot write blocks the install") {
    MacInstallFacts facts;
    facts.bundle = "/Applications/etude.app";
    facts.parent_writable = false;
    const MacInstallVerdict verdict = check_mac_install(facts);
    CHECK(verdict.block == MacInstallBlock::NotWritable);
    CHECK(verdict.reason.find("can't change its folder") != std::string::npos);
    // A writable volume with a writable folder is the only way through.
    facts.parent_writable = true;
    CHECK(check_mac_install(facts).can_install());
}

TEST_CASE("mac install: the folders an update leaves are hidden and are not a second app") {
    CHECK(mac_staging_name("1.2.3") == ".etude-update-1.2.3");
    CHECK(mac_previous_name("1.1.0") == ".etude-1.1.0.previous");
    for (const std::string& name : {mac_staging_name("1.2.3"), mac_previous_name("1.1.0")}) {
        CHECK(name.front() == '.');
        CHECK(name.rfind(".app") != name.size() - 4);
    }
    CHECK(kMacPreviousPattern == ".etude-*.previous");
}
