// SPDX-License-Identifier: GPL-3.0-or-later
// etude-update-helper: the root side of etude's app updates. The app never runs as root. It
// downloads an update, checks it, and leaves the tarball, its signature and a request in
// <prefix>/update/, a folder it owns. This program, started by systemd as root, treats all of that as
// hostile: it copies the files out by descriptor, verifies the signature itself against the key built
// into it, unpacks into a folder only root can write to, and only then points <prefix>/current at the
// new version. The data backup and restore run as the app's user. What it remembers (the update in
// progress, how many times the new version was started) is in <state dir>/state, so a power cut or a
// reboot never loses the way back.
//
//   etude-update-helper [--config FILE] apply    one update, from the request in <prefix>/update/
//   etude-update-helper [--config FILE] count    before each start of the app's service
//                                                  (ExecStartPre=+): counts it, goes back when out
//                                                  of starts, finishes an interrupted going back;
//                                                  its own trouble ends with 0, a going back that
//                                                  cannot put the data back with 75
//   etude-update-helper [--config FILE] crashed  before the crash screen (ExecStartPre=+): a version
//                                                  on trial that crashed is gone back from at once
//   etude-update-helper [--config FILE] idle     before the crash screen (ExecCondition=+): 0 unless
//                                                  the helper is stopping or starting the app itself
//   etude-update-helper [--config FILE] sandboxed count | crashed | idle
//                                                  what the app units run: the same, as a transient
//                                                  service under the update service's sandbox
//                                                  (sandbox.hpp), ending with the same status
//   etude-update-helper [--config FILE] resume   at boot: finishes what a reboot interrupted
//
// The settings are read from /etc/etude-update.conf (deploy/pi/setup.sh writes it): a file of
// KEY=value lines that must be root's and not writable by anyone else; it is parsed, never sourced.
#include "apply.hpp"
#include "context.hpp"
#include "interrupt.hpp"
#include "safe_io.hpp"
#include "sandbox.hpp"
#include "settle.hpp"

#include "etude/update_conf.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace etude::helper {

void note(const std::string& text) {
    std::fprintf(stderr, "etude-update: %s\n", text.c_str());
    std::fflush(stderr);
}

namespace {

constexpr const char* kDefaultConfig = "/etc/etude-update.conf";

// The settings file: root's, one hard link, nobody else may write it, a plain file of a size that fits.
bool load_conf(const std::string& path, UpdateConf& out, std::string& error) {
    FileLimits limits;
    limits.max_bytes = kMaxConfBytes;
    limits.check_owner = true;
    limits.owner = 0;
    limits.allow_group_other_write = false;
    PlainFile file = open_plain_file(AT_FDCWD, path, limits);
    if (file.status != OpenStatus::Ok) {
        error = path + ": " + (file.status == OpenStatus::Failed ? errno_text(file.error)
                               : file.status == OpenStatus::WrongOwner
                                   ? "it must be root's, with no write for group or others"
                                   : open_status_text(file.status));
        return false;
    }
    std::string text;
    if (!read_all_text(file.fd.get(), kMaxConfBytes, text, error)) {
        return false;
    }
    const ConfResult parsed = parse_update_conf(text);
    if (!parsed.ok()) {
        error = path + ": " + conf_error_text(parsed);
        return false;
    }
    out = parsed.conf;
    return true;
}

int usage() {
    std::fprintf(stderr, "usage: etude-update-helper [--config FILE] apply | count | crashed | resume | idle | sandboxed MODE\n");
    return 2;
}

// This program's own path, for the transient service that runs it again.
std::string own_path(const char* argv0) {
    char buffer[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (n > 0) {
        return std::string(buffer, static_cast<std::size_t>(n));
    }
    return argv0;
}

// `sandboxed MODE`: the mode as a transient service under the update service's sandbox. The status is
// the unit's; when the sandbox cannot be used the mode runs here, as it did before the sandbox, since
// a count that is skipped is a trial that never rolls back.
std::optional<int> run_in_sandbox(const Context& c, const std::string& mode, const std::string& config,
                                  const std::string& sandbox_file, const char* argv0) {
    SandboxRun run;
    run.helper = own_path(argv0);
    run.config = config == kDefaultConfig ? std::string() : config;
    run.mode = mode;
    run.unit = "etude-helper-" + mode + "-" + std::to_string(::getpid());
    run.limit_s = sandbox_limit_seconds(c, mode);
    std::string error;
    if (!load_sandbox(sandbox_file, run.properties, error)) {
        note("no sandbox: " + error);
        return std::nullopt;
    }
    // The sandbox names the state folder as a writable path, and a path that is not there ends the
    // unit's set-up: the helper makes it itself when it runs, so it is there for this.
    ::mkdir(c.conf.state_dir.c_str(), 0700);
    return run_sandboxed(c, run);
}

} // namespace

} // namespace etude::helper

int main(int argc, char** argv) {
    using namespace etude::helper;
    std::string config = kDefaultConfig;
    std::string sandbox_file = kDefaultSandboxFile;
    int busy_status = 1; // what `idle` ends with when an update is in flight
    bool report_start = false;
    std::vector<std::string> words;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config = argv[++i];
        } else if (std::strcmp(argv[i], "--sandbox") == 0 && i + 1 < argc) {
            sandbox_file = argv[++i];
        } else if (std::strcmp(argv[i], "--report-start") == 0) {
            report_start = true;
        } else if (std::strcmp(argv[i], "--busy-status") == 0 && i + 1 < argc) {
            busy_status = std::atoi(argv[++i]);
        } else {
            words.emplace_back(argv[i]);
        }
    }
    if (report_start) {
        // The first act of a sandboxed run, so the launcher knows it began (sandbox.hpp).
        const std::string line = std::string(kSandboxStartMarker) + "\n";
        if (::write(1, line.data(), line.size()) < 0) {
            // nothing to say it to
        }
    }
    const std::string command = words.empty() ? std::string() : words[0];
    const bool sandboxed = command == "sandboxed";
    const std::string mode = sandboxed ? (words.size() == 2 ? words[1] : std::string()) : command;
    if ((!sandboxed && words.size() != 1) || (sandboxed && words.size() != 2)) {
        return usage();
    }
    const bool gating_mode = mode == "count" || mode == "crashed" || mode == "idle";
    if (sandboxed ? !gating_mode : !(gating_mode || mode == "apply" || mode == "resume")) {
        return usage();
    }
    // `count` runs before every start of the app, `crashed` and `idle` before the crash screen: their own
    // trouble (not root, no settings, no app user) must never keep the app or the screen from starting,
    // so it ends with 0 and a line in the journal. The one thing that stops a start is `count`'s 75, a
    // going back that could not put the data back (and it needs a working helper to be known).
    const int on_trouble = gating_mode ? 0 : 2;
    if (::geteuid() != 0) {
        note("it only runs as root");
        return on_trouble;
    }
    ::umask(022);
    install_interrupt_handlers();
    Context c;
    std::string error;
    if (!load_conf(config, c.conf, error) || !find_app_user(c.conf.app_user, c.user, error)) {
        note("refusing to run: " + error);
        return on_trouble;
    }
    c.service_timeout_s = c.conf.service_timeout_s;
    c.ready_timeout_s = c.conf.ready_timeout_s;
    if (sandboxed) {
        if (const std::optional<int> status = run_in_sandbox(c, mode, config, sandbox_file, argv[0])) {
            return *status;
        }
        // The sandbox did not start: `idle` reads only root's lock, so it runs as it is; `count` and
        // `crashed` do the part that needs no sandbox (count_in_place), never the data restore.
        note(mode + ": the sandbox could not be started; running without it what needs none");
        if (mode == "count") {
            return count_in_place(c);
        }
        if (mode == "crashed") {
            return settle_after_crash_in_place(c);
        }
    }
    if (mode == "apply") {
        return apply_update(c);
    }
    if (mode == "idle") {
        const int busy = update_idle(c);
        return busy == 1 ? busy_status : busy;
    }
    if (mode == "resume") {
        return resume_boot(c);
    }
    if (mode == "crashed") {
        return settle_after_crash(c);
    }
    return count_start(c);
}
