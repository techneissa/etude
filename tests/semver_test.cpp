// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/semver.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("semver: reads major, minor, patch, prerelease and ignores build metadata") {
    const auto v = parse_semver("1.22.333");
    REQUIRE(v.has_value());
    CHECK(v->major == 1);
    CHECK(v->minor == 22);
    CHECK(v->patch == 333);
    CHECK(v->prerelease.empty());

    const auto pre = parse_semver("2.0.0-rc.1+build.5");
    REQUIRE(pre.has_value());
    CHECK(pre->prerelease == "rc.1");
    CHECK(parse_semver("1.0.0+001").has_value()); // leading zeros are fine in build metadata
    CHECK(parse_semver("1.0.0-alpha-1").has_value());
    CHECK(parse_semver("0.0.0").has_value());
}

TEST_CASE("semver: text that is not a semantic version is refused") {
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "v1.2.3", " 1.2.3", "1.2.3 ", "1.2.x",
                            "01.2.3", "1.02.3", "1.2.03", "1.2.3-", "1.2.3-01", "1.2.3-a..b",
                            "1.2.3-é", "1.2.3+", "-1.2.3", "1.-2.3", "99999999999.0.0",
                            "1.2.3-rc_1"}) {
        CAPTURE(bad);
        CHECK_FALSE(parse_semver(bad).has_value());
    }
}

TEST_CASE("semver: a newer, the same and an older version") {
    CHECK(compare_versions("1.2.4", "1.2.3") == VersionOrder::Newer);
    CHECK(compare_versions("1.3.0", "1.2.9") == VersionOrder::Newer);
    CHECK(compare_versions("2.0.0", "1.99.99") == VersionOrder::Newer);
    CHECK(compare_versions("1.10.0", "1.9.0") == VersionOrder::Newer); // numbers, not text
    CHECK(compare_versions("1.2.3", "1.2.3") == VersionOrder::Same);
    CHECK(compare_versions("1.2.3+a", "1.2.3+b") == VersionOrder::Same);
    CHECK(compare_versions("1.2.2", "1.2.3") == VersionOrder::Older);
    CHECK(compare_versions("0.9.0", "1.0.0") == VersionOrder::Older);
}

TEST_CASE("semver: prerelease precedence follows semver.org") {
    // The example chain from the standard, each older than the next.
    const char* chain[] = {"1.0.0-alpha",   "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                           "1.0.0-beta.2",  "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
    for (int i = 0; i + 1 < 8; ++i) {
        CAPTURE(chain[i]);
        CAPTURE(chain[i + 1]);
        CHECK(compare_versions(chain[i + 1], chain[i]) == VersionOrder::Newer);
        CHECK(compare_versions(chain[i], chain[i + 1]) == VersionOrder::Older);
    }
    CHECK(compare_versions("1.0.0-rc.1", "1.0.0-rc.1") == VersionOrder::Same);
}

TEST_CASE("semver: a malformed version on either side is never newer") {
    CHECK(compare_versions("v1.2.4", "1.2.3") == VersionOrder::Malformed);
    CHECK(compare_versions("1.2.4", "latest") == VersionOrder::Malformed);
    CHECK(compare_versions("", "") == VersionOrder::Malformed);
    CHECK(compare_versions("1.2", "1.2.3") == VersionOrder::Malformed);
}
