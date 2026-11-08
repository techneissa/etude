// SPDX-License-Identifier: GPL-3.0-or-later
// The section band: a row of its own under the bar (with a pinned bar it stays; with the bar
// revealed it hangs under it), where the song's sections are practised and cut without opening the
// settings. Left to right (band_layout.hpp):
//
//   loop       on: ends the loop; off: loops the section at the playhead
//   track      the sections as named bands, the cuts between them, the playhead and a loop set at
//              the keys. A tap on a band loops it, a long press opens its menu (rename, join with
//              the next, add to the practice list, delete, undo, edit the cuts on the lane), a cut
//              is dragged to move it (snapping to bars), a drag along the track scrubs. With no
//              sections a tap seeks and a long press opens the lane. (core band_touch.hpp)
//   time       "0:42 / 2:21"
//   cut        a cut at the playhead
//   list       the practice list, a popover: its items and their passes and speed-up, start and
//   stop,
//              skip, add all, clear, and the hand path
//   suggest    the suggested cuts, shown dashed on the lane
//   lane       the band grows into the lane under it: the section editor's roll docked in place
//   editor     the full section editor, for zooming in and fine cuts
//
// Every control draws its states through the kit's `draw_state`. The band holds no sections of its
// own: it reads and edits them through the SectionsPort, and the song through the SongPort.
#pragma once

#include "band_layout.hpp"
#include "kit.hpp"
#include "sections_port.hpp"
#include "song_port.hpp"

#include "etude/band_touch.hpp"

#include <string>

namespace etude::app::ui {

// Where the host puts the band this frame, in points.
struct BandPlace {
    RectF band;         // the band's row
    float below = 0.0f; // the bottom of the band, or of the lane under it: popovers start here
    float keyboard_top = 0.0f;
    float display_w = 0.0f;
    float alpha = 1.0f;      // the bar's reveal
    bool live = true;        // far enough in to take touches
    bool difficulty = false; // tint each bar of the track by how hard it is (AppSettings)
};

class SectionBand {
  public:
    void draw(Kit& k, SongPort& song, const BandPlace& place);

    // The lane is asked for (the lane button, a long press off the sections, a menu's "Edit cuts",
    // suggest). The host gives it its room (core LaneFit) and draws it.
    bool lane() const { return lane_; }
    void set_lane(bool on) { lane_ = on; }
    // The practice list's popover.
    void open_list() { list_open_ = true; }
    bool list_open() const { return list_open_; }
    bool menu_open() const { return menu_open_; }
    // Something the band opened, or a touch on it, keeps the bar shown.
    bool holds_bar() const { return lane_ || list_open_ || menu_open_ || touch_.active(); }
    void close_popovers() {
        list_open_ = false;
        menu_open_ = false;
    }
    // The touch on the track became a bar swipe: no scrub, cut drag, long press or seek.
    void cancel_touch() { touch_.cancel(); }

  private:
    void track(Kit& k, SongPort& song, SectionsPort* sp, const BandLayout& l, bool live);
    void tools(Kit& k, SongPort& song, SectionsPort* sp, const BandLayout& l, bool live);
    void list_popover(Kit& k, SectionsPort& sp, const BandPlace& place, float anchor_x);
    void menu_popover(Kit& k, SectionsPort& sp, const BandPlace& place);
    const FitText& clock(double position_s, double duration_s);

    bool lane_ = false;
    bool tint_ = false; // BandPlace::difficulty of the frame being drawn
    bool list_open_ = false;
    bool menu_open_ = false;
    SectionId menu_section_ = kNoSection;
    float menu_anchor_x_ = 0.0f;
    float list_anchor_x_ = 0.0f;
    float list_h_ = 0.0f; // the list popover's content height last frame
    float menu_h_ = 0.0f;
    std::size_t plan_focus_ = 0; // the practice list item being edited
    int opened_frame_ = -1;      // a popover opened this frame ignores the tap that opened it

    BandTouch touch_;
    double cut_s_ = 0.0;        // where a dragged cut is now
    double cut_label_s_ = -1.0; // the time `cut_label_` names
    std::string cut_label_;     // "Bar 12", made again only when the dragged cut moves
    double seen_loop_start_ = 0.0;
    double seen_loop_end_ = 0.0;

    FitText clock_;
    int clock_position_ = -1;
    int clock_duration_ = -1;
};

} // namespace etude::app::ui
