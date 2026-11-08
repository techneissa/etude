// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/song.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace etude {

// The accompaniment parts of a song (the parts that are not a practice hand: see instrument.hpp),
// what the accompaniment button and its menu list. A part is kept by its key, the track and
// channel it came from, so a mute stays with the part in the same file whatever else changes.
struct AccompanimentPart {
    std::size_t index = 0;   // in Song::parts
    std::uint32_t key = 0;   // part_key()
    std::string name;        // for the menu: the part's name, else its instrument, else "Part N"
    bool voice = false;      // is_voice_part()
    int notes = 0;
};

inline constexpr std::uint32_t part_key(std::uint16_t track, std::uint8_t channel) {
    return (static_cast<std::uint32_t>(track) << 8) | channel;
}

// The song's accompaniment parts in the order of Song::parts, at most kMaxAccompanimentParts, with
// distinct names (a repeated name gets " 2", " 3", ... that no other part has). Empty for a song
// with none. Parts past the cap have no row of their own: they follow "All" (the controller mutes
// the whole accompaniment while every listed part is muted).
inline constexpr std::size_t kMaxAccompanimentParts = 32;
std::vector<AccompanimentPart> accompaniment_parts(const Song& song);

// How the button looks: every part sounds, some do, none does.
enum class AccompanimentState : std::uint8_t { On, Partial, Off };

// The state when `muted` of `parts` parts are muted. No parts counts as On.
AccompanimentState accompaniment_state(std::size_t parts, std::size_t muted);

// Which parts are muted, as sorted part keys. `parts` are the song's accompaniment parts; keys of
// parts the song does not have are dropped, so a stored list from another version of the file
// cannot mute anything.
std::vector<std::uint32_t> muted_keys_in(const std::vector<AccompanimentPart>& parts,
                                         const std::vector<std::uint32_t>& keys);

// The state of `parts` with `muted` (sorted keys) muted.
AccompanimentState accompaniment_state(const std::vector<AccompanimentPart>& parts,
                                       const std::vector<std::uint32_t>& muted);

// `muted` after the player taps the button (or the menu's All row): with every part sounding it
// mutes them all; otherwise (some or none sounding) every part sounds. The result is sorted keys.
std::vector<std::uint32_t> after_button_tap(const std::vector<AccompanimentPart>& parts,
                                            const std::vector<std::uint32_t>& muted);

// `muted` with one part flipped (sorted keys).
std::vector<std::uint32_t> after_part_toggle(const std::vector<std::uint32_t>& muted,
                                             std::uint32_t key);

// Every key muted: what a global default of "accompaniment off" starts a song with.
std::vector<std::uint32_t> all_keys(const std::vector<AccompanimentPart>& parts);

} // namespace etude
