// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/section_overview.hpp"
#include "etude/touch_gesture.hpp"

#include <cstdint>

namespace etude {

// One touch on the section band's track, from the press to the lift, as a pure state machine fed
// the touch's position and the time. What it becomes:
//
//   a tap on a section          loop it (again: end the loop)
//   a tap off any section       seek there (a song with no sections)
//   a tap on a cut's grab zone  loop the section under the finger
//   a long press on a section   that section's menu (rename, join, delete, ...)
//   a long press off sections   the lane opens (the roll to cut the song on)
//   a drag along the track      scrub: the playhead follows the finger
//   a drag that began on a cut  move that cut, shown where the finger is, committed on the lift
//
// The touch slop and the long-press time are the recogniser's (`kTouchSlopPt`, `kLongPressS`):
// a press that moves less than the slop (in any direction) is still a tap or a long press, and a
// long press fires while the finger is still down. The drag's axis is decided once, as the finger
// leaves the slop (`decide_drag_axis`): a vertical one is the bar's swipe and ends the touch with
// nothing done, so a swipe that drifts sideways never scrubs or moves a cut, whether the bar
// claims it or not. `cancel` (the touch became a bar swipe, or a second finger came) ends it with
// nothing done.
struct BandTouch {
    enum class Mode : std::uint8_t { Idle, Pending, Scrub, Marker, Done };

    // What a frame of holding did.
    enum class Hold : std::uint8_t {
        None,
        Scrub,   // seek to the finger (every frame of a scrub)
        Menu,    // a long press on a section: `hit.section`'s menu, once
        Lane,    // a long press off the sections: open the lane, once
        MoveCut, // the cut being dragged is now at the finger (shown, not yet committed)
    };
    // What the lift did.
    enum class Lift : std::uint8_t {
        None,
        Loop,    // loop `section` (or end its loop)
        Seek,    // seek to `seek_x`
        MoveCut, // commit the cut `hit.marker` at the finger's last x
    };

    Mode mode = Mode::Idle;
    OverviewHit hit; // what the press landed on
    float press_x = 0.0f;
    float press_y = 0.0f;
    double press_s = 0.0;
    float x = 0.0f;                 // the finger now
    bool moved = false;             // a dragged cut has left its slop
    SectionId section = kNoSection; // Lift::Loop: the section to loop
    float seek_x = 0.0f;            // Lift::Seek

    // A press at (`at_x`, `at_y`) at time `t`, landing on `on` (overview_hit).
    void press(const OverviewHit& on, float at_x, float at_y, double t);
    // The finger at (`at_x`, `at_y`) at time `t`, still down. `slop` is the touch slop in the
    // caller's units (kTouchSlopPt times its unit).
    Hold hold(float at_x, float at_y, double t, float slop, double long_press_s = kLongPressS);
    // The finger lifted. `section_at_x` is the section under the press point (for a tap on a cut's
    // zone), or kNoSection.
    Lift lift(SectionId section_at_x);
    // The touch is taken away: nothing more happens until the next press.
    void cancel();
    // 0..1 while a press is held still toward a long press, else 0.
    float long_press_progress(double t, double long_press_s = kLongPressS) const;
    bool active() const { return mode != Mode::Idle && mode != Mode::Done; }

  private:
    bool axis_decided_ = false;
};

// What a tap on the band's loop toggle does: with a loop on, end it; else loop the section at the
// playhead; with no section there, nothing (the player is told to cut the song first).
enum class LoopTap : std::uint8_t { EndLoop, LoopSection, NoSection };
LoopTap loop_toggle_tap(bool loop_on, SectionId at_playhead);

} // namespace etude
