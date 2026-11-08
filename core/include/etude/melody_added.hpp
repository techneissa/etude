// SPDX-License-Identifier: GPL-3.0-or-later
// The songs a player added to the Melodies page from a song's details card ("Add to Melodies"): each
// a content hash with the step the player chose and the song's library path, kept per profile as one
// line of text in the profile's settings (`melody_added`: `HASH:STEP:PATH` words apart by a space, the
// hash in 16 hex digits, the path with its spaces and percent signs written %20 and %25; the path may
// be left out).
//
// A song is found by its hash, so a moved or renamed file keeps its place. A file the player edits
// gets a new hash: its entry is then found by its path and follows the new hash (`rebind`), keeping
// its step and its place in the list. An entry found by neither is hidden (the song is gone for now)
// and does not count toward `kMaxMelodyAdded`; it is kept, in case the file comes back, until room is
// needed, when the oldest hidden entries go first. Which entries are hidden is the library's to say:
// the functions that need it take their hashes.
#pragma once

#include "etude/melody_manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// At most this many songs the library has are kept, and in all this many entries with those it has not.
inline constexpr std::size_t kMaxMelodyAdded = 24;
inline constexpr std::size_t kMaxMelodyStored = 48;
// A path longer than this is not kept (the entry is then found by its hash only).
inline constexpr std::size_t kMaxMelodyPath = 120;
// The longest text the settings keep for them: 16 hex digits, two colons-worth of fields, the path (at
// most three bytes a byte written out) and a space each.
inline constexpr std::size_t kMaxMelodyAddedBytes = kMaxMelodyStored * (16 + 1 + 1 + 1 + 3 * kMaxMelodyPath + 1);

struct MelodyAddedSong {
    std::uint64_t hash = 0;
    int step = 0;      // kMelodyFirstStep to kMelodyLastStep
    std::string path;  // the library's name for the file ("Classical/fur.mid", "tunes.abc#X=3"); may be empty
};

class MelodyAdded {
  public:
    // Reads the settings' text. What is not a hash with a step, a hash twice (the first stays) and
    // anything past `kMaxMelodyStored` is left out.
    static MelodyAdded parse(std::string_view text);
    std::string text() const;

    std::size_t size() const { return songs_.size(); }
    const MelodyAddedSong& at(std::size_t i) const { return songs_[i]; }
    // The step the player chose for the song, or 0 for a song not added: found by its hash, else (an
    // edited file) by its path.
    int step_of(std::uint64_t hash, std::string_view path = {}) const;
    // Adds the song at `step`, or moves it there (and keeps its path up to date when `path` is given).
    // A song found by its path under another hash is that entry's edited file: it takes the new hash
    // and the step. A new song needs room: fewer than `kMaxMelodyAdded` entries are not in `hidden`, and, when the
    // list holds `kMaxMelodyStored`, an oldest hidden one to make way for it. False when the step is
    // not a step, the hash is 0 or there is no room.
    bool set(std::uint64_t hash, int step, std::string_view path = {},
             std::span<const std::uint64_t> hidden = {});
    // Takes the song out, found by its hash or its path; false when it was not in.
    bool remove(std::uint64_t hash, std::string_view path = {});
    // The file of the entry `old_hash` was edited: it is `new_hash` now, on `path`, with its step and
    // its place kept. False when `old_hash` is not in. An entry already holding `new_hash` makes this
    // one a repeat and it goes.
    bool rebind(std::uint64_t old_hash, std::uint64_t new_hash, std::string_view path);
    // How many entries are not in `hidden`.
    std::size_t visible(std::span<const std::uint64_t> hidden) const;

  private:
    std::vector<MelodyAddedSong> songs_; // oldest first
};

} // namespace etude
