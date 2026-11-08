// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/library.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace etude {

// The song library while it fills in. The app lists the folders first (a file name, a size and a
// modification time per file, no file is read) and parses the songs afterwards, off the first
// frame. Everything here is a pure function over those values: the app does the listing, the
// reading and the thread.

// What the folder listing knows about a file without opening it.
struct LibraryFile {
    std::string relative; // to its library folder, '/' separated
    std::uint64_t size = 0;
    std::int64_t mtime = 0; // any monotone-per-file stamp the listing has (ticks, seconds)
};

// A cheap fingerprint of size and modification time. A file whose stamp is unchanged is not read
// again; the content hash that identifies the song in the history is taken when it is parsed.
std::uint64_t file_stamp(std::uint64_t size, std::int64_t mtime);

enum class EntryState : std::uint8_t {
    Pending, // listed, not parsed yet
    Loaded,  // parsed: `entry` is filled in
    Failed,  // read or parse failed: `error` says why; the file is left out when stepping
};

struct LibraryItem {
    std::string relative;
    std::uint64_t stamp = 0;
    EntryState state = EntryState::Pending;
    SongEntry entry;   // Loaded only
    std::string error; // Failed only
};

struct LibraryState {
    std::vector<LibraryItem> items; // in browsing order: by name, see list_state

    std::size_t pending() const;
    std::size_t loaded() const;
    std::size_t failed() const;
    bool settled() const { return pending() == 0; }
};

// The state for a folder listing: one pending item per file, sorted by folded name then by name
// (so the order never waits for a parse). `previous` is the state of an earlier listing; an item
// whose name and stamp are unchanged keeps its Loaded or Failed result.
LibraryState list_state(std::span<const LibraryFile> files, const LibraryState* previous = nullptr);

// The first Pending item at or after `from`, or none.
std::optional<std::size_t> next_pending(const LibraryState& state, std::size_t from = 0);

// Records a parse. `stamp` is the stamp the worker read the file under; a result for an item that
// has since changed (another stamp, or an index past the end) is dropped and false is returned.
bool apply_loaded(LibraryState& state, std::size_t index, std::uint64_t stamp, SongEntry entry);
bool apply_failed(LibraryState& state, std::size_t index, std::uint64_t stamp, std::string error);

// The song after (step 1) or before (step -1) `current` that has not Failed, wrapping; the first
// such song when `current` is none or out of range. None when there is no such song.
std::optional<std::size_t> step_item(const LibraryState& state, std::optional<std::size_t> current,
                                     int step);

// Parsed song metadata by content hash: a file that was touched or moved, with the same bytes and
// the same name, is not parsed twice. The title comes from the file name, so a copy under another
// name is a different entry.
class EntryCache {
  public:
    const SongEntry* find(std::uint64_t content_hash, std::string_view file_name) const;
    // `source` names where the bytes came from (the file's path, and the tune in it): inserting
    // new bytes for a source drops the parse its old bytes left. Empty: nothing is dropped.
    void insert(std::uint64_t content_hash, std::string_view file_name, const SongEntry& entry,
                std::string_view source = {});
    std::size_t size() const { return entries_.size(); }
    static constexpr std::size_t kMaxEntries = 20000;

  private:
    struct Slot {
        std::string file_name;
        SongEntry entry;
    };
    std::map<std::uint64_t, Slot> entries_;
    std::map<std::string, std::uint64_t> owners_; // source -> the hash it last inserted
};

} // namespace etude
