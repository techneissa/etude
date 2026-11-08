// SPDX-License-Identifier: GPL-3.0-or-later
// toast: the first topics (split from toast_test.cpp).
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

namespace {

constexpr float kGap = 8.0f;

} // namespace

TEST_CASE("a toast is whole for its time, fades over the end, and is gone after") {
    CHECK(toast_alpha(0.0, false, false) == 1.0f);
    CHECK(toast_alpha(kToastShowS - kToastFadeS, false, false) == 1.0f);
    CHECK(toast_alpha(kToastShowS - kToastFadeS * 0.5, false, false) == doctest::Approx(0.5f));
    CHECK(toast_alpha(kToastShowS - 0.001, false, false) > 0.0f);
    CHECK(toast_alpha(kToastShowS, false, false) == 0.0f); // exactly at the end it is gone
    CHECK(toast_alpha(kToastShowS + 5.0, false, false) == 0.0f);
}

TEST_CASE("a toast shows for about a second and a half") {
    CHECK(kToastShowS == doctest::Approx(1.5));
    CHECK(kToastFadeS < kToastShowS);
}

TEST_CASE("with reduced effects there is no fade: full, then gone") {
    CHECK(toast_alpha(0.0, false, true) == 1.0f);
    CHECK(toast_alpha(kToastShowS - 0.001, false, true) == 1.0f);
    CHECK(toast_alpha(kToastShowS, false, true) == 0.0f);
    CHECK(toast_alpha(kToastWarnShowS - 0.001, true, true) == 1.0f);
    CHECK(toast_alpha(kToastWarnShowS, true, true) == 0.0f);
}

TEST_CASE("a warning stays longer and fades at its own end") {
    CHECK(kToastWarnShowS > kToastShowS);
    CHECK(toast_alpha(kToastShowS, true, false) == 1.0f);
    CHECK(toast_alpha(kToastWarnShowS - kToastFadeS * 0.5, true, false) == doctest::Approx(0.5f));
    CHECK(toast_alpha(kToastWarnShowS, true, false) == 0.0f);
}

TEST_CASE("a toast that offers Undo stays for the time it takes to reach it, then fades and goes") {
    CHECK(kToastActionShowS > kToastWarnShowS);
    CHECK(toast_show_s(false) == kToastShowS);
    CHECK(toast_show_s(true) == kToastWarnShowS);
    CHECK(toast_show_s(false, ToastAction::Undo) == kToastActionShowS);
    CHECK(toast_show_s(true, ToastAction::Undo) == kToastActionShowS);
    CHECK(toast_alpha(kToastShowS + 0.5, false, false) == 0.0f);
    CHECK(toast_alpha(kToastShowS + 0.5, false, false, ToastAction::Undo) == 1.0f);
    CHECK(toast_alpha(kToastActionShowS - kToastFadeS * 0.5, false, false, ToastAction::Undo) ==
          doctest::Approx(0.5f));
    CHECK(toast_alpha(kToastActionShowS, false, false, ToastAction::Undo) == 0.0f);
    // With reduced effects: whole, then gone.
    CHECK(toast_alpha(kToastActionShowS - 0.001, false, true, ToastAction::Undo) == 1.0f);
    CHECK(toast_alpha(kToastActionShowS, false, true, ToastAction::Undo) == 0.0f);
}

TEST_CASE("the action belongs to the toast it was set on: a new toast, a dismissal or the end clears it") {
    ToastState t;
    CHECK(t.action() == ToastAction::None);
    t.show("Section deleted", 0.0, false);
    CHECK(t.action() == ToastAction::None); // a toast offers nothing until asked
    t.set_action(ToastAction::Undo);
    CHECK(t.action() == ToastAction::Undo);
    CHECK(t.alpha(3.0, false) == 1.0f); // past a plain toast's time
    // A newer toast, of any kind, replaces it whole.
    t.show("Added to the practice list", 3.5, false);
    CHECK(t.action() == ToastAction::None);
    t.show("Sections joined", 4.0, false);
    t.set_action(ToastAction::Undo);
    t.dismiss();
    CHECK_FALSE(t.active());
    CHECK(t.action() == ToastAction::None);
    CHECK(t.alpha(4.1, false) == 0.0f);
    // It ends with the time and takes the action with it.
    t.show("Cut at Bar 4", 10.0, false);
    t.set_action(ToastAction::Undo);
    CHECK(t.alpha(10.0 + kToastActionShowS - 0.01, false) > 0.0f);
    CHECK(t.alpha(10.0 + kToastActionShowS, false) == 0.0f);
    CHECK(t.action() == ToastAction::None);
}

TEST_CASE("a message queued between frames offers no action, even over one that did") {
    ToastState t;
    t.show("Section deleted", 0.0, false);
    t.set_action(ToastAction::Undo);
    t.queue("Saved", false);
    CHECK(t.action() == ToastAction::None);
}

TEST_CASE("a clock that stepped back, or a bad age, is the start or nothing") {
    CHECK(toast_alpha(-3.0, false, false) == 1.0f);
    CHECK(toast_alpha(std::numeric_limits<double>::quiet_NaN(), false, false) == 0.0f);
    CHECK(toast_alpha(std::numeric_limits<double>::infinity(), false, false) == 0.0f);
}

TEST_CASE("the fade never rises and stays in 0..1") {
    float last = 1.0f;
    for (double t = 0.0; t < 2.0; t += 0.005) {
        const float a = toast_alpha(t, false, false);
        CHECK(a >= 0.0f);
        CHECK(a <= 1.0f);
        CHECK(a <= last);
        last = a;
    }
}

// --- Placement ----------------------------------------------------------------------------------
//
// Real regions from the app's own layout (compute_layout, split_top_row, the sheet's and the
// editor's layouts) at the three screens the UI is held to and UI scales 1.0 and 1.5; the pill is
// the size of "Opened satie-gymnopedie-1" in the toast's font.

namespace {

// A set with no notes to keep out of: the notes' rect and the surface are the whole window.
void free_of_notes(ToastRegions& r, float w, float h) {
    r.notes = {0.0f, 0.0f, w, h};
    r.surface = {0.0f, 0.0f, w, h};
}

// The rules every placed toast keeps: inside the window, clear of every avoid rect, and out of the
// notes unless inside the open sheet or editor or the toast's own slot.
void check_clear(const ToastPlacement& p, const Screen& s) {
    if (p.spot == ToastSpot::None) {
        CHECK(p.form == ToastForm::None);
        return;
    }
    CHECK(p.form != ToastForm::None);
    CHECK(inside(p.box, s.w, s.h));
    for (std::size_t i = 0; i < s.regions.avoid_count; ++i) {
        CHECK_FALSE(overlap(p.box, s.regions.avoid[i]));
    }
    const auto in_rect = [&](const RectF& r) {
        return p.box.x >= r.x - 1e-3f && p.box.y >= r.y - 1e-3f &&
               p.box.right() <= r.right() + 1e-3f && p.box.bottom() <= r.bottom() + 1e-3f;
    };
    if (overlap(p.box, s.regions.notes)) {
        CHECK((in_rect(s.regions.surface) || in_rect(s.regions.slot)));
    }
}

} // namespace

TEST_CASE("nothing open: a toast with no control goes in the slot under the top row") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            CAPTURE(size.w);
            CAPTURE(u);
            Screen s(size, u);
            const ToastPlacement p = s.place();
            CHECK(p.spot == ToastSpot::Slot);
            CHECK(p.form == ToastForm::Full);
            CHECK(p.box.w == s.pill_w);
            CHECK(p.box.y >= s.lay.top_row.bottom());
            CHECK_FALSE(overlap(p.box, s.status));
            check_clear(p, s);
        }
    }
}

TEST_CASE("the bar shown: a toast for one of its buttons goes to the slot, not under it") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            CAPTURE(size.w);
            CAPTURE(u);
            Screen s(size, u);
            s.show_bar();
            const RectF button{s.w - 300.0f * u, 4.0f * u, 48.0f * u, 49.0f * u};
            const ToastPlacement p = s.place(button);
            CHECK(p.spot != ToastSpot::Anchor);
            check_clear(p, s);
            if (p.spot != ToastSpot::None) {
                CHECK_FALSE(overlap(p.box, s.status));
            }
        }
    }
}

TEST_CASE("the tempo popover open: a toast for a control in it stays out of it and the notes") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            CAPTURE(size.w);
            CAPTURE(u);
            Screen s(size, u);
            s.show_bar();
            const RectF card = s.open_popover();
            const RectF row{card.x + 10.0f * u, card.y + 100.0f * u, card.w - 20.0f * u, 44.0f * u};
            const ToastPlacement p = s.place(row);
            CHECK(p.spot != ToastSpot::Anchor);
            CHECK_FALSE(overlap(p.box, card));
            check_clear(p, s);
        }
    }
}

TEST_CASE("the sheet open: a toast never covers its header or rail, with or without a control") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            CAPTURE(size.w);
            CAPTURE(u);
            Screen s(size, u);
            RectF header;
            const RectF card = s.open_sheet(&header);
            // No control: the slot runs into the sheet's header, so the message is dropped
            // rather than put over it.
            const ToastPlacement free = s.place();
            check_clear(free, s);
            CHECK(free.spot == ToastSpot::None);
            CHECK_FALSE(overlap(free.box, header));
            // A control in the page, a row low enough that a pill fits under it.
            const RectF row{card.x + card.w * 0.5f, card.y + card.h * 0.4f, 200.0f * u, 44.0f * u};
            const ToastPlacement by_row = s.place(row);
            check_clear(by_row, s);
            CHECK(by_row.spot == ToastSpot::Anchor);
            CHECK(by_row.box.y == row.bottom() + s.gap);
            // A control in the header: its toast goes where the header is not.
            const RectF in_header{header.x + 10.0f * u, header.y, 44.0f * u, 44.0f * u};
            const ToastPlacement by_header = s.place(in_header);
            check_clear(by_header, s);
            CHECK_FALSE(overlap(by_header.box, header));
        }
    }
}

TEST_CASE("the editor open: a toast never covers its toolbar or ruler") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            CAPTURE(size.w);
            CAPTURE(u);
            Screen s(size, u);
            const EditorLayout l = s.open_editor();
            const ToastPlacement free = s.place();
            check_clear(free, s);
            CHECK(free.spot == ToastSpot::None); // the editor's toolbar is where the slot runs
            const RectF button = {l.toolbar.right() - 44.0f * u, l.toolbar.y, 44.0f * u, l.toolbar.h};
            const ToastPlacement by_button = s.place(button);
            check_clear(by_button, s);
            CHECK(by_button.spot != ToastSpot::Anchor); // under it is the ruler
            // A control on the action bar, at the bottom: above it is the loupe, free of the rules.
            const ToastPlacement by_action = s.place(l.actions);
            check_clear(by_action, s);
        }
    }
}

TEST_CASE("the editor open: the toast of the far right action stands inside the editor's card, not lost at its edge") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            CAPTURE(size.w);
            CAPTURE(u);
            Screen s(size, u);
            const EditorLayout l = s.open_editor();
            const ActionSlots a = action_slots(l.actions, u);
            // Delete is the last button of the row, near the card's right edge, which lies inside
            // the window by the editor's margin: a pill centred on it would pass the card.
            const ToastPlacement p = s.place(a.band_delete);
            check_clear(p, s);
            REQUIRE(p.spot == ToastSpot::Anchor);
            CHECK(p.box.right() <= s.regions.surface.right() + 1e-3f);
            CHECK(p.box.x >= s.regions.surface.x - 1e-3f);
            // The same with the wider pill of an edit's toast, which carries an Undo chip.
            const ToastPlacement wide = place_toast(300.0f * u, 44.0f * u, s.min_w, s.icon, false,
                                                    a.band_delete, s.w, s.h, s.regions, s.gap);
            REQUIRE(wide.spot == ToastSpot::Anchor);
            CHECK(wide.box.right() <= s.regions.surface.right() + 1e-3f);
            CHECK_FALSE(overlap(wide.box, a.band_delete));
        }
    }
}

TEST_CASE("every placement over a grid of controls keeps every rule, in every state") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            for (int state = 0; state < 4; ++state) {
                CAPTURE(size.w);
                CAPTURE(u);
                CAPTURE(state);
                Screen s(size, u);
                if (state == 1) {
                    s.show_bar();
                } else if (state == 2) {
                    s.open_sheet();
                } else if (state == 3) {
                    s.open_editor();
                }
                for (float ax = 0.0f; ax <= s.w - 40.0f * u; ax += s.w / 12.0f) {
                    for (float ay = 0.0f; ay <= s.h - 40.0f * u; ay += s.h / 12.0f) {
                        const RectF anchor{ax, ay, 40.0f * u, 40.0f * u};
                        check_clear(s.place(anchor), s);
                        check_clear(s.place(anchor, true, 900.0f * u), s);
                    }
                }
            }
        }
    }
}

TEST_CASE("a long message is shortened to the window's width, never over the status line") {
    Screen s({800.0f, 480.0f}, 1.0f);
    s.show_bar();
    const float room = s.w - 2.0f * s.gap;
    REQUIRE(room > s.min_w);
    const ToastPlacement p = s.place({}, false, room + 100.0f);
    CHECK(p.spot == ToastSpot::Slot);
    CHECK(p.form == ToastForm::Shortened);
    CHECK(p.box.w == doctest::Approx(room));
    check_clear(p, s);
}

TEST_CASE("a message that fits the room exactly stays whole; a pixel over is shortened") {
    Screen s({1366.0f, 768.0f}, 1.0f);
    const float room = s.w - 2.0f * s.gap;
    const ToastPlacement whole = s.place({}, false, room);
    CHECK(whole.form == ToastForm::Full);
    CHECK(whole.box.w == room);
    const ToastPlacement cut = s.place({}, false, room + 1.0f);
    CHECK(cut.form == ToastForm::Shortened);
    CHECK(cut.box.w == room);
}

TEST_CASE("no room for the shortest pill: a free strip, else a warning's icon, else nothing") {
    Screen s({800.0f, 480.0f}, 1.0f);
    s.regions.slot = {12.0f, 0.0f, s.min_w - 1.0f, 57.0f}; // one pixel short of the shortest pill
    // Nothing else free: a plain message is dropped, a warning shows as the icon alone.
    const ToastPlacement plain = s.place();
    CHECK(plain.spot == ToastSpot::None);
    CHECK(plain.form == ToastForm::None);
    const ToastPlacement warn = s.place({}, true);
    CHECK(warn.spot == ToastSpot::Slot);
    CHECK(warn.form == ToastForm::Icon);
    CHECK(warn.box.w == s.icon); // a finger wide
    CHECK(warn.box.h == s.icon);
    CHECK(warn.box.x + warn.box.w * 0.5f == doctest::Approx(s.regions.slot.x + s.regions.slot.w * 0.5f));
    check_clear(warn, s);
    // A free strip of the open sheet takes the whole message.
    s.regions.strip = {100.0f, 400.0f, 600.0f, 44.0f};
    s.regions.surface = {50.0f, 80.0f, 700.0f, 395.0f};
    const ToastPlacement strip = s.place();
    CHECK(strip.spot == ToastSpot::Strip);
    CHECK(strip.form == ToastForm::Full);
    CHECK(strip.box.x + strip.box.w * 0.5f == doctest::Approx(400.0f));
    check_clear(strip, s);
    // Exactly the shortest pill fits the status area: shortened there, before the strip.
    s.regions.slot = {12.0f, 0.0f, s.min_w, 57.0f};
    const ToastPlacement edge = s.place({}, false, 400.0f);
    CHECK(edge.spot == ToastSpot::Strip); // the strip holds the whole 400 px message first
    s.regions.strip = {};
    const ToastPlacement shorter = s.place({}, false, 400.0f);
    CHECK(shorter.spot == ToastSpot::Slot);
    CHECK(shorter.form == ToastForm::Shortened);
    CHECK(shorter.box.w == doctest::Approx(s.min_w));
}

TEST_CASE("a pill taller than the status area does not go there") {
    Screen s({800.0f, 480.0f}, 1.0f);
    s.regions.slot = {12.0f, 0.0f, 700.0f, s.pill_h - 1.0f};
    CHECK(s.place().spot == ToastSpot::None);
    s.regions.slot.h = s.pill_h;
    CHECK(s.place().spot == ToastSpot::Slot);
}

TEST_CASE("boundary: touching a region is clear, one pixel of overlap moves the toast") {
    // A control with free space under it, and a rect starting `gap` + `h` under its bottom edge.
    ToastRegions r;
    free_of_notes(r, 800.0f, 480.0f);
    r.slot = {8.0f, 0.0f, 400.0f, 38.0f};
    const RectF anchor{300.0f, 100.0f, 60.0f, 44.0f}; // bottom 144: a pill goes at 152..190
    const float kPillW = 160.0f;
    const float kPillH = 38.0f;
    const auto at = [&](const RectF& blocker) {
        ToastRegions with = r;
        REQUIRE(with.add_avoid(blocker));
        return place_toast(kPillW, kPillH, 80.0f, 44.0f, false, anchor, 800.0f, 480.0f, with, kGap);
    };
    // The blocker's top at the pill's bottom (190): touching, so the pill stays under the control.
    const ToastPlacement touching = at({0.0f, 190.0f, 800.0f, 100.0f});
    CHECK(touching.spot == ToastSpot::Anchor);
    CHECK(touching.box.y == 152.0f);
    // One pixel higher: it overlaps; above the control is free, so it goes there.
    const ToastPlacement over = at({0.0f, 189.0f, 800.0f, 100.0f});
    CHECK(over.spot == ToastSpot::Anchor);
    CHECK(over.box.bottom() == anchor.y - kGap);
    // Both sides blocked by one pixel: the status area.
    ToastRegions both = r;
    REQUIRE(both.add_avoid({0.0f, 189.0f, 800.0f, 100.0f}));
    REQUIRE(both.add_avoid({0.0f, 50.0f, 800.0f, 51.0f})); // down to 101, over the 54..92 slot's bottom
    const ToastPlacement moved =
        place_toast(kPillW, kPillH, 80.0f, 44.0f, false, anchor, 800.0f, 480.0f, both, kGap);
    CHECK(moved.spot == ToastSpot::Slot);
    // The same, touching above: 100 - 8 - 38 = 54 is the pill's top, so a blocker ending at 54 is clear.
    ToastRegions above = r;
    REQUIRE(above.add_avoid({0.0f, 189.0f, 800.0f, 100.0f}));
    REQUIRE(above.add_avoid({0.0f, 40.0f, 800.0f, 14.0f})); // 40..54
    const ToastPlacement touch_above =
        place_toast(kPillW, kPillH, 80.0f, 44.0f, false, anchor, 800.0f, 480.0f, above, kGap);
    CHECK(touch_above.spot == ToastSpot::Anchor);
    CHECK(touch_above.box.y == 54.0f);
    above.avoid_count = 0;
    REQUIRE(above.add_avoid({0.0f, 189.0f, 800.0f, 100.0f}));
    REQUIRE(above.add_avoid({0.0f, 40.0f, 800.0f, 15.0f})); // 40..55: one pixel into the pill
    CHECK(place_toast(kPillW, kPillH, 80.0f, 44.0f, false, anchor, 800.0f, 480.0f, above, kGap).spot ==
          ToastSpot::Slot);
}

TEST_CASE("boundary: the notes' top edge, and the open surface that hides them") {
    ToastRegions r;
    r.notes = {0.0f, 100.0f, 800.0f, 380.0f};
    r.slot = {8.0f, 0.0f, 400.0f, 100.0f};
    const RectF anchor{300.0f, 20.0f, 60.0f, 44.0f}; // under it the pill lies 72..110
    // The pill's bottom would be at 110, 10 px into the notes: not there; the status area instead.
    ToastPlacement p = place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, anchor, 800.0f, 480.0f, r, 8.0f);
    CHECK(p.spot == ToastSpot::Slot);
    // An anchor whose pill ends exactly at the notes' top (bottom 54: 54 + 8 + 38 = 100).
    const RectF higher{300.0f, 10.0f, 60.0f, 44.0f};
    p = place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, higher, 800.0f, 480.0f, r, 8.0f);
    CHECK(p.spot == ToastSpot::Anchor);
    CHECK(p.box.bottom() == 100.0f);
    // One pixel lower: 1 px into the notes.
    const RectF lower{300.0f, 11.0f, 60.0f, 44.0f};
    p = place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, lower, 800.0f, 480.0f, r, 8.0f);
    CHECK(p.spot == ToastSpot::Slot);
    // Inside an open sheet (the notes behind it are paused and dimmed) the notes' rect is no
    // longer in the way, but only inside it: one pixel out of the sheet is in the notes again.
    const RectF in_sheet{300.0f, 170.0f, 60.0f, 44.0f}; // under it the pill lies 222..260
    r.surface = {100.0f, 110.0f, 600.0f, 150.0f};       // ends at 260
    p = place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, in_sheet, 800.0f, 480.0f, r, 8.0f);
    CHECK(p.spot == ToastSpot::Anchor);
    CHECK(p.box.y == 222.0f);
    r.surface.h = 149.0f; // the sheet ends one pixel above the pill's bottom: above the control
    p = place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, in_sheet, 800.0f, 480.0f, r, 8.0f);
    CHECK(p.spot == ToastSpot::Anchor);
    CHECK(p.box.bottom() == in_sheet.y - 8.0f);
    r.surface.y = 125.0f; // and one pixel too short at the top as well: out of the sheet, so not there
    r.surface.h = 134.0f;
    p = place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, in_sheet, 800.0f, 480.0f, r, 8.0f);
    CHECK(p.spot == ToastSpot::Slot);
}

TEST_CASE("boundary: the window's own edge") {
    ToastRegions r;
    free_of_notes(r, 800.0f, 480.0f);
    r.slot = {0.0f, 0.0f, 800.0f, 60.0f};
    // A control at the right edge: the pill is pulled in by `gap`, and stays inside.
    const RectF edge{756.0f, 200.0f, 44.0f, 44.0f};
    const ToastPlacement p = place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, edge, 800.0f, 480.0f, r, 8.0f);
    CHECK(p.spot == ToastSpot::Anchor);
    CHECK(p.box.right() == 792.0f);
    // The window too short for the pill under or above its control: the status area.
    const ToastPlacement tight =
        place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, RectF{600.0f, 0.0f, 60.0f, 60.0f}, 800.0f, 60.0f,
                    r, 8.0f);
    CHECK(tight.spot == ToastSpot::Slot);
    CHECK(inside(tight.box, 800.0f, 60.0f));
    CHECK(place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, {}, 800.0f, 37.0f, r, 8.0f).spot ==
          ToastSpot::None); // not even the status area is that tall
}

TEST_CASE("a pill wider than the window is cut to it and shortened") {
    ToastRegions r;
    free_of_notes(r, 800.0f, 480.0f);
    r.slot = {0.0f, 0.0f, 800.0f, 60.0f};
    const ToastPlacement p = place_toast(5000.0f, 38.0f, 80.0f, 44.0f, false, {}, 800.0f, 480.0f, r, 8.0f);
    CHECK(p.spot == ToastSpot::Slot);
    CHECK(p.form == ToastForm::Shortened);
    CHECK(p.box.w == 784.0f);
}

TEST_CASE("bad numbers give nothing, or a pill inside the window") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ToastRegions r;
    free_of_notes(r, 800.0f, 480.0f);
    r.slot = {8.0f, 0.0f, 400.0f, 60.0f};
    const RectF bad_anchor{nan, 10.0f, 20.0f, 20.0f};
    const ToastPlacement a = place_toast(100.0f, 30.0f, 40.0f, 44.0f, false, bad_anchor, 800.0f, 480.0f, r, 8.0f);
    CHECK(a.spot == ToastSpot::Slot);
    CHECK(std::isfinite(a.box.x));
    const ToastPlacement b = place_toast(nan, nan, nan, 44.0f, true, {}, nan, nan, ToastRegions{}, nan);
    CHECK(b.spot == ToastSpot::None);
    ToastRegions bad = r;
    bad.avoid[0] = {nan, nan, nan, nan}; // a rect that cannot be read keeps everything out
    bad.avoid_count = 1;
    const ToastPlacement c =
        place_toast(100.0f, 30.0f, 40.0f, 44.0f, true, {}, 800.0f, 480.0f, bad, 8.0f);
    CHECK(c.spot == ToastSpot::None);
    CHECK(place_toast(100.0f, 30.0f, 40.0f, 44.0f, false, {}, 0.0f, 0.0f, r, 8.0f).spot == ToastSpot::None);
}

TEST_CASE("the avoid list takes kMaxToastAvoid rects and ignores empty ones") {
    ToastRegions r;
    CHECK(r.add_avoid({}));
    CHECK(r.avoid_count == 0);
    for (std::size_t i = 0; i < kMaxToastAvoid; ++i) {
        CHECK(r.add_avoid({1.0f, 1.0f, 2.0f, 2.0f}));
    }
    CHECK(r.avoid_count == kMaxToastAvoid);
    CHECK_FALSE(r.invalid);
}

TEST_CASE("a rect that cannot be kept fails placement closed: a full list, a rect that is not finite") {
    const auto place_in = [](const ToastRegions& r) {
        return place_toast(160.0f, 38.0f, 80.0f, 44.0f, true, {}, 800.0f, 480.0f, r, 8.0f);
    };
    ToastRegions ok;
    free_of_notes(ok, 800.0f, 480.0f);
    ok.slot = {8.0f, 0.0f, 400.0f, 60.0f};
    REQUIRE(place_in(ok).spot == ToastSpot::Slot);

    ToastRegions full = ok;
    for (std::size_t i = 0; i < kMaxToastAvoid; ++i) {
        REQUIRE(full.add_avoid({700.0f, 300.0f, 10.0f, 10.0f})); // far from the status area
    }
    CHECK_FALSE(full.invalid);
    CHECK(place_in(full).spot == ToastSpot::Slot);
    CHECK_FALSE(full.add_avoid({700.0f, 300.0f, 10.0f, 10.0f})); // the 13th is lost
    CHECK(full.invalid);
    CHECK(full.avoid_count == kMaxToastAvoid);
    CHECK(place_in(full).spot == ToastSpot::None); // nothing, not a toast over what was lost

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const RectF bad : {RectF{nan, 0.0f, 10.0f, 10.0f}, RectF{0.0f, inf, 10.0f, 10.0f},
                            RectF{0.0f, 0.0f, nan, 10.0f}, RectF{0.0f, 0.0f, 10.0f, inf}}) {
        ToastRegions r = ok;
        CHECK_FALSE(r.add_avoid(bad));
        CHECK(r.invalid);
        CHECK(r.avoid_count == 0);
        CHECK(place_in(r).spot == ToastSpot::None);
    }
}

TEST_CASE("an unset notes rect counts as the whole window: nothing goes over it but the surface") {
    ToastRegions r;
    r.strip = {8.0f, 0.0f, 400.0f, 60.0f}; // a host that forgot `notes`
    const ToastPlacement none =
        place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, {}, 800.0f, 480.0f, r, 8.0f);
    CHECK(none.spot == ToastSpot::None);
    r.surface = {0.0f, 0.0f, 800.0f, 480.0f};
    CHECK(place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, {}, 800.0f, 480.0f, r, 8.0f).spot ==
          ToastSpot::Strip);
    // Set, the notes' real rect lets the strip through again.
    r.surface = {};
    r.notes = {0.0f, 60.0f, 800.0f, 420.0f};
    CHECK(place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, {}, 800.0f, 480.0f, r, 8.0f).spot ==
          ToastSpot::Strip);
    // The slot is the toast's own region: it needs no surface, even with `notes` unset.
    ToastRegions q;
    q.slot = {0.0f, 60.0f, 800.0f, 44.0f};
    CHECK(place_toast(160.0f, 38.0f, 80.0f, 44.0f, false, {}, 800.0f, 480.0f, q, 8.0f).spot ==
          ToastSpot::Slot);
}

TEST_CASE("a warning keeps its words: when the bar is shown, pinned or not, it gives way") {
    // The rule the host applies: only a warning that lacks its words (the icon, or nothing) asks.
    CHECK(toast_lacks_words(true, ToastForm::Icon));
    CHECK(toast_lacks_words(true, ToastForm::None));
    CHECK_FALSE(toast_lacks_words(true, ToastForm::Full));
    CHECK_FALSE(toast_lacks_words(true, ToastForm::Shortened));
    CHECK_FALSE(toast_lacks_words(false, ToastForm::None)); // a plain message may be dropped
    CHECK_FALSE(toast_lacks_words(false, ToastForm::Icon));
    CHECK(bar_gives_way(true, true));        // shown (pinned or not): it hides, the words fit
    CHECK_FALSE(bar_gives_way(true, false)); // nothing shown to hide
    CHECK_FALSE(bar_gives_way(false, true)); // no warning that needs room

    // The slot spans the window, so a warning keeps its words with the bar shown too: the bar
    // never has to give way to it.
    Screen s({800.0f, 480.0f}, 1.0f);
    s.show_bar(false, 640.0f, 80.0f);
    const ToastPlacement shown = s.place({}, true);
    REQUIRE(shown.spot == ToastSpot::Slot);
    CHECK(shown.form == ToastForm::Full);
    CHECK_FALSE(toast_lacks_words(true, shown.form));
    check_clear(shown, s);
    // Where no slot is left (a tiny window), the warning is the icon alone and a plain message is
    // dropped.
    s.regions.slot.w = s.min_w - 1.0f;
    const ToastPlacement icon = s.place({}, true);
    REQUIRE(icon.spot == ToastSpot::Slot);
    CHECK(icon.form == ToastForm::Icon);
    CHECK(toast_lacks_words(true, icon.form));
    CHECK(s.place({}, false).spot == ToastSpot::None);
}

TEST_CASE("the warning icon is a finger wide, even where the pill is shorter") {
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            Screen s(size, u);
            s.regions.slot.w = s.min_w - 1.0f; // no room for the words
            const ToastPlacement p = s.place({}, true);
            CAPTURE(size.w);
            CAPTURE(u);
            REQUIRE(p.form == ToastForm::Icon);
            CHECK(p.box.w >= etude::app::ui::tok::kHit * u - 1e-3f);
            CHECK(p.box.h >= etude::app::ui::tok::kHit * u - 1e-3f);
            CHECK(p.box.h >= s.pill_h);
            check_clear(p, s); // the hit area is the box, clear of the bar's buttons and the notes
        }
    }
}

TEST_CASE("the section band under the bar: 52 pt, 50 pt slim, and the toast keeps off it") {
    using namespace etude::app::ui;
    for (const float u : kScales) {
        CHECK(band_height_pt(false, u) == doctest::Approx(52.0f * u));
        CHECK(band_height_pt(true, u) == doctest::Approx(50.0f * u));
        // The band hanging under a revealed, unpinned bar is the slim one: a hit area (46 pt) and
        // the room the bar's buttons above it need to keep their dead gap.
        CHECK(band_height_pt(true, u) >= etude::app::ui::tok::kHit * u);
        CHECK(band_height_pt(true, u) <= 52.0f * u);
        for (const bool slim : {false, true}) {
            CAPTURE(u);
            CAPTURE(slim);
            const float w = 1366.0f;
            const float h = 768.0f;
            const float row_bottom = 57.0f * u;
            const float band_bottom = row_bottom + band_height_pt(slim, u);
            const float pill_h = 38.0f * u;
            const float gap = 8.0f * u;
            app::ui::ToastRegionsInput in;
            in.display_w = w;
            in.display_h = h;
            in.row_top = 0.0f;
            in.row_bottom = row_bottom;
            in.bar_shown = true;
            in.has_song = true;
            in.slim = slim;
            in.toast_h = pill_h;
            in.gap = gap;
            in.u = u;
            // A control with its whole underside kept out, so a pill can only go above it: the
            // notes are exempt (a surface), leaving the band the one thing in the way.
            const auto above = [&](float anchor_y) {
                ToastRegions r;
                REQUIRE(app::ui::fill_toast_regions(r, in));
                r.surface = {0.0f, 0.0f, w, h};
                const RectF anchor{600.0f, anchor_y, 60.0f * u, 44.0f * u};
                REQUIRE(r.add_avoid({0.0f, anchor.bottom(), w, h - anchor.bottom()}));
                return std::pair{place_toast(220.0f * u, pill_h, 100.0f * u, 44.0f * u, false,
                                             anchor, w, h, r, gap),
                                 anchor};
            };
            // The pill's top exactly at the band's bottom: touching, so it stays by its control.
            const float touching_y = band_bottom + pill_h + gap;
            const auto touch = above(touching_y);
            CHECK(touch.first.spot == ToastSpot::Anchor);
            CHECK(touch.first.box.y == doctest::Approx(band_bottom));
            // One pixel lower: still clear.
            const auto clear = above(touching_y + 1.0f);
            CHECK(clear.first.spot == ToastSpot::Anchor);
            // One pixel higher: it reaches into the band, so it is not by its control.
            const auto over = above(touching_y - 1.0f);
            CHECK(over.first.spot != ToastSpot::Anchor);
        }
    }
}

TEST_CASE("a toast keeps the part of its text that is a song's title, inside what it kept") {
    ToastState t;
    t.show("Opened Satie, hands split by pitch", 0.0, false, {7, 5});
    CHECK(t.name().begin == 7);
    CHECK(t.name().size == 5);
    // A new toast without a name has none.
    t.show("Song sound off", 1.0, false);
    CHECK(t.name().size == 0);
    // A text cut to the buffer keeps the name as far as the kept text reaches.
    const std::string title(400, 'x');
    t.queue("Opened " + title, false, {7, title.size()});
    CHECK(t.name().begin == 7);
    CHECK(t.name().begin + t.name().size <= ToastState::kMaxBytes - 3);
    CHECK(t.name().size > 0);
    // A span past the text is empty.
    t.show("short", 2.0, false, {50, 10});
    CHECK(t.name().size == 0);
}

TEST_CASE("the toast never covers the status line, at any size or scale, with a long title") {
    // "Opened " and a very long song title: wider than the window, so it is shortened.
    for (const Size size : kScreens) {
        for (const float u : kScales) {
            for (const bool bar : {false, true}) {
                for (const bool warning : {false, true}) {
                    CAPTURE(size.w);
                    CAPTURE(u);
                    CAPTURE(bar);
                    Screen s(size, u);
                    if (bar) {
                        s.show_bar();
                    }
                    for (const float pw : {s.pill_w, 900.0f * u, 3000.0f * u}) {
                        const ToastPlacement p = s.place({}, warning, pw);
                        CAPTURE(pw);
                        REQUIRE(p.spot == ToastSpot::Slot);
                        const RectF row{0.0f, s.lay.top_row.y, s.w,
                                        s.lay.top_row.bottom() - s.lay.top_row.y};
                        CHECK_FALSE(overlap(p.box, row)); // not the top row at all
                        CHECK_FALSE(overlap(p.box, s.status));
                        CHECK(p.box.y >= s.lay.top_row.bottom() - 1e-3f);
                        check_clear(p, s); // nor the bar, the band, the lane or the key label
                        // Centred under the row.
                        CHECK(p.box.x + p.box.w * 0.5f == doctest::Approx(s.w * 0.5f));
                    }
                }
            }
        }
    }
}

TEST_CASE("the song-opened toast keeps the name and the composer line, each cut at a character") {
    ToastState t;
    t.show("Gymnopedie No. 1", 0.0, false, {0, 16}, {true, "Erik Satie"});
    CHECK(t.song());
    CHECK(std::string(t.text()) == "Gymnopedie No. 1");
    CHECK(std::string(t.detail()) == "Erik Satie");
    // No composer: the second line is empty, the form stays.
    t.show("Gymnopedie No. 1", 1.0, false, {0, 16}, {true, ""});
    CHECK(t.song());
    CHECK(std::string(t.detail()).empty());
    // Any other toast has neither.
    t.show("Song sound off", 2.0, false);
    CHECK_FALSE(t.song());
    CHECK(std::string(t.detail()).empty());
    // A composer longer than the buffer is cut inside no UTF-8 character, ending in an ellipsis.
    std::string long_composer;
    while (long_composer.size() < 300) {
        long_composer += "\xC3\xA9"; // e acute, two bytes
    }
    t.queue("Title", false, {0, 5}, {true, long_composer});
    const std::string kept = t.detail();
    CHECK(kept.size() <= ToastState::kMaxDetailBytes);
    CHECK(kept.substr(kept.size() - 3) == "\xE2\x80\xA6");
    CHECK(((kept.size() - 3) % 2) == 0);
}

TEST_CASE("a pill in the slot stands at the slot's top, so a one-line toast does not drop in a taller slot") {
    ToastRegions r;
    r.notes = {0.0f, 57.0f, 800.0f, 400.0f};
    r.slot = {0.0f, 65.0f, 800.0f, 60.0f};
    const auto one = place_toast(200.0f, 36.0f, 60.0f, 44.0f, false, {}, 800.0f, 480.0f, r, 8.0f);
    CHECK(one.spot == ToastSpot::Slot);
    CHECK(one.box.y == doctest::Approx(65.0f));
    const auto two = place_toast(200.0f, 56.0f, 60.0f, 44.0f, false, {}, 800.0f, 480.0f, r, 8.0f);
    CHECK(two.spot == ToastSpot::Slot);
    CHECK(two.box.bottom() <= r.slot.bottom() + 1e-3f);
    // Wider than the window less its margins: it takes that width and is shortened, not dropped.
    const auto wide = place_toast(2000.0f, 56.0f, 60.0f, 44.0f, false, {}, 800.0f, 480.0f, r, 8.0f);
    CHECK(wide.form == ToastForm::Shortened);
    CHECK(wide.box.w == doctest::Approx(800.0f - 16.0f));
}

TEST_CASE("a plain message set after a song-opened toast in the same frame has none of its song form") {
    PendingToast p;
    p.set_song("Gymnopedie No. 1", "Erik Satie", false, true);
    CHECK(p.lines().on);
    CHECK(p.lines().composer);
    CHECK(p.name.size == 16);
    // Another message lands before the frame draws: the later one wins whole.
    p.set("Song sound off", false);
    CHECK(p.text == "Song sound off");
    CHECK_FALSE(p.lines().on);
    CHECK_FALSE(p.lines().composer);
    CHECK(p.detail.empty());
    CHECK(p.name.size == 0);
    // Through the toast itself: no bold title, no second line, no composer is on screen.
    ToastState t;
    t.queue(p.text, p.warning, p.name, p.lines());
    t.begin_frame(0.0);
    CHECK_FALSE(t.song());
    CHECK_FALSE(t.composer());
    CHECK(std::string(t.detail()).empty());
    // A warning replaces a song form the same way.
    p.set_song("Title", "", false, false);
    p.set("Settings could not be read", true);
    CHECK(p.warning);
    CHECK_FALSE(p.lines().on);
}

TEST_CASE("a song-opened toast set after a plain message keeps its song form") {
    PendingToast p;
    p.set("Song sound off", false);
    p.set_song("Gymnopedie No. 1", "Erik Satie", false, true);
    CHECK(p.text == "Gymnopedie No. 1");
    CHECK(p.lines().on);
    CHECK(p.lines().composer);
    CHECK(p.detail == "Erik Satie");
    ToastState t;
    t.queue(p.text, p.warning, p.name, p.lines());
    t.begin_frame(0.0);
    CHECK(t.song());
    CHECK(t.composer());
    CHECK(std::string(t.detail()) == "Erik Satie");
    p.clear();
    CHECK(p.empty());
    CHECK_FALSE(p.lines().on);
}

TEST_CASE("the composer icon shows only with a composer, not with the open's note alone") {
    ToastState t;
    t.show("Title", 0.0, false, {0, 5}, {true, "Erik Satie", true});
    CHECK(t.composer());
    t.show("Title", 1.0, false, {0, 5}, {true, "note lengths guessed", false});
    CHECK(t.song());
    CHECK_FALSE(t.composer());
    t.show("Title", 2.0, false, {0, 5}, {true, "", true});
    CHECK_FALSE(t.composer());
    t.show("Song sound off", 3.0, false);
    CHECK_FALSE(t.composer());
}
