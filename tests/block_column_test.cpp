// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/block_column.hpp"
#include "etude/keyboard.hpp"
#include "etude/keyboard_view.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr float kEps = 1e-5f;

const KeyRange kRanges[] = {kRange88,         kRange76,         kRange61,        kRange49,
                            KeyRange{40, 77}, KeyRange{47, 72}, KeyRange{0, 127}};

// The whole keyboard across the window.
KeyViewFrame whole(const KeyboardLayout& layout) {
    KeyViewFrame f;
    f.layout_range = layout.range();
    f.pan = 0.0f;
    f.span_whites = static_cast<float>(layout.white_key_count());
    return f;
}

} // namespace

TEST_CASE("slots never overlap and tile the keyboard from the first slot to the last") {
    for (KeyRange r : kRanges) {
        const KeyboardLayout layout(r, BlockLayout::EvenLanes);
        INFO("range " << r.lowest << ".." << r.highest);
        // The first slot starts at the keyboard's start, or inside the first key when the range
        // starts in the middle of a group (A0: the G# slot before it is cut away).
        float edge = layout.slot(r.lowest).x;
        CHECK(edge >= 0.0f);
        CHECK(edge < layout.white_width());
        for (int n = r.lowest; n <= r.highest; ++n) {
            const KeyRect s = layout.slot(n);
            INFO("note " << n);
            REQUIRE(s.width > 0.0f);
            CHECK(s.x == doctest::Approx(edge).epsilon(kEps)); // no gap, no overlap
            CHECK(s.black == is_black_key(n));
            edge = s.x + s.width;
        }
        CHECK(edge <= 1.0f + kEps);
        // The last slot ends at the keyboard's end, or inside the last key when the range ends in
        // the middle of a group (C8: the C# slot past it is cut away).
        CHECK(edge >= layout.key(r.highest).x - kEps);
    }
}

TEST_CASE("even lanes: each slot lies within the part of its key that shows along the top edge") {
    for (KeyRange r : kRanges) {
        const KeyboardLayout layout(r, BlockLayout::EvenLanes);
        for (int n = r.lowest; n <= r.highest; ++n) {
            const KeyRect s = layout.slot(n);
            const KeyRect k = layout.key(n);
            INFO("note " << n);
            CHECK(s.x >= k.x - kEps);
            CHECK(s.x + s.width <= k.x + k.width + kEps);
            if (is_black_key(n)) {
                CHECK(s.x == doctest::Approx(k.x).epsilon(kEps)); // a black key is its slot
                CHECK(s.width == doctest::Approx(k.width).epsilon(kEps));
                continue;
            }
            // A white key's slot is what its black neighbours leave of it.
            for (int m : {n - 1, n + 1}) {
                if (r.contains(m) && is_black_key(m)) {
                    const KeyRect b = layout.key(m);
                    CHECK((s.x + s.width <= b.x + kEps || s.x >= b.x + b.width - kEps));
                }
            }
        }
    }
}

TEST_CASE("full slots are within 5% of each other: 3/5 and 4/7 of a white key") {
    for (KeyRange r : kRanges) {
        const KeyboardLayout layout(r);
        float lo = 1.0f;
        float hi = 0.0f;
        for (int n = r.lowest; n <= r.highest; ++n) {
            const KeyRect s = layout.slot(n);
            // An end slot cut by the range is narrower; every other one is a whole slot.
            if (n == r.lowest || n == r.highest) {
                continue;
            }
            lo = std::min(lo, s.width);
            hi = std::max(hi, s.width);
        }
        if (r.key_count() > 3) {
            CHECK(lo == doctest::Approx(4.0f / 7.0f * layout.white_width()).epsilon(kEps));
            CHECK(hi == doctest::Approx(3.0f / 5.0f * layout.white_width()).epsilon(kEps));
            CHECK(hi / lo <= 1.05f + 1e-5f); // 0.600 against 0.571
        }
    }
}

TEST_CASE("a range that starts inside a group keeps that group's slots, cut to the range") {
    // A0: the F-B group starts two white keys before the range (F0, G0), so A0's slot is the
    // group's fifth, 16/7 to 20/7 of a white key from F0.
    const KeyboardLayout a0(kRange88);
    const float ww = a0.white_width();
    CHECK(a0.slot(21).x == doctest::Approx((16.0f / 7.0f - 2.0f) * ww));
    CHECK(a0.slot(21).width == doctest::Approx(4.0f / 7.0f * ww));
    CHECK(a0.slot(22).width == doctest::Approx(4.0f / 7.0f * ww));
    const KeyboardLayout e1(kRange76); // E1: the last slot of the C-E group
    CHECK(e1.slot(28).x == doctest::Approx(0.4f * e1.white_width()));
    CHECK(e1.slot(28).width == doctest::Approx(0.6f * e1.white_width()));
    // C8 at the top: its slot, not the whole key.
    CHECK(a0.slot(108).width == doctest::Approx(0.6f * ww));
}

TEST_CASE("lane() is the key with KeyWidth and the slot with EvenLanes") {
    const KeyboardLayout key_width(kRange61);
    const KeyboardLayout even(kRange61, BlockLayout::EvenLanes);
    for (int n = 36; n <= 96; ++n) {
        CHECK(key_width.lane(n).x == key_width.key(n).x);
        CHECK(key_width.lane(n).width == key_width.key(n).width);
        CHECK(even.lane(n).x == even.slot(n).x);
        CHECK(even.lane(n).width == even.slot(n).width);
        // The slots do not depend on the layout.
        CHECK(key_width.slot(n).x == even.slot(n).x);
    }
    CHECK(even.lane(35).width == 0.0f);
    CHECK(key_width.lane(97).width == 0.0f);
}

TEST_CASE("even lanes' blocks never overlap and never touch, at any window width or range") {
    for (KeyRange r : kRanges) {
        const KeyboardLayout layout(r, BlockLayout::EvenLanes);
        const KeyViewFrame view = whole(layout);
        for (float px : {800.0f, 1366.0f, 1920.0f, 2560.0f}) {
            INFO("range " << r.lowest << " width " << px);
            float right = -1.0f;
            for (int n = r.lowest; n <= r.highest; ++n) {
                const BlockColumn c = block_column(layout, view, n, px);
                REQUIRE(c.visible);
                CHECK(c.w > 0.0f);
                CHECK(c.x >= right + kMinLaneGutterPx - 1e-3f);
                CHECK(c.x >= c.lane_x);
                CHECK(c.x + c.w <= c.lane_x + c.lane_w + 1e-3f);
                right = c.x + c.w;
            }
        }
    }
}

TEST_CASE("lane and block widths scale with the keyboard's width") {
    for (BlockLayout mode : {BlockLayout::EvenLanes, BlockLayout::KeyWidth}) {
        const KeyboardLayout layout(kRange88, mode);
        const KeyViewFrame view = whole(layout);
        for (int n : {21, 60, 61, 66, 108}) {
            const BlockColumn base = block_column(layout, view, n, 800.0f);
            for (float px : {1366.0f, 1920.0f, 2560.0f}) {
                const BlockColumn c = block_column(layout, view, n, px);
                const float k = px / 800.0f;
                CHECK(c.lane_x == doctest::Approx(base.lane_x * k));
                CHECK(c.lane_w == doctest::Approx(base.lane_w * k));
                // The gutter is a share of the lane once it is past its 1 px floor.
                if (mode == BlockLayout::KeyWidth ||
                    base.lane_w * kLaneGutterShare >= kMinLaneGutterPx) {
                    CHECK(c.w == doctest::Approx(base.w * k));
                }
                CHECK(c.w > base.w);
            }
        }
    }
    // Even lanes at 1366 px: about 0.6 of a white key less a 1 px gutter.
    const KeyboardLayout layout(kRange88, BlockLayout::EvenLanes);
    const KeyViewFrame view = whole(layout);
    const BlockColumn c4 = block_column(layout, view, 60, 1366.0f);
    CHECK(c4.lane_w == doctest::Approx(0.6f * 1366.0f / 52.0f));
    CHECK(c4.w == doctest::Approx(c4.lane_w - 1.0f));
}

TEST_CASE("the view's zoom and pan move and widen the lanes") {
    const KeyboardLayout layout(KeyRange{60, 72}, BlockLayout::EvenLanes);
    KeyViewFrame view = whole(layout);
    const BlockColumn at_rest = block_column(layout, view, 61, 1000.0f);
    view.span_whites = 4.0f; // zoomed in: four white keys across the window
    view.pan = 0.5f;
    const BlockColumn zoomed = block_column(layout, view, 61, 1000.0f);
    CHECK(zoomed.lane_w == doctest::Approx(at_rest.lane_w * 8.0f / 4.0f));
    CHECK(zoomed.lane_x == doctest::Approx((0.6f - 0.5f) * 250.0f));
}

TEST_CASE("key width: the blocks are the keys less their insets, black ones over the white") {
    const KeyboardLayout layout(kRange61);
    const KeyViewFrame view = whole(layout);
    const float px = 1366.0f;
    for (int n = 36; n <= 96; ++n) {
        const BlockColumn c = block_column(layout, view, n, px);
        const KeyRect k = layout.key(n);
        INFO("note " << n);
        // As the blocks have always been drawn: the key less a share of its width on each side.
        const float inset = k.width * px * (is_black_key(n) ? kBlackBlockInset : kWhiteBlockInset);
        CHECK(c.x == doctest::Approx(k.x * px + inset));
        CHECK(c.w == doctest::Approx(k.width * px - 2.0f * inset));
        if (is_black_key(n)) {
            const BlockColumn white = block_column(layout, view, n + 1, px);
            CHECK(c.layer > white.layer); // drawn after, so on top
            CHECK(c.x + c.w > white.x);   // it does lie over the white key's block
            CHECK(c.outline_px >= kMinBlackBlockOutlinePx);
        } else {
            CHECK(c.layer == 0);
            CHECK(c.outline_px == 0.0f);
        }
    }
    // Even lanes overlap nothing, so no block needs an outline; black blocks still draw last.
    const KeyboardLayout even(kRange61, BlockLayout::EvenLanes);
    for (int n = 36; n <= 96; ++n) {
        const BlockColumn c = block_column(even, view, n, px);
        CHECK(c.outline_px == 0.0f);
        CHECK(c.layer == (is_black_key(n) ? 1 : 0));
    }
}

TEST_CASE("a note outside the layout or a bad width has no block") {
    const KeyboardLayout layout(kRange61);
    const KeyViewFrame view = whole(layout);
    CHECK_FALSE(block_column(layout, view, 35, 1000.0f).visible);
    CHECK_FALSE(block_column(layout, view, 60, 0.0f).visible);
    CHECK_FALSE(block_column(layout, view, 60, -5.0f).visible);
}
