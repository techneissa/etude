// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/version.hpp"

#include "etude/semver.hpp"

#include <doctest/doctest.h>

#include <cstdio>
#include <string>

using namespace etude;

TEST_CASE("version: the build's version is a plain semantic version") {
    const auto parsed = parse_semver(app_version());
    REQUIRE(parsed.has_value());
    CHECK(parsed->prerelease.empty());
    CHECK(version_line() == "etude " + std::string(app_version()));
}

TEST_CASE("version: the build's version is the project version in CMakeLists.txt") {
    CHECK(std::string(app_version()) == ETUDE_PROJECT_VERSION);
}

#ifdef ETUDE_APP_PATH
TEST_CASE("version: `etude --version` prints the version line and nothing else") {
    const std::string command = std::string("'") + ETUDE_APP_PATH + "' --version 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::string out;
    char buffer[256];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
        out += buffer;
    }
    CHECK(::pclose(pipe) == 0);
    CHECK(out == version_line() + "\n");
}
#endif
