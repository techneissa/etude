// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/machine_os.hpp"

#include <cstddef>

namespace etude {
namespace {

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::string unquoted(std::string_view value) {
    value = trimmed(value);
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
        value.back() == value.front()) {
        value = value.substr(1, value.size() - 2);
    }
    return std::string(value);
}

} // namespace

void parse_os_release(std::string_view text, MachineOsText& out) {
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = trimmed(text.substr(at, end - at));
        at = end + 1;
        const std::size_t eq = line.find('=');
        if (line.empty() || line.front() == '#' || eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = line.substr(0, eq);
        const std::string value = unquoted(line.substr(eq + 1));
        if (key == "ID") {
            out.id = value;
        } else if (key == "ID_LIKE") {
            out.id_like = value;
        } else if (key == "VERSION_ID") {
            out.version_id = value;
        }
    }
}

std::string debian_arch(std::string_view uname_machine) {
    if (uname_machine == "aarch64" || uname_machine == "arm64") {
        return "arm64";
    }
    if (uname_machine == "x86_64" || uname_machine == "amd64") {
        return "amd64";
    }
    if (uname_machine == "armv7l" || uname_machine == "armv8l") {
        return "armhf";
    }
    return std::string(uname_machine);
}

std::string build_debian_arch() {
#if defined(__aarch64__)
    return "arm64";
#elif defined(__x86_64__)
    return "amd64";
#elif defined(__arm__)
    return "armhf";
#else
    return {};
#endif
}

std::string first_line(std::string_view text) {
    const std::size_t end = text.find('\n');
    return std::string(trimmed(text.substr(0, end)));
}

bool signed_os_fits_machine(std::string_view os, const MachineOs& machine) {
    if (os == kMacOsName) {
        // The signed comment carries no macOS version: the one in release-macos.json (unsigned) is a
        // courtesy, and a Mac that cannot run the bundle keeps the old one through the rollback.
        return machine.id == kMacOsName;
    }
    struct Release {
        std::string_view codename;
        const char* version;
    };
    static constexpr Release kReleases[] = {{"bullseye", "11"}, {"bookworm", "12"}, {"trixie", "13"},
                                            {"forky", "14"}};
    for (const Release& release : kReleases) {
        if (release.codename == os) {
            ReleaseInfo info;
            info.arch = std::string(machine.arch);
            info.min_os = release.version;
            return release_fits_machine(info, machine);
        }
    }
    return false;
}

} // namespace etude
