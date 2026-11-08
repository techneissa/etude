// SPDX-License-Identifier: GPL-3.0-or-later
// The crash screen's layout against the touch rules, with the real tokens: every button is 12 mm tall at
// least, no two overlap or sit closer than 2 mm, everything stays inside the window, and a pair that does
// not fit the column side by side is stacked, never shrunk.
#include "ui/crash_screen_layout.hpp"

#include <doctest/doctest.h>

using namespace etude::app::ui;

namespace {

bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

bool inside(const Rect& r, float w, float h) {
    return r.x >= -0.01f && r.y >= -0.01f && r.right() <= w + 0.01f && r.bottom() <= h + 0.01f;
}

// The dead gap between two hit areas: along the axis they do not overlap on.
float gap_between(const Rect& a, const Rect& b) {
    const float dx = std::max(b.x - a.right(), a.x - b.right());
    const float dy = std::max(b.y - a.bottom(), a.y - b.bottom());
    return std::max(dx, dy);
}

} // namespace

TEST_CASE("crash screen layout: the buttons are 12 mm tall with 2 mm between, and everything is inside the window") {
    struct Window { float w, h, u; };
    for (const Window win : {Window{800, 480, 1.0f}, Window{800, 480, 1.5f}, Window{1366, 768, 1.0f},
                             Window{1366, 768, 1.78f}, Window{1920, 1080, 1.0f}}) {
        for (const int lines : {1, 2, 3, 4}) {
            // The widest words ("Restart and send report") and the narrowest ("Try again", "Restart").
            for (const std::array<float, 2> widths : {std::array<float, 2>{tok::kHit * win.u * 2.4f, tok::kHit * win.u * 5.6f},
                                                      std::array<float, 2>{tok::kHit * win.u * 2.0f, tok::kHit * win.u * 3.0f}}) {
                CAPTURE(win.w);
                CAPTURE(win.u);
                CAPTURE(lines);
                const CrashScreenGeom g = crash_screen_geometry(win.w, win.h, win.u, lines, 2, widths);
                const float hit = tok::kHit * win.u;
                const float gap = tok::kHitGap * win.u;
                REQUIRE(g.buttons == 2);
                for (int i = 0; i < 2; ++i) {
                    CHECK(g.button[static_cast<std::size_t>(i)].h >= hit - 0.01f);
                    CHECK(g.button[static_cast<std::size_t>(i)].w >= widths[static_cast<std::size_t>(i)] - 0.01f);
                    CHECK(inside(g.button[static_cast<std::size_t>(i)], win.w, win.h));
                }
                CHECK_FALSE(overlap(g.button[0], g.button[1]));
                CHECK(gap_between(g.button[0], g.button[1]) >= gap - 0.01f);
                CHECK(inside(g.badge, win.w, win.h));
                CHECK(inside(g.title, win.w, win.h));
                CHECK(inside(g.detail, win.w, win.h));
                // Top to bottom: the badge, the title, the paragraph, the buttons; nothing on another.
                CHECK(g.title.y >= g.badge.bottom());
                CHECK(g.detail.y >= g.title.bottom());
                CHECK(g.button[0].y >= g.detail.bottom());
                CHECK(g.stacked == (g.button[1].y > g.button[0].y));
                if (g.stacked) {
                    CHECK(widths[0] + gap + widths[1] > crash_screen_column_w(win.w, win.u));
                }
            }
        }
    }
}

TEST_CASE("crash screen layout: the column is centred, and a lone button is centred under the words") {
    const CrashScreenGeom g = crash_screen_geometry(1366, 768, 1.0f, 2, 1, {200.0f, 0.0f});
    REQUIRE(g.buttons == 1);
    CHECK(g.button[0].x + g.button[0].w * 0.5f == doctest::Approx(1366 * 0.5f));
    CHECK(g.column.x + g.column.w * 0.5f == doctest::Approx(1366 * 0.5f));
    CHECK(g.column.y + g.column.h * 0.5f == doctest::Approx(768 * 0.5f).epsilon(0.02));
    const CrashScreenGeom none = crash_screen_geometry(1366, 768, 1.0f, 2, 0, {});
    CHECK(none.buttons == 0);
}

TEST_CASE("crash screen layout: on the 7 inch panel the longest pair is stacked and still fits") {
    const float u = 1.0f;
    const CrashScreenGeom g = crash_screen_geometry(800, 480, u, 4, 2, {tok::kHit * u * 5.6f, tok::kHit * u * 5.6f});
    CHECK(g.button[1].bottom() <= 480.0f);
    CHECK(g.badge.y >= 0.0f);
}

TEST_CASE("crash screen layout at the wide steps: bigger, buttons 12 mm tall and apart, all inside the window") {
    struct Window { float w, h, u; };
    for (const Window win : {Window{1366, 768, 1.0f}, Window{1920, 1080, 1.4f}, Window{2560, 1440, 1.6f}}) {
        const int step = tok::wide_step(win.w);
        const CrashScreenSizes z = crash_screen_sizes(step);
        for (const int lines : {1, 3, 5}) {
            for (const int title_lines : {1, 2}) {
                CAPTURE(win.w);
                CAPTURE(lines);
                const float hit = tok::kHit * win.u;
                const float gap = tok::kHitGap * win.u;
                const std::array<float, 2> widths{hit * 3.0f * z.grow, hit * 6.0f * z.grow};
                const CrashScreenGeom g = crash_screen_geometry(win.w, win.h, win.u, lines, 2, widths, step, title_lines);
                REQUIRE(g.buttons == 2);
                for (int i = 0; i < 2; ++i) {
                    CHECK(g.button[static_cast<std::size_t>(i)].h >= hit * z.grow - 0.01f);
                    CHECK(inside(g.button[static_cast<std::size_t>(i)], win.w, win.h));
                }
                CHECK_FALSE(overlap(g.button[0], g.button[1]));
                CHECK(gap_between(g.button[0], g.button[1]) >= gap - 0.01f);
                CHECK(g.badge.w == doctest::Approx(z.tile * win.u));
                CHECK(g.title.y >= g.badge.bottom());
                CHECK(g.detail.y >= g.title.bottom());
                CHECK(g.button[0].y >= g.detail.bottom());
                CHECK(inside(g.column, win.w, win.h));
            }
        }
    }
}

TEST_CASE("crash screen sizes: the column, emblem, type and buttons grow by step, step 0 is unchanged") {
    const CrashScreenSizes a = crash_screen_sizes(0);
    const CrashScreenSizes b = crash_screen_sizes(1);
    const CrashScreenSizes c = crash_screen_sizes(2);
    CHECK(a.column_w == 560.0f);
    CHECK(a.tile == 72.0f);
    CHECK(a.title_pt == tok::kTitle);
    CHECK(a.body_pt == tok::kBody);
    CHECK(a.grow == 1.0f);
    CHECK(a.column_w < b.column_w);
    CHECK(b.column_w < c.column_w);
    CHECK(a.tile < b.tile);
    CHECK(b.tile < c.tile);
    CHECK(a.icon < b.icon);
    CHECK(b.icon < c.icon);
    CHECK(a.title_pt < b.title_pt);
    CHECK(a.body_pt < b.body_pt);
    CHECK(a.button_pt < b.button_pt);
    CHECK(b.button_pt < c.button_pt);
    CHECK(a.grow < b.grow);
    CHECK(b.grow < c.grow);
}
