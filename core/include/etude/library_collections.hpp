// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// Collections: the folders inside the Songs folder, nested as they are on disk. A song's folder is
// its path's folders joined by '/' ("Classical/Baroque"; empty at the top). A collection holds the
// songs in its folder and in the folders inside it.

// Whether a song in `folder` is in the collection at `path` (empty: the whole library).
bool in_collection(std::string_view folder, std::string_view path);

struct Collection {
    std::string path; // "Classical/Baroque"
    std::string name; // "Baroque"
    std::uint32_t songs = 0;
};
// The collections right inside `parent`, with the songs in each (its own folders' counted in),
// ordered by name (folded, so case and accents do not split them). `out` is reused.
void child_collections(std::span<const std::string> folders, std::string_view parent,
                       std::vector<Collection>& out);

// The parts of a collection's path, top first, each with the path up to it: "A/B" gives
// {"A", "A"}, {"B", "A/B"}. Views into `path`.
struct Crumb {
    std::string_view name;
    std::string_view path;
};
void breadcrumb(std::string_view path, std::vector<Crumb>& out);

// The Inbox: songs new to the library that the player has not done anything with yet. A song is a
// new import for kInboxDays after the library first listed it, until it is played or put in a
// group. The Inbox shows first among the collections while it holds any.
inline constexpr std::int64_t kInboxDays = 7;
bool is_new_import(std::int64_t added_utc_s, std::int64_t now_utc_s, bool played, bool grouped);

// The A to Z index beside a list sorted by name: slot 0..25 for a title starting with a to z
// (folded), 26 for anything else ("#"). Each slot holds the first place in the order of a song in
// it, or -1.
inline constexpr int kAlphaSlots = 27;
using AlphaIndex = std::array<std::int32_t, kAlphaSlots>;
int alpha_slot(std::string_view folded_title);
void alpha_index(std::span<const std::uint32_t> order, std::span<const std::string> folded_titles,
                 AlphaIndex& out);
// The slot to scroll to for a finger on `slot`: that slot when it holds songs, else the nearest
// one after it that does, else the nearest before; -1 when the index is empty.
int alpha_target(const AlphaIndex& index, int slot);

} // namespace etude
