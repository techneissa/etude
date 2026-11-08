// SPDX-License-Identifier: GPL-3.0-or-later
// The accompaniment button on the bar and its menu, shared by the three bar styles. Shown only for
// a song that has accompaniment parts (the parts that are not a practice hand):
//
//   the button   a tap mutes or unmutes every part; the icon is a voice glyph when every part is a
//                voice and an ensemble glyph otherwise, morphing to its slashed form (grey) when
//                all are muted and to a half slash when some are. A long press or a right click
//                opens the menu.
//   the chevron  its own hit area beside the button, so the menu is never touch-only.
//   the menu     a card under the bar: an "All" row, one row per part by name, and the volume; in
//                two columns where a short window has no room for one (accompaniment_card.hpp).
//
// Each part's mute is kept per song (SongPracticeSync); the volume is the player's own
// (AppSettings::accompaniment_volume, also on Settings > Practice).
#pragma once

#include "bar.hpp"
#include "tempo.hpp"

namespace etude::app::ui {

class AccompanimentMenu {
  public:
    // The button in a hit area `hit` big round `centre`, its face `drawn` big; `caption` (may be
    // null) goes under the icon. Opening the menu closes the tempo popover.
    void button(Kit& k, SongPort& song, TempoControls& tempo, ImVec2 centre, ImVec2 hit,
                float drawn, bool live, const char* caption);
    // The chevron beside it, its face as big as the button's.
    void chevron(Kit& k, TempoControls& tempo, ImVec2 centre, ImVec2 hit, float drawn, bool live,
                 const char* caption);

    // The menu under the bar, from `below_y` down to the keyboard. Draws nothing while shut.
    void popover(Kit& k, UiModel& model, SongPort& song, const Frame& f, float below_y);

    bool open() const { return open_; }
    void close() { open_ = false; }

  private:
    bool open_ = false;
    float open_p_ = 0.0f;
    OutsideTap outside_; // the tap outside that closes it, never the release that opened it
    float anchor_x_ = 0.0f; // the middle of the button and its chevron
    float column_h_[2] = {0.0f, 0.0f}; // each column's rows' height last frame (0: not known)
};

} // namespace etude::app::ui
