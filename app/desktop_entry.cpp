// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_entry.hpp"

#include "update_files.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace etude::app {

namespace {

namespace fs = std::filesystem;

constexpr const char* kEntryName = "etude.desktop";

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

bool read_file(const fs::path& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream all;
    all << in.rdbuf();
    *out = all.str();
    return true;
}

bool is_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

fs::path icon_file(const fs::path& base, const std::string& icon) {
    return base / "icons" / "hicolor" / "256x256" / "apps" / (icon + ".png");
}

} // namespace

DesktopFolders desktop_folders(const std::string& xdg_data_home, const std::string& xdg_data_dirs,
                               const std::string& home) {
    DesktopFolders f;
    // The standard says a relative path in these variables is invalid and to be ignored.
    if (fs::path(xdg_data_home).is_absolute()) {
        f.data_home = xdg_data_home;
    } else if (fs::path(home).is_absolute()) {
        f.data_home = fs::path(home) / ".local" / "share";
    }
    std::string dirs = xdg_data_dirs.empty() ? "/usr/local/share:/usr/share" : xdg_data_dirs;
    std::size_t at = 0;
    while (at <= dirs.size()) {
        std::size_t end = dirs.find(':', at);
        end = end == std::string::npos ? dirs.size() : end;
        if (end > at && fs::path(dirs.substr(at, end - at)).is_absolute()) {
            f.data_dirs.emplace_back(dirs.substr(at, end - at));
        }
        at = end + 1;
    }
    return f;
}

DesktopFolders desktop_folders_from_env() {
    const auto get = [](const char* name) {
        const char* v = std::getenv(name);
        return v == nullptr ? std::string() : std::string(v);
    };
    return desktop_folders(get("XDG_DATA_HOME"), get("XDG_DATA_DIRS"), get("HOME"));
}

std::string desktop_entry_with_icon(const std::string& entry, const std::string& icon) {
    const std::vector<std::string> lines = split_lines(entry);
    std::vector<std::string> out;
    bool in_group = false;
    bool seen_group = false;
    bool done = false;
    for (const std::string& line : lines) {
        const bool header = !line.empty() && line.front() == '[';
        if (in_group && header && !done) { // the group ends without an Icon= line
            out.push_back("Icon=" + icon);
            done = true;
        }
        if (header) {
            in_group = line == "[Desktop Entry]";
            seen_group = seen_group || in_group;
        }
        if (in_group && line.rfind("Icon=", 0) == 0) {
            if (!done) {
                out.push_back("Icon=" + icon);
                done = true;
            }
            continue;
        }
        out.push_back(line);
    }
    if (seen_group && !done) {
        out.push_back("Icon=" + icon);
    }
    std::string text;
    for (const std::string& line : out) {
        text += line + "\n";
    }
    return text;
}

std::string icon_line(const std::string& entry) {
    bool in_group = false;
    for (const std::string& line : split_lines(entry)) {
        if (!line.empty() && line.front() == '[') {
            in_group = line == "[Desktop Entry]";
        } else if (in_group && line.rfind("Icon=", 0) == 0) {
            return line.substr(5);
        }
    }
    return {};
}

DesktopResult apply_desktop_icon(const DesktopFolders& folders, const std::string& icon,
                                 std::span<const unsigned char> png256,
                                 std::span<const std::string> same_as) {
    if (folders.data_home.empty()) {
        return DesktopResult::NoUserFolder;
    }
    const fs::path mine = folders.data_home / "applications" / kEntryName;
    // The entry to copy: the system's (so a reinstall's new Exec= line is taken), else the user's own.
    std::string base;
    bool system_entry = false;
    for (std::size_t i = 0; !system_entry && i < folders.data_dirs.size(); ++i) {
        system_entry = read_file(folders.data_dirs[i] / "applications" / kEntryName, &base);
    }
    if (!system_entry && !read_file(mine, &base)) {
        return DesktopResult::NotInstalled;
    }
    if (!system_entry && base.rfind(kDesktopCopyMarker, 0) == 0) {
        base.erase(0, std::string(kDesktopCopyMarker).size() + 1); // the copy's own marker line
    }
    std::string current;
    const bool have_copy = read_file(mine, &current);
    const bool ours = have_copy && current.rfind(kDesktopCopyMarker, 0) == 0;
    // The system's entry already shows the icon: no copy is wanted, and ours is taken away.
    const std::string given = icon_line(base);
    const bool gives_it =
        system_entry &&
        (given == icon || std::find(same_as.begin(), same_as.end(), given) != same_as.end());
    if (gives_it) {
        if (!ours) {
            return DesktopResult::Unchanged;
        }
        std::error_code ec;
        return fs::remove(mine, ec) && !ec ? DesktopResult::Done : DesktopResult::Failed;
    }
    bool installed = is_file(icon_file(folders.data_home, icon));
    for (std::size_t i = 0; !installed && i < folders.data_dirs.size(); ++i) {
        installed = is_file(icon_file(folders.data_dirs[i], icon));
    }
    if (!installed) {
        std::error_code ec;
        const fs::path file = icon_file(folders.data_home, icon);
        fs::create_directories(file.parent_path(), ec);
        const std::string bytes(reinterpret_cast<const char*>(png256.data()), png256.size());
        if (ec || !write_file_atomic(file, bytes)) {
            return DesktopResult::Failed;
        }
    }
    const std::string wanted =
        std::string(kDesktopCopyMarker) + "\n" + desktop_entry_with_icon(base, icon);
    if (have_copy && current == wanted) {
        return DesktopResult::Unchanged;
    }
    if (have_copy && !ours) { // the player's own copy: left as it is
        return DesktopResult::Unchanged;
    }
    std::error_code ec;
    fs::create_directories(mine.parent_path(), ec);
    if (ec || !write_file_atomic(mine, wanted)) {
        return DesktopResult::Failed;
    }
    return DesktopResult::Done;
}

DesktopResult apply_launcher_colour(const DesktopFolders& folders, etude::IconColour colour,
                                    std::span<const unsigned char> png256) {
    static const std::array<std::string, 1> plain{"etude"}; // the entry's own icon, wine
    return apply_desktop_icon(folders, etude::icon_name(colour), png256,
                              colour == etude::kDefaultIconColour
                                  ? std::span<const std::string>(plain)
                                  : std::span<const std::string>());
}

} // namespace etude::app
