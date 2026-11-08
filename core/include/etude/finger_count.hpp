// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>

namespace etude {

// How many fingers are on the screen, as the finger events count them, so a finger that lands can
// be told from a second one. Real fingers and the dev flags' scripted ones (synthetic events SDL
// never tracks) are counted apart: a lift that never came is forgotten for a real finger once SDL
// itself says none is down, and a scripted finger is never forgotten by time.
//
// A real finger's count can only be wrong when a lift or a cancel was lost, and a finger that rests
// sends no event at all, so time alone never forgets it: `settle` asks SDL, and only when the
// pointer is up and no finger event has come for `kFingerIdleFrames` frames.
inline constexpr int kFingerIdleFrames = 3;

class FingerCount {
  public:
    // A finger landed (`scripted`: a synthetic event) in frame `frame`.
    void down(bool scripted, long frame) {
        (scripted ? scripted_ : real_) += 1;
        last_event_frame_ = frame;
    }
    // A finger lifted.
    void up(bool scripted, long frame) {
        int& n = scripted ? scripted_ : real_;
        n = std::max(0, n - 1);
        last_event_frame_ = frame;
    }
    // A finger moved: the count does not change, but the touch is alive.
    void moved(long frame) { last_event_frame_ = frame; }
    // Every finger was cancelled (the system took the touch away).
    void cancelled(long frame) {
        real_ = 0;
        scripted_ = 0;
        last_event_frame_ = frame;
    }

    // Fingers on the screen, and whether a landing one has another beside it.
    int total() const { return real_ + scripted_; }
    int real() const { return real_; }

    // Once a frame: the count of real fingers may be stale (nothing says otherwise, but nothing
    // has come for a while and the pointer is up), so SDL should be asked what is really down.
    bool needs_check(bool pointer_down, long frame) const {
        return real_ > 0 && !pointer_down && frame > last_event_frame_ + kFingerIdleFrames;
    }
    // SDL's answer: how many real fingers are down on the screen's touch devices. Zero forgets
    // the real ones (their lifts were lost); more leaves the count alone.
    void settle(int sdl_real_fingers) {
        if (sdl_real_fingers <= 0) {
            real_ = 0;
        }
    }

  private:
    int real_ = 0;
    int scripted_ = 0;
    long last_event_frame_ = -100;
};

} // namespace etude
