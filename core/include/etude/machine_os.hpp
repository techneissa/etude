// SPDX-License-Identifier: GPL-3.0-or-later
// What the update check reads of the machine, as text: /etc/os-release, /etc/debian_version and the
// kernel's machine name, turned into the MachineOs that release_fits_machine compares
// (release_info.hpp). The files are read by the app; this file only parses what it was given.
#pragma once

#include "etude/release_info.hpp"

#include <string>
#include <string_view>

namespace etude {

// The fields of MachineOs, owning their text.
struct MachineOsText {
    std::string arch;
    std::string id;
    std::string id_like;
    std::string version_id;
    std::string debian_version;

    // Views into this object: valid while it lives and does not move.
    MachineOs view() const { return {arch, id, id_like, version_id, debian_version}; }
};

// Reads ID, ID_LIKE and VERSION_ID from an os-release file's text (KEY=value or KEY="value", lines
// that start with # ignored; the last of a repeated key wins). Other keys are left alone.
void parse_os_release(std::string_view text, MachineOsText& out);

// dpkg's name for a kernel machine name ("aarch64" is "arm64", "x86_64" is "amd64", "armv7l" is
// "armhf"); the text itself when it knows no other.
std::string debian_arch(std::string_view uname_machine);

// dpkg's name for the userland this program was built for ("arm64", "amd64", "armhf"; empty for any
// other build). The kernel's machine name is not it: a 32-bit userland runs on a 64-bit kernel, and
// an arm64 release must not be taken for it.
std::string build_debian_arch();

// Whether a release built on the Debian release `os` (the codename in its signed comment: "bullseye",
// "bookworm", "trixie", "forky") runs on this machine: a Debian system, as release_fits_machine reads
// one, at that release's version or newer. An `os` this does not know fits nothing.
bool signed_os_fits_machine(std::string_view os, const MachineOs& machine);

// The first line of /etc/debian_version, trimmed.
std::string first_line(std::string_view text);

} // namespace etude
