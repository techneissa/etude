// SPDX-License-Identifier: GPL-3.0-or-later
// The Board tab's boards of the run the results card is about, one per period, built on the frame
// thread from the household's runs in memory. A board is rebuilt only when what it reads changed:
// the card, the song, the day, the profile in use or the number of runs.
#pragma once

#include "etude/song_board.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace etude::app {

class SongBoardCache {
  public:
    // Whether the boards are not those of this card, song, day, profile and number of runs (all
    // the runs of every profile): they must be built again. Cheap: no run is read.
    bool stale(std::uint64_t song_hash, std::int32_t today, std::int64_t active,
               std::uint32_t card_serial, std::size_t run_total) const {
        return !built_ || !(key_ == Key{song_hash, today, active, card_serial, run_total});
    }
    // Builds Week, Month and All time for `song_hash` (`run_total`: the runs there are in all) and the card's `run` (the points and the
    // comparison of the head are its own) when the key changed since the last call.
    void refresh(std::span<const ProfileRuns> household, std::uint64_t song_hash,
                 std::int32_t today, std::int64_t active, std::uint32_t card_serial,
                 std::size_t run_total, const RunRecord* run = nullptr);
    // The card went: the boards are dropped.
    void clear() { built_ = false; }

    // The board of `period`; null until built.
    const SongBoard* board(Period period) const {
        return built_ ? &boards_[static_cast<std::size_t>(period)] : nullptr;
    }

  private:
    struct Key {
        std::uint64_t song_hash = 0;
        std::int32_t today = 0;
        std::int64_t active = 0;
        std::uint32_t serial = 0;
        std::size_t runs = 0;
        friend bool operator==(const Key&, const Key&) = default;
    };
    bool built_ = false;
    Key key_;
    std::array<SongBoard, 3> boards_;
};

} // namespace etude::app
