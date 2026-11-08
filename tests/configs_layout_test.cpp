// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Configs page puts its cards and the copy row's pills (configs_layout.hpp), with the
// real tokens: every card is a hit area of at least 12 mm both ways, two are at least 2 mm apart
// and the three fit the width; a pill is at least a hit wide, pills a dead gap apart wrap onto new
// rows.
#include "../app/ui/configs_layout.hpp"
#include "../app/ui/configs_logic.hpp"
#include "../app/ui/crash_layout.hpp"
#include "../app/ui/sheet_layout.hpp"

#include "atkinson_metrics.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

using namespace etude::app::ui;

TEST_CASE("the cards fit the width, a hit tall and wide, a dead gap apart (across or stacked)") {
    for (const float u : {1.0f, 1.41f, 1.6f}) {
        for (const float w : {500.0f, 826.0f, 1100.0f}) {
            const ConfigsCards c = configs_cards(w * u, u);
            CHECK(c.w >= tok::kHit * u);
            CHECK(c.h >= tok::kHit * u);
            CHECK(c.gap >= tok::kHitGap * u);
            CHECK(c.x(2) + c.w <= w * u + 0.01f);
            if (c.columns == 3) { // side by side: a dead gap across
                CHECK(c.x(1) - (c.x(0) + c.w) >= tok::kHitGap * u - 0.01f);
            } else { // stacked: a dead gap down, never overlapping
                CHECK(c.y(1) - (c.y(0) + c.h) >= tok::kHitGap * u - 0.01f);
                CHECK(c.y(2) + c.h <= c.total_h() + 0.01f);
            }
            // The preview and the name stay inside the card.
            CHECK(c.pad * 2.0f + c.preview_w <= c.w + 0.01f);
            CHECK(c.pad + c.preview_h + c.name_h <= c.h);
        }
    }
}

namespace {

struct Window {
    float w;
    float h;
    float px_per_mm;
    const char* name;
};
// The sizes the page is judged at: the two it must fit (1366x768 and 1920x1080 at 96 dpi), and
// 800x480 at 96 dpi and on the 7 inch panel (154 mm wide).
constexpr Window kWindows[] = {{1366.0f, 768.0f, etude::kDefaultPxPerMm, "1366x768"},
                               {1920.0f, 1080.0f, etude::kDefaultPxPerMm, "1920x1080"},
                               {800.0f, 480.0f, etude::kDefaultPxPerMm, "800x480"},
                               {800.0f, 480.0f, 800.0f / 154.0f, "800x480 panel"}};

// The settings page's area in `win` at `--ui-scale` `scale`, from the real sheet and rail layout
// and the real top row; `*u_out` is the kit's unit there.
PageArea page_in(const Window& win, float* u_out, float scale = 1.0f) {
    const float ppmm = win.px_per_mm * scale;
    const float u = etude::ui_scale(win.w, win.h, ppmm);
    const etude::ScreenLayout lay =
        etude::compute_layout(win.w, win.h, ppmm, etude::LookSettings{});
    const SheetFrame sheet = sheet_frame(win.w, win.h, lay.top_row.bottom() + tok::kS2 * u, u);
    *u_out = u;
    return page_area(sheet, rail_layout(sheet.w, sheet.h, kRailPages, u), u);
}

// The width of `text` in the page's words, at the real font, in points.
float words_w(const std::string& text, float u) {
    return etude::test::text_em(false, text) * tok::kBody * u;
}

} // namespace

// The page's fit at 1366x768 and 1920x1080 is not a sum made here: the audit's page-scroll check
// reads the height the page draws (the sheet's own measure of it) and fails when it scrolls from
// 1366x768 up. What is held here is what that depends on: the three cards stand side by side there.
TEST_CASE("at 1366x768 and 1920x1080 the three cards stand side by side, the buttons under each") {
    for (const Window& win : kWindows) {
        if (win.w < 1366.0f) {
            continue; // the 7 inch panel may scroll
        }
        float u = 1.0f;
        const PageArea area = page_in(win, &u);
        CAPTURE(win.name);
        const ConfigsCards c = configs_cards(area.w, u);
        CHECK(c.columns == 3);
        CHECK_FALSE(c.beside);
        CHECK(c.total_h() == c.h);
    }
}

TEST_CASE("a profile pill is as wide as its whole name: nothing is cut, even a 48-byte name") {
    const std::string at_48 =
        "Wolfgang Amadeus Mozart's Practice Config Number"; // the config name's cap; a profile name
                                                            // caps at 32
    REQUIRE(at_48.size() == 48);
    for (const Window& win : kWindows) {
        float u = 1.0f;
        const PageArea area = page_in(win, &u);
        CAPTURE(win.name);
        const float text = words_w(at_48, u);
        const float pill = configs_pill_width(text, u);
        // The pill holds the name whole with the avatar before it and the pad after it.
        CHECK(pill >= text + (tok::kS2 + configs_tok::kAvatar + tok::kS2) * u);
        // At 1366 and up, and on a 96 dpi 800x480 window, the pill fits the page's width.
        if (win.px_per_mm == etude::kDefaultPxPerMm) {
            CHECK(pill <= area.w);
        }
    }
}

TEST_CASE("pills that do not fit one line go onto a second line, a long name among them") {
    for (const Window& win : kWindows) {
        float u = 1.0f;
        const PageArea area = page_in(win, &u);
        CAPTURE(win.name);
        // The largest name a profile can have (32 bytes) and two shorter ones, and the 48-byte
        // name.
        const std::string long32(32, 'm');
        const std::vector<std::string> names{long32, "Ben", std::string(48, 'e'), long32, long32};
        std::vector<float> widths;
        for (const std::string& n : names) {
            widths.push_back(configs_pill_width(words_w(n, u), u));
        }
        std::vector<PillSpot> spots(widths.size());
        const int rows = configs_place_pills(widths, area.w, u, spots);
        CHECK(rows >= 2);
        for (std::size_t i = 0; i < widths.size(); ++i) {
            // A pill that fits a line starts and ends inside it; none starts past the line's end.
            CHECK(spots[i].x < area.w);
            if (widths[i] <= area.w) {
                CHECK(spots[i].x + widths[i] <= area.w + 0.01f);
            }
        }
    }
}

TEST_CASE("a pill is at least a hit wide and grows with its name") {
    CHECK(configs_pill_width(0.0f, 1.0f) >= tok::kHit);
    CHECK(configs_pill_width(80.0f, 1.0f) > configs_pill_width(30.0f, 1.0f));
    CHECK(configs_pill_width(40.0f, 1.5f) > configs_pill_width(40.0f, 1.0f));
}

TEST_CASE("pills sit side by side a dead gap apart and wrap onto a new row when full") {
    const std::array<float, 4> widths{100.0f, 100.0f, 100.0f, 100.0f};
    std::array<PillSpot, 4> spots{};
    // Room for three: the fourth starts the second row.
    const int rows = configs_place_pills(widths, 320.0f, 1.0f, spots);
    CHECK(rows == 2);
    CHECK(spots[0].x == 0.0f);
    CHECK(spots[1].x == doctest::Approx(100.0f + tok::kHitGap));
    CHECK(spots[3].x == 0.0f);
    CHECK(spots[3].y == doctest::Approx(tok::kHit + tok::kHitGap));
    CHECK(configs_pills_height(rows, 1.0f) == doctest::Approx(2 * tok::kHit + tok::kHitGap));
    // No two pills overlap or sit closer than the dead gap.
    for (std::size_t i = 0; i < spots.size(); ++i) {
        for (std::size_t j = i + 1; j < spots.size(); ++j) {
            const bool apart_x = spots[j].x >= spots[i].x + widths[i] + tok::kHitGap - 0.01f ||
                                 spots[i].x >= spots[j].x + widths[j] + tok::kHitGap - 0.01f;
            const bool apart_y = spots[j].y >= spots[i].y + tok::kHit + tok::kHitGap - 0.01f ||
                                 spots[i].y >= spots[j].y + tok::kHit + tok::kHitGap - 0.01f;
            CHECK((apart_x || apart_y));
        }
    }
}

TEST_CASE("no pills, no rows") {
    std::array<PillSpot, 1> spots{};
    CHECK(configs_place_pills({}, 300.0f, 1.0f, spots) == 0);
    CHECK(configs_pills_height(0, 1.0f) == 0.0f);
}

TEST_CASE("the copy question's card is on screen at every size, with 12 mm buttons 2 mm apart") {
    struct Win {
        float w;
        float h;
        float u;
    };
    for (const Win win :
         {Win{800.0f, 480.0f, 1.0f}, Win{1366.0f, 768.0f, 1.0f}, Win{1920.0f, 1080.0f, 1.41f}}) {
        for (const int lines : {1, 2}) {
            const ConfigsConfirmPlan p =
                plan_configs_confirm(win.w, win.h, win.u, lines, lines + 1);
            // The card is inside the window.
            CHECK(p.card.x0 >= 0.0f);
            CHECK(p.card.y0 >= 0.0f);
            CHECK(p.card.x1 <= win.w);
            CHECK(p.card.y1 <= win.h);
            // Everything is inside the card.
            for (const Box& b : {p.avatar, p.title, p.detail, p.cancel, p.copy}) {
                CHECK(p.card.holds(b));
            }
            // Both buttons are a hit tall and wide, a dead gap apart, side by side, not
            // overlapping.
            for (const Box& b : {p.cancel, p.copy}) {
                CHECK(b.w() >= tok::kHit * win.u);
                CHECK(b.h() >= tok::kHit * win.u - 0.01f);
            }
            CHECK(p.copy.x0 - p.cancel.x1 >= tok::kHitGap * win.u - 0.01f);
            // The words stack in order.
            CHECK(p.avatar.y1 <= p.title.y0);
            CHECK(p.title.y1 <= p.detail.y0);
            CHECK(p.detail.y1 <= p.cancel.y0);
            // The text width is the card's inside.
            CHECK(configs_confirm_text_w(win.w, win.u) <= p.card.w() + 0.01f);
        }
    }
}

TEST_CASE("the copy question follows the popup rule: the sizes step up from 1366 points wide") {
    const ConfigsConfirmPlan small = plan_configs_confirm(800.0f, 480.0f, 1.0f, 1, 2);
    const ConfigsConfirmPlan wide = plan_configs_confirm(1366.0f, 768.0f, 1.0f, 1, 2);
    CHECK(small.step == 0);
    CHECK(wide.step == 1);
    CHECK(small.title_pt == tok::kTitle);  // step 0 keeps its sizes
    CHECK(wide.title_pt == tok::kDisplay); // a title is kDisplay from step 1
    CHECK(wide.detail_pt == tok::kBody);
    CHECK(wide.copy.h() > small.copy.h()); // the buttons are popup_grow taller
    CHECK(wide.card.w() > small.card.w()); // and the card wider (popup_width)
    CHECK(small.face_h == 0.0f);
}

TEST_CASE(
    "the three buttons fit under a card at every window size and UI scale, 12 mm and 2 mm apart") {
    for (const Window& win : kWindows) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            float u = 1.0f;
            const PageArea area = page_in(win, &u, scale);
            const ConfigsCards c = configs_cards(area.w, u);
            CAPTURE(win.name);
            CAPTURE(scale);
            CAPTURE(c.w);
            CAPTURE(c.btns_w);
            // The three always fit under a card: across when the page is wide enough, else stacked.
            CHECK(c.buttons_fit);
            if (area.w >= 3.0f * c.btns_w + 2.0f * c.gap) {
                CHECK(c.columns == 3);
            } else {
                CHECK(c.columns == 1);
                CHECK(c.w == doctest::Approx(area.w).epsilon(0.01));
            }
            for (int i = 0; i < kConfigButtons; ++i) {
                CHECK(c.btn >= tok::kHit * u - 0.01f); // a hit square, 12 mm both ways
                CHECK(c.button_x(i) >= -0.01f);
                CHECK(c.button_x(i) + c.btn <= c.w + 0.01f);
                if (i > 0) { // a dead gap from the one before
                    CHECK(c.button_x(i) - (c.button_x(i - 1) + c.btn) >= tok::kHitGap * u - 0.01f);
                }
            }
            // And a dead gap from the card's own tap area, which they never overlap: under it, or
            // (stacked cards) beside it.
            if (c.beside) {
                CHECK(c.button_x(0) - c.tap_w >= tok::kHitGap * u - 0.01f);
                CHECK(c.btns_y >= -0.01f);
            } else {
                CHECK(c.btns_y - c.tap_h >= tok::kHitGap * u - 0.01f);
            }
            CHECK(c.btns_y + c.btn <= c.h + 0.01f);
            CHECK(c.tap_w <= c.w + 0.01f);
            CHECK(c.tap_w >= tok::kHit * u);
        }
    }
}

TEST_CASE("the cards' tap area is a hit area of its own, above the buttons") {
    const ConfigsCards c = configs_cards(826.0f, 1.0f);
    CHECK(c.tap_h >= tok::kHit);
    CHECK(c.w >= tok::kHit);
    CHECK(c.h == doctest::Approx(c.tap_h + c.gap + c.btn)); // three across: the buttons under it
}

namespace {

// `words` broken as the page breaks them: at spaces, by the width they measure at the real font
// (regular, `pt` design points times `u`) in `room` points.
std::vector<std::string> broken(const char* words, float pt, float u, float room) {
    return wrap_report(words, room, [&](const std::string& line) {
        return etude::test::text_em(false, line) * pt * u;
    });
}

} // namespace

TEST_CASE(
    "an empty card's words and the Reset all row's words are not cut at 800x480 and 1366x768") {
    for (const Window& win : kWindows) {
        for (const float scale : {1.0f, 1.5f}) {
            float u = 1.0f;
            const PageArea area = page_in(win, &u, scale);
            const ConfigsCards c = configs_cards(area.w, u);
            CAPTURE(win.name);
            CAPTURE(scale);
            // "Save current" in the preview of an empty card, at the body size.
            const float room = configs_empty_words_room(c, u);
            const float need = etude::test::text_em(false, "Save current") * tok::kBody * u;
            const std::vector<std::string> card = broken("Save current", tok::kBody, u, room);
            // Whatever the lines, none is wider than the room: nothing needs cutting.
            for (const std::string& line : card) {
                CHECK(etude::test::text_em(false, line) * tok::kBody * u <= room);
            }
            // The layout follows the measurement: one line where the words fit, more where not.
            CHECK((need <= room) == (card.size() == 1));
            // The Reset all row, in both its words, at the label size.
            for (const char* words : {"Reset all to default settings", "Tap again to reset"}) {
                const float row_room = configs_reset_words_room(area.w, u);
                const std::vector<std::string> lines = broken(words, tok::kLabel, u, row_room);
                for (const std::string& line : lines) {
                    CHECK(etude::test::text_em(false, line) * tok::kLabel * u <= row_room);
                }
                const float one = etude::test::text_em(false, words) * tok::kLabel * u;
                CHECK((one <= row_room) == (lines.size() == 1));
                // The row is as tall as its lines need, never under a hit.
                CHECK(configs_reset_row_h(static_cast<int>(lines.size()), u) >= tok::kHit * u);
            }
        }
    }
}

TEST_CASE("at 800x480 and 1366x768 the words fit on one line, and a narrow room puts them on two") {
    for (const Window& win : kWindows) {
        float u = 1.0f;
        const PageArea area = page_in(win, &u);
        const ConfigsCards c = configs_cards(area.w, u);
        CAPTURE(win.name);
        CHECK(broken("Save current", tok::kBody, u, configs_empty_words_room(c, u)).size() == 1);
        CHECK(broken("Reset all to default settings", tok::kLabel, u,
                     configs_reset_words_room(area.w, u))
                  .size() == 1);
    }
    // A room narrower than the words: they go on two lines, none cut.
    const float one = etude::test::text_em(false, "Save current") * tok::kBody;
    const std::vector<std::string> two = broken("Save current", tok::kBody, 1.0f, one * 0.8f);
    CHECK(two.size() == 2);
    // Two lines: the row is exactly two lines and its pads tall, one line stays a hit.
    CHECK(configs_reset_row_h(2, 1.0f) ==
          doctest::Approx(2.0f * configs_words_line_h(1.0f) + 2.0f * tok::kS2));
    CHECK(configs_reset_row_h(1, 1.0f) == doctest::Approx(tok::kHit));
    CHECK(configs_reset_row_h(2, 1.0f) > configs_reset_row_h(1, 1.0f));
}

TEST_CASE(
    "a card name of 48 bytes is broken by its measured width, never cut, and the card holds it") {
    const std::string name = "Wolfgang Amadeus Mozart's Practice Config Number";
    REQUIRE(name.size() == 48);
    for (const Window& win : kWindows) {
        for (const bool bold : {false, true}) {
            float u = 1.0f;
            const PageArea area = page_in(win, &u);
            const ConfigsCards one = configs_cards(area.w, u);
            const float room = configs_name_room(one);
            CAPTURE(win.name);
            CAPTURE(bold);
            const std::vector<std::string> lines =
                wrap_report(name, room, [&](const std::string& line) {
                    return etude::test::text_em(bold, line) * tok::kBody * u;
                });
            // Nothing is cut: the lines hold every character, none wider than the room.
            std::string joined;
            for (const std::string& line : lines) {
                CHECK(etude::test::text_em(bold, line) * tok::kBody * u <= room + 0.01f);
                joined += (joined.empty() ? "" : " ") + line;
            }
            CHECK(joined == name);
            // The card's name line holds all of them, and the tap area grows with it.
            const int n = static_cast<int>(lines.size());
            const ConfigsCards c = configs_cards(area.w, u, n);
            CHECK(c.name_h >= static_cast<float>(n) * configs_words_line_h(u) - 0.01f);
            CHECK(c.tap_h >= c.pad + c.preview_h + c.name_h);
            CHECK(c.h >= c.tap_h);
            // A name that fits a line costs no more than the first line.
            CHECK(configs_name_h(1, u) == doctest::Approx(configs_tok::kNameH * u));
            CHECK(configs_name_h(2, u) ==
                  doctest::Approx(configs_tok::kNameH * u + configs_words_line_h(u)));
        }
    }
}

TEST_CASE("the line that says there is no one to copy from is broken by its measured width") {
    for (const Window& win : kWindows) {
        float u = 1.0f;
        const PageArea area = page_in(win, &u);
        CAPTURE(win.name);
        const float room = configs_note_room(area.w, u);
        const auto width = [&](const std::string& line) {
            return etude::test::text_em(false, line) * tok::kBody * u;
        };
        const std::vector<std::string> lines = wrap_report(kConfigNoOtherProfile, room, width);
        for (const std::string& line : lines) {
            CHECK(width(line) <= room + 0.01f);
        }
        CHECK((width(kConfigNoOtherProfile) <= room) == (lines.size() == 1));
    }
    // In a room too narrow for it, it takes two lines.
    const auto width = [](const std::string& line) {
        return etude::test::text_em(false, line) * tok::kBody;
    };
    CHECK(wrap_report(kConfigNoOtherProfile, width(kConfigNoOtherProfile) * 0.6f, width).size() >=
          2);
}
