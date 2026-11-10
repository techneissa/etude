// SPDX-License-Identifier: GPL-3.0-or-later
// How a segmented control's cells share its track, with no ImGui in it. Sizes in points; `u` is the
// kit's unit (points per design point).
//
// Rule: every cell is at least `tok::kCell` wide (a hit area and the dead gap to the next cell; the
// cell's hit area is the cell less that gap) and as wide as its own content. When the track is too
// narrow for all of them in one row, the cells wrap into rows of as equal a count as possible, never
// into a row of cells under `tok::kCell`. Only a track narrower than one cell (under 54 pt, below
// any window the app supports) gives a narrower cell.
#pragma once

#include "tokens.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace etude::app::ui {

struct SegmentRows {
    int rows = 1;
    int per_row = 1;     // cells in each full row; the last row may hold fewer
    float cell_w = 0.0f; // every cell's width, the track divided by `per_row`
};

// `n` cells in a track `track_w` wide, each needing at least `content_w` (its icon and label with
// their padding).
inline SegmentRows segment_rows(float track_w, int n, float content_w, float u) {
    SegmentRows out;
    if (n <= 0 || !(track_w > 0.0f)) {
        return out;
    }
    const float least = std::max(content_w, tok::kCell * u);
    const int fit = std::clamp(static_cast<int>(std::floor(track_w / least + 1e-4f)), 1, n);
    out.rows = (n + fit - 1) / fit;
    out.per_row = (n + out.rows - 1) / out.rows; // balanced: 6 in two rows is 3 and 3, not 5 and 1
    out.cell_w = track_w / static_cast<float>(out.per_row);
    return out;
}

// Where one cell of a segmented track puts its hit area and its raised pill (the selected cell's
// fill), as offsets from the cell's own edges so a test and the drawing share them. The track is
// `kFace` tall; every cell's hit area is the cell less half the dead gap at each side, and the pill
// sits the same distance inside the track on all four sides: `kInset`, or half the dead gap where
// that is more (the pill stays inside the hit area). The unselected cells draw nothing of their
// own: the track shows flush between them.
struct SegmentCellBoxes {
    float hit_x0 = 0.0f, hit_x1 = 0.0f; // the hit area, along x
    float pill_x0 = 0.0f, pill_x1 = 0.0f;
    float pill_y0 = 0.0f, pill_y1 = 0.0f; // relative to the track's top
    float track_h = 0.0f;
};

inline float segment_pill_inset(float u) { return std::max(tok::kInset, tok::kHitGap * 0.5f) * u; }

inline SegmentCellBoxes segment_cell_boxes(float x0, float x1, float u) {
    SegmentCellBoxes b;
    const float half_gap = tok::kHitGap * u * 0.5f;
    const float inset = segment_pill_inset(u);
    b.hit_x0 = x0 + half_gap;
    b.hit_x1 = x1 - half_gap;
    b.pill_x0 = x0 + inset;
    b.pill_x1 = x1 - inset;
    b.track_h = tok::kFace * u;
    b.pill_y0 = inset;
    b.pill_y1 = b.track_h - inset;
    return b;
}

// The chosen pill of a track slides from the old cell to the new one: its x and width ease out over
// `tok::kPillSlide` (no bounce) and end exactly on the new cell. Pure, so a test steps it with the
// frame times the app feeds it. One `PillSlide` is kept per control.
struct PillSlide {
    float from_x0 = 0.0f, from_x1 = 0.0f; // where the move began (the pill's edges)
    float to_x0 = 0.0f, to_x1 = 0.0f;     // the chosen cell's pill
    float t = 1.0f;                       // 0 at the start of a move, 1 when it has landed
    int cell = -1;                        // the cell the pill belongs on
    int row = 0;                          // its row of cells (a wrapped track)
    bool seen = false;                    // drawn before
};

// Ease-out, cubic: fast at first, settling softly.
inline float pill_ease(float t) {
    const float c = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - c;
    return 1.0f - inv * inv * inv;
}

// The pill's edges now.
inline void pill_slide_at(const PillSlide& s, float* x0, float* x1) {
    if (s.t >= 1.0f) {
        *x0 = s.to_x0; // exactly on the cell, never a rounding off it
        *x1 = s.to_x1;
        return;
    }
    const float e = pill_ease(s.t);
    *x0 = s.from_x0 + (s.to_x0 - s.from_x0) * e;
    *x1 = s.from_x1 + (s.to_x1 - s.from_x1) * e;
}

// One frame. The pill belongs on `cell` (in row `row`) with edges `x0`..`x1`. It jumps when drawn for
// the first time (`fresh`: also a control not drawn on the frame before, as a page just opened) or
// with `reduced` effects, or to another row; a change of cell slides from where the pill is now,
// also mid-slide; the same cell with moved edges (a resize) keeps a slide going to the new edges and
// otherwise follows them at once. `dt` is the frame's time.
inline void pill_slide_step(PillSlide& s, int cell, int row, float x0, float x1, float dt, bool fresh,
                            bool reduced) {
    const bool jump = fresh || !s.seen || reduced || row != s.row;
    if (jump) {
        s.from_x0 = s.to_x0 = x0;
        s.from_x1 = s.to_x1 = x1;
        s.t = 1.0f;
    } else if (cell != s.cell) {
        float now0 = 0.0f;
        float now1 = 0.0f;
        pill_slide_at(s, &now0, &now1);
        s.from_x0 = now0;
        s.from_x1 = now1;
        s.to_x0 = x0;
        s.to_x1 = x1;
        s.t = 0.0f;
    } else if (s.t >= 1.0f) {
        s.from_x0 = s.to_x0 = x0;
        s.from_x1 = s.to_x1 = x1;
    } else {
        s.to_x0 = x0;
        s.to_x1 = x1;
    }
    s.cell = cell;
    s.row = row;
    s.seen = true;
    if (s.t < 1.0f) {
        s.t = std::min(1.0f, s.t + dt / tok::kPillSlide);
    }
}

// The one cell whose words are bold while the pill's centre is at `pill_centre`: the cell with the
// nearest centre among `centres`. Each word changes weight once on a move, as the pill's centre
// crosses the middle between two cells' centres, and a frame never draws two weights of one word.
// Returns the index in `centres`, -1 for none.
inline int pill_bold_cell(float pill_centre, std::span<const float> centres) {
    int best = -1;
    float best_gap = 0.0f;
    for (std::size_t i = 0; i < centres.size(); ++i) {
        const float gap = std::fabs(centres[i] - pill_centre);
        if (best < 0 || gap < best_gap) {
            best = static_cast<int>(i);
            best_gap = gap;
        }
    }
    return best;
}

// How chosen a cell reads (0..1) while the pill slides, for the words' colour: the cell that loses it
// fades out as the cell that gains it fades in, over the same time and curve as the pill. `weight` is
// the stored linear progress toward `on`; the shown value is eased the way the pill is.
inline float cell_weight_step(float* weight, bool on, float dt, bool fresh, bool reduced) {
    const float target = on ? 1.0f : 0.0f;
    if (fresh || reduced) {
        *weight = target;
    } else if (*weight < target) {
        *weight = std::min(target, *weight + dt / tok::kPillSlide);
    } else if (*weight > target) {
        *weight = std::max(target, *weight - dt / tok::kPillSlide);
    }
    return on ? pill_ease(*weight) : 1.0f - pill_ease(1.0f - *weight);
}

} // namespace etude::app::ui
