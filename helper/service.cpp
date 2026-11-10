// SPDX-License-Identifier: GPL-3.0-or-later
#include "service.hpp"

#include "etude/update_state.hpp"

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace etude::helper {

namespace {

std::string trimmed(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\n' || text[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// True when a component of `path` is a link that root did not make (a link the app's user made: the
// unit's sandbox follows links in the paths it names, so one the app can change could point the
// backup's writable path somewhere else).
bool has_foreign_link(const std::string& path) {
    for (std::size_t at = path.find('/', 1);; at = path.find('/', at + 1)) {
        const std::string part = at == std::string::npos ? path : path.substr(0, at);
        struct stat st{};
        if (::lstat(part.c_str(), &st) == 0 && S_ISLNK(st.st_mode) && st.st_uid != 0) {
            return true;
        }
        if (at == std::string::npos) {
            return false;
        }
    }
}

} // namespace

bool Service::systemctl(const std::vector<std::string>& args, std::string* output,
                        bool interruptible) const {
    std::vector<std::string> argv{c_.conf.systemctl};
    argv.insert(argv.end(), args.begin(), args.end());
    const ProgramResult r = run_program(argv, c_.service_timeout_s, interruptible);
    if (output != nullptr) {
        *output = r.output;
    }
    return r.ran && !r.timed_out && !r.interrupted && r.exit_code == 0;
}

std::string Service::state(const std::string& unit) const {
    return known_app_unit(unit) ? state_of(unit) : std::string();
}

std::string Service::state_of(const std::string& unit) const {
    std::vector<std::string> argv{c_.conf.systemctl, "is-active", unit};
    const ProgramResult r = run_program(argv, c_.query_timeout_s(), false);
    if (!r.ran || r.timed_out || r.interrupted) {
        return {};
    }
    // The exit status is 0 only for "active"; the word is what says which of the others it is. One
    // word and nothing else: anything longer is not an answer.
    const std::string word = trimmed(r.output);
    // One lower-case word and nothing else: an error message (the bus down) is not a state.
    bool plain = !word.empty() && word.size() <= 32;
    for (const char ch : word) {
        plain = plain && ((ch >= 'a' && ch <= 'z') || ch == '-');
    }
    if (!plain) {
        return {};
    }
    return word;
}

bool Service::running(const std::string& unit) const {
    const std::string now = state(unit);
    return now != "inactive" && now != "failed";
}

std::string Service::active_unit() const {
    for (const char* unit : {"etude.service", "etude-labwc.service"}) {
        if (running(unit)) {
            return unit;
        }
    }
    return {};
}

bool Service::stop(const std::string& unit, std::string& error) const {
    if (!known_app_unit(unit)) {
        error = "not one of the app's services: " + unit;
        return false;
    }
    systemctl({"stop", unit}, nullptr, true); // the one call a SIGTERM may cut short
    const std::string now = state(unit);
    if (now == "inactive" || now == "failed") {
        return true;
    }
    error = unit + (now.empty() ? " is in a state that could not be read"
                                : " is " + now) +
            " after it was told to stop";
    return false;
}

bool Service::ended_by_stop(const std::string& unit) const {
    if (!known_app_unit(unit) || state(unit) != "failed") {
        return false;
    }
    std::string output;
    if (!systemctl({"show", "--property=Result", "--property=ExecMainStatus", "--property=ExecMainCode", unit},
                   &output)) {
        return false;
    }
    std::string result;
    std::string status;
    std::string code;
    std::size_t at = 0;
    while (at < output.size()) {
        std::size_t end = output.find('\n', at);
        end = end == std::string::npos ? output.size() : end;
        const std::string line = output.substr(at, end - at);
        at = end + 1;
        if (line.rfind("Result=", 0) == 0) {
            result = line.substr(7);
        } else if (line.rfind("ExecMainStatus=", 0) == 0) {
            status = line.substr(15);
        } else if (line.rfind("ExecMainCode=", 0) == 0) {
            code = line.substr(13);
        }
    }
    // ExecMainCode 2 is CLD_KILLED (by a signal), 3 CLD_DUMPED (a core): ExecMainStatus is then the signal.
    return result == "timeout" || (result == "signal" && code == "2" && status == "9");
}

bool Service::stop_hard(const std::string& unit, bool& killed, std::string& error) const {
    killed = false;
    if (!known_app_unit(unit)) {
        error = "not one of the app's services: " + unit;
        return false;
    }
    const auto down = [&] {
        const std::string now = state(unit);
        return now == "inactive" || now == "failed";
    };
    for (int attempt = 0; attempt < 2; ++attempt) {
        systemctl({"stop", unit});
        if (down()) {
            return true;
        }
    }
    killed = true;
    for (const char* whom : {"main", "all"}) {
        std::vector<std::string> argv{c_.conf.systemctl, "kill", "--kill-whom=" + std::string(whom),
                                      "--signal=SIGKILL", unit};
        run_program(argv, c_.query_timeout_s(), false);
        // A killed process takes a moment to be reaped and the unit to say so.
        for (int wait = 0; wait < 3; ++wait) {
            if (down()) {
                return true;
            }
            struct timespec nap{c_.kill_nap_ms / 1000, (c_.kill_nap_ms % 1000) * 1'000'000L};
            ::nanosleep(&nap, nullptr);
        }
    }
    const std::string now = state(unit);
    error = unit + (now.empty() ? " is in a state that could not be read" : " is " + now) +
            " after it was told to stop twice and killed";
    return false;
}

// The helper starts the app on purpose (an update, a going back), so it clears the failed state first:
// a unit that failed is started again by `start` either way, and the reset keeps its result from
// being read as the new run's.
void Service::forgive(const std::string& unit) const { systemctl({"reset-failed", unit}); }

bool Service::start_queued(const std::string& unit) const {
    if (!known_app_unit(unit)) {
        return false;
    }
    forgive(unit);
    return systemctl({"start", "--no-block", unit});
}

bool Service::start_crash_screen(const std::string& app_unit) const {
    const std::string unit = crash_screen_unit(app_unit);
    if (unit.empty()) {
        return false;
    }
    forgive(unit);
    return systemctl({"start", "--no-block", unit});
}

bool Service::start(const std::string& unit) const {
    if (!known_app_unit(unit)) {
        return false;
    }
    forgive(unit);
    return systemctl({"start", unit});
}

bool Service::restart(const std::string& unit) const {
    if (!known_app_unit(unit)) {
        return false;
    }
    forgive(unit);
    return systemctl({"restart", unit});
}

EnvLookup Service::xdg_data_home(const std::string& unit) const {
    std::string text;
    if (!known_app_unit(unit) ||
        !systemctl({"show", "--property=Environment", "--value", unit}, &text)) {
        return {EnvKind::Unreadable, {}};
    }
    return environment_lookup(text, "XDG_DATA_HOME");
}

std::string data_folder_for(const Context& c, const Service& service, const std::string& unit) {
    const EnvLookup xdg = service.xdg_data_home(unit.empty() ? "etude.service" : unit);
    if (xdg.kind == EnvKind::Unreadable) {
        return {};
    }
    const std::string folder = app_data_folder(c.user.home, xdg.value);
    if (!folder.empty() && has_foreign_link(folder)) {
        note("the data folder " + folder + " goes through a link that root did not make: refused");
        return {};
    }
    return folder;
}

} // namespace etude::helper
