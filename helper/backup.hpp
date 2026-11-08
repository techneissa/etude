// SPDX-License-Identifier: GPL-3.0-or-later
// Backing up and restoring the app's data (the history database and the settings, with their WAL
// and backup files) around an update. These run in a child that has become the app's user, never as
// root: the data folder is the app's, so whatever a planted link or a bad file makes them do, they do
// with the app's own rights. Part of the root update helper (helper/main.cpp).
//
// The backup of version V is <data>/update-backup/V/, written beside it as V.new, flushed, then renamed
// into place, with a marker file last, so a half-written backup is never taken for a whole one and an
// older backup survives a backup that fails (a full disk). A restore copies every file to a temporary
// name first, flushes them, then renames them all and removes the files the backup does not have, so
// a power cut in the middle is finished by running it again.
#pragma once

#include <array>
#include <string>
#include <string_view>

namespace etude::helper {

inline constexpr std::array<std::string_view, 5> kBackedUp = {
    "history.db", "history.db-wal", "history.db-shm", "settings.conf", "settings.conf.bak"};

inline constexpr std::string_view kBackupFolder = "update-backup";
inline constexpr std::string_view kBackupMarker = ".complete";

// "" when the data of `version` is backed up, else why not. A `data_dir` that does not exist has
// nothing to back up: also "".
std::string backup_data(const std::string& data_dir, const std::string& version);

// "" when the data is put back as the backup of `version` had it, else why not. Needs a complete
// backup of that version: without one nothing is touched.
std::string restore_data(const std::string& data_dir, const std::string& version);

} // namespace etude::helper
