// SPDX-License-Identifier: GPL-3.0-or-later
// The free-play scale overlay's controls: a pill in the top row that names the current key and
// scale ("E♭ Major ⌄", or "Scale" while it is off) and the popover it opens, hung under the row:
// the key ring on the left, as large as the room allows, and on the right all fourteen scale types
// in a grid, the Fingers toggle and Off (layout and the rules in scale_card_layout.hpp). A pick on
// the ring or a type turns the overlay on. The choice is the player's, kept per profile in
// AppSettings (scale_overlay, scale_root, scale_type, scale_fingers); the keyboard's drawing of it
// is the scene's (scene_scale.cpp). Free play only: the host hides both while a song is loaded.
#pragma once

#include "bar.hpp"
#include "ui_model.hpp"

namespace etude::app::ui {

class ScalePill {
  public:
    // The pill's width in points for what the model says now: the host leaves this much of the
    // status area for it.
    float width(const Kit& k, const AppSettings& app) const;

    // Draws the pill with its left edge at `x`, centred in the top row, in a layer of its own, at
    // most `max_w` wide (the room before the bar or the gear). False, and nothing drawn, when that
    // is under a hit. `live` false draws it without taking a tap.
    bool pill(Kit& k, UiModel& model, const Frame& f, float x, float max_w, bool live);

    // The popover under the row, down to the keyboard. Draws nothing while shut.
    void popover(Kit& k, UiModel& model, const Frame& f);

    // True when `p` is on the pill as the last frame drew it (the row's tap that reveals the bar
    // leaves the pill out, as it does the library's button).
    bool contains(ImVec2 p) const {
        return p.x >= area_.x0 && p.x < area_.x1 && p.y >= area_.y0 && p.y < area_.y1;
    }

    bool open() const { return open_; }
    void close() { open_ = false; }
    void forget() { area_ = {}; } // not drawn this frame
    void open_now() { open_ = true; }

  private:
    bool open_ = false;
    float open_p_ = 0.0f;
    float pill_x_ = 0.0f;
    Box area_;
    OutsideTap outside_;
};

} // namespace etude::app::ui
