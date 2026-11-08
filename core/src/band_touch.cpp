// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/band_touch.hpp"

#include "etude/tap_rule.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

void BandTouch::press(const OverviewHit& on, float at_x, float at_y, double t) {
    *this = BandTouch{};
    hit = on;
    press_x = at_x;
    press_y = at_y;
    x = at_x;
    press_s = t;
    mode = on.kind == OverviewHit::Kind::Marker ? Mode::Marker : Mode::Pending;
}

BandTouch::Hold BandTouch::hold(float at_x, float at_y, double t, float slop,
                                double long_press_s) {
    if (!std::isfinite(at_x) || !std::isfinite(at_y)) {
        return Hold::None;
    }
    x = at_x;
    const float dx = at_x - press_x;
    const float dy = at_y - press_y;
    const bool beyond = exceeds_slop(dx, dy, slop);
    // The axis is decided once, as the finger leaves the slop: a vertical drag is a bar swipe (or
    // nothing), never a scrub or a cut moved, whether the bar claims it or not.
    if (beyond && !axis_decided_ && (mode == Mode::Pending || mode == Mode::Marker)) {
        axis_decided_ = true;
        if (decide_drag_axis(dx, dy) == DragAxis::Vertical) {
            mode = Mode::Done;
            return Hold::None;
        }
    }
    switch (mode) {
    case Mode::Pending:
        if (beyond) {
            mode = Mode::Scrub;
            return Hold::Scrub;
        }
        if (t - press_s >= long_press_s - 1e-9) { // exactly the long-press time counts
            mode = Mode::Done;
            return hit.kind == OverviewHit::Kind::Segment ? Hold::Menu : Hold::Lane;
        }
        return Hold::None;
    case Mode::Scrub:
        return Hold::Scrub;
    case Mode::Marker:
        moved = moved || beyond;
        return moved ? Hold::MoveCut : Hold::None;
    case Mode::Idle:
    case Mode::Done:
        break;
    }
    return Hold::None;
}

BandTouch::Lift BandTouch::lift(SectionId section_at_x) {
    Lift out = Lift::None;
    switch (mode) {
    case Mode::Pending:
        if (hit.kind == OverviewHit::Kind::Segment) {
            section = hit.section;
            out = Lift::Loop;
        } else {
            seek_x = press_x;
            out = Lift::Seek;
        }
        break;
    case Mode::Marker:
        if (moved) {
            out = Lift::MoveCut;
        } else if (section_at_x != kNoSection) {
            section = section_at_x;
            out = Lift::Loop;
        }
        break;
    case Mode::Idle:
    case Mode::Scrub:
    case Mode::Done:
        break;
    }
    mode = Mode::Idle;
    return out;
}

void BandTouch::cancel() {
    if (mode != Mode::Idle) {
        mode = Mode::Done;
    }
}

float BandTouch::long_press_progress(double t, double long_press_s) const {
    if (mode != Mode::Pending || !(long_press_s > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp((t - press_s) / long_press_s, 0.0, 1.0));
}

LoopTap loop_toggle_tap(bool loop_on, SectionId at_playhead) {
    if (loop_on) {
        return LoopTap::EndLoop;
    }
    return at_playhead != kNoSection ? LoopTap::LoopSection : LoopTap::NoSection;
}

} // namespace etude
