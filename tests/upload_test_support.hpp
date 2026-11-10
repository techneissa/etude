// SPDX-License-Identifier: GPL-3.0-or-later
// What the data and security tests share: a temporary folder, small file helpers and a shell runner.
#pragma once

#include <doctest/doctest.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
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

// A shell script a test runs as a stand-in program ("#!/bin/sh" first line). macOS checks each new
// executable on its first run (about 0.25 s), so every script is a hard link of one already-run
// trampoline that sources `<path>.body`: never copy the script elsewhere.
inline void write_script(const fs::path& path, const std::string& script) {
    static const struct Trampoline {
        fs::path file;
        Trampoline() {
            std::random_device device;
            file = fs::temp_directory_path() / ("etude-script-" + std::to_string(device()) + std::to_string(device()));
            std::ofstream(file) << "#!/bin/sh\n. \"$0.body\"\n";
            std::ofstream(file.string() + ".body") << ":\n";
            ::chmod(file.c_str(), 0755);
            const pid_t pid = ::fork(); // the one slow first run, here and not in the first test
            if (pid == 0) {
                ::execl(file.c_str(), file.c_str(), static_cast<char*>(nullptr));
                ::_exit(0);
            }
            if (pid > 0) {
                ::waitpid(pid, nullptr, 0);
            }
        }
        ~Trampoline() {
            std::error_code ec;
            fs::remove(file, ec);
            fs::remove(file.string() + ".body", ec);
        }
    } trampoline;
    constexpr std::string_view kShebang = "#!/bin/sh\n";
    write_file(path.string() + ".body", script.rfind(kShebang, 0) == 0 ? script.substr(kShebang.size()) : script);
    std::error_code ec;
    fs::remove(path, ec);
    fs::create_hard_link(trampoline.file, path, ec);
    if (ec) {
        fs::copy_file(trampoline.file, path, ec);
    }
}

// The output (stderr too) and exit status of a shell command.
struct ShellRun {
    int status = -1;
    std::string out;
};

inline ShellRun shell(const std::string& command) {
    ShellRun run;
    FILE* pipe = ::popen((command + " 2>&1 </dev/null").c_str(), "r");
    REQUIRE(pipe != nullptr);
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
        run.out += buffer;
    }
    const int raw = ::pclose(pipe);
    run.status = raw == -1 ? -1 : WEXITSTATUS(raw);
    return run;
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
