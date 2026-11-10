// SPDX-License-Identifier: GPL-3.0-or-later
// <prefix>'s own layout: versions/<X.Y.Z>/ and the relative links `current` and `previous` that
// point into it, all root's. Part of the root update helper (helper/main.cpp).
#pragma once

#include "context.hpp"

#include <string>
#include <vector>

namespace etude::helper {

// "" or why <prefix> and <prefix>/versions are not what an update may work in: real folders (no
// links), root's, not writable by group or others. Makes versions/ (0755) when it is not there.
std::string check_layout(const Context& c);

// The version `current` or `previous` points at ("versions/1.2.3"), or "" when it does not point at
// a plain version folder.
std::string link_version(const Context& c, const std::string& link);

// True when `version` must not be removed: `current` or `previous` points at it, or one of them exists
// and is not a plain `versions/X` link (then what it points at cannot be told, so nothing goes).
bool version_in_use(const Context& c, const std::string& version);

// Points `link` ("current" or "previous") at versions/<version> by creating a link beside it and
// renaming it over; an empty `version` removes the link. False with `error` when the rename (or the
// removal) failed, which changed nothing. Once it has happened, a prefix folder that cannot be written
// out is logged and the call still counts as done: callers undo or remove on false.
bool point_link(const Context& c, const std::string& link, const std::string& version,
                std::string& error);

#if defined(ETUDE_HELPER_TEST_HOOKS)
// Tests only: while set, the fsync of the prefix folder after a link is switched fails.
void fail_fsync_in_test(bool fail);
#endif

// Removes what an update that was cut short left: versions/*.new, .incoming*, <prefix>/.*.new.
void clean_leftovers(const Context& c);

// Every folder under versions/ named as a plain version.
std::vector<std::string> list_versions(const Context& c);

// Removes the version folders but `keep_a` and `keep_b`.
void prune_versions(const Context& c, const std::string& keep_a, const std::string& keep_b);

} // namespace etude::helper
