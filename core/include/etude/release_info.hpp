// SPDX-License-Identifier: GPL-3.0-or-later
// release.json, the small file that sits beside each release's tarball: which version it is, the
// system it was built for, the oldest system it runs on, and the tarball's name, size and SHA-256.
// Pure text in, a checked struct out. The file is only a pointer: the tarball's own minisign
// signature (minisign.hpp) is what makes the update trusted, and its trusted comment must equal
// release_trusted_comment() of what release.json announced. Only plain releases (X.Y.Z) are
// offered: a version with a prerelease part is refused.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// The `os` of a release built for a Mac (and `ID` of its MachineOs): its file is a zip of the app
// bundle instead of a tarball, and its release.json is release-macos.json (update_feed.hpp).
inline constexpr std::string_view kMacOsName = "macos";

struct ReleaseInfo {
    std::string version; // semantic version, no leading "v": "1.2.3"
    std::string os;      // the system it was built on, a Debian codename: "trixie"
    std::string arch;    // "arm64"
    std::string min_os;  // the oldest system version it runs on, dotted numbers: "13"
    std::string file;    // the archive's name: release_file_name(version, os, arch)
    std::string sha256;  // 64 lowercase hex digits of the tarball
    std::uint64_t size = 0; // bytes of the tarball
};

enum class ReleaseInfoError : std::uint8_t {
    None,
    TooLarge,     // over kMaxReleaseInfoBytes
    NotJson,      // not one flat JSON object of string and whole-number values
    MissingField, // a field above is absent
    BadField,     // present but not valid (a bad version or hash, a file name that is not the rule's)
};

struct ReleaseInfoResult {
    ReleaseInfo info;
    ReleaseInfoError error = ReleaseInfoError::None;
    std::string field; // the field at fault for MissingField and BadField

    bool ok() const { return error == ReleaseInfoError::None; }
};

inline constexpr std::size_t kMaxReleaseInfoBytes = 16 * 1024;
inline constexpr std::uint64_t kMaxReleaseBytes = 1024ULL * 1024 * 1024; // the largest tarball size accepted

// Keys: version, os, arch, min_os, file, sha256 (strings) and size (a whole number). Other keys with
// string or whole-number values are ignored, so a later release can add some.
ReleaseInfoResult parse_release_info(std::string_view json);

// "etude-1.2.3-trixie-arm64.tar.gz", and for the Mac "etude-1.2.3-macos-arm64.zip"
std::string release_file_name(std::string_view version, std::string_view os, std::string_view arch);

// The trusted comment the signer writes with `minisign -t`: "etude 1.2.3 trixie arm64".
std::string release_trusted_comment(const ReleaseInfo& info);

// What a trusted comment says, read back: "etude 1.2.3 trixie arm64" is version 1.2.3 built on
// trixie for arm64. The comment is only trusted once the signature that carries it has been checked.
struct ReleaseComment {
    std::string version;
    std::string os;
    std::string arch;
};

// The three fields of release_trusted_comment's shape: a plain version X.Y.Z, then two names of
// lower-case letters, digits and `_` (a Debian codename, an architecture); false for anything else.
bool parse_release_trusted_comment(std::string_view comment, ReleaseComment& out);

// The text after "trusted comment:" in a .minisig file's text, or empty. Not checked: the caller
// hands it to verify_minisign as the expected comment, which is what authenticates it.
std::string signature_trusted_comment(std::string_view signature_file);

// What the updater reads of the machine: the architecture (dpkg's name, "arm64") and these from
// /etc/os-release and /etc/debian_version. Leave a field empty when the file lacks it.
struct MachineOs {
    std::string_view arch;
    std::string_view id;             // ID: "debian" for Raspberry Pi OS, "raspbian" on older ones, "macos" on a Mac
    std::string_view id_like;        // ID_LIKE: space-separated, "debian" for Ubuntu and others
    std::string_view version_id;     // VERSION_ID: "13"; on a Mac the macOS version, "14.5"
    std::string_view debian_version; // /etc/debian_version, for a system that only looks like Debian
};

// Whether a release runs on this machine: for a Mac release (`os` "macos"), a Mac of the same arch
// whose macOS version is at least min_os. Otherwise: the same arch, a Debian system (ID debian or raspbian, or
// an ID_LIKE that names debian), and a Debian version at least the release's min_os. The Debian
// version is VERSION_ID for debian and raspbian; for a derivative (Ubuntu, whose VERSION_ID is its
// own number) it is /etc/debian_version, which must be a plain number there ("trixie/sid" fails).
// Versions compare as dotted numbers padded with zeros, so "13" equals "13.0". Anything that does
// not read as dotted numbers does not fit.
bool release_fits_machine(const ReleaseInfo& info, const MachineOs& machine);

} // namespace etude
