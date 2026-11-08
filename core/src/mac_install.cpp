// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/mac_install.hpp"

namespace etude {

std::string mac_bundle_of_executable(std::string_view exe) {
    // <bundle>.app/Contents/MacOS/<program>
    constexpr std::string_view kInside = ".app/Contents/MacOS/";
    const std::size_t at = exe.rfind(kInside);
    if (at == std::string_view::npos || at == 0 || exe.front() != '/') {
        return {};
    }
    const std::string_view program = exe.substr(at + kInside.size());
    if (program.empty() || program.find('/') != std::string_view::npos) {
        return {};
    }
    const std::string_view bundle = exe.substr(0, at + 4);
    if (bundle.rfind('/') == bundle.size() - 5) { // "/.app": a bundle with no name
        return {};
    }
    return std::string(bundle);
}

MacInstallVerdict check_mac_install(const MacInstallFacts& facts) {
    MacInstallVerdict verdict;
    const auto block = [&](MacInstallBlock why, const char* reason) {
        verdict.block = why;
        verdict.reason = reason;
        return verdict;
    };
    if (facts.bundle.empty()) {
        return block(MacInstallBlock::NotABundle, "it isn't running as an app");
    }
    if (facts.bundle.find("/AppTranslocation/") != std::string::npos) {
        return block(MacInstallBlock::Translocated,
                     "it was opened in place from Downloads (App Translocation)");
    }
    if (facts.volume_read_only && facts.volume_is_disk_image) {
        return block(MacInstallBlock::DiskImage, "it runs from a disk image");
    }
    if (facts.volume_read_only) {
        return block(MacInstallBlock::ReadOnlyVolume, "it runs from a read-only volume");
    }
    if (!facts.parent_writable) {
        return block(MacInstallBlock::NotWritable, "this account can't change its folder");
    }
    return verdict;
}

bool mac_mount_is_disk_image(std::string_view hdiutil_plist, std::string_view mount_point) {
    if (mount_point.empty()) {
        return false;
    }
    const std::string element = "<string>" + std::string(mount_point) + "</string>";
    return hdiutil_plist.find(element) != std::string_view::npos;
}

std::string mac_staging_name(std::string_view version) {
    return ".etude-update-" + std::string(version);
}

std::string mac_previous_name(std::string_view from_version) {
    return ".etude-" + std::string(from_version) + ".previous";
}

} // namespace etude
