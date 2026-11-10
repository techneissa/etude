// SPDX-License-Identifier: GPL-3.0-or-later
// Where a settings row's parts go inside one page column, so the controls of every row line up.
// No ImGui in it; `etude --check-settings` checks the rows the pages really draw (row_layout.hpp,
// column_check.hpp).
//
// A row has a label side (its icon, its text, an info button) and a control side. Four kinds:
// - Wide: a segmented track, a slider or a stepper beside its label. In one column every Wide
//   control starts at the same x (`ColumnPlan::control_x`) and ends at the same x
//   (`control_right`): the label side is as wide as the widest label of the column's wide rows
//   needs (`LabelZones`), so a control never starts where its own label happens to end. A row
//   whose cells cannot share that width, or a column too narrow for any control beside its label
//   (`ColumnPlan::stacked`), puts the control under its label, from the icons' x to
//   `control_right`, again at one x for the column. A slider's track runs along the span with its
//   knob overhanging either end, so the track starts where a segmented track beside it does.
// - Compact: a switch, a value pill, a chevron. Its right edge is `control_right`, always; its left
//   edge is its own width in from there.
// - Block: a control with no label (a row of choices that is the row): from the x the rows' icons
//   start at to `control_right`, like a control stacked under its label.
// - Inline: a Wide control that never stacks (a stepper). Where the column does not stack it is a
//   Wide control; in a column too narrow for a label zone it ends at `control_right` and is
//   `kMinStepper` wide.
//
// A column with a slider or a stepper that has a reset button keeps a gutter at its trailing end for
// the button (`kResetReserve`): every control of the column ends that much sooner, so they still
// end at one x, and the button's hit area runs from `kHitGap` past `control_right` to the column's
// edge. The gutter is there whether or not a button is showing, so rows never move when a value
// leaves its default.
#pragma once

#include "tokens.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace etude::app::ui {

// Design points, scaled by the kit's unit.
inline constexpr float kRowInset = tok::kS3; // a column's edge to a row's first or last part
// The label's end to the first Wide control (a slider's knob overhangs it).
inline constexpr float kZoneGap = tok::kS4;
inline constexpr float kMinWideControl = 104.0f; // a slider's shortest track and its value
inline constexpr float kMinStepper = 120.0f;     // a stepper's two hit cells and room for a number
// The label side is never wider than this of a column; until the labels are measured it is
// `kDefaultZoneShare`.
inline constexpr float kMaxZoneShare = 0.56f;
// The gutter a reset button takes at a column's trailing end: its hit area (`tok::kHit`) and the dead
// gap before it, less the margin every row already keeps (`kRowInset`), which the hit area uses.
inline constexpr float kResetReserve = tok::kHit + tok::kHitGap - kRowInset;
inline constexpr float kDefaultZoneShare = 0.38f;

enum class RowKind : std::uint8_t { Wide, Compact, Block, Inline };

struct Span {
    float x0 = 0.0f;
    float x1 = 0.0f;
    float w() const { return x1 - x0; }
};

struct ColumnPlan {
    float left = 0.0f;          // the column's content box
    float right = 0.0f;
    float control_x = 0.0f;     // where Wide controls start beside their labels
    float control_right = 0.0f; // where every control ends
    bool stacked = false;       // no Wide control fits beside its label: it goes under it
    bool reset = false;         // the column keeps a gutter after `control_right` for a reset button
};

// `label_need` is how far from `left` the widest Wide label of the column ends, its icon and info
// button included (0 when not measured yet). `reset`: some row of the column has a reset button,
// so the column keeps its gutter.
inline ColumnPlan plan_column(float left, float right, float label_need, float u,
                              bool reset = false) {
    ColumnPlan p;
    p.left = left;
    p.right = right;
    p.reset = reset;
    const float width = std::max(0.0f, right - left);
    p.control_right = right - (kRowInset + (reset ? kResetReserve : 0.0f)) * u;
    const float zone = label_need > 0.0f ? label_need + kZoneGap * u : kDefaultZoneShare * width;
    p.control_x = left + std::min(zone, kMaxZoneShare * width);
    p.stacked = p.control_right - p.control_x < kMinWideControl * u;
    return p;
}

// A control under its label, or with no label at all: from where the rows' icons start to where
// every control ends.
inline Span block_span(const ColumnPlan& p, float u) {
    return {p.left + kRowInset * u, p.control_right};
}

// Where a Wide control goes: beside its label, or `stacked` under it (the column's, or one whose
// cells do not fit the room beside the label).
inline Span wide_span(const ColumnPlan& p, bool stacked, float u) {
    if (stacked || p.stacked) {
        return block_span(p, u);
    }
    return {p.control_x, p.control_right};
}

// A Wide control that keeps its row whatever the column's width (a stepper).
inline Span inline_span(const ColumnPlan& p, float u) {
    if (p.stacked) {
        return {p.control_right - kMinStepper * u, p.control_right};
    }
    return {p.control_x, p.control_right};
}

inline Span compact_span(const ColumnPlan& p, float natural_w) {
    return {p.control_right - natural_w, p.control_right};
}

// A slider's track and its touch area at the row's end (`kKnobReach`: the knob's radius, so a knob at
// the end stays inside the touch area and the face the audit checks).
// - Where the track ends: a track under its label that runs to the end of a column with a reset
//   gutter stops `kKnobReach` short of `span_end`, so its knob and its touch area end before the
//   button; any other track ends at `span_end`.
// - Where its touch area ends: the knob's reach past the track's end, never past `control_right` in
//   a column with a reset gutter, so the dead gap `tok::kHitGap` before the button's hit area
//   (`reset_span`) is kept.
inline constexpr float kKnobReach = 14.0f;
inline float slider_track_end(const ColumnPlan& p, float span_end, bool stacked, float u) {
    return stacked && p.reset ? span_end - kKnobReach * u : span_end;
}
inline float track_hit_right(const ColumnPlan& p, float track_end, float u) {
    const float reach = track_end + kKnobReach * u;
    return p.reset ? std::min(reach, p.control_right) : reach;
}

// Where a row's reset button's hit area goes: `tok::kHit` wide, `tok::kHitGap` clear of the
// control, ending at the column's edge.
inline Span reset_span(const ColumnPlan& p, float u) {
    return {p.control_right + tok::kHitGap * u, p.control_right + (tok::kHitGap + tok::kHit) * u};
}


// What the wide rows of each column report, so the next frame's `plan_column` knows how far the
// widest label reaches. A report is taken for the frame after it: a frame's rows all read the value
// the previous frame's rows made, so one column never has two zones. `slot` names a column of a
// page (hash of the page, the table column, the window); `frame` is the UI's frame number.
class LabelZones {
  public:
    float need(std::uint32_t slot, long frame) {
        Entry& e = find(slot);
        roll(e, frame);
        return e.published;
    }
    void report(std::uint32_t slot, long frame, float label_end) {
        Entry& e = find(slot);
        roll(e, frame);
        e.building = std::max(e.building, label_end);
    }
    // Whether the previous frame had a row with a reset button in this column.
    bool reset(std::uint32_t slot, long frame) {
        Entry& e = find(slot);
        roll(e, frame);
        return e.published_reset;
    }
    void report_reset(std::uint32_t slot, long frame) {
        Entry& e = find(slot);
        roll(e, frame);
        e.building_reset = true;
    }

  private:
    struct Entry {
        std::uint32_t slot = 0;
        bool used = false;
        long frame = -1;
        float building = 0.0f;
        float published = 0.0f;
        bool building_reset = false;
        bool published_reset = false;
    };
    static void roll(Entry& e, long frame) {
        if (e.frame != frame) {
            e.published = e.building;
            e.building = 0.0f;
            e.published_reset = e.building_reset;
            e.building_reset = false;
            e.frame = frame;
        }
    }
    Entry& find(std::uint32_t slot) {
        const std::size_t n = entries_.size();
        std::size_t i = slot % n;
        for (std::size_t tries = 0; tries < n; ++tries, i = (i + 1) % n) {
            Entry& e = entries_[i];
            if (!e.used) {
                e.used = true;
                e.slot = slot;
                return e;
            }
            if (e.slot == slot) {
                return e;
            }
        }
        return entries_[slot % n]; // full: share one, a column then just reads a wider zone
    }
    std::array<Entry, 64> entries_{};
};

} // namespace etude::app::ui
