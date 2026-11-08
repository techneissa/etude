// SPDX-License-Identifier: GPL-3.0-or-later
// Where everything of the crash screen goes, with no ImGui in it, so tests/crash_screen_layout_test.cpp
// checks the touch rules with the real tokens. Points, `u` is the kit's unit.
//
// One column, centred in the window both ways: the badge, the title, the paragraph (as many lines as
// the caller wrapped it into), and the buttons under them, side by side where both fit the column and
// stacked where they do not (the 800x480 panel with the longest words). Every button is `tok::kHit`
// tall at least, with `tok::kHitGap` or more between two. A wide window takes the larger steps
// (`tok::wide_step`, `crash_screen_sizes`): a bigger emblem, title, paragraph and buttons, the column
// wider to match.
#pragma once

#include "about_layout.hpp"
#include "add_songs_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace crash_screen_tok {
inline constexpr float kLineRatio = 1.3f; // a text line's height in its type size
} // namespace crash_screen_tok

// What the screen draws at a `tok::wide_step`: the column's width (the widest the words run, in
// design points), the emblem's tile and icon, the type (steps of the kit's ladder), the spacing steps
// and how much taller the buttons are than a hit area. Step 0 is what 800x480 has always drawn; the
// larger steps use the empty space of a wide window.
struct CrashScreenSizes {
    float column_w;
    float tile;
    float icon;
    float title_pt;
    float body_pt;
    float button_pt;
    float badge_gap;  // the emblem to the title
    float title_gap;  // the title to the paragraph
    float body_gap;   // the paragraph to the buttons
    float button_gap; // between two buttons
    float grow;       // the buttons' hit and face height over `kHit` and `kFace`
};

inline CrashScreenSizes crash_screen_sizes(int step) {
    switch (step) {
    case 0:
        return {560.0f, 72.0f, tok::kIconXl, tok::kTitle, tok::kBody, tok::kLabel, tok::kS4, tok::kS2,
                tok::kS5, tok::kS3, 1.0f};
    case 1:
        return {720.0f, 96.0f, tok::kIconHero, tok::kDisplay, tok::kTitle, tok::kBody, tok::kS5,
                tok::kS3, tok::kS6, tok::kS4, tok::kWideGrow1};
    default:
        return {960.0f, 128.0f, tok::kIconHuge, tok::kDisplay, tok::kTitle, tok::kTitle, tok::kS6,
                tok::kS4, tok::kS6, tok::kS5, tok::kWideGrow2};
    }
}

// The column's width in a window `w` wide, at `step`.
inline float crash_screen_column_w(float w, float u, int step = 0) {
    return std::min(crash_screen_sizes(step).column_w * u, std::max(w - 2.0f * tok::kS5 * u, 0.0f));
}

struct CrashScreenGeom {
    Rect column;
    Rect badge;
    Rect title;
    Rect detail;
    std::array<Rect, 2> button{};
    int buttons = 0;
    bool stacked = false;
};

// A window `w` by `h`; the paragraph is `detail_lines` lines and the title `title_lines`; `n` buttons
// (0 to 2) whose chips are `widths` wide (their words'); `step` is `tok::wide_step` of `w`.
inline CrashScreenGeom crash_screen_geometry(float w, float h, float u, int detail_lines, int n,
                                             std::array<float, 2> widths, int step = 0,
                                             int title_lines = 1) {
    CrashScreenGeom g;
    const CrashScreenSizes z = crash_screen_sizes(step);
    const float hit = tok::kHit * u * z.grow;
    const float gap = std::max(z.button_gap, tok::kHitGap) * u;
    const float col_w = crash_screen_column_w(w, u, step);
    const float badge = z.tile * u;
    const float title_h = z.title_pt * crash_screen_tok::kLineRatio * u * static_cast<float>(std::max(title_lines, 1));
    const float line_h = z.body_pt * crash_screen_tok::kLineRatio * u;
    const float detail_h = static_cast<float>(std::max(detail_lines, 0)) * line_h;
    n = std::clamp(n, 0, 2);
    g.buttons = n;
    const float row_w = n == 2 ? widths[0] + gap + widths[1] : n == 1 ? widths[0] : 0.0f;
    g.stacked = n == 2 && row_w > col_w;
    const float buttons_h = n == 0 ? 0.0f : g.stacked ? 2.0f * hit + gap : hit;
    const float total = badge + z.badge_gap * u + title_h + z.title_gap * u + detail_h +
                        (n == 0 ? 0.0f : z.body_gap * u + buttons_h);
    const float x = (w - col_w) * 0.5f;
    float y = std::max(tok::kS3 * u, (h - total) * 0.5f);
    g.column = {x, y, col_w, total};
    g.badge = {x + (col_w - badge) * 0.5f, y, badge, badge};
    y = g.badge.bottom() + z.badge_gap * u;
    g.title = {x, y, col_w, title_h};
    y = g.title.bottom() + z.title_gap * u;
    g.detail = {x, y, col_w, detail_h};
    y = g.detail.bottom() + z.body_gap * u;
    if (n == 0) {
        return g;
    }
    if (g.stacked) {
        for (int i = 0; i < n; ++i) {
            const float bw = std::min(widths[static_cast<std::size_t>(i)], col_w);
            g.button[static_cast<std::size_t>(i)] = {x + (col_w - bw) * 0.5f, y + static_cast<float>(i) * (hit + gap), bw, hit};
        }
        return g;
    }
    float bx = x + (col_w - row_w) * 0.5f;
    for (int i = 0; i < n; ++i) {
        const float bw = widths[static_cast<std::size_t>(i)];
        g.button[static_cast<std::size_t>(i)] = {bx, y, bw, hit};
        bx += bw + gap;
    }
    return g;
}

} // namespace etude::app::ui
