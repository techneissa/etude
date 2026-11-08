// SPDX-License-Identifier: GPL-3.0-or-later
// The files the app and the helper hand each other in <prefix>/update/, a folder the app's user
// owns: everything the helper takes from it is opened by name inside the open folder, without
// following a link, and judged on the open descriptor; everything it puts there is written whole
// under a temporary name and renamed over. Part of the root update helper (helper/main.cpp).
#pragma once

#include "context.hpp"
#include "fd.hpp"

#include "etude/update_handoff.hpp"

#include <string>

namespace etude::helper {

inline constexpr const char* kRequestFile = "request";
inline constexpr const char* kReleaseFile = "release.tar.gz";
inline constexpr const char* kSignatureFile = "release.tar.gz.minisig";
inline constexpr const char* kReadyFile = "ready";
inline constexpr const char* kStatusFile = "status";
// (the same names as core update_handoff.hpp's kUpdate*File, which the app writes by)

// The folder, by descriptor.
Fd open_update_dir(const Context& c, std::string& error);

// The request: `version=X.Y.Z` and nothing else, taken out of the folder (it is removed whatever it
// says, so it cannot start another run). "" with `error` when there is none (`error` empty), or it is
// not a plain file of the app's own, or says anything else.
std::string take_request(const Context& c, int update_fd, std::string& error);

// Whether the app has written ready for `version`: a plain file of the app's, `version=<version>`.
bool ready_for(const Context& c, int update_fd, const std::string& version);

// Removes the ready file, whatever it is.
void clear_ready(int update_fd);

// The status file as it stands (root's own, written by `write_status`); state None when it is not
// there or not a plain file of root's.
UpdateStatus read_status(const Context& c);

// Writes how a run ended; best effort (a status that is a folder is left alone). `data` is set only
// while the data of a going back is not put back (the crash screen shows it).
void write_status(const Context& c, const std::string& state, const std::string& version,
                  const std::string& from, const std::string& reason, const std::string& data = {});

} // namespace etude::helper
