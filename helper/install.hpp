// SPDX-License-Identifier: GPL-3.0-or-later
// Turning what the app left in its folder into a version folder under <prefix>/versions/, without
// ever trusting the app: the tarball and its signature are copied out of the app's folder (opened by
// name, no link followed, judged on the open descriptor) into the helper's own root-only folder, and
// from the copy they are verified against the key built into this program, the signed comment
// (`etude <version> <os> <arch>`) is matched to the request and to this machine, and the
// helper itself unpacks them into versions/<version>.new, checks the tree, and renames it to
// versions/<version>. Root never moves, unpacks or looks into a folder the app wrote. Part of the root
// update helper (helper/main.cpp).
#pragma once

#include "context.hpp"

#include <string>

namespace etude::helper {

inline constexpr std::uint64_t kMaxTarballBytes = 512ULL * 1024 * 1024;
inline constexpr std::uint64_t kMaxSignatureBytes = 4096;
inline constexpr std::size_t kMaxUnpackedBytes = 1024ULL * 1024 * 1024;

struct StageResult {
    bool ok = false;
    std::string error; // for the player: "the download did not pass its signature check"
};

// `update_fd` is the app's folder, open. `version` is the request's; `running` the version that is
// current (an update must be newer). On success versions/<version> is a root-owned tree, whole.
StageResult stage_release(const Context& c, int update_fd, const std::string& version,
                          const std::string& running);

// Removes what a failed install left of `version` under versions/.
void discard_version(const Context& c, const std::string& version);

} // namespace etude::helper
