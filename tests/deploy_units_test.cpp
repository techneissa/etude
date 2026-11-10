// SPDX-License-Identifier: GPL-3.0-or-later
// The helper's sandbox in deploy/pi, read as text: one list of hardening properties that no unit
// repeats, a write list that leaves out the data folder, and the setup that installs it.
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
