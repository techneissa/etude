// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/ui/learn_colours_layout.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

using namespace etude::app::ui;

namespace {

constexpr float kEps = 0.01f;
constexpr float kUnits[] = {1.0f, 1.25f, 1.375f, 1.6f};
constexpr float kWidths[] = {60.0f, 120.0f, 200.0f, 360.0f, 480.0f, 565.0f, 609.0f, 800.0f, 1000.0f};

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps &&
           b.y < a.bottom() - kEps;
}

// The dead space between two rectangles (0 when they touch or overlap).
float gap_between(const Rect& a, const Rect& b) {
    const float dx = std::max({0.0f, b.x - a.right(), a.x - b.right()});
    const float dy = std::max({0.0f, b.y - a.bottom(), a.y - b.bottom()});
    return std::max(dx, dy);
}

} // namespace

TEST_CASE("learn colours layout: every shade is a 12 mm hit, 2 mm from the next, at every width and unit") {
    for (const float u : kUnits) {
        for (const float w : kWidths) {
            CAPTURE(u);
            CAPTURE(w);
            const LearnColoursLayout g = learn_colours_layout({w, u});
            for (const Rect& c : g.cells) {
                CHECK(c.w >= tok::kHit * u - kEps);
                CHECK(c.h >= tok::kHit * u - kEps);
            }
            for (std::size_t i = 0; i < g.cells.size(); ++i) {
                for (std::size_t j = i + 1; j < g.cells.size(); ++j) {
                    CHECK_FALSE(overlaps(g.cells[i], g.cells[j]));
                    CHECK(gap_between(g.cells[i], g.cells[j]) >= tok::kHitGap * u - kEps);
                }
            }
        }
    }
}

TEST_CASE("learn colours layout: the shades stay inside the room and the height holds them all") {
    for (const float u : kUnits) {
        for (const float w : kWidths) {
            CAPTURE(u);
            CAPTURE(w);
            const LearnColoursLayout g = learn_colours_layout({w, u});
            for (const Rect& c : g.cells) {
                CHECK(c.x >= -kEps);
                CHECK(c.y >= -kEps);
                // Only a room under one cell wide (never a page) lets a shade out of it.
                if (w >= tok::kHit * u) {
                    CHECK(c.right() <= w + kEps);
                }
                CHECK(c.bottom() <= g.height + kEps);
            }
        }
    }
}

TEST_CASE("learn colours layout: all seven share one line spread over the room where they fit, else "
          "they wrap") {
    const float u = 1.0f;
    const LearnColoursLayout wide = learn_colours_layout({800.0f, u});
    CHECK(wide.per_line == 7);
    CHECK(wide.height == doctest::Approx(tok::kHit * u));
    CHECK(wide.cells.front().x == doctest::Approx(0.0f));
    CHECK(wide.cells.back().right() == doctest::Approx(800.0f)); // lines up with the tab track
    for (const Rect& c : wide.cells) {
        CHECK(c.y == doctest::Approx(0.0f));
    }

    const LearnColoursLayout narrow = learn_colours_layout({300.0f, u});
    CHECK(narrow.per_line < 7);
    CHECK(narrow.per_line >= 1);
    CHECK(narrow.height > tok::kHit * u);
    const LearnColoursLayout one = learn_colours_layout({60.0f, u});
    CHECK(one.per_line == 1);
}

TEST_CASE("learn colours layout: the real pages, 1366x768 and 800x480, hold the row") {
    // A page's room in pixels and the unit at the real sizes (as the icon picker's test): one column
    // of 795 points at 1366x768, 565 at 800x480 at 96 dpi, 609 pixels at u = 1.375 on the 154x86 mm panel.
    struct Real {
        float w;
        float u;
    };
    for (const Real r : {Real{795.0f, 1.0f}, Real{565.0f, 1.0f}, Real{609.0f, 1.375f}, Real{380.0f, 1.0f}}) {
        CAPTURE(r.w);
        const LearnColoursLayout g = learn_colours_layout({r.w, r.u});
        CHECK(g.height > 0.0f);
        for (const Rect& c : g.cells) {
            CHECK(c.right() <= r.w + kEps);
        }
    }
}

TEST_CASE("learn colours layout: the tab is kept while the sheet is open and is Scales again when it opens") {
    LearnColoursTab tab;
    CHECK(tab.index == 0);
    tab.index = 2; // Ear picked, the sheet stays open
    CHECK(tab.index == 2);
    tab.sheet_opened(); // closed and opened again
    CHECK(tab.index == 0);
}
