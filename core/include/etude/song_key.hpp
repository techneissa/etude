// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// A song's identity in the practice history.
//
// The content hash of the file's bytes is the whole identity. Files with the same bytes are the
// same song and share one history, wherever they are and whatever they are called; a renamed or
// moved file keeps its history; an edited file (other bytes) is a new song. The path is metadata
// only: the songs table keeps the latest place the song was seen, and the key never depends on it.
// The one exception is a song whose hash is unknown (0): its normalized path stands in as the key,
// so runs of such a song still group together. With neither the key is empty and the run belongs to
// no song row.
//
// Keys are stored, so their form never changes: "h:" and 16 lowercase hex digits, or "p:" and the
// normalized path.
std::string song_key(std::uint64_t content_hash, std::string_view path);

// A path with '\' read as '/', repeated and trailing '/' dropped and a leading "./" removed. Case
// is kept. Empty when nothing is left.
std::string normalized_song_path(std::string_view path);

} // namespace etude
