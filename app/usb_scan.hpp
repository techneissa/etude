// SPDX-License-Identifier: GPL-3.0-or-later
// Finding USB sticks and the songs on them, for the Add songs page. Everything here reads the file
// system and so runs on a worker thread (app/sources_controller.hpp), never on the frame thread.
// The scan is bounded: a depth, a count of songs and a count of folders looked into, so a stick
// with a whole disk's files on it cannot hold the worker. Links are not followed and hidden files
// and folders are not entered. Nothing is ever written to a stick. No SDL.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace etude::app {

// The most song files one scan lists, the deepest folder it enters (the stick's top is 0), and the
// most folders it looks into.
inline constexpr std::size_t kMaxStickSongs = 500;
inline constexpr int kMaxStickDepth = 6;
inline constexpr std::size_t kMaxStickFolders = 3000;
// The most entries one folder is read for, so the list held for sorting has a limit too.
inline constexpr std::size_t kMaxFolderEntries = 20000;

struct StickSong {
    std::string path; // under the stick's top, '/' separated
    std::uint64_t size = 0;
};

struct StickScan {
    std::vector<StickSong> songs; // in path order
    bool truncated = false;       // the caps stopped the scan: there may be more
};

// The song files (the library's formats; not PDFs or photos) under `top`, within the caps.
StickScan scan_stick(const std::filesystem::path& top, std::size_t max_songs = kMaxStickSongs,
                     int max_depth = kMaxStickDepth, std::size_t max_folders = kMaxStickFolders,
                     std::size_t max_entries = kMaxFolderEntries);

struct Volume {
    std::string label; // the folder's name, which is the stick's label
    std::filesystem::path path;
    bool operator==(const Volume& o) const { return path == o.path; }
};

// Where removable volumes are mounted. `shared` folders hold one folder for each user, which holds
// the volumes (/media, /run/media); `direct` folders hold the volumes (/Volumes). A folder that
// is a link, hidden, unreadable or on the same device as one of the `system` paths is not a stick
// (the Mac's startup volume shows in /Volumes as a link to /, its data volume as another disk).
// `pretend`, when not empty, is the only volume (a developer flag): listed while the folder exists.
struct VolumeRoots {
    std::vector<std::filesystem::path> shared;
    std::vector<std::filesystem::path> direct;
    std::vector<std::filesystem::path> system;
    std::filesystem::path pretend;
};

// The roots of this platform: /media and /run/media on Linux, /Volumes on the Mac.
VolumeRoots default_volume_roots();

// The volumes mounted now, in path order.
std::vector<Volume> list_volumes(const VolumeRoots& roots);

} // namespace etude::app
