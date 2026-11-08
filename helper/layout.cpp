// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout.hpp"

#include "safe_io.hpp"

#include "etude/semver.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <cerrno>
#include <cstdlib>

namespace etude::helper {

namespace {

bool plain_version(const std::string& v) {
    const std::optional<SemVer> parsed = parse_semver(v);
    return v.size() <= 32 && parsed && parsed->prerelease.empty() && v.find('+') == std::string::npos;
}

std::string trusted_folder(const std::string& path) {
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        return path + ": " + errno_text(errno);
    }
    if (!S_ISDIR(st.st_mode)) {
        return path + " is not a folder (a link or a file)";
    }
    if (st.st_uid != ::geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return path + " is not root's alone: its owner and mode must be root and not group or world writable";
    }
    return {};
}

// Every folder above the prefix, as it really is (links resolved), must be root's (or the helper's own
// user's, which is root in service) and not writable by group or others, and must not be the app's:
// whoever can rename a folder above the prefix can put anything where the prefix was.
std::string ancestors_problem(const Context& c) {
    char real[PATH_MAX];
    if (::realpath(c.conf.prefix.c_str(), real) == nullptr) {
        return c.conf.prefix + ": " + errno_text(errno);
    }
    std::string path = real;
    while (path.size() > 1) {
        const std::size_t slash = path.rfind('/');
        path = slash == 0 ? "/" : path.substr(0, slash);
        struct stat st{};
        if (::stat(path.c_str(), &st) != 0) {
            return path + ": " + errno_text(errno);
        }
        const bool trusted_owner = st.st_uid == 0 || st.st_uid == ::geteuid();
        const bool app_owns = c.user.uid != ::geteuid() && st.st_uid == c.user.uid;
        if (!S_ISDIR(st.st_mode) || !trusted_owner || app_owns ||
            (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
            return path + " above the prefix is not root's alone (the app could rename a folder "
                          "above " + c.conf.prefix + ")";
        }
        if (path == c.ancestor_floor || path == "/") {
            break;
        }
    }
    return {};
}

} // namespace

std::string check_layout(const Context& c) {
    std::string problem = trusted_folder(c.conf.prefix);
    if (problem.empty()) {
        problem = ancestors_problem(c);
    }
    if (!problem.empty()) {
        return problem;
    }
    if (::mkdir(c.versions_dir().c_str(), 0755) != 0 && errno != EEXIST) {
        return c.versions_dir() + ": " + errno_text(errno);
    }
    return trusted_folder(c.versions_dir());
}

std::string link_version(const Context& c, const std::string& link) {
    char target[256];
    const ssize_t n = ::readlink((c.conf.prefix + "/" + link).c_str(), target, sizeof target - 1);
    if (n <= 0) {
        return {};
    }
    std::string text(target, static_cast<std::size_t>(n));
    while (text.size() > 1 && text.back() == '/') {
        text.pop_back();
    }
    constexpr std::string_view kPrefix = "versions/";
    if (text.compare(0, kPrefix.size(), kPrefix) != 0) {
        return {};
    }
    text.erase(0, kPrefix.size());
    return plain_version(text) ? text : std::string();
}

bool version_in_use(const Context& c, const std::string& version) {
    for (const char* link : {"current", "previous"}) {
        struct stat st{};
        if (::lstat((c.conf.prefix + "/" + link).c_str(), &st) != 0) {
            continue; // no such link (`previous` may be none): nothing there to protect
        }
        const std::string pointed = link_version(c, link);
        if (pointed.empty()) {
            // It is there but is not the `versions/X` form the helper writes (a `./versions/X`, an
            // absolute path, a link to something else): what it points at cannot be told, so nothing
            // is removed while it is like that.
            return true;
        }
        if (pointed == version) {
            return true;
        }
    }
    return false;
}

#if defined(ETUDE_HELPER_TEST_HOOKS)
namespace {
int g_fail_links_from = 0;
bool g_fail_fsync = false;
} // namespace
void fail_links_from_in_test(int nth) { g_fail_links_from = nth; }
void fail_fsync_in_test(bool fail) { g_fail_fsync = fail; }
#endif

bool point_link(const Context& c, const std::string& link, const std::string& version,
                std::string& error) {
#if defined(ETUDE_HELPER_TEST_HOOKS)
    if (g_fail_links_from > 0 && --g_fail_links_from == 0) {
        g_fail_links_from = -1; // every call from here on
    }
    if (g_fail_links_from < 0) {
        error = "a link that a test made fail";
        return false;
    }
#endif
    const std::string path = c.conf.prefix + "/" + link;
    // The link is only as good as the folder entry that holds it: a power cut after the rename but
    // before the folder is written could bring back the old link while the state says the new one. The
    // rename has happened by then, so a folder that cannot be written out is logged, not failed: the
    // callers treat a false answer as "nothing changed" and would undo or remove what is in use.
    const auto durable = [&]() {
        int why = 0;
        bool ok = false;
#if defined(ETUDE_HELPER_TEST_HOOKS)
        if (g_fail_fsync) {
            why = EIO;
        } else
#endif
        {
            const int dir = ::open(c.conf.prefix.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            ok = dir >= 0 && ::fsync(dir) == 0;
            why = errno;
            if (dir >= 0) {
                ::close(dir);
            }
        }
        if (!ok) {
            note(c.conf.prefix + ": the " + link + " link is in place but could not be written out (" +
                 errno_text(why) + ")");
        }
        return true;
    };
    if (version.empty()) {
        if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
            error = path + ": " + errno_text(errno);
            return false;
        }
        return durable();
    }
    if (!plain_version(version)) {
        error = "not a version: " + version;
        return false;
    }
    const std::string temp = c.conf.prefix + "/." + link + ".new";
    ::unlink(temp.c_str());
    if (::symlink(("versions/" + version).c_str(), temp.c_str()) != 0 ||
        ::rename(temp.c_str(), path.c_str()) != 0) {
        error = path + ": " + errno_text(errno);
        ::unlink(temp.c_str());
        return false;
    }
    return durable();
}

void clean_leftovers(const Context& c) {
    std::string ignore;
    for (const char* name : {".current.new", ".previous.new"}) {
        ::unlink((c.conf.prefix + "/" + name).c_str());
    }
    Fd versions = open_directory(c.versions_dir(), ignore);
    if (!versions.ok()) {
        return;
    }
    DIR* dir = ::fdopendir(::dup(versions.get()));
    if (dir == nullptr) {
        return;
    }
    std::vector<std::string> doomed;
    while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        const bool new_tree = name.size() > 4 && name.compare(name.size() - 4, 4, ".new") == 0;
        if (new_tree || name.compare(0, 9, ".incoming") == 0) {
            doomed.push_back(name);
        }
    }
    ::closedir(dir);
    for (const std::string& name : doomed) {
        remove_tree_at(versions.get(), name, ignore);
    }
}

std::vector<std::string> list_versions(const Context& c) {
    std::vector<std::string> out;
    DIR* dir = ::opendir(c.versions_dir().c_str());
    if (dir == nullptr) {
        return out;
    }
    while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (plain_version(name)) {
            out.push_back(name);
        }
    }
    ::closedir(dir);
    return out;
}

void prune_versions(const Context& c, const std::string& keep_a, const std::string& keep_b) {
    std::string ignore;
    Fd versions = open_directory(c.versions_dir(), ignore);
    if (!versions.ok()) {
        return;
    }
    for (const std::string& name : list_versions(c)) {
        // Never what `current` or `previous` points at, looked at just before each removal.
        if (name != keep_a && name != keep_b && !version_in_use(c, name)) {
            std::string error;
            if (remove_tree_at(versions.get(), name, error)) {
                note("removed version " + name);
            } else {
                note("could not remove version " + name + ": " + error);
            }
        }
    }
}

} // namespace etude::helper
