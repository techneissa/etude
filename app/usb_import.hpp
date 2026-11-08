// SPDX-License-Identifier: GPL-3.0-or-later
// Copies songs from a USB stick into the Songs folder's Inbox, for the Add songs page. It runs on a
// worker thread (app/sources_controller.hpp), one job at a time. The stick is only ever read: a
// file is opened from the stick's top one folder at a time with no link followed, without blocking,
// and must be a regular file on the stick's own device within the size cap; the songs go in through
// UploadSink, so a name, a file type and a file's first bytes are checked as for the phone page, a
// half file is never listed, and a taken name becomes "name (2).mid". A song already in the Songs
// folder (the same size, the same content hash and the same bytes) is skipped. When the stick goes
// away mid-copy the job stops at once and the half file is removed. No SDL.
#pragma once

#include "ui/sources_port.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace etude::app {

// How many bytes are read at a time (a stick is slow; the job looks at its stop flag and the
// stick between reads).
inline constexpr std::size_t kImportChunk = 64 * 1024;

struct ImportJob {
    std::filesystem::path stick;        // the stick's top folder
    std::vector<std::string> songs;     // paths under it, '/' separated, as scan_stick lists them
    std::filesystem::path songs_folder; // the Songs folder (the Inbox is inside it)
    std::size_t chunk = kImportChunk;
};

struct ImportHooks {
    // After each song is handled (index into the job's list, its mark) and, with `mark` None,
    // when one starts: for the progress shown.
    std::function<void(std::size_t index, ui::SongMark mark)> on_song;
    // Polled between reads: true stops the job (the player cancelled or the stick is gone).
    std::function<bool()> stop;
    // Called after every chunk read (`copying`: in the pass that writes the Inbox, not the one that
    // reads for the hash and the comparison), for a test to act mid-copy.
    std::function<void(bool copying)> on_chunk;
    // The hash a song is known by, in place of core content_hash_extend (a test makes two songs
    // collide to see that the bytes are compared).
    std::function<std::uint64_t(std::uint64_t hash, const char* data, std::size_t n)> hash;
};

struct ImportResult {
    ui::ImportEnd end = ui::ImportEnd::Done;
    int copied = 0;
    int duplicates = 0;
    int failed = 0;
    std::vector<ui::SongMark> marks; // one for each song of the job
};

// Runs the job to its end. `end` is Removed when the stick disappeared (or `stop` said so), Failed
// when the Inbox could not be written, else Done (a song that failed has its own mark).
ImportResult import_songs(const ImportJob& job, const ImportHooks& hooks);

} // namespace etude::app
