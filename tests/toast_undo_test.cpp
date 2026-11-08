// SPDX-License-Identifier: GPL-3.0-or-later
// toast: undo (split from toast_test.cpp).
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

#include "toast_test_support.hpp"

using namespace etude;
using namespace toast_test_support;

// ---- Undo: only while the edit it takes back is the latest

TEST_CASE("an edit that went through offers Undo with the counter it left; a refused edit offers none") {
    ToastState t;
    t.show("Section deleted", 0.0, false);
    CHECK(t.offer_undo(4, 5));
    CHECK(t.action() == ToastAction::Undo);
    t.show("That would make a section too short", 1.0, false);
    CHECK_FALSE(t.offer_undo(5, 5)); // the counter did not move: nothing to undo
    CHECK(t.action() == ToastAction::None);
    ToastState none;
    CHECK_FALSE(none.offer_undo(1, 2)); // no toast, no offer
}

TEST_CASE("an Undo is refused once anything was edited or undone after the edit, and its toast goes") {
    ToastState t;
    t.show("Sections joined", 0.0, false);
    REQUIRE(t.offer_undo(7, 8));
    CHECK_FALSE(t.resolve_action(8)); // still the latest edit: it stays
    CHECK(t.active());
    CHECK(t.action() == ToastAction::Undo);
    CHECK(t.resolve_action(9)); // a later edit: Undo would take back that one
    CHECK_FALSE(t.active());
    CHECK(t.action() == ToastAction::None);
    // The same for an undo done elsewhere, and a song loaded (the counter moves on both).
    t.show("Cut at Bar 4", 2.0, false);
    REQUIRE(t.offer_undo(9, 10));
    CHECK(t.resolve_action(11));
    CHECK_FALSE(t.active());
    // A toast with no action is never dismissed by the counter.
    t.show("Added to the practice list", 3.0, false);
    CHECK_FALSE(t.resolve_action(99));
    CHECK(t.active());
}

TEST_CASE("a toast that must show always finds a place, over the notes if nothing else is clear") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            CAPTURE(size.w);
            CAPTURE(u);
            Screen s(size, u);
            s.show_bar();
            const RectF card = s.open_popover(); // a popover under the bar takes the slot's room
            // A plain toast with an anchor that blocks every place is dropped; a must-show is not.
            const RectF anchor{s.w * 0.5f - 20.0f * u, s.h * 0.5f, 40.0f * u, 30.0f * u};
            const float pw = 300.0f * u;
            const float ph = 44.0f * u;
            const ToastPlacement plain = place_toast(pw, ph, s.min_w, s.icon, false, anchor, s.w, s.h,
                                                     s.regions, s.gap, false);
            const ToastPlacement must = place_toast(pw, ph, s.min_w, s.icon, false, anchor, s.w, s.h,
                                                    s.regions, s.gap, true);
            REQUIRE(must.spot != ToastSpot::None);
            CHECK(must.form != ToastForm::None);
            // Never over a kept-out rect, the anchor or outside the window, even over the notes.
            CHECK(inside(must.box, s.w, s.h));
            CHECK_FALSE(overlap(must.box, card));
            CHECK_FALSE(overlap(must.box, anchor));
            for (std::size_t i = 0; i < s.regions.avoid_count; ++i) {
                CHECK_FALSE(overlap(must.box, s.regions.avoid[i]));
            }
            // And when a plain toast found a place, the must-show took the same one.
            if (plain.spot != ToastSpot::None) {
                CHECK(must.spot == plain.spot);
                CHECK(must.box.x == doctest::Approx(plain.box.x));
                CHECK(must.box.y == doctest::Approx(plain.box.y));
            }
        }
    }
}

TEST_CASE("a must-show toast with no room anywhere is still nothing: it never covers a region") {
    ToastRegions r;
    r.notes = {0.0f, 40.0f, 400.0f, 300.0f};
    REQUIRE(r.add_avoid({0.0f, 0.0f, 400.0f, 340.0f}));
    r.slot = {0.0f, 10.0f, 400.0f, 44.0f};
    const ToastPlacement p = place_toast(200.0f, 44.0f, 100.0f, 44.0f, false, {}, 400.0f, 340.0f, r,
                                         8.0f, true);
    CHECK(p.spot == ToastSpot::None);
}

TEST_CASE("an Undo of the host's own (a saved palette put back) carries the own bit and a later edit toast does not") {
    ToastState t;
    t.show("Saved 1 cleared", 0.0, false);
    t.set_action(ToastAction::Undo, kUndoOwnBit | 3u);
    CHECK(t.action() == ToastAction::Undo);
    CHECK((t.action_token() & kUndoOwnBit) != 0);
    // The next toast replaces it; a section edit's offer has the counter, never the bit.
    t.show("Section deleted", 1.0, false);
    CHECK(t.action() == ToastAction::None);
    REQUIRE(t.offer_undo(4, 5));
    CHECK((t.action_token() & kUndoOwnBit) == 0);
}
