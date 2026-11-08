// SPDX-License-Identifier: GPL-3.0-or-later
// The steps of a Mac update that touch the file system and other programs, one function each so a
// test can run them against a temporary folder: the facts about the running bundle, the hash of the
// download, unpacking it beside the running bundle, checking the unpacked bundle, and starting the
// relaunch script (mac_relaunch_script.hpp). The judging is core mac_install.hpp's. No SDL.
#pragma once

#include "run_program.hpp"

#include "etude/mac_install.hpp"
#include "etude/minisign.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace etude::app {

// The program being run: /proc/self/exe on Linux, the executable path on a Mac; empty when unknown.
std::filesystem::path running_executable();

// The bundle `exe` is in (links resolved) and whether its folder and volume can be written. A
// read-only volume is looked up in `hdiutil info` (through `run`) to tell a disk image from another one.
MacInstallFacts gather_mac_install_facts(const std::filesystem::path& exe,
                                         const ProgramRunner& run = run_program);

// Exchanges two folders in one atomic call (renamex_np with RENAME_SWAP on a Mac, renameat2 with
// RENAME_EXCHANGE on Linux): at no moment is either path missing. False with `*error` (an errno
// value) when the system or the volume cannot.
bool swap_directories(const std::filesystem::path& a, const std::filesystem::path& b, int* error = nullptr);

// `etude --swap-bundle A B`, run before anything else starts: swap_directories and nothing more.
// The relaunch script uses it, from a copy of the new version's program, for the swap and for the
// rollback. 0 when done, 1 with the reason on stderr.
int run_swap_bundle(const char* a, const char* b);

// BLAKE2b-512 of the file, read in pieces of 256 KiB: the hash a prehashed minisign signature
// covers. False when the file cannot be read.
bool digest_of_file(const std::filesystem::path& file, Digest512& digest);

struct StepResult {
    bool ok = false;
    std::string why; // for the log and the page: one sentence
};

// `ditto -x -k` of `zip` into the new, empty folder `staging`.
StepResult unpack_release_zip(const ProgramRunner& run, const std::filesystem::path& zip,
                              const std::filesystem::path& staging);

// The one `.app` directly inside `folder`; empty when there is none or more than one.
std::filesystem::path find_unpacked_bundle(const std::filesystem::path& folder);

// A bundle fit to replace the running one: its program is there and executable, its code signature
// holds (`codesign --verify --deep --strict`) and its CFBundleShortVersionString is `version`.
StepResult check_new_bundle(const ProgramRunner& run, const std::filesystem::path& bundle,
                            std::string_view version);

// What the relaunch script is told: see the top of the script.
struct RelaunchPlan {
    long app_pid = 0;
    std::filesystem::path bundle;      // the running app, which will hold the new version
    std::filesystem::path new_bundle;  // unpacked and checked
    std::filesystem::path previous;    // where the running bundle is kept
    std::filesystem::path update_dir;  // holds ready, status, the script and its log
    std::string version;               // the new version
    std::string from;                  // the running version
    int ready_wait_s = 30;
};

// The arguments of the script run by /bin/sh: [sh, script, pid, bundle, ...].
std::vector<std::string> relaunch_command(const std::filesystem::path& script, const RelaunchPlan& plan);

// Writes the script into plan.update_dir (mode 0700, whole) and starts it in a session of its own.
// `spawn` stands in for spawn_detached in a test.
using SpawnFn = std::function<bool(const std::vector<std::string>& argv, const std::string& log)>;
StepResult start_relaunch(const RelaunchPlan& plan, const SpawnFn& spawn = {});

} // namespace etude::app
