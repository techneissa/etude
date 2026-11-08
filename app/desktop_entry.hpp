// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher entry on Linux and the Pi: switching its icon to a colour's set, in the user's own
// folders so no root is needed. The system's etude.desktop (installed by deploy/pi/setup.sh,
// next to every colour's hicolor icons) is copied to the user's applications folder with its Icon=
// line set to the colour's icon name; the user's copy wins over the system's (and is made again from the
// system's entry each time, so a reinstall's changes are not lost). When the colour's
// icons are not installed anywhere, the 256 px PNG built into the binary is put in the user's
// hicolor folder. No SDL.
#pragma once

#include "etude/icon_colour.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace etude::app {

// The folders the launcher looks in (the XDG data home and data dirs).
struct DesktopFolders {
    std::filesystem::path data_home;
    std::vector<std::filesystem::path> data_dirs;
};

// From the environment as given (`xdg_data_home`, `xdg_data_dirs`, `home` read from XDG_DATA_HOME,
// XDG_DATA_DIRS and HOME; empty for unset): the XDG defaults are ~/.local/share and
// /usr/local/share:/usr/share. A relative path in either variable is ignored, as the standard says.
// With no HOME and no absolute XDG_DATA_HOME `data_home` is empty: there is no user folder to
// write.
DesktopFolders desktop_folders(const std::string& xdg_data_home, const std::string& xdg_data_dirs,
                               const std::string& home);
DesktopFolders desktop_folders_from_env();

// `entry` with its [Desktop Entry] group's Icon= line set to `icon` (added at the group's end when
// there is none). Other groups and lines are kept as they are.
std::string desktop_entry_with_icon(const std::string& entry, const std::string& icon);

enum class DesktopResult : unsigned char {
    Done,         // the user's entry names the icon now, or the copy made earlier is gone
    Unchanged,    // nothing to do: it did already, or the system's entry already gives the icon
    NotInstalled, // no etude.desktop in any folder: a build run in place has no launcher to
                  // change
    NoUserFolder, // nowhere to write: no HOME and no absolute XDG_DATA_HOME
    Failed,       // a file or folder could not be written (the app carries on)
};

// Points the user's launcher entry at the icon set `icon` ("etude-teal"). `png256` is put in the
// user's hicolor folder when no folder holds that icon's 256 px file. When the system's entry
// already gives `icon` (its Icon= is `icon` or one of `same_as`), no copy is wanted: none is
// written, and one this function wrote earlier (it carries a marker line) is removed; a copy the
// user made themselves is never touched.
DesktopResult apply_desktop_icon(const DesktopFolders& folders, const std::string& icon,
                                 std::span<const unsigned char> png256,
                                 std::span<const std::string> same_as = {});

// `colour`'s launcher: apply_desktop_icon for its icon set, wine also counting as the plain
// "etude" icon the system's entry names.
DesktopResult apply_launcher_colour(const DesktopFolders& folders, etude::IconColour colour,
                                    std::span<const unsigned char> png256);

// The marker line at the top of a copy this module writes.
inline constexpr const char* kDesktopCopyMarker = "# Written by etude for the icon colour chosen "
                                                  "in Settings > About; it is rewritten or removed "
                                                  "when the colour changes.";

} // namespace etude::app
