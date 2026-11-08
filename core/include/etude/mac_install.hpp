// SPDX-License-Identifier: GPL-3.0-or-later
// What a Mac needs to decide before the app replaces its own bundle: which bundle is running, whether
// it can be replaced where it is, and the names of the folders an update leaves beside it. Pure text
// in, plain structs out: the app gathers the facts (app/mac_update.hpp), this file judges them.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// "/Applications/etude.app" for "/Applications/etude.app/Contents/MacOS/etude"; empty when
// `exe` is not the main program of a `.app` bundle.
std::string mac_bundle_of_executable(std::string_view exe);

enum class MacInstallBlock : std::uint8_t {
    None,           // the bundle can be replaced where it stands
    NotABundle,     // not running from a .app (a build folder, a loose binary)
    Translocated,   // macOS runs a downloaded app from a read-only copy (App Translocation)
    DiskImage,      // run from a mounted disk image (read-only)
    ReadOnlyVolume, // another volume that is mounted read-only
    NotWritable,    // the folder holding the bundle is not writable by this user
};

struct MacInstallFacts {
    std::string bundle;          // mac_bundle_of_executable of the running program
    bool parent_writable = false; // the folder that holds the bundle can be written (and searched)
    bool volume_read_only = false; // the volume the bundle is on is mounted read-only
    bool volume_is_disk_image = false; // and that volume is a mounted disk image
};

struct MacInstallVerdict {
    MacInstallBlock block = MacInstallBlock::None;
    std::string reason; // for the status line: why Update cannot install here; empty for None

    bool can_install() const { return block == MacInstallBlock::None; }
};

MacInstallVerdict check_mac_install(const MacInstallFacts& facts);

// Whether `mount_point` is one of the disk images `hdiutil info -plist` lists: its text holds the
// mount point as a <string> element.
bool mac_mount_is_disk_image(std::string_view hdiutil_plist, std::string_view mount_point);

// The folder an update unpacks into, beside the running bundle so the final move stays on one
// volume: ".etude-update-1.2.3".
std::string mac_staging_name(std::string_view version);
// Where the bundle of the version being replaced is kept: ".etude-1.1.0.previous". Hidden, and
// without ".app", so neither Finder nor Launch Services lists it as a second copy of the app.
std::string mac_previous_name(std::string_view from_version);
// The pattern for every kept previous version, which the relaunch helper clears but the newest.
inline constexpr std::string_view kMacPreviousPattern = ".etude-*.previous";

} // namespace etude
