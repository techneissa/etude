// SPDX-License-Identifier: GPL-3.0-or-later
// The units of deploy/pi, read as text: the helper's sandbox is one list shared by the update service,
// the boot one and the app units' sandboxed `count` and `idle`; and every app unit has a watchdog.
#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string pi_file(const std::string& name) {
    std::ifstream in(std::string(ETUDE_SOURCE_DIR) + "/deploy/pi/" + name, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

// The `Name=` of each property line of a unit or drop-in (no comments, sections or blank lines).
std::set<std::string> property_names(const std::string& text) {
    std::set<std::string> names;
    for (const std::string& line : lines_of(text)) {
        const std::size_t eq = line.find('=');
        if (!line.empty() && line[0] != '#' && line[0] != '[' && eq != std::string::npos) {
            names.insert(line.substr(0, eq));
        }
    }
    return names;
}

bool has_line(const std::string& text, const std::string& line) {
    for (const std::string& each : lines_of(text)) {
        if (each == line) {
            return true;
        }
    }
    return false;
}

const char* const kAppUnits[] = {"etude.service", "etude-labwc.service"};
const char* const kCrashUnits[] = {"etude-crash-screen.service", "etude-labwc-crash-screen.service"};
constexpr const char* kHelper = "/usr/local/lib/etude/etude-update-helper";

} // namespace

TEST_CASE("deploy units: the helper's sandbox is one list, and no unit carries a list of its own") {
    const std::string sandbox = pi_file("etude-update-sandbox.conf");
    const std::set<std::string> names = property_names(sandbox);
    for (const char* needed : {"ProtectSystem", "ReadWritePaths", "ProtectHome", "PrivateTmp", "NoNewPrivileges",
                               "CapabilityBoundingSet", "SystemCallFilter", "RestrictAddressFamilies",
                               "MemoryDenyWriteExecute", "RestrictSUIDSGID"}) {
        CAPTURE(needed);
        CHECK(names.count(needed) == 1);
    }
    CHECK(has_line(sandbox, "ProtectSystem=strict"));
    CHECK(has_line(sandbox, "ProtectHome=read-only"));
    // The writable paths are the prefix, the helper's folder and the app user's home: not the data folder,
    // which the app owns and can turn into a link that systemd would follow.
    CHECK(has_line(sandbox, "ReadWritePaths=@PREFIX@ /var/lib/etude-update -@APP_HOME@"));
    for (const std::string& line : lines_of(sandbox)) {
        if (!line.empty() && line[0] != '#') {
            CAPTURE(line);
            CHECK(line.find("@DATA_DIR@") == std::string::npos);
        }
    }
    CHECK(sandbox.find("[Service]") != std::string::npos);
    // None of the units repeats a property of it: a second copy is what drifts.
    for (const char* unit : {"etude-update.service", "etude-update-resume.service", "etude.service",
                             "etude-labwc.service", "etude-crash-screen.service", "etude-labwc-crash-screen.service"}) {
        CAPTURE(unit);
        const std::set<std::string> own = property_names(pi_file(unit));
        for (const std::string& name : names) {
            CAPTURE(name);
            // The app units set NoNewPrivileges for their own reason (see below), not as the helper's
            // sandbox: the one list is for the helper's.
            const bool app_unit = std::string(unit).find("update") == std::string::npos;
            CHECK((own.count(name) == 0 || (app_unit && name == "NoNewPrivileges")));
        }
    }
    // Setup installs that one file beside both update units, rendered for the prefix and the data folder.
    const std::string setup = pi_file("setup.sh");
    CHECK(setup.find("etude-update-sandbox.conf") != std::string::npos);
    CHECK(setup.find("etude-update.service etude-update-resume.service; do") != std::string::npos);
    CHECK(setup.find("\"$UNIT_DIR/$sandboxed_unit.d/sandbox.conf\"") != std::string::npos);
    CHECK(setup.find("s|@APP_HOME@|$APP_HOME|g") != std::string::npos);
    // ... and refuses a home the app could swap: checked with every link resolved (tested under a real
    // system in scripts/update-helper-test.sh).
    CHECK(setup.find("check_app_home") != std::string::npos);
    const std::string lib = pi_file("install-lib.sh");
    CHECK(lib.find("realpath -e") != std::string::npos);
    CHECK(lib.find("/ | /usr | /usr/* | /var | /etc | /etc/* | /root | /root/*") != std::string::npos);
}

TEST_CASE("deploy units: the app's count and the crash screens' crashed and idle run in the sandbox, nothing in place") {
    for (const char* unit : kAppUnits) {
        CAPTURE(unit);
        const std::string text = pi_file(unit);
        CHECK(has_line(text, std::string("ExecStartPre=+") + kHelper + " sandboxed count"));
        CHECK(text.find(std::string(kHelper) + " count") == std::string::npos);
    }
    for (const char* unit : kCrashUnits) {
        CAPTURE(unit);
        const std::string text = pi_file(unit);
        CHECK(has_line(text, std::string("ExecCondition=+") + kHelper + " sandboxed idle"));
        CHECK(has_line(text, std::string("ExecStartPre=+") + kHelper + " sandboxed crashed"));
        CHECK(text.find(std::string(kHelper) + " idle") == std::string::npos);
        CHECK(text.find(std::string(kHelper) + " crashed") == std::string::npos);
        CHECK(text.find("count-safe") == std::string::npos);
    }
}

TEST_CASE("deploy units: no restart by itself, no safe mode: a failure starts the crash screen, and the player restarts") {
    const std::string crash_of[] = {"etude-crash-screen.service", "etude-labwc-crash-screen.service"};
    const std::string app_of[] = {"etude.service", "etude-labwc.service"};
    for (int i = 0; i < 2; ++i) {
        const std::string text = pi_file(app_of[i].c_str());
        CAPTURE(app_of[i]);
        CHECK(has_line(text, "Restart=no"));
        CHECK(has_line(text, "OnFailure=" + crash_of[i]));
        CHECK(has_line(text, "StartLimitIntervalSec=0")); // the player's starts are never refused
        for (const char* gone : {"Restart=on-failure", "RestartSec", "RestartSteps", "StartLimitBurst", "safe"}) {
            CAPTURE(gone);
            CHECK(text.find(std::string("\n") + gone) == std::string::npos);
        }
        CHECK(text.find("safe-mode") == std::string::npos);
        // The crash screen: the same binary with --crash-screen, which restarts the app by exiting 0.
        const std::string crash = pi_file(crash_of[i].c_str());
        CAPTURE(crash_of[i]);
        CHECK(crash.find("--crash-screen") != std::string::npos);
        CHECK(has_line(crash, "OnSuccess=" + app_of[i]));
        CHECK(has_line(crash, "Restart=no"));
        CHECK(has_line(crash, "ConditionPathExists=/dev/tty0"));
        // A crash in the crash screen just stops: nothing starts it again, and it names no unit to start.
        for (const std::string& line : lines_of(crash)) {
            CHECK(line.rfind("OnFailure=", 0) == std::string::npos);
            CHECK(line.rfind("Restart=on-failure", 0) == std::string::npos);
        }
        CHECK(crash.find("safe-mode") == std::string::npos);
        CHECK(crash.find("Conflicts=getty@tty1.service etude.service etude-labwc.service") != std::string::npos);
    }
}

TEST_CASE("deploy units: under labwc the app runs through the session script, which passes its exit status on") {
    // labwc ends 0 whatever its session command did, so a unit that ran labwc directly would end well after
    // a crash: no OnFailure=, and the crash screen's OnSuccess= would start the app after the screen crashed.
    for (const char* unit : {"etude-labwc.service", "etude-labwc-crash-screen.service"}) {
        CAPTURE(unit);
        const std::string text = pi_file(unit);
        bool starts_through_script = false;
        for (const std::string& line : lines_of(text)) {
            CHECK(line.rfind("ExecStart=/usr/bin/labwc", 0) == std::string::npos);
            starts_through_script = starts_through_script ||
                                    line.rfind("ExecStart=/usr/local/lib/etude/etude-labwc-session @PREFIX@/current/bin/etude", 0) == 0;
        }
        CHECK(starts_through_script);
        CHECK(text.find("RuntimeDirectory=etude-labwc") != std::string::npos); // where the script keeps the status
    }
    const std::string script = pi_file("etude-labwc-session");
    CHECK(script.rfind("#!/bin/sh\n", 0) == 0);
    CHECK(script.find("-S \"$command\"") != std::string::npos); // labwc runs the script again as the session
    CHECK(script.find("--child") != std::string::npos);
    // The other units do not use it.
    for (const char* unit : {"etude.service", "etude-crash-screen.service"}) {
        CAPTURE(unit);
        CHECK(pi_file(unit).find("etude-labwc-session") == std::string::npos);
    }
}

TEST_CASE("deploy units: every app unit has the watchdog, with notify access for the process that answers it") {
    for (const char* unit : kAppUnits) {
        CAPTURE(unit);
        const std::string text = pi_file(unit);
        CHECK(has_line(text, "WatchdogSec=60"));
        // The app is the unit's main process under KMSDRM; under labwc it is labwc's child.
        CHECK(has_line(text, std::string(unit) == "etude.service" ? "NotifyAccess=main" : "NotifyAccess=all"));
        // A watchdog timeout fails the unit like a crash, and the crash screen shows: nothing restarts it.
        CHECK(has_line(text, "Restart=no"));
    }
    // The app starts no setuid program: under KMSDRM it may not gain privileges (the labwc session's
    // needs are untested, and on the checklist).
    for (const char* unit : {"etude.service", "etude-crash-screen.service"}) {
        CAPTURE(unit);
        CHECK(has_line(pi_file(unit), "NoNewPrivileges=yes"));
    }
    for (const char* unit : {"etude-labwc.service", "etude-labwc-crash-screen.service"}) {
        CAPTURE(unit);
        CHECK_FALSE(has_line(pi_file(unit), "NoNewPrivileges=yes"));
    }
}
