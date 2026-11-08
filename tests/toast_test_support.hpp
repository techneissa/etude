// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the toast_*test.cpp files (split from toast_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/toast.hpp"
#include "ui/sheet_layout.hpp"
#include "ui/toast_regions.hpp"
#include "etude/editor_layout.hpp"
#include "etude/screen_layout.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace toast_test_support {

using namespace etude;

inline bool inside(const RectF& r, float w, float h) {
    return r.x >= 0.0f && r.y >= 0.0f && r.right() <= w && r.bottom() <= h;
}

struct Size {
    float w;
    float h;
};

constexpr Size kScreens[] = {{800.0f, 480.0f}, {1366.0f, 768.0f}, {1920.0f, 1080.0f}};

constexpr float kScales[] = {1.0f, 1.5f};

struct Screen {
    float w, h, u;
    ScreenLayout lay;
    ToastRegions regions;
    float pill_w, pill_h, min_w, icon, gap;

    app::ui::ToastRegionsInput in;
    RectF status; // the top row's status line, which no toast covers

    Screen(Size size, float scale) : w(size.w), h(size.h), u(scale) {
        lay = compute_layout(w, h, kDefaultPxPerMm * u, LookSettings{});
        pill_w = 220.0f * u;
        pill_h = 38.0f * u;
        icon = etude::app::ui::tok::kHit * u; // a finger wide
        min_w = (6.0f * 14.5f + 32.0f) * u;
        gap = 8.0f * u;
        in.display_w = w;
        in.display_h = h;
        in.row_top = lay.top_row.y;
        in.row_bottom = lay.top_row.bottom();
        in.toast_h = std::max(pill_h, icon);
        in.gap = gap;
        in.u = u;
        status = lay.status_text;
        refill();
    }
    // The host's own block (fill_toast_regions) from `in`.
    void refill() {
        regions = ToastRegions{};
        REQUIRE(app::ui::fill_toast_regions(regions, in));
    }

    // The bar shown with a song: its buttons at the right end of the status area and the overview
    // under the top row.
    void show_bar(bool slim = false, float bar_w_pt = 420.0f, float keep_pt = 160.0f) {
        const TopRowSplit split = split_top_row(lay, bar_w_pt * u, keep_pt * u, false);
        in.bar_shown = true;
        in.bar = split.bar;
        in.has_song = true;
        in.slim = slim;
        status = split.status_text;
        refill();
    }
    // The tempo popover open under the bar.
    RectF open_popover() {
        const RectF card{w * 0.45f, lay.top_row.bottom(), 300.0f * u, 220.0f * u};
        REQUIRE(regions.add_avoid(card));
        return card;
    }
    // The settings sheet open: its header and rail kept clear, its whole card the surface.
    RectF open_sheet(RectF* header_out = nullptr) {
        const app::ui::SheetFrame f =
            app::ui::sheet_frame(w, h, lay.top_row.bottom() + tok_s2(), u);
        const RectF card{f.x, f.y, f.w, f.h};
        const app::ui::RailLayout rail = app::ui::rail_layout(f.w, f.h, 7, u);
        // The header child: from the page column, a pad under the sheet's top, one hit tall.
        const float cx = rail.w + app::ui::tok::kS4 * u;
        const float cw = f.w - cx - app::ui::tok::kS4 * u;
        const RectF header{f.x + cx, f.y + app::ui::tok::kS2 * u, cw + app::ui::tok::kS1 * u,
                           (app::ui::tok::kHit + 4.0f) * u};
        REQUIRE(regions.add_avoid(header));
        REQUIRE(regions.add_avoid({f.x, f.y, rail.w, f.h}));
        regions.surface = card;
        if (header_out != nullptr) {
            *header_out = header;
        }
        return card;
    }
    // The section editor open: its toolbar and ruler kept clear, its area the surface.
    EditorLayout open_editor() {
        const float margin = 12.0f * u;
        const float top = lay.top_row.bottom() + tok_s2();
        const RectF area{margin, top, w - 2.0f * margin, h - top - margin};
        const float pad = 10.0f * u;
        const EditorLayout l =
            editor_layout({area.x + pad, area.y + pad, area.w - 2.0f * pad, area.h - 2.0f * pad}, u);
        REQUIRE(regions.add_avoid(l.toolbar));
        REQUIRE(regions.add_avoid(l.ruler));
        regions.surface = area;
        return l;
    }
    float tok_s2() const { return 8.0f * u; }

    ToastPlacement place(const RectF& anchor = {}, bool warning = false, float pw = -1.0f) const {
        return place_toast(pw < 0.0f ? pill_w : pw, pill_h, min_w, icon, warning, anchor, w, h,
                           regions, gap);
    }
};

inline bool overlap(const RectF& a, const RectF& b) {
    return a.x < b.right() - 1e-3f && b.x < a.right() - 1e-3f && a.y < b.bottom() - 1e-3f &&
           b.y < a.bottom() - 1e-3f;
}

} // namespace toast_test_support
