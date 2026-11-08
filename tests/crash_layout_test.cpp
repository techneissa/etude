// SPDX-License-Identifier: GPL-3.0-or-later
// The crash notice's layout holds the touch rules at every window size: hit areas of 12 mm, a dead
// gap of 2 mm between them, everything inside its card and the card inside the screen.
#include "ui/crash_layout.hpp"
#include "ui/crash_screen_layout.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace etude::app::ui;

namespace {

bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

bool inside(const Rect& r, const Rect& outer) {
    return r.x >= outer.x - 0.01f && r.y >= outer.y - 0.01f && r.right() <= outer.right() + 0.01f &&
           r.bottom() <= outer.bottom() + 0.01f;
}

} // namespace

TEST_CASE("crash notice layout: both chips are 12 mm tall with 2 mm between, inside the card") {
    for (const float u : {1.0f, 1.5f, 1.78f}) {
        {
            // The note area of a 800x480 window and of a 1366x768 one.
            for (const Rect area : {Rect{0, 40 * u, 800, 300}, Rect{0, 60 * u, 1366 * u, 420 * u}}) {
                const float hit = etude::app::ui::tok::kHit * u;
                const float gap = etude::app::ui::tok::kHitGap * u;
                const CrashNoticeGeom g = crash_notice_geometry(area.x, area.y, area.w, area.h, u,
                                                                hit * 2.3f, hit * 3.2f);
                CHECK(g.dismiss.h >= hit - 0.01f);
                CHECK(g.send.h >= hit - 0.01f);
                CHECK(g.dismiss.w >= hit);
                CHECK(g.send.w >= hit);
                CHECK(g.send.x - g.dismiss.right() >= gap - 0.01f);
                CHECK_FALSE(overlap(g.dismiss, g.send));
                CHECK(inside(g.dismiss, g.card));
                CHECK(inside(g.send, g.card));
                CHECK(inside(g.badge, g.card));
                CHECK(inside(g.title, g.card));
                CHECK(inside(g.detail, g.card));
                // The words stand clear of the chips.
                CHECK(g.detail.bottom() <= g.dismiss.y + 0.01f);
                CHECK(inside(g.card, {area.x, area.y, area.w, area.h > g.card.h ? area.h : g.card.h}));
            }
        }
    }
}

TEST_CASE("crash sheet layout: Back and the action are 12 mm tall and apart, the body under them") {
    for (const float u : {1.0f, 1.5f}) {
        for (const Rect screen : {Rect{0, 0, 800, 480}, Rect{0, 0, 1366, 768}, Rect{0, 0, 1920, 1080}}) {
            const float hit = etude::app::ui::tok::kHit * u;
            const float gap = etude::app::ui::tok::kHitGap * u;
            const CrashSheetGeom g = crash_sheet_geometry(screen.w, screen.h, 56.0f * u, u, hit * 3.6f);
            CHECK(g.back.h >= hit - 0.01f);
            CHECK(g.action.h >= hit - 0.01f);
            CHECK(g.action.x - g.back.right() >= gap);
            CHECK(inside(g.back, g.sheet));
            CHECK(inside(g.action, g.sheet));
            CHECK(inside(g.body, g.sheet));
            CHECK(g.body.y >= g.back.bottom());
            CHECK(g.sheet.y >= 56.0f * u); // under the top row
            CHECK(g.sheet.bottom() <= screen.h);
            CHECK(g.body.h > 0.0f);
        }
    }
}

TEST_CASE("crash report wrapping: lines are cut at spaces, a long word where it must be") {
    const auto width = [](const std::string& s) { return static_cast<float>(s.size()) * 8.0f; };
    const std::vector<std::string> lines = wrap_report("alpha beta gamma\n\ndelta", 8.0f * 11, width);
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "alpha beta");
    CHECK(lines[1] == "gamma");
    CHECK(lines[2].empty());
    CHECK(lines[3] == "delta");
    const std::vector<std::string> cut = wrap_report("abcdefghijklmnopqrstuvwxyz", 8.0f * 10, width);
    REQUIRE(cut.size() == 3);
    CHECK(cut[0] == "abcdefghij");
    CHECK(cut[2] == "uvwxyz");
    for (const std::string& line : cut) {
        CHECK(width(line) <= 8.0f * 10);
    }
    CHECK(wrap_report("", 80.0f, width).empty());
}

TEST_CASE("crash notice layout at the wide steps: bigger, still 12 mm hits with 2 mm gaps, inside the area") {
    struct Window { float w, h, u; };
    for (const Window win : {Window{800, 480, 1.0f}, Window{1366, 768, 1.0f}, Window{1920, 1080, 1.4f}}) {
        const int step = etude::app::ui::tok::wide_step(win.w);
        const CrashNoticeSizes z = crash_notice_sizes(step);
        const Rect area{0, 60 * win.u, win.w, win.h - 60 * win.u};
        for (const int lines : {1, 2}) {
            CAPTURE(win.w);
            CAPTURE(lines);
            const float hit = etude::app::ui::tok::kHit * win.u;
            const float gap = etude::app::ui::tok::kHitGap * win.u;
            const CrashNoticeGeom g = crash_notice_geometry(area.x, area.y, area.w, area.h, win.u,
                                                            hit * 2.3f, hit * 3.2f, step, lines, lines);
            CHECK(g.dismiss.h >= hit * z.grow - 0.01f);
            CHECK(g.send.h >= hit * z.grow - 0.01f);
            CHECK(g.send.x - g.dismiss.right() >= gap - 0.01f);
            CHECK_FALSE(overlap(g.dismiss, g.send));
            CHECK(inside(g.dismiss, g.card));
            CHECK(inside(g.send, g.card));
            CHECK(inside(g.badge, g.card));
            CHECK(inside(g.title, g.card));
            CHECK(inside(g.detail, g.card));
            CHECK(g.badge.w >= z.tile * win.u - 0.01f);
            CHECK(g.detail.bottom() <= g.dismiss.y + 0.01f);
            CHECK(g.title.right() <= g.card.right() + 0.01f);
            CHECK(inside(g.card, area));
            CHECK(g.title.w == doctest::Approx(crash_notice_text_w(area.w, win.u, step)));
        }
    }
    // The card grows with the step, and the 800x480 panel's is the one it always was.
    CHECK(crash_notice_card_w(800, 1.0f, 0) == doctest::Approx(520.0f));
    CHECK(crash_notice_card_w(1366, 1.0f, 1) > crash_notice_card_w(1366, 1.0f, 0));
    // From step 1 the card is a share of the area (50 to 60%) and never wider than its cap.
    CHECK(crash_notice_card_w(1366, 1.0f, 1) == doctest::Approx(1366 * 0.55f));
    CHECK(crash_notice_card_w(1920, 1.4f, 2) == doctest::Approx(1920 * 0.60f));
    CHECK(crash_notice_card_w(5000, 1.6f, 2) == doctest::Approx(1100.0f * 1.6f));
    CHECK(crash_notice_card_w(1920, 1.4f, 2) > crash_notice_card_w(1920, 1.4f, 1));
}

TEST_CASE("crash notice sizes: each step is larger than the one before, and uses the kit's ladder") {
    using namespace etude::app::ui;
    const CrashNoticeSizes a = crash_notice_sizes(0);
    const CrashNoticeSizes b = crash_notice_sizes(1);
    const CrashNoticeSizes c = crash_notice_sizes(2);
    CHECK(a.title_pt == tok::kBody);
    CHECK(a.detail_pt == tok::kLabel);
    CHECK(c.title_pt == tok::kDisplay);
    // Step 1 already takes the top of the ladder, as the crash screen does.
    CHECK(b.title_pt == tok::kDisplay);
    CHECK(b.detail_pt == tok::kTitle);
    CHECK(a.title_pt < b.title_pt);
    CHECK(b.title_pt <= c.title_pt);
    CHECK(a.detail_pt < b.detail_pt);
    CHECK(b.detail_pt <= c.detail_pt);
    CHECK(a.button_pt < b.button_pt);
    CHECK(b.button_pt < c.button_pt);
    CHECK(a.icon < b.icon);
    CHECK(b.icon < c.icon);
    CHECK(a.tile < b.tile);
    CHECK(b.tile < c.tile);
    // The notice and the crash screen draw the same emblem and the same button height at a step.
    for (int step = 1; step <= 2; ++step) {
        CHECK(crash_notice_sizes(step).tile == crash_screen_sizes(step).tile);
        CHECK(crash_notice_sizes(step).icon == crash_screen_sizes(step).icon);
        CHECK(crash_notice_sizes(step).button_pt == crash_screen_sizes(step).button_pt);
    }
    CHECK(a.card_w < b.card_w);
    CHECK(b.card_w < c.card_w);
    CHECK(a.grow == 1.0f);
}

TEST_CASE("wide step: chosen by the window's width, the same for the notice and the crash screen") {
    using etude::app::ui::tok::wide_grow;
    using etude::app::ui::tok::wide_step;
    CHECK(wide_step(0.0f) == 0);
    CHECK(wide_step(800.0f) == 0);
    CHECK(wide_step(1280.0f) == 0);
    CHECK(wide_step(1365.9f) == 0);
    CHECK(wide_step(1366.0f) == 1);
    CHECK(wide_step(1600.0f) == 1);
    CHECK(wide_step(1919.9f) == 1);
    CHECK(wide_step(1920.0f) == 2);
    CHECK(wide_step(3840.0f) == 2);
    CHECK(wide_grow(0) == 1.0f);
    CHECK(wide_grow(1) > wide_grow(0));
    CHECK(wide_grow(2) > wide_grow(1));
    // Both screens grow their buttons by the same steps.
    for (int step = 0; step <= 2; ++step) {
        CHECK(crash_notice_sizes(step).grow == wide_grow(step));
        CHECK(crash_screen_sizes(step).grow == wide_grow(step));
    }
}
