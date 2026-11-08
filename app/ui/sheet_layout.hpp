// SPDX-License-Identifier: GPL-3.0-or-later
// The settings sheet's frame and its rail of pages, with no ImGui in it, so
// tests/sheet_layout_test.cpp checks the rules with the real tokens. Sizes in points; `u` is the
// kit's unit (points per design point).
//
// Rules:
// - Every rail row keeps its full height at every window size and UI scale: `tok::kCell` (a hit
//   area and the dead gap to the next row) with the name beside the icon, `tok::kCell` +
//   `kRailCaptionExtra` with the name under it. Rows never
//   shrink and never overlap; when they do not fit, the rail scrolls.
// - A rail that scrolls shows whole rows only: its view is a whole number of rows tall, centred in
//   the sheet, and it comes to rest on a row edge, so no tab is ever cut at rest. The chosen
//   page's row is always in view.
// - A narrow row's icon and caption stay inside the row, the caption under the icon.
#pragma once

#include "page_ids.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace etude::app::ui {

inline constexpr float kSheetMaxW = 1000.0f; // design points
inline constexpr float kSheetMaxH = 620.0f;
inline constexpr float kRailWideFrom = 640.0f; // a sheet this wide gets names beside the icons
inline constexpr float kRailWideW = 140.0f; // the icon, then the longest name ("Add songs") whole
inline constexpr float kRailNarrowW = 76.0f;
inline constexpr float kRailCaptionExtra = 8.0f; // a narrow row's caption under its icon
inline constexpr float kRailCaptionShare = 0.9f; // of `tok::kCaption`
inline constexpr float kRailCaptionDrop = 11.0f; // from the icon's centre to the caption's top
inline constexpr float kRailFaceInset = 2.0f;   // a row's face, top and bottom, inside its hit
inline constexpr float kSheetHeadExtra = 4.0f;    // the page header's height past `tok::kHit`
inline constexpr float kSheetItemGap = 4.0f;      // ImGui's item spacing, under the header
inline constexpr float kTwoColumnsFrom = 540.0f;  // a page this wide lays out in two columns

struct SheetFrame {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// The sheet in a `display_w` by `display_h` window whose content starts at `top` (under the top
// row), before it slides in.
inline SheetFrame sheet_frame(float display_w, float display_h, float top, float u) {
    const float margin = tok::kS3 * u;
    SheetFrame f;
    // Whole points, so its edges stay sharp at any scale.
    f.w = std::floor(std::min(display_w - 2.0f * margin, kSheetMaxW * u));
    f.h = std::floor(std::max(0.0f, std::min(display_h - top - margin, kSheetMaxH * u)));
    f.x = std::round((display_w - f.w) * 0.5f);
    f.y = top;
    return f;
}

struct RailLayout {
    bool wide = false;   // names beside the icons; else captions under them
    float w = 0.0f;      // the rail's width
    float pad = 0.0f;    // between the sheet's edge and the rows, on every side
    float row_h = 0.0f;  // every row's height, never less than `tok::kCell`
    float lead = 0.0f;   // from the first pad to the view's top: what whole rows leave over, halved
    float view_h = 0.0f; // the rail's visible height: whole rows when it scrolls, else all the rows
    int rows_in_view = 0; // how many rows the view shows (all of them when it does not scroll)
    float content_h = 0.0f;
    bool scrolls = false; // the rows do not all fit in `view_h`
};

inline RailLayout rail_layout(float sheet_w, float sheet_h, int rows, float u) {
    RailLayout r;
    r.wide = sheet_w >= kRailWideFrom * u;
    r.w = (r.wide ? kRailWideW : kRailNarrowW) * u;
    r.pad = tok::kS2 * u;
    r.row_h = (tok::kCell + (r.wide ? 0.0f : kRailCaptionExtra)) * u;
    const float room = std::max(0.0f, sheet_h - 2.0f * r.pad);
    r.content_h = r.row_h * static_cast<float>(std::max(rows, 0));
    r.scrolls = r.content_h > room;
    if (r.scrolls) {
        r.rows_in_view = std::max(1, static_cast<int>(std::floor(room / r.row_h)));
        r.view_h = r.row_h * static_cast<float>(r.rows_in_view);
        r.lead = std::max(0.0f, std::floor((room - r.view_h) * 0.5f));
    } else {
        r.rows_in_view = std::max(rows, 0);
        r.view_h = room;
    }
    return r;
}

// Where a scrolling rail rests: `scroll` moved to the nearest row edge inside 0..`max_scroll`
// (the last edge when `max_scroll` is not a whole number of rows), so the rows in view are whole.
inline float rail_rest(float scroll, float row_h, float max_scroll) {
    if (!std::isfinite(scroll) || !std::isfinite(row_h) || !std::isfinite(max_scroll) ||
        row_h <= 0.0f || max_scroll <= 0.0f) {
        return 0.0f;
    }
    const float last = std::floor(max_scroll / row_h + 1e-4f) * row_h;
    return std::clamp(std::round(scroll / row_h) * row_h, 0.0f, last);
}

// The rest that shows row `row` as near the middle of the view as the ends allow.
inline float rail_scroll_to(int row, int rows, int rows_in_view, float row_h) {
    const int first = std::clamp(row - (rows_in_view - 1) / 2, 0, std::max(0, rows - rows_in_view));
    return static_cast<float>(first) * row_h;
}

inline constexpr float kRailFadeH = 12.0f;    // design points: the soft strip at a scrolling rail's end
inline constexpr float kRailFadeGlow = 0.10f; // its white at the very edge: light enough to leave the ink readable

// Which ends of a scrolling rail fade, 0..1 each: the top while rows sit above the view, the
// bottom while rows sit below it, nothing on an end with nothing past it and nothing when the rail
// fits. It grows over half a row of scrolling, so it comes and goes with the scroll; with reduced
// effects it is all or nothing.
struct RailFade {
    float top = 0.0f;
    float bottom = 0.0f;
};

inline RailFade rail_fade(bool scrolls, float scroll, float max_scroll, float row_h, bool reduced) {
    RailFade f;
    if (!scrolls || !std::isfinite(scroll) || !std::isfinite(max_scroll) || !std::isfinite(row_h) ||
        row_h <= 0.0f || max_scroll <= 0.0f) {
        return f;
    }
    const float ramp = row_h * 0.5f;
    const float above = std::clamp(scroll, 0.0f, max_scroll);
    const float below = max_scroll - above;
    f.top = reduced ? (above > 0.5f ? 1.0f : 0.0f) : std::clamp(above / ramp, 0.0f, 1.0f);
    f.bottom = reduced ? (below > 0.5f ? 1.0f : 0.0f) : std::clamp(below / ramp, 0.0f, 1.0f);
    return f;
}

// How tall each rail page is, kept in design points so it stays a good estimate when the window or
// the UI scale changes. A change of either marks every page stale but keeps its height: the sheet
// goes on using them while they are measured again, one page a frame, and never falls back to its
// cap once every page has a height. A page's new measure replaces its old one, smaller or larger.
template <std::size_t N>
struct PageHeights {
    std::array<float, N> pt{}; // design points; 0: never measured
    std::array<bool, N> fresh{};
    float w = 0.0f;
    float h = 0.0f;
    float u = 0.0f;

    // The window and unit the heights are measured for: a change makes them all stale.
    void at(float window_w, float window_h, float unit) {
        if (window_w != w || window_h != h || unit != u) {
            fresh = {};
            w = window_w;
            h = window_h;
            u = unit;
        }
    }
    // The first stale page other than `skip` (the page a shown sheet measures where it draws it),
    // or N when there is none.
    std::size_t next_stale(std::size_t skip) const {
        for (std::size_t i = 0; i < N; ++i) {
            if (!fresh[i] && i != skip) {
                return i;
            }
        }
        return N;
    }
    // Page `i` measured `px` tall at unit `unit`.
    void set(std::size_t i, float px, float unit) {
        if (i < N && std::isfinite(px) && unit > 0.0f) {
            pt[i] = std::max(1.0f, px) / unit;
            fresh[i] = true;
        }
    }
    // The tallest page in pixels at unit `unit`; 0 until every page has a height.
    float tallest(float unit) const {
        float most = 0.0f;
        for (const float v : pt) {
            if (v <= 0.0f) {
                return 0.0f;
            }
            most = std::max(most, v);
        }
        return most * unit;
    }
};

// Where a narrow row's icon and caption go in a row from `row_y` that is `row_h` tall.
struct RailCaption {
    float icon_cy = 0.0f;   // the icon's centre
    float caption_y = 0.0f; // the caption's top
    float caption_size = 0.0f;
};

inline RailCaption rail_caption(float row_y, float row_h, float u) {
    RailCaption c;
    c.caption_size = tok::kCaption * kRailCaptionShare * u;
    c.icon_cy = row_y + row_h * 0.5f - c.caption_size * 0.55f;
    c.caption_y = c.icon_cy + kRailCaptionDrop * u;
    return c;
}

// Where a page's header and body go in the sheet, relative to the sheet's top left: the header row
// from `head_y`, the body from `body_y`, both `w` wide from `x`.
struct PageArea {
    float x = 0.0f;
    float w = 0.0f;
    float head_y = 0.0f;
    float head_h = 0.0f;
    float body_y = 0.0f;
    float body_h = 0.0f;
    int columns = 1;
};

// The page body while its open or switch lift moves it down `lift` points: the child window starts
// `lift` lower and is `lift` shorter (its bottom may clip by that much), but the page lays itself
// out against `layout_h`, the unlifted body height, so a page within the lift of fitting does not
// change its whole/split arrangement during the slide.
struct LiftedBody {
    float top = 0.0f;      // where the child window starts
    float window_h = 0.0f; // its height
    float layout_h = 0.0f; // the height the page and its header lay out against
};

inline LiftedBody lifted_body(const PageArea& a, float lift) {
    return {a.body_y + lift, std::max(0.0f, a.body_h - lift), a.body_h};
}

inline PageArea page_area(const SheetFrame& sheet, const RailLayout& rail, float u) {
    PageArea a;
    a.x = rail.w + tok::kS4 * u;
    a.w = std::max(0.0f, sheet.w - a.x - tok::kS4 * u);
    a.head_y = tok::kS2 * u;
    a.head_h = (tok::kHit + kSheetHeadExtra) * u;
    a.body_y = a.head_y + a.head_h + kSheetItemGap * u;
    a.body_h = std::max(0.0f, sheet.h - a.body_y - tok::kS3 * u);
    a.columns = a.w >= kTwoColumnsFrom * u ? 2 : 1;
    return a;
}

// The sheet's height once its pages are measured: as tall as the tallest page's body needs
// (`tallest_body`, the content of the tallest rail page laid out in `cap`'s body), so no page sits in
// a mostly empty card, but never taller than `cap` (the 620 pt x u cap and the window) and never
// so short that the rail would scroll where the cap would not. `tallest_body` 0 or less (not
// measured yet) or not finite keeps `cap`. Whole points.
inline SheetFrame fit_sheet_height(const SheetFrame& cap, float tallest_body, int rail_rows, float u) {
    if (!std::isfinite(tallest_body) || tallest_body <= 0.0f) {
        return cap;
    }
    const RailLayout rail = rail_layout(cap.w, cap.h, rail_rows, u);
    const PageArea area = page_area(cap, rail, u);
    const float rail_h = rail.content_h + 2.0f * rail.pad;
    const float want = std::max(area.body_y + tallest_body + tok::kS3 * u, rail_h);
    SheetFrame f = cap;
    f.h = std::min(cap.h, std::ceil(want));
    return f;
}

} // namespace etude::app::ui
