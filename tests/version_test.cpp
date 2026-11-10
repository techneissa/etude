// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/version.hpp"

#include "etude/semver.hpp"

#include <doctest/doctest.h>

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
