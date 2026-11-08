// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace etude {

// What the player set for one song in the library, kept by the song's content hash (so a renamed
// or moved file keeps it) and never written into the file.
struct SongPrefs {
    std::string title;    // the player's name for the song; empty: the name the file gives
    std::string composer; // the player's composer; empty: the one the file names
    std::string genre;    // a genre the player set; empty: the folder's
    bool favourite = false;
    std::int64_t added_utc_s = 0; // when the library first listed the song; 0 not yet
    // When the player last chose this song as the version of its piece to open (the library groups
    // versions of one piece under one card; the one chosen last is the one the card opens); 0 never.
    std::int64_t chosen_utc_s = 0;

    friend bool operator==(const SongPrefs&, const SongPrefs&) = default;
};

// The longest name, composer or genre kept, in bytes.
inline constexpr std::size_t kMaxSongPrefsText = 200;

// The same with each text made valid UTF-8 (a stray byte read as Latin-1), its runs of white space
// and control characters one space, its ends trimmed and cut to kMaxSongPrefsText at a character
// boundary, and a negative time made 0.
SongPrefs sanitized(const SongPrefs& prefs);

// What shows: the player's text when there is one, else the file's.
inline std::string_view shown_text(std::string_view player, std::string_view file) {
    return player.empty() ? file : player;
}

// One change the player made to a song's settings: the one field it changes, so it can be laid
// over settings read later without touching the rest.
struct SongPrefEdit {
    enum class Field : std::uint8_t { Title, Composer, Favourite, Chosen };
    std::uint64_t hash = 0;
    Field field = Field::Title;
    std::string text; // Title and Composer: the new text (empty: the file's own)
    bool on = false;  // Favourite
    std::int64_t time_utc_s = 0; // Chosen: when
};

// `prefs` with the edited field set.
void apply_edit(SongPrefs& prefs, const SongPrefEdit& edit);

// The rows to store for `edits` made before `stored` was read (in order, later ones winning): each
// edited song's stored settings, or none, with only its edited fields changed. One row per song,
// in the order the songs were first edited.
std::vector<std::pair<std::uint64_t, SongPrefs>>
apply_edits(const std::unordered_map<std::uint64_t, SongPrefs>& stored,
            std::span<const SongPrefEdit> edits);

} // namespace etude
