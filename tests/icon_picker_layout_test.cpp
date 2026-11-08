// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/ui/icon_picker_facts.hpp"
#include "../app/ui/icon_picker_layout.hpp"
#include "atkinson_metrics.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace etude::app::ui;

namespace {

constexpr float kEps = 0.01f;
constexpr float kUnits[] = {1.0f, 1.25f, 1.375f, 1.6f};
constexpr float kWidths[] = {200.0f, 360.0f, 560.0f, 700.0f, 800.0f, 1000.0f};

// The page's room in pixels and the unit at the three real sizes: 1366x768 (795 points at u = 1), 800x480
// at 96 dpi (565 at u = 1) and the real 154x86 mm panel (609 pixels at u = 1.375).
struct Real {
    const char* name;
    float w;
    float u;
};
constexpr Real kReal[] = {{"1366x768", 795.0f, 1.0f}, {"800x480", 565.0f, 1.0f}, {"panel", 609.0f, 1.375f}};

// A string's width at the real font: its advances from the font files, at `size` points times `u`.
float width_of(bool bold, float size, float u, const std::string& text) {
    return etude::test::text_em(bold, text) * size * u;
}

// What the caption and label fonts measure the page's words at, with the pad the page adds.
float name_width(float u) {
    float w = 0.0f;
    for (const etude::IconColourInfo& info : etude::kIconColours) {
        w = std::max(w, width_of(true, tok::kCaption, u, std::string(info.name)));
    }
    return w + tok::kS1 * u;
}

float title_width(float u) {
    float w = 0.0f;
    for (int title = 1; title <= etude::kTierCount; ++title) {
        w = std::max(w, width_of(true, tok::kLabel, u, std::string(etude::tier_title(title))));
    }
    return w + tok::kS1 * u;
}

float stats_width(float u, const IconPickerFacts& facts) {
    float w = 0.0f;
    for (const std::string& text : facts.side) {
        w = std::max(w, width_of(false, tok::kCaption, u, text));
    }
    return w + tok::kS1 * u;
}

float hint_width(float u) {
    float w = 0.0f;
    for (const etude::IconColourInfo& info : etude::kIconColours) {
        w = std::max(w, width_of(false, tok::kLabel, u, icon_unlock_hint(info)));
    }
    return w + tok::kS1 * u;
}

// The page's sizes for `facts` in a room `w` wide: as the page measures them.
IconPickerSizes sizes(float w, float u, const IconPickerFacts& facts, bool sides = true,
                      bool hint = true) {
    IconPickerSizes s;
    s.w = w;
    s.u = u;
    s.name_w = name_width(u);
    s.title_w = title_width(u);
    s.stats_w = stats_width(u, facts);
    s.sides = sides;
    s.hint = hint;
    s.hint_lines = hint_width(u) > w ? 2 : 1;
    return s;
}

// Medals 96 .. 134: Pianist, the next title Concert Pianist at 135 and the colour Aubergine: the longest words.
const IconPickerFacts& longest() {
    static const IconPickerFacts f = icon_picker_facts(96);
    return f;
}

IconPickerSizes sizes(float w, float u, bool sides = true, bool hint = true) {
    return sizes(w, u, longest(), sides, hint);
}

// What the page measured at u = 1 with the loaded fonts in a 58 medal case (Soloist, next Pianist at 96):
// the metrics table above is an upper bound of that (an ImGui size is the line's height, not the em), so
// the cases that decide where things go take the measured widths and the cases about cut words the bound.
IconPickerSizes measured(float w, float u) {
    IconPickerSizes s;
    s.w = w;
    s.u = u;
    s.name_w = 55.0f * u;
    s.title_w = 90.0f * u;
    s.stats_w = 90.0f * u;
    s.sides = true;
    s.hint = true;
    s.hint_lines = 1;
    return s;
}

bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps &&
           b.y < a.bottom() - kEps;
}

// The least distance between two rectangles that do not overlap (0 when they touch).
float gap_between(const Rect& a, const Rect& b) {
    const float dx = std::max({a.x - b.right(), b.x - a.right(), 0.0f});
    const float dy = std::max({a.y - b.bottom(), b.y - a.bottom(), 0.0f});
    return dx > 0.0f && dy > 0.0f ? std::min(dx, dy) : std::max(dx, dy);
}

// Every touch target of the row: the eight cells and, when shown, the medal.
std::vector<Rect> targets(const IconPickerLayout& g, bool sides) {
    std::vector<Rect> out;
    for (const IconPickerCell& c : g.cells) {
        out.push_back(c.hit);
    }
    if (sides) {
        out.push_back(g.medal_hit);
    }
    return out;
}

bool inside(const Rect& part, const Rect& whole) {
    return part.x >= whole.x - kEps && part.right() <= whole.right() + kEps &&
           part.y >= whole.y - kEps && part.bottom() <= whole.bottom() + kEps;
}

} // namespace

TEST_CASE("icon picker: every target is 12 mm, 2 mm from the next, and nothing overlaps") {
    for (const float u : kUnits) {
        for (const float w : kWidths) {
            for (const bool sides : {false, true}) {
                CAPTURE(u);
                CAPTURE(w);
                CAPTURE(sides);
                const IconPickerLayout g = icon_picker(sizes(w * u, u, sides));
                const std::vector<Rect> all = targets(g, sides);
                for (std::size_t i = 0; i < all.size(); ++i) {
                    CHECK(all[i].w >= tok::kHit * u - kEps);
                    CHECK(all[i].h >= tok::kHit * u - kEps);
                    for (std::size_t j = i + 1; j < all.size(); ++j) {
                        CHECK_FALSE(overlap(all[i], all[j]));
                        CHECK(gap_between(all[i], all[j]) >= tok::kHitGap * u - kEps);
                    }
                }
            }
        }
    }
}

TEST_CASE("icon picker: a name sits under every icon, inside its cell, at every size") {
    for (const float u : kUnits) {
        for (const float w : kWidths) {
            CAPTURE(u);
            CAPTURE(w);
            const IconPickerLayout g = icon_picker(sizes(w * u, u));
            for (const IconPickerCell& c : g.cells) {
                CHECK(c.hit.w >= name_width(u) - kEps);
                CHECK(c.name.h >= picker_tok::kNameH * u - kEps);
                CHECK(c.name.w >= name_width(u) - kEps);
                CHECK(inside(c.swatch, c.hit));
                CHECK(inside(c.name, c.hit));
                CHECK_FALSE(overlap(c.swatch, c.name));
                CHECK(c.name.y >= c.swatch.bottom() - kEps);
            }
        }
    }
}

TEST_CASE("icon picker: one row where eight fit, else four, two or one, always inside the room") {
    const float u = 1.0f;
    const IconPickerLayout wide = icon_picker(sizes(1000.0f, u, false));
    CHECK(wide.per_row == 8);
    const IconPickerLayout mid = icon_picker(sizes(400.0f, u, false));
    CHECK(mid.per_row == 4);
    const IconPickerLayout narrow = icon_picker(sizes(200.0f, u, false));
    CHECK(narrow.per_row == 2);
    const IconPickerLayout tiny = icon_picker(sizes(100.0f, u, false));
    CHECK(tiny.per_row == 1);
    CHECK(tiny.height > narrow.height);
    CHECK(narrow.height > mid.height);
    CHECK(mid.height > wide.height);
    for (const IconPickerLayout* g : {&wide, &mid, &narrow}) {
        const float w = g == &wide ? 1000.0f : g == &mid ? 400.0f : 200.0f;
        float low = w;
        float high = 0.0f;
        for (const IconPickerCell& c : g->cells) {
            CHECK(c.hit.x >= -kEps);
            CHECK(c.hit.right() <= w + kEps);
            low = std::min(low, c.hit.x);
            high = std::max(high, c.hit.right());
        }
        CHECK(low == doctest::Approx(w - high).epsilon(0.001)); // centred
    }
}

TEST_CASE("icon picker: the cells run in the colours' order, left to right then down") {
    const IconPickerLayout g = icon_picker(sizes(400.0f, 1.0f));
    for (std::size_t i = 1; i < g.cells.size(); ++i) {
        const Rect& a = g.cells[i - 1].hit;
        const Rect& b = g.cells[i].hit;
        CHECK((b.x > a.x || b.y > a.y));
    }
}

TEST_CASE("icon picker: 1366x768, the medal on the left and its stats on the right of one row of eight") {
    // The About page is 795 points wide there, at u = 1.
    const IconPickerLayout g = icon_picker(measured(795.0f, 1.0f));
    CHECK(g.beside);
    CHECK(g.per_row == 8);
    CHECK(g.medal_hit.right() <= g.cells[0].hit.x - tok::kHitGap + kEps);
    CHECK(g.stat[0].x >= g.cells[7].hit.right() + tok::kHitGap - kEps);
    CHECK(g.stat[0].right() <= 795.0f + kEps);
    CHECK(g.line.w == 0.0f);
}

TEST_CASE("icon picker: 800x480, the sides stay beside two rows of four") {
    // 565 points wide at u = 1 (a 96 dpi screen).
    const IconPickerLayout g = icon_picker(measured(565.0f, 1.0f));
    CHECK(g.beside);
    CHECK(g.per_row == 4);
    CHECK(g.cells[0].hit.y == doctest::Approx(g.cells[3].hit.y));
    CHECK(g.cells[4].hit.y > g.cells[0].hit.bottom());
    // The medal and the stats are level with the middle of the two rows.
    const float middle = (g.cells[0].hit.y + g.cells[7].hit.bottom()) * 0.5f;
    CHECK(g.medal_hit.y + g.medal_hit.h * 0.5f == doctest::Approx(middle).epsilon(0.02));
    CHECK(g.stat[1].y + g.stat[1].h * 0.5f == doctest::Approx(middle).epsilon(0.02));
}

TEST_CASE("icon picker: the real 7 inch panel puts the sides above the cells in one line") {
    // 800 px across a 154 mm panel is u = 1.375; the page has 609 px there.
    const float u = 1.375f;
    const IconPickerLayout g = icon_picker(measured(609.0f, u));
    CHECK_FALSE(g.beside);
    CHECK(g.line.w > 0.0f);
    CHECK(g.stat[0].h == 0.0f);
    // The medal and its title come first, the stats right of them, the cells below.
    CHECK(g.line.x >= g.medal_hit.right() - kEps);
    CHECK(g.line.right() <= 609.0f + kEps);
    CHECK(g.medal_hit.w >= tok::kHit * u - kEps);
    CHECK(g.medal_hit.h >= tok::kHit * u - kEps);
    for (const IconPickerCell& c : g.cells) {
        CHECK(c.hit.y >= g.medal_hit.bottom() + tok::kHitGap * u - kEps);
    }
    CHECK(inside(g.medal, g.medal_hit));
    CHECK(inside(g.medal_title, g.medal_hit));
    CHECK(g.per_row == 4);
    for (const IconPickerCell& c : g.cells) {
        CHECK(c.hit.right() <= 609.0f + kEps);
    }
}

TEST_CASE("icon picker: no sides while the medals are not known") {
    const IconPickerLayout g = icon_picker(sizes(795.0f, 1.0f, false));
    CHECK_FALSE(g.beside);
    CHECK(g.medal_hit.w == 0.0f);
    CHECK(g.line.w == 0.0f);
    CHECK(g.cells[0].hit.y == doctest::Approx(0.0f));
}

TEST_CASE("icon picker: the line for a locked colour is kept only when asked for") {
    const IconPickerLayout with = icon_picker(sizes(795.0f, 1.0f, true, true));
    const IconPickerLayout without = icon_picker(sizes(795.0f, 1.0f, true, false));
    CHECK(with.hint.h > 0.0f);
    CHECK(without.hint.h == 0.0f);
    CHECK(with.hint.y >= with.cells[7].hit.bottom() - kEps);
    CHECK(with.hint.bottom() == doctest::Approx(with.height));
    CHECK(with.height > without.height);
}

TEST_CASE("icon picker: no stat or medal word is cut, at the three real sizes, for every medal count") {
    for (const Real& r : kReal) {
        for (int points = 0; points <= etude::kMaxMedalPoints; ++points) {
            CAPTURE(r.name);
            CAPTURE(points);
            const IconPickerFacts facts = icon_picker_facts(points);
            const IconPickerLayout g = icon_picker(sizes(r.w, r.u, facts));
            CHECK(g.medal_hit.w >= title_width(r.u) - kEps);
            if (g.beside) {
                // The stat lines are as wide as the widest of them.
                for (std::size_t i = 0; i < facts.side.size(); ++i) {
                    CHECK(g.stat[i].w >= width_of(false, tok::kCaption, r.u, facts.side[i]) - kEps);
                }
                continue;
            }
            // Above the cells: one line or two, each fitting the room, in the height of the medal's line.
            const auto width = [&](const std::string& text) {
                return width_of(false, tok::kCaption, r.u, text);
            };
            const std::vector<std::string> lines = icon_picker_lines(
                {facts.line.begin(), facts.line.end()}, g.line.w, width);
            CHECK(lines.size() <= 2);
            for (const std::string& line : lines) {
                CAPTURE(line);
                CHECK(width(line) <= g.line.w + kEps);
            }
            CHECK(static_cast<float>(lines.size()) * picker_tok::kStatH * r.u <= g.line.h + kEps);
        }
    }
}

TEST_CASE("icon picker: the longest stats fit the 7 inch panel in a line or two, uncut") {
    const float u = 1.375f;
    const IconPickerFacts& f = longest();
    CHECK(f.line[1] == "Next: Concert Pianist at 135 \xC2\xB7 unlocks Aubergine");
    CHECK(f.line[0] == "96 of 192 medals \xC2\xB7 Next: Concert Pianist at 135 \xC2\xB7 unlocks Aubergine");
    const IconPickerLayout g = icon_picker(sizes(609.0f, u, f));
    CHECK_FALSE(g.beside);
    const auto width = [&](const std::string& text) { return width_of(false, tok::kCaption, u, text); };
    const std::vector<std::string> lines = icon_picker_lines({f.line.begin(), f.line.end()}, g.line.w, width);
    REQUIRE(!lines.empty());
    for (const std::string& line : lines) {
        CHECK(width(line) <= g.line.w + kEps);
    }
}

TEST_CASE("icon picker: a locked colour's line, \"Aubergine unlocks at Concert Pianist\", is never cut") {
    const etude::IconColourInfo& aubergine = etude::kIconColours[5];
    CHECK(icon_unlock_hint(aubergine) == "Aubergine unlocks at Concert Pianist");
    for (const Real& r : kReal) {
        for (const float w : {r.w, 300.0f, 220.0f}) {
            CAPTURE(r.name);
            CAPTURE(w);
            const IconPickerLayout g = icon_picker(sizes(w, r.u));
            const auto width = [&](const std::string& text) { return width_of(false, tok::kLabel, r.u, text); };
            for (const etude::IconColourInfo& info : etude::kIconColours) {
                const std::vector<std::string> lines = icon_picker_lines({icon_unlock_hint(info)}, w, width);
                CAPTURE(info.name);
                // One line, or two where the page kept two for it; every line fits.
                CHECK(lines.size() <= 2);
                CHECK(static_cast<float>(lines.size()) * picker_tok::kHintH * r.u <= g.hint.h + kEps);
                for (const std::string& line : lines) {
                    CHECK(width(line) <= w + kEps);
                }
            }
        }
    }
}
