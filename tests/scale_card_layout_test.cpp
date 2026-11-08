// SPDX-License-Identifier: GPL-3.0-or-later
#include "atkinson_metrics.hpp"
#include "ui/scale_card_layout.hpp"

#include "etude/scales.hpp"
#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr float kEps = 1e-3f;

struct Size {
    float w;
    float h;
};
// A 7 inch panel at a taste scale above 1 is under 600 points wide: its controls cannot keep 12 mm
// with 2 mm gaps, so 800x480 is covered at 1.0 only.
constexpr Size kSizes[] = {{1366.0f, 768.0f}, {800.0f, 480.0f}};

// The widest of the types' words in a chip: about this many points at the type size of a chip.
constexpr float kCellW = 128.0f;

ScaleCardLayout layout_at(Size s, float unit) {
    const ScreenLayout lay = compute_layout(s.w, s.h, kDefaultPxPerMm * unit, LookSettings{});
    const float top = lay.top_row.bottom() + tok::kS1 * unit;
    return scale_card_layout(s.w, s.h, top, lay.keyboard.y - tok::kS2 * unit, 213.0f, kCellW * unit,
                             unit);
}

std::vector<Box> cells(const ScaleCardLayout& l) {
    std::vector<Box> out(l.type.begin(), l.type.end());
    out.push_back(l.fingers);
    out.push_back(l.off);
    return out;
}

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

} // namespace

TEST_CASE("scale card: all fourteen types are cells of at least a hit, inside the card") {
    for (const Size s : kSizes) {
        for (const float unit : {1.0f, 1.25f, 1.5f}) {
            if (s.w < 1000.0f && unit > 1.0f) {
                continue; // see kSizes
            }
            CAPTURE(s.w);
            CAPTURE(unit);
            const ScaleCardLayout l = layout_at(s, unit);
            CHECK(l.type.size() == 14);
            CHECK(l.card.x0 >= 0.0f);
            CHECK(l.card.x1 <= s.w + kEps);
            CHECK(l.card.y1 <= s.h + kEps);
            const float hit = tok::kHit * unit;
            for (const Box& c : cells(l)) {
                CHECK(c.w() >= hit - kEps);
                CHECK(c.h() >= hit - kEps);
                CHECK(l.card.holds(c));
            }
            CHECK(l.card.holds(l.ring));
        }
    }
}

TEST_CASE("scale card: nothing overlaps, the card stays on the screen and over the keyboard") {
    for (const Size s : kSizes) {
        for (const float unit : {1.0f, 1.25f, 1.5f}) {
            if (s.w < 1000.0f && unit > 1.0f) {
                continue; // see kSizes
            }
            CAPTURE(s.w);
            CAPTURE(unit);
            const ScaleCardLayout l = layout_at(s, unit);
            const std::vector<Box> all = cells(l);
            for (std::size_t i = 0; i < all.size(); ++i) {
                CHECK_FALSE(overlap(all[i], l.ring));
                for (std::size_t j = i + 1; j < all.size(); ++j) {
                    CHECK_FALSE(overlap(all[i], all[j]));
                    // A dead gap apart.
                    const float dx = std::max(all[i].x0 - all[j].x1, all[j].x0 - all[i].x1);
                    const float dy = std::max(all[i].y0 - all[j].y1, all[j].y0 - all[i].y1);
                    CHECK(std::max(dx, dy) >= tok::kHitGap * unit - kEps);
                }
            }
            const ScreenLayout lay =
                compute_layout(s.w, s.h, kDefaultPxPerMm * unit, LookSettings{});
            CHECK(l.card.x0 >= 0.0f);
            CHECK(l.card.x1 <= s.w + kEps);
            CHECK(l.card.y0 >= lay.top_row.bottom() - kEps);
            if (unit == 1.0f && s.w > 1000.0f) { // the 7 inch panel's card must cover the keys a little
                CHECK(l.card.y1 <= lay.keyboard.y + kEps); // over the keyboard only when it must
            }
        }
    }
}

TEST_CASE("scale card: the ring's keys are at least a hit and, with room, well above one") {
    for (const Size s : kSizes) {
        for (const float unit : {1.0f, 1.25f, 1.5f}) {
            if (s.w < 1000.0f && unit > 1.0f) {
                continue; // see kSizes
            }
            CAPTURE(s.w);
            CAPTURE(unit);
            const ScaleCardLayout l = layout_at(s, unit);
            const KeyRingLayout ring = key_ring_layout(l.ring, unit, scale_card_tok::kRingKeyMax);
            CHECK(ring.key >= tok::kHit * unit - kEps);
            if (unit == 1.0f) {
                CHECK(ring.key >= 1.2f * tok::kHit * unit); // "well above kHit"
            }
        }
    }
}

// The card for the window at the unit as the pill draws it: the cells' width is the widest word's
// chip, measured at the real font.
ScaleCardLayout real_layout_at(Size s, float unit) {
    float cell_w = tok::kChipMinW * unit;
    for (const ScaleType t : kAllScaleTypes) {
        const float em = etude::test::text_em(false, scale_type_name(t));
        cell_w = std::max(cell_w, std::ceil(2.0f * tok::kS3 * unit +
                                            em * std::round(tok::kLabel * unit) + tok::kHairline *
                                            unit) + tok::kS1 * unit);
    }
    const ScreenLayout lay = compute_layout(s.w, s.h, kDefaultPxPerMm * unit, LookSettings{});
    const float top = lay.top_row.bottom() + tok::kS1 * unit;
    return scale_card_layout(s.w, s.h, top, lay.keyboard.y - tok::kS2 * unit, 213.0f * unit, cell_w,
                             unit);
}

// The windows the card is drawn in, each at the units it supports: 800x480 stands for the 7 inch
// panel (about 1.42 units) and keeps 12 mm hits up to there; a taste scale above that no longer
// leaves a card of 12 mm hits room in it (the tightest card needs 5 rows of a hit and a gap).
struct Case {
    Size s;
    float unit;
};
constexpr Case kRealCases[] = {{{1366.0f, 768.0f}, 1.0f},  {{1366.0f, 768.0f}, 1.25f},
                               {{1366.0f, 768.0f}, 1.5f},  {{800.0f, 480.0f}, 1.0f},
                               {{800.0f, 480.0f}, 1.25f},  {{800.0f, 480.0f}, 1.42f}};

TEST_CASE("scale card: no type's words are cut at the real font, and none is abbreviated") {
    for (const Case c : kRealCases) {
        CAPTURE(c.s.w);
        CAPTURE(c.unit);
        const ScaleCardLayout l = real_layout_at(c.s, c.unit);
        CHECK(l.fits);
        const float pad = tok::kS1 * c.unit;
        const float hit = tok::kHit * c.unit;
        std::array<std::string_view, scale_card_tok::kTypes> names;
        for (std::size_t i = 0; i < names.size(); ++i) {
            names[i] = scale_type_name(kAllScaleTypes[i]);
        }
        const auto room = [&](std::size_t i) { return std::max(hit, l.type[i].w()) - 2.0f * pad; };
        const auto width = [&](std::string_view text, float pt) {
            return etude::test::text_em(false, text) * std::round(pt * c.unit);
        };
        const ScaleLabels words = scale_labels(names, room, width);
        CHECK(words.fits);
        for (std::size_t i = 0; i < names.size(); ++i) {
            CAPTURE(names[i]);
            CHECK(width(words.first[i], words.pt) <= room(i));
            if (!words.second[i].empty()) {
                CHECK(width(words.second[i], words.pt) <= room(i));
                CHECK(words.first[i] + " " + words.second[i] == names[i]);
                // Two lines sit in the face, each in its own half.
                CHECK(2.0f * std::round(words.pt * c.unit) <= tok::kFace * c.unit);
            } else {
                CHECK(words.first[i] == names[i]);
            }
        }
        if (!l.tight) {
            // "Fingers" and "Off" in their cells, on one line.
            CHECK(width("Fingers", tok::kLabel) <= l.fingers.w() - 2.0f * pad);
            CHECK(width("Off", tok::kLabel) <= l.off.w() - 2.0f * pad);
        }
    }
}

TEST_CASE("scale card: the cells at the real font keep their hits, gaps and the window") {
    for (const Case c : kRealCases) {
        CAPTURE(c.s.w);
        CAPTURE(c.unit);
        const ScaleCardLayout l = real_layout_at(c.s, c.unit);
        CHECK(l.card.x0 >= -kEps);
        CHECK(l.card.x1 <= c.s.w + kEps);
        CHECK(l.card.y1 <= c.s.h + kEps);
        const std::vector<Box> all = cells(l);
        for (std::size_t i = 0; i < all.size(); ++i) {
            CHECK(all[i].w() >= tok::kHit * c.unit - kEps);
            CHECK(all[i].h() >= tok::kHit * c.unit - kEps);
            CHECK(l.card.holds(all[i]));
            CHECK_FALSE(overlap(all[i], l.ring));
            for (std::size_t j = i + 1; j < all.size(); ++j) {
                const float dx = std::max(all[i].x0 - all[j].x1, all[j].x0 - all[i].x1);
                const float dy = std::max(all[i].y0 - all[j].y1, all[j].y0 - all[i].y1);
                CHECK(std::max(dx, dy) >= tok::kHitGap * c.unit - kEps);
            }
        }
    }
}
