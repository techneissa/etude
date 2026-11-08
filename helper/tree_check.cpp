// SPDX-License-Identifier: GPL-3.0-or-later
#include "tree_check.hpp"

#include "safe_io.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

namespace etude::helper {

namespace {

constexpr int kMaxDepth = 64;
constexpr std::size_t kMaxEntries = 200'000;

struct Walk {
    std::string root;      // the real path of the folder being checked
    std::size_t seen = 0;
};

bool inside(const std::string& root, const std::string& path) {
    return path == root || (path.size() > root.size() && path.compare(0, root.size(), root) == 0 &&
                            path[root.size()] == '/');
}

std::string mode_problem(const struct stat& st) {
    if (st.st_uid != ::geteuid() || st.st_gid != ::getegid()) {
        return "not owned by the helper's own user (root)";
    }
    if ((st.st_mode & (S_ISUID | S_ISGID | S_ISVTX)) != 0) {
        return "has a set-uid, set-gid or sticky bit";
    }
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return "can be written by others than root";
    }
    return {};
}

// A link is fine only when it resolves (`realpath`, following every link on the way) to something
// inside the folder: one that points at nothing, or out, is refused.
std::string link_problem(const Walk& w, const std::string& path) {
    char resolved[PATH_MAX];
    if (::realpath(path.c_str(), resolved) == nullptr) {
        return errno == ENOENT ? "a link that points at nothing"
                               : "a link that does not resolve: " + errno_text(errno);
    }
    return inside(w.root, resolved) ? std::string() : "a link that leaves the folder";
}

std::string check_entry(Walk& w, const std::string& path, int depth);

std::string check_folder(Walk& w, const std::string& path, int depth) {
    DIR* d = ::opendir(path.c_str());
    if (d == nullptr) {
        return "cannot read " + path + ": " + errno_text(errno);
    }
    std::vector<std::string> names;
    while (const dirent* entry = ::readdir(d)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            names.push_back(name);
        }
    }
    ::closedir(d);
    for (const std::string& name : names) {
        const std::string problem = check_entry(w, path + "/" + name, depth + 1);
        if (!problem.empty()) {
            return problem;
        }
    }
    return {};
}

std::string shown(const Walk& w, const std::string& path) {
    return path.size() > w.root.size() ? path.substr(w.root.size() + 1) : std::string(".");
}

std::string check_entry(Walk& w, const std::string& path, int depth) {
    if (depth > kMaxDepth || ++w.seen > kMaxEntries) {
        return "too many entries or folders nested too deep";
    }
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        return "cannot look at " + shown(w, path) + ": " + errno_text(errno);
    }
    if (S_ISLNK(st.st_mode)) {
        const std::string problem = link_problem(w, path);
        return problem.empty() ? problem : problem + ": " + shown(w, path);
    }
    if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
        return "a file that is not a plain file, folder or link: " + shown(w, path);
    }
    std::string problem = mode_problem(st);
    if (problem.empty() && S_ISREG(st.st_mode) && st.st_nlink != 1) {
        problem = "has more than one hard link";
    }
    if (!problem.empty()) {
        return shown(w, path) + " " + problem;
    }
    return S_ISDIR(st.st_mode) ? check_folder(w, path, depth) : std::string();
}

} // namespace

std::string tree_problem(const std::string& root) {
    struct stat st{};
    if (::lstat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        return "not a folder";
    }
    char resolved[PATH_MAX];
    if (::realpath(root.c_str(), resolved) == nullptr) {
        return "not a folder";
    }
    Walk w;
    w.root = resolved;
    std::string problem = mode_problem(st);
    if (problem.empty()) {
        problem = check_folder(w, w.root, 0);
    } else {
        problem = "the folder " + problem;
    }
    if (!problem.empty()) {
        return problem;
    }
    struct stat app{};
    if (::stat((w.root + "/bin/etude").c_str(), &app) != 0 || !S_ISREG(app.st_mode) ||
        (app.st_mode & S_IXUSR) == 0) {
        return "no bin/etude";
    }
    struct stat share{};
    if (::stat((w.root + "/share/etude").c_str(), &share) != 0 || !S_ISDIR(share.st_mode)) {
        return "no share/etude";
    }
    return {};
}

} // namespace etude::helper
