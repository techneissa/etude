// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox.hpp"

#include "process.hpp"
#include "safe_io.hpp"
#include "settle.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>

namespace etude::helper {

namespace {

constexpr std::uint64_t kMaxSandboxBytes = 8 * 1024;
constexpr std::size_t kMaxProperties = 64;

bool property_line(const std::string& line) {
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) {
        return false;
    }
    for (std::size_t i = 0; i < eq; ++i) {
        if (std::isalnum(static_cast<unsigned char>(line[i])) == 0) {
            return false;
        }
    }
    for (const char ch : line) {
        if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f) {
            return false;
        }
    }
    return true;
}

std::string trimmed(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    return line.substr(start);
}

} // namespace

bool load_sandbox(const std::string& path, std::vector<std::string>& properties, std::string& error,
                  uid_t owner) {
    FileLimits limits;
    limits.max_bytes = kMaxSandboxBytes;
    limits.check_owner = true;
    limits.owner = owner;
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
    if (!read_all_text(file.fd.get(), kMaxSandboxBytes, text, error)) {
        error = path + ": " + error;
        return false;
    }
    properties.clear();
    std::size_t from = 0;
    while (from <= text.size()) {
        std::size_t at = text.find('\n', from);
        if (at == std::string::npos) {
            at = text.size();
        }
        const std::string line = trimmed(text.substr(from, at - from));
        from = at + 1;
        if (line.empty() || line[0] == '#' || line == "[Service]") {
            continue;
        }
        if (!property_line(line) || properties.size() >= kMaxProperties) {
            error = path + ": not a property line: " + line.substr(0, 60);
            return false;
        }
        properties.push_back(line);
    }
    if (properties.empty()) {
        error = path + ": it holds no properties";
        return false;
    }
    return true;
}

std::vector<std::string> sandbox_argv(const SandboxRun& run) {
    std::vector<std::string> argv{run.systemd_run, "--pipe", "--wait", "--collect", "--quiet",
                                  "--unit=" + run.unit};
    // A run that hangs is ended by systemd, kill included, so it cannot outlast the unit that waits. Set
    // before the file's properties, so a RuntimeMaxSec there (a root's choice) wins.
    argv.push_back("--property=RuntimeMaxSec=" + std::to_string(run.limit_s));
    argv.push_back("--property=TimeoutStopSec=10");
    for (const std::string& property : run.properties) {
        argv.push_back("--property=" + property);
    }
    argv.push_back("--");
    argv.push_back(run.helper);
    if (!run.config.empty()) {
        argv.push_back("--config");
        argv.push_back(run.config);
    }
    argv.push_back("--report-start");
    if (run.mode == "idle") {
        argv.push_back("--busy-status");
        argv.push_back(std::to_string(kSandboxedBusy));
    }
    argv.push_back(run.mode);
    return argv;
}

int sandbox_limit_seconds(const Context& c, const std::string& mode) {
    if (mode == "idle") {
        // A lock that is not waited for, two looks at systemd and the state file.
        return 2 * worst_case_call_seconds(c.query_timeout_s()) + 15;
    }
    return worst_case_count_seconds(c) + 15;
}

namespace {

// The status the unit ends with, from the status of a run that started (it said so: the marker). The
// statuses the helper ends with pass; anything else, a crash, a run ended at its limit or by the memory
// limit (systemd-run ends 1 for those), is the helper's own trouble: 0, and it is not run again.
int unit_status(const std::string& mode, int code) {
    if (code == 0) {
        return 0;
    }
    if (mode == "idle") {
        if (code == kSandboxedBusy) {
            return 1;
        }
    } else if (mode == "count" && code == kRestoreUnfinished) {
        return code;
    }
    note(mode + ": the sandboxed run ended with " + std::to_string(code) +
         " (it crashed, ran out of time or memory, or is not a status it has); it is not run again");
    return 0;
}

// `output` without the line that says the run started; whether it was there.
bool take_start_marker(std::string& output) {
    const std::string line = std::string(kSandboxStartMarker) + "\n";
    const std::size_t at = output.find(line);
    if (at == std::string::npos) {
        return false;
    }
    output.erase(at, line.size());
    return true;
}

void print_output(const std::string& output) {
    if (output.empty()) {
        return;
    }
    std::fputs(output.c_str(), stderr);
    if (output.back() != '\n') {
        std::fputc('\n', stderr);
    }
    std::fflush(stderr);
}

} // namespace

std::optional<int> run_sandboxed(const Context& c, const SandboxRun& run) {
    ProgramResult r = run_program(sandbox_argv(run), run.limit_s + run.launch_margin_s, true);
    const bool started = take_start_marker(r.output);
    print_output(r.output);
    if (!r.ran || (!started && r.exit_code == 127)) {
        note(run.mode + ": systemd-run could not be started");
        return std::nullopt; // not there to run: an install without it
    }
    if (r.timed_out || r.interrupted) {
        // systemd-run was ended, not the unit it started: stop that too, so nothing goes on after the
        // caller has given up on it.
        run_program({c.conf.systemctl, "stop", run.unit}, c.query_timeout_s(), false);
        note(run.mode + ": the sandboxed run " + (r.interrupted ? "was cut short" : "took too long") +
             " and was stopped");
        return 0;
    }
    if (!started) {
        // systemd would not start the unit (an unknown property, a name that is taken, a mount that is
        // refused or hangs under the home), or it died before it could say so. Nothing of the mode ran,
        // so the caller does what needs no sandbox in place (never the whole of it: what keeps a unit
        // from starting can be the app's doing).
        note(run.mode + ": the sandboxed run did not start (status " + std::to_string(r.exit_code) + ")");
        return std::nullopt;
    }
    return unit_status(run.mode, r.exit_code);
}

} // namespace etude::helper
