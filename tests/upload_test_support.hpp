// SPDX-License-Identifier: GPL-3.0-or-later
// What the upload and folder-watch tests share: a temporary folder and small file helpers.
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace upload_test {

namespace fs = std::filesystem;

// A new empty folder under the system's temporary folder, removed with everything in it at the end.
struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device device;
        path = fs::temp_directory_path() /
               ("etude-upload-" + std::to_string(device()) + std::to_string(device()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

inline void write_file(const fs::path& p, const std::string& bytes = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << bytes;
}

inline std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The smallest thing the MIDI check takes: the header chunk's start and some bytes.
inline std::string midi_bytes(const std::string& extra = "") {
    return std::string("MThd\0\0\0\6\0\1\0\1\0\x60", 14) + extra;
}

// Every file under `root` ('/' separated, sorted), hidden folders included.
inline std::string tree(const fs::path& root) {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        names.push_back(it->path().lexically_relative(root).generic_string() +
                        (it->is_directory() ? "/" : ""));
    }
    std::sort(names.begin(), names.end());
    std::string out;
    for (const std::string& n : names) {
        out += n + "\n";
    }
    return out;
}

} // namespace upload_test
