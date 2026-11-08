// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace etude {

// How long the revealed control bar stays after the last use. A mouse can hover to keep it open, so
// it leaves sooner; a finger has no hover, so after a touch it stays longer. While the player is at
// the keys (held keys, or a song playing) it goes sooner in both cases.
inline constexpr double kBarHoldMouseS = 3.0;
inline constexpr double kBarHoldMousePlayingS = 1.0;
inline constexpr double kBarHoldTouchS = 5.0;
inline constexpr double kBarHoldTouchPlayingS = 2.5;

// `touch`: the last use of the bar was a finger. `at_keys`: the player is at the piano.
constexpr double bar_hold_s(bool touch, bool at_keys) {
    if (touch) {
        return at_keys ? kBarHoldTouchPlayingS : kBarHoldTouchS;
    }
    return at_keys ? kBarHoldMousePlayingS : kBarHoldMouseS;
}

} // namespace etude
