// SPDX-License-Identifier: GPL-3.0-or-later
#include "mac_update.hpp"

#include "mac_relaunch_script.hpp"
#include "update_files.hpp"

#include <sys/statvfs.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdio.h> // renamex_np
#include <sys/mount.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fstream>
#include <system_error>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kDigestPiece = 256 * 1024;
constexpr int kUnpackSeconds = 300;
constexpr int kCheckSeconds = 120;
constexpr std::string_view kProgramPath = "Contents/MacOS/etude";

std::string first_line_of(const std::string& text) {
    return text.substr(0, text.find('\n'));
}

// The words of a program's failure: what it printed, cut to one short line.
std::string said(const ProgramResult& result) {
    std::string line = first_line_of(result.output);
    return line.size() > 160 ? line.substr(0, 160) : line;
}

} // namespace

fs::path running_executable() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return {};
    }
    path.resize(std::char_traits<char>::length(path.c_str()));
    return path;
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : exe;
#endif
}

MacInstallFacts gather_mac_install_facts(const fs::path& exe, const ProgramRunner& run) {
    MacInstallFacts facts;
    std::error_code ec;
    const fs::path real = fs::canonical(exe, ec);
    if (ec) {
        return facts;
    }
    facts.bundle = mac_bundle_of_executable(real.string());
    if (facts.bundle.empty()) {
        return facts;
    }
    // Renaming a bundle needs the folder around it: write and search.
    const fs::path parent = fs::path(facts.bundle).parent_path();
    facts.parent_writable = ::access(parent.c_str(), W_OK | X_OK) == 0;
    struct statvfs volume {};
    facts.volume_read_only = ::statvfs(parent.c_str(), &volume) == 0 && (volume.f_flag & ST_RDONLY) != 0;
#if defined(__APPLE__)
    struct statfs mount {};
    if (facts.volume_read_only && ::statfs(parent.c_str(), &mount) == 0) {
        const ProgramResult images = run({"/usr/bin/hdiutil", "info", "-plist"}, 15);
        facts.volume_is_disk_image =
            images.started && images.exit_code == 0 && mac_mount_is_disk_image(images.output, mount.f_mntonname);
    }
#else
    (void)run;
#endif
    return facts;
}

bool swap_directories(const fs::path& a, const fs::path& b, int* error) {
    int result = -1;
#if defined(__APPLE__)
    result = ::renamex_np(a.c_str(), b.c_str(), RENAME_SWAP);
#elif defined(__linux__) && defined(SYS_renameat2)
    result = static_cast<int>(::syscall(SYS_renameat2, AT_FDCWD, a.c_str(), AT_FDCWD, b.c_str(), 2U /* RENAME_EXCHANGE */));
#else
    errno = ENOSYS;
#endif
    if (result != 0 && error != nullptr) {
        *error = errno;
    }
    return result == 0;
}

int run_swap_bundle(const char* a, const char* b) {
    int error = 0;
    if (!swap_directories(a, b, &error)) {
        std::fprintf(stderr, "swap-bundle: %s and %s could not be exchanged: %s\n", a, b,
                     std::strerror(error));
        return 1;
    }
    return 0;
}

bool digest_of_file(const fs::path& file, Digest512& digest) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    FileDigest hash;
    std::string piece(kDigestPiece, '\0');
    while (in.read(piece.data(), static_cast<std::streamsize>(piece.size())) || in.gcount() > 0) {
        hash.update({reinterpret_cast<const std::uint8_t*>(piece.data()),
                     static_cast<std::size_t>(in.gcount())});
    }
    if (!in.eof()) {
        return false;
    }
    digest = hash.finish();
    return true;
}

StepResult unpack_release_zip(const ProgramRunner& run, const fs::path& zip, const fs::path& staging) {
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    if (ec) {
        return {false, "The update's folder could not be made beside the app"};
    }
    const ProgramResult result =
        run({"/usr/bin/ditto", "-x", "-k", zip.string(), staging.string()}, kUnpackSeconds);
    if (!result.started || result.timed_out || result.exit_code != 0) {
        fs::remove_all(staging, ec);
        return {false, "The download could not be unpacked"};
    }
    return {true, {}};
}

fs::path find_unpacked_bundle(const fs::path& folder) {
    fs::path found;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".app" || !it->is_directory(ec)) {
            continue;
        }
        if (!found.empty()) {
            return {}; // two bundles: not what was released
        }
        found = it->path();
    }
    return found;
}

StepResult check_new_bundle(const ProgramRunner& run, const fs::path& bundle, std::string_view version) {
    std::error_code ec;
    const fs::path program = bundle / std::string(kProgramPath);
    if (!fs::is_regular_file(program, ec) || ::access(program.c_str(), X_OK) != 0) {
        return {false, "The new version has no program to start"};
    }
    const ProgramResult signature =
        run({"/usr/bin/codesign", "--verify", "--deep", "--strict", bundle.string()}, kCheckSeconds);
    if (!signature.started || signature.timed_out || signature.exit_code != 0) {
        return {false, "The new version's code signature does not hold" +
                           (said(signature).empty() ? std::string() : ": " + said(signature))};
    }
    const ProgramResult plist = run({"/usr/bin/plutil", "-extract", "CFBundleShortVersionString", "raw",
                                     "-o", "-", (bundle / "Contents/Info.plist").string()},
                                    kCheckSeconds);
    const std::string found = first_line_of(plist.output);
    if (!plist.started || plist.exit_code != 0 || found != version) {
        return {false, "The new version says it is " + (found.empty() ? std::string("unknown") : found) +
                           ", not " + std::string(version)};
    }
    return {true, {}};
}

std::vector<std::string> relaunch_command(const fs::path& script, const RelaunchPlan& plan) {
    return {"/bin/sh",
            script.string(),
            std::to_string(plan.app_pid),
            plan.bundle.string(),
            plan.new_bundle.string(),
            plan.previous.string(),
            (plan.update_dir / "ready").string(),
            (plan.update_dir / "status").string(),
            plan.version,
            plan.from,
            std::to_string(plan.ready_wait_s)};
}

StepResult start_relaunch(const RelaunchPlan& plan, const SpawnFn& spawn) {
    const fs::path script = plan.update_dir / "relaunch.sh";
    std::error_code ec;
    fs::remove(script, ec);
    int error = 0;
    if (!write_file_atomic(script, std::string(mac_relaunch_script()), &error)) {
        return {false, "The installer could not be saved: " + std::error_code(error, std::generic_category()).message()};
    }
    fs::permissions(script, fs::perms::owner_all, ec);
    if (ec) {
        return {false, "The installer could not be made runnable"};
    }
    const std::string log = (plan.update_dir / "relaunch.log").string();
    const std::vector<std::string> argv = relaunch_command(script, plan);
    const bool started = spawn ? spawn(argv, log) : spawn_detached(argv, log);
    if (!started) {
        return {false, "The installer could not be started"};
    }
    return {true, {}};
}

} // namespace etude::app
