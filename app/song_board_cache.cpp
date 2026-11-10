// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_board_cache.hpp"

namespace etude::app {

void SongBoardCache::refresh(std::span<const ProfileRuns> household, std::uint64_t song_hash,
                             std::int32_t today, std::int64_t active, std::uint32_t card_serial,
                             std::size_t run_total, const RunRecord* run) {
    Key key;
    key.song_hash = song_hash;
    key.today = today;
    key.active = active;
    key.serial = card_serial;
    key.runs = run_total;
    if (built_ && key == key_) {
        return;
    }
    key_ = key;
    built_ = true;
    boards_ = build_song_boards(household, song_hash, today, active, run);
}

} // namespace etude::app
