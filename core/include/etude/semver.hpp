// SPDX-License-Identifier: GPL-3.0-or-later
// Semantic versions (semver.org 2.0.0) for the update check: read "1.2.3" or "1.2.3-rc.1", and say
// whether an offered version is newer than the installed one. Pure text, no file system.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace etude {

struct SemVer {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::string prerelease; // "" for a release, else the dot-separated part after the first '-'
};

// Reads "MAJOR.MINOR.PATCH" with an optional "-prerelease" and an optional "+build" (ignored, as
// the standard says). Refuses everything else: a leading "v", a missing part, a leading zero in a
// number, an empty or non-ASCII identifier, a number past 32 bits, spaces.
std::optional<SemVer> parse_semver(std::string_view text);

// -1, 0 or 1 by semver precedence: a prerelease is older than its release, numeric identifiers
// compare as numbers and sort before text ones, and a longer list wins over its own prefix.
int compare_semver(const SemVer& a, const SemVer& b);

// How an offered version stands against the installed one.
enum class VersionOrder : std::uint8_t { Newer, Same, Older, Malformed };

// Malformed when either text is not a semantic version: an update is never offered on a guess.
VersionOrder compare_versions(std::string_view offered, std::string_view installed);

} // namespace etude
