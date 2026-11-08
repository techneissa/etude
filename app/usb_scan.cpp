// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_scan.hpp"

#include "etude/upload_rules.hpp"

#include <sys/stat.h>

#include <algorithm>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

bool hidden(const std::string& name) { return name.empty() || name.front() == '.'; }

// Folders a stick carries for its own sake, which hold no songs of the player's.
bool system_folder(const std::string& name) {
    return name == "System Volume Information" || name == "$RECYCLE.BIN" || name == "lost+found";
}

struct Walk {
    const std::size_t max_songs;
    const int max_depth;
    const std::size_t max_folders;
    const std::size_t max_entries;
    StickScan out;
    std::size_t folders = 0;
    bool stop = false; // a cap on songs or folders was reached (a depth cap only flags)
};

void walk(const fs::path& dir, const std::string& prefix, int depth, Walk& w) {
    if (w.stop) {
        return;
    }
    if (++w.folders > w.max_folders) {
        w.stop = w.out.truncated = true;
        return;
    }
    std::error_code ec;
    std::vector<fs::directory_entry> entries;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (entries.size() >= w.max_entries) {
            w.out.truncated = true; // a folder of more than it is worth sorting is cut short
            break;
        }
        entries.push_back(*it);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.path().filename().string() < b.path().filename().string();
    });
    for (const fs::directory_entry& entry : entries) {
        const std::string name = entry.path().filename().string();
        if (hidden(name) || entry.is_symlink(ec)) {
            continue;
        }
        if (entry.is_directory(ec)) {
            if (depth < w.max_depth && !system_folder(name)) {
                walk(entry.path(), prefix + name + "/", depth + 1, w);
            } else if (depth >= w.max_depth) {
                w.out.truncated = true; // there is more below
            }
        } else if (entry.is_regular_file(ec) && kind_of_name(name) == FileKind::Song) {
            if (w.out.songs.size() >= w.max_songs) {
                w.stop = w.out.truncated = true;
                return;
            }
            const std::uintmax_t size = entry.file_size(ec);
            w.out.songs.push_back({prefix + name, ec ? 0 : static_cast<std::uint64_t>(size)});
        }
        if (w.stop) {
            return;
        }
    }
}

// The device a path is on, or 0 when it cannot be read.
std::uint64_t device_of(const fs::path& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 ? static_cast<std::uint64_t>(st.st_dev) : 0;
}

void add_volumes(const fs::path& parent, const VolumeRoots& roots, std::vector<Volume>& out) {
    std::error_code ec;
    std::vector<std::uint64_t> own;
    for (const fs::path& path : roots.system) {
        own.push_back(device_of(path));
    }
    for (fs::directory_iterator it(parent, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (hidden(name) || it->is_symlink(ec) || !it->is_directory(ec)) {
            continue; // the Mac's startup volume is a link to /
        }
        const std::uint64_t device = device_of(it->path());
        if (device == 0 || std::find(own.begin(), own.end(), device) != own.end()) {
            continue; // unreadable, or the machine's own disk
        }
        out.push_back({name, it->path()});
    }
}

} // namespace

StickScan scan_stick(const fs::path& top, std::size_t max_songs, int max_depth,
                     std::size_t max_folders, std::size_t max_entries) {
    Walk w{max_songs, max_depth, max_folders, max_entries, {}, 0, false};
    walk(top, "", 0, w);
    std::sort(w.out.songs.begin(), w.out.songs.end(),
              [](const StickSong& a, const StickSong& b) { return a.path < b.path; });
    return std::move(w.out);
}

VolumeRoots default_volume_roots() {
    VolumeRoots roots;
#ifdef __APPLE__
    roots.direct = {"/Volumes"};
    roots.system = {"/", "/System/Volumes/Data"};
#else
    roots.shared = {"/media", "/run/media"};
#endif
    return roots;
}

std::vector<Volume> list_volumes(const VolumeRoots& roots) {
    std::vector<Volume> volumes;
    std::error_code ec;
    if (!roots.pretend.empty()) {
        if (fs::is_directory(roots.pretend, ec)) {
            volumes.push_back({roots.pretend.filename().string(), roots.pretend});
        }
        return volumes;
    }
    for (const fs::path& direct : roots.direct) {
        add_volumes(direct, roots, volumes);
    }
    for (const fs::path& shared : roots.shared) {
        for (fs::directory_iterator it(shared, fs::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec)) {
            if (!hidden(it->path().filename().string()) && it->is_directory(ec)) {
                add_volumes(it->path(), roots, volumes);
            }
        }
    }
    std::sort(volumes.begin(), volumes.end(),
              [](const Volume& a, const Volume& b) { return a.path < b.path; });
    return volumes;
}

} // namespace etude::app
