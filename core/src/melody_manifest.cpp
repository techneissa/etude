// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/melody_manifest.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace etude {

namespace {

constexpr std::string_view kSeparator = " | ";

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string_view> split_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    while (true) {
        const std::size_t at = line.find(kSeparator);
        if (at == std::string_view::npos) {
            fields.push_back(trimmed(line));
            return fields;
        }
        fields.push_back(trimmed(line.substr(0, at)));
        line.remove_prefix(at + kSeparator.size());
    }
}

bool has_empty(const std::vector<std::string_view>& fields) {
    return std::any_of(fields.begin(), fields.end(),
                       [](std::string_view f) { return f.empty(); });
}

struct Reader {
    MelodyManifest out;
    std::unordered_map<std::string, std::size_t> file_at;
    std::unordered_map<std::string, std::size_t> path_at;
    std::vector<int> in_path; // per file: the path that has it, or -1
    int line = 0;

    void fail(const std::string& what) {
        out.errors.push_back("line " + std::to_string(line) + ": " + what);
    }

    void read_file(const std::vector<std::string_view>& f) {
        if (f.size() != 7 || has_empty(f)) {
            fail("a file line has seven fields, none empty (file, name, title, composer, step, source, licence)");
            return;
        }
        MelodyFileEntry e;
        e.file = std::string(f[1]);
        e.title = std::string(f[2]);
        e.composer = std::string(f[3]);
        const std::string step(f[4]);
        e.step = step.size() == 1 && step[0] >= '0' && step[0] <= '9' ? step[0] - '0' : 0;
        e.source = std::string(f[5]);
        e.licence = std::string(f[6]);
        if (e.step < kMelodyFirstStep || e.step > kMelodyLastStep) {
            fail(e.file + ": the step is " + step + ", not 1 to 5");
        }
        if (e.file.size() < 5 || e.file.compare(e.file.size() - 4, 4, ".mid") != 0 ||
            e.file.find('/') != std::string::npos) {
            fail(e.file + ": a file is a name ending in .mid");
        }
        if (file_at.count(e.file) != 0) {
            fail(e.file + " is listed twice");
            return;
        }
        file_at[e.file] = out.files.size();
        out.files.push_back(std::move(e));
        in_path.push_back(-1);
    }

    void read_version(const std::vector<std::string_view>& f) {
        if (f.size() != 3 || has_empty(f)) {
            fail("a version line has three fields, none empty (version, file, tag)");
            return;
        }
        const auto file = file_at.find(std::string(f[1]));
        if (file == file_at.end()) {
            fail("version for the unlisted file " + std::string(f[1]));
            return;
        }
        MelodyFileEntry& e = out.files[file->second];
        if (!e.version.empty()) {
            fail(e.file + " has a version tag already");
            return;
        }
        if (f[2].size() > kMelodyMaxTag) {
            fail(e.file + ": the tag \"" + std::string(f[2]) + "\" is over " + std::to_string(kMelodyMaxTag) +
                 " characters");
            return;
        }
        e.version = std::string(f[2]);
    }

    void read_path(const std::vector<std::string_view>& f) {
        if (f.size() != 4 || has_empty(f)) {
            fail("a path line has four fields, none empty (path, id, title, icon)");
            return;
        }
        MelodyPathEntry p;
        p.id = std::string(f[1]);
        p.title = std::string(f[2]);
        p.icon = std::string(f[3]);
        if (std::find(kMelodyIcons.begin(), kMelodyIcons.end(), f[3]) == kMelodyIcons.end()) {
            fail("path " + p.id + ": the icon " + p.icon + " is not one the page draws");
        }
        if (out.paths.size() >= static_cast<std::size_t>(kMelodyMaxPaths)) {
            fail("more than " + std::to_string(kMelodyMaxPaths) + " paths: the page lays out no more");
        }
        if (path_at.count(p.id) != 0) {
            fail("path " + p.id + " is listed twice");
            return;
        }
        path_at[p.id] = out.paths.size();
        out.paths.push_back(std::move(p));
    }

    void read_goal(const std::vector<std::string_view>& f) {
        if (f.size() != 4 || has_empty(f)) {
            fail("a goal line has four fields, none empty (goal, path id, file, goal)");
            return;
        }
        const auto path = path_at.find(std::string(f[1]));
        const auto file = file_at.find(std::string(f[2]));
        if (path == path_at.end()) {
            fail("goal for the unknown path " + std::string(f[1]));
            return;
        }
        if (file == file_at.end()) {
            fail("goal for the unlisted file " + std::string(f[2]));
            return;
        }
        if (in_path[file->second] >= 0) {
            fail(std::string(f[2]) + " is in a path already");
            return;
        }
        if (out.paths[path->second].files.size() >= static_cast<std::size_t>(kMelodyMaxPieces)) {
            fail("path " + out.paths[path->second].id + " has more than " +
                 std::to_string(kMelodyMaxPieces) + " pieces: the page lays out no more");
            return;
        }
        in_path[file->second] = static_cast<int>(path->second);
        out.paths[path->second].files.push_back(file->second);
        out.paths[path->second].goals.emplace_back(f[3]);
    }

    void check_whole() {
        for (const MelodyPathEntry& p : out.paths) {
            if (p.files.empty()) {
                out.errors.push_back("path " + p.id + " has no pieces");
            }
            for (std::size_t i = 1; i < p.files.size(); ++i) {
                const MelodyFileEntry& before = out.files[p.files[i - 1]];
                const MelodyFileEntry& after = out.files[p.files[i]];
                if (after.step < before.step) {
                    out.errors.push_back("path " + p.id + ": " + after.file + " (step " +
                                         std::to_string(after.step) + ") comes after " +
                                         before.file + " (step " + std::to_string(before.step) + ")");
                }
            }
        }
        for (std::size_t i = 0; i < out.files.size(); ++i) {
            if (in_path[i] < 0) {
                out.errors.push_back(out.files[i].file + " is in no path");
            }
        }
    }
};

} // namespace

MelodyManifest parse_melody_manifest(std::string_view text) {
    Reader r;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = trimmed(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++r.line;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string_view> fields = split_fields(line);
        if (fields[0] == "file") {
            r.read_file(fields);
        } else if (fields[0] == "path") {
            r.read_path(fields);
        } else if (fields[0] == "goal") {
            r.read_goal(fields);
        } else if (fields[0] == "version") {
            r.read_version(fields);
        } else {
            r.fail("unknown record \"" + std::string(fields[0]) + "\"");
        }
    }
    r.check_whole();
    return std::move(r.out);
}

} // namespace etude
