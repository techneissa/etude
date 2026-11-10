// SPDX-License-Identifier: GPL-3.0-or-later
// The song sound setting, apart from SongController so the window options can name it.
#pragma once

#include <cstdint>

namespace etude::app {

// What you hear of the song (the song sound setting). Your own playing always sounds.
enum class SongSound : std::uint8_t {
    Mute,      // default: nothing from the song
    OtherHand, // the hand you are not practising; nothing when you practise both
    BothHands, // the whole song
};

} // namespace etude::app
