// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace etude {

// Which answer the screen still wants. A replay is read on the history thread and comes back some
// frames after it was asked for, by which time the player may have asked for another run or opened
// another song: the screen takes an answer only when its serial is the latest request's and its
// song is the one asked about and still the one open. Anything else is dropped.
class ReplayRequestGate {
  public:
    // A request was made: it, and only it, is wanted now. `song_hash` 0 (a song with no content
    // hash) asks nothing: no answer will apply.
    void begin(std::uint64_t serial, std::uint64_t song_hash) {
        serial_ = serial;
        song_hash_ = song_hash;
    }
    // Nothing is wanted any more (the card went, another song opened).
    void cancel() { serial_ = 0; }

    bool pending() const { return serial_ != 0; }
    std::uint64_t serial() const { return serial_; }

    // Whether an answer with `serial` about `answer_song_hash` applies while `open_song_hash` is
    // the open song's.
    bool accepts(std::uint64_t serial, std::uint64_t answer_song_hash,
                 std::uint64_t open_song_hash) const {
        return serial_ != 0 && song_hash_ != 0 && serial == serial_ &&
               answer_song_hash == song_hash_ && open_song_hash == song_hash_;
    }

  private:
    std::uint64_t serial_ = 0;
    std::uint64_t song_hash_ = 0;
};

} // namespace etude
