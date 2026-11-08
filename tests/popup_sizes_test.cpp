// SPDX-License-Identifier: GPL-3.0-or-later
// The one rule for a popup or message drawn over the app (tok::popup_type, popup_grow, popup_width,
// popup_line_w, the info bubble's step): the 800x480 panel is never changed, the wide steps take the
// crash notice's sizes, a card's width and a line's length stay under their caps, and the info
// bubble inside the Settings sheet keeps its old size. The two cards laid out by pure plans (the
// octave-shift offer and the delete-profile confirm) keep their touch rules at every step.
#include <doctest/doctest.h>

#include "ui/crash_layout.hpp"
#include "ui/osk_layout.hpp"
#include "ui/profile_layout.hpp"
#include "ui/transpose_card.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;
using tok::PopupText;

namespace {
constexpr float kEps = 1e-3f;
constexpr std::array<PopupText, 4> kRoles{PopupText::Title, PopupText::Text, PopupText::Detail,
                                          PopupText::Button};
} // namespace

TEST_CASE("popup rule: step 0 keeps every size, width and button as the popup draws them today") {
    for (const float pt : {tok::kCaption, tok::kLabel, tok::kBody, tok::kTitle, 18.0f}) {
        for (const PopupText role : kRoles) {
            CHECK(tok::popup_type(pt, 0, role) == pt);
        }
    }
    CHECK(tok::popup_grow(0) == 1.0f);
    for (const float base : {260.0f, 344.0f, 420.0f, 480.0f, 780.0f}) {
        CHECK(tok::popup_width(base, 0, 800.0f) == base);
    }
}

TEST_CASE("popup rule: the wide steps take the crash notice's sizes, step 2 the same design steps") {
    for (const int step : {1, 2}) {
        CAPTURE(step);
        const CrashNoticeSizes crash = crash_notice_sizes(1);
        CHECK(tok::popup_type(tok::kBody, step, PopupText::Title) == crash.title_pt);   // Display
        CHECK(tok::popup_type(tok::kLabel, step, PopupText::Text) == crash.detail_pt);  // Title
        CHECK(tok::popup_type(tok::kLabel, step, PopupText::Button) == crash.button_pt); // Body
        CHECK(tok::popup_type(tok::kLabel, step, PopupText::Detail) == tok::kBody);
        CHECK(tok::popup_grow(step) == crash.grow);
    }
    // Never smaller than the size a popup already has.
    CHECK(tok::popup_type(tok::kDisplay, 1, PopupText::Button) == tok::kDisplay);
    CHECK(tok::popup_type(18.0f, 1, PopupText::Text) == tok::kTitle);
    CHECK(tok::popup_type(tok::kTitle, 2, PopupText::Text) == tok::kTitle);
}

TEST_CASE("popup rule: a card grows with the step and stays under its caps") {
    for (const float base : {260.0f, 344.0f, 420.0f, 480.0f}) {
        CAPTURE(base);
        const float one = tok::popup_width(base, 1, 1366.0f);
        const float two = tok::popup_width(base, 2, 1920.0f / 1.4f);
        CHECK(one > base);
        CHECK(two >= one);
        CHECK(one <= tok::kPopupMaxW + kEps);
        CHECK(two <= tok::kPopupMaxW + kEps);
        CHECK(one <= tok::kPopupShare * 1366.0f + kEps);
        CHECK(two <= tok::kPopupShare * 1920.0f / 1.4f + kEps);
    }
    // A card already wider than the cap keeps its own width; a window's share caps a narrow one.
    CHECK(tok::popup_width(780.0f, 2, 1920.0f / 1.4f) == 780.0f);
    CHECK(tok::popup_width(420.0f, 1, 800.0f) == doctest::Approx(0.6f * 800.0f));
    CHECK(tok::popup_width(420.0f, 1, 600.0f) == 420.0f); // the share would be under its own width
}

TEST_CASE("popup rule: a line of reading text is about 60 characters, under the window's share") {
    // At the sentence step an average character is half an em: 60 of them, 600 design points.
    CHECK(tok::popup_line_w(tok::kTitle, 1366.0f) == doctest::Approx(600.0f));
    CHECK(tok::popup_line_w(tok::kTitle, 1920.0f / 1.4f) == doctest::Approx(600.0f));
    CHECK(tok::popup_line_w(tok::kTitle, 800.0f) == doctest::Approx(0.6f * 800.0f));
    CHECK(tok::popup_line_w(tok::kDisplay, 3840.0f) == tok::kPopupMaxW);
}

TEST_CASE("info bubble: inside the Settings sheet it keeps its old size at every window width") {
    for (const float window_w : {800.0f, 1366.0f, 1600.0f, 1920.0f, 3840.0f}) {
        CAPTURE(window_w);
        const int step = tok::info_bubble_step(tok::wide_step(window_w), true);
        CHECK(step == 0);
        CHECK(tok::info_bubble_text(step) == tok::kLabel);
        CHECK(tok::info_bubble_wrap(step, window_w) == tok::kInfoBubbleW);
    }
    // Elsewhere it takes the window's step: unchanged at 800, the sentence step and a longer line
    // from 1366.
    CHECK(tok::info_bubble_step(tok::wide_step(800.0f), false) == 0);
    for (const float window_w : {1366.0f, 1920.0f}) {
        const int step = tok::info_bubble_step(tok::wide_step(window_w), false);
        CHECK(step > 0);
        CHECK(tok::info_bubble_text(step) == tok::kTitle);
        CHECK(tok::info_bubble_wrap(step, window_w) > tok::kInfoBubbleW);
    }
}

TEST_CASE("offer card: a wider step is a wider card with taller rows, inside the window") {
    for (const float display_w : {1366.0f, 1920.0f}) {
        const int step = tok::wide_step(display_w);
        const float u = display_w >= 1920.0f ? 1.4f : 1.0f;
        const TransposeCardPlan old = plan_offer_card(display_w, 100.0f, 700.0f, u, 1, false);
        const TransposeCardPlan wide = plan_offer_card(display_w, 100.0f, 700.0f, u, 1, false, step);
        CAPTURE(display_w);
        CHECK(wide.w > old.w);
        CHECK(wide.w <= display_w - tok::kS5 * u + kEps);
        CHECK(wide.h > old.h);
        CHECK(wide.fits());
    }
    const TransposeCardPlan small = plan_offer_card(800.0f, 100.0f, 400.0f, 1.0f, 1, false, 0);
    CHECK(small.w == plan_offer_card(800.0f, 100.0f, 400.0f, 1.0f, 1, false).w);
}

namespace {

struct Window {
    float w;
    float h;
    float panel_w_mm; // the panel's size: 154x86 mm is the 7 inch one, 344x194 mm the 15.6 inch one
    float panel_h_mm;
    // The unit the app draws with: the density is the mean of the two axes' pixels per millimetre.
    float u() const { return etude::ui_scale(w, h, (w / panel_w_mm + h / panel_h_mm) * 0.5f); }
};
constexpr std::array<Window, 3> kPanels{{{800.0f, 480.0f, 154.0f, 86.0f}, {1366.0f, 768.0f, 344.0f, 194.0f}, {1920.0f, 1080.0f, 344.0f, 194.0f}}};

} // namespace

TEST_CASE("profile delete confirm: on the real panels the controls are 12 mm apart, 2 mm from the keys "
          "and above the keyboard") {
    for (const Window& win : kPanels) {
        const float u = win.u();
        const Box display{0.0f, 0.0f, win.w, win.h};
        const float kb = profile_keyboard_height(display.x1, display.y1, u);
        for (const bool keys : {false, true}) {
            // A tight card (over the keyboard) has one heading line, a long name giving way to a
            // generic heading; the whole card takes a second line for a long name.
            const bool tight = plan_profile_confirm(60.0f * u, tok::kHit * u, display, u,
                                                    keys ? display.y1 - kb : display.y1)
                                   .form == ConfirmForm::Tight;
            for (const int title_lines : {1, 2}) {
                if (tight && title_lines == 2) {
                    continue;
                }
                CAPTURE(win.w);
                CAPTURE(keys);
                CAPTURE(title_lines);
                const float keys_top = keys ? display.y1 - kb : display.y1;
                const ProfileConfirmPlan p =
                    plan_profile_confirm(60.0f * u, tok::kHit * u, display, u, keys_top, title_lines);
                const float hit = tok::kHit * u * tok::popup_grow(p.step);
                CHECK(p.step == tok::wide_step(win.w));
                CHECK(p.cancel.h() >= hit - kEps);
                CHECK(p.remove.h() >= hit - kEps);
                CHECK(p.field.h() >= tok::kHit * u - kEps);
                CHECK(p.remove.x0 - p.cancel.x1 >= 3.0f * tok::kHit * u);
                CHECK(display.holds(p.card, -kEps));
                CHECK(p.card.holds(p.cancel, -kEps));
                CHECK(p.card.holds(p.remove, -kEps));
                CHECK(p.card.holds(p.field, -kEps));
                if (keys) {
                    // The first row of keys: its hit areas start a pad and half a dead gap under the
                    // keyboard's top; the card's controls keep the 2 mm hit gap from them.
                    const float key_top = keys_top + (osk_tok::kKeyPad + osk_tok::kKeyGap * 0.5f) * u;
                    for (const Box& b : {p.field, p.cancel, p.remove}) {
                        CHECK(b.y1 + tok::kHitGap * u <= key_top + kEps);
                    }
                }
                if (p.form == ConfirmForm::Roomy) {
                    CHECK(p.title.h() >= static_cast<float>(title_lines) * profile_confirm_title(p.step) *
                                             profile_tok::kConfirmLineRatio * u - kEps);
                    CHECK(p.card.holds(p.title, -kEps));
                }
            }
        }
    }
    CHECK(profile_confirm_text(0) == profile_tok::kConfirmText);
    CHECK(profile_confirm_text(1) == tok::kTitle);
    // The keyboard reserves room for the confirm's own line height at every step.
    CHECK(tight_confirm_height(1.0f, 1) > tight_confirm_height(1.0f, 0));
}

TEST_CASE("profile delete confirm: at 800x480 with the keyboard up the whole card is on screen, above the keys "
          "with 2 mm, and it keeps the heading, the warning, the field and both buttons") {
    for (const Window& win : kPanels) {
        const float u = win.u();
        const Box display{0.0f, 0.0f, win.w, win.h};
        const float top_row = 82.0f; // about where the status row ends on these windows
        const float keys_top = display.y1 - profile_keyboard_height(display.x1, display.y1, u);
        // The warning's width: 21 characters of bold words, about 0.55 em each.
        const int step = tok::wide_step(win.w);
        const float warning_w = 21.0f * 0.55f * profile_confirm_text(step) * u;
        const ProfileConfirmPlan p =
            plan_profile_confirm(60.0f * u, top_row, display, u, keys_top, 1, warning_w);
        CAPTURE(win.w);
        CHECK(p.card.y0 >= -kEps);          // not off the top of the screen
        CHECK(p.card.y1 <= keys_top + kEps); // and not into the keys: their own pad keeps 2 mm
        const float key_top = keys_top + (osk_tok::kKeyPad + osk_tok::kKeyGap * 0.5f) * u;
        for (const Box& b : {p.field, p.cancel, p.remove}) {
            CHECK(b.y1 + tok::kHitGap * u <= key_top + kEps);
            CHECK(b.h() >= tok::kHit * u - kEps);
        }
        // Every part is there, inside the card: the heading, the warning, (the description), the row.
        CHECK(p.card.holds(p.title, -kEps));
        REQUIRE(p.lines >= 1);
        const Box& warning = p.line[static_cast<std::size_t>(p.lines - 1)];
        CHECK(p.card.holds(warning, -kEps));
        CHECK(warning.y1 <= p.field.y0 + kEps);
        CHECK(p.title.y1 <= p.field.y0 + kEps);
        CHECK(p.title_pt >= tok::kBody); // the heading never goes below the body step
        if (win.w < 1000.0f) {
            // The 7 inch panel: the card reaches over the top row, so the description goes first and
            // the heading keeps its step if it fits.
            CHECK(p.form == ConfirmForm::Tight);
            CHECK(p.lines == 1);
            CHECK(p.card.y0 < top_row);
            // The heading at the left of the same row as the warning at the right, side by side:
            // with that room it keeps its usual step.
            CHECK(p.title.y0 == doctest::Approx(warning.y0));
            CHECK(p.title.y1 == doctest::Approx(warning.y1));
            CHECK(p.title.x1 <= warning.x0 + kEps);
            CHECK(p.title.x0 < warning.x0);
            CHECK(warning.x1 == doctest::Approx(p.card.x1 - tok::kS2 * u));
            CHECK(p.title_pt == profile_confirm_title(p.step));
        } else {
            // 1366x768 and 1920x1080 with the keyboard up: the whole card, under the top row; the
            // heading is over the warning, as it always was.
            CHECK(p.form == ConfirmForm::Roomy);
            CHECK(p.title.y1 <= warning.y0 + kEps);
            CHECK(p.lines == profile_tok::kConfirmLines);
            CHECK(p.card.y0 >= top_row - kEps);
        }
    }
}

namespace {

std::string read_source(const char* name) {
    std::ifstream in(std::string(ETUDE_SOURCE_DIR) + "/app/ui/" + name, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

int count_of(const std::string& text, const std::string& what) {
    int n = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("info bubble: the Settings sheet's pages are drawn inside the guard that keeps the bubble's "
          "old size, and the bubble reads it") {
    // Every call of the page drawing in the sheet has `InSettings` in scope: one guard before each.
    const std::string sheet = read_source("sheet.cpp");
    REQUIRE_FALSE(sheet.empty());
    CHECK(count_of(sheet, "draw_page(k, ") == 2);
    CHECK(count_of(sheet, "const InSettings in_settings(k);") == 2);
    std::size_t at = 0;
    for (int i = 0; i < 2; ++i) {
        const std::size_t guard = sheet.find("const InSettings in_settings(k);", at);
        const std::size_t call = sheet.find("draw_page(k, ", guard);
        REQUIRE(guard != std::string::npos);
        REQUIRE(call != std::string::npos);
        CHECK(call - guard < 120); // the guard is the line before the call
        at = call + 1;
    }
    // The button records where it was opened, and the bubble takes its step from that.
    CHECK(read_source("kit_internal.cpp").find("k.info_in_settings = k.in_settings;") != std::string::npos);
    CHECK(read_source("kit_toast.cpp").find("tok::info_bubble_step(step, k.info_in_settings)") !=
          std::string::npos);
}

namespace {

// A stand-in font: every character `em` wide, so a long name's lines can be counted without ImGui.
float fixed_width(const std::string& line, float em) { return static_cast<float>(line.size()) * em; }

std::string without_spaces(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
    return text;
}

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        out += out.empty() ? line : " " + line;
    }
    return out;
}

} // namespace

TEST_CASE("long names: a 32 byte profile name's heading and a 38 character keyboard name wrap, whole") {
    const std::string heading = "Delete " + std::string(32, 'W') + "?";
    const std::string keyboard = "Yamaha PSR-E383 Digital Keyboard 61";
    for (const Window& win : kPanels) {
        const float u = win.u();
        const Box display{0.0f, 0.0f, win.w, win.h};
        const ProfileConfirmPlan p = plan_profile_confirm(60.0f * u, tok::kHit * u, display, u, display.y1);
        REQUIRE(p.form == ConfirmForm::Roomy);
        // The heading: capital letters about 0.7 em wide at the heading's size.
        const float em = profile_confirm_title(p.step) * 0.7f * u;
        const auto lines = wrap_report(heading, p.title.w(), [&](const std::string& l) { return fixed_width(l, em); });
        CAPTURE(win.w);
        CHECK(lines.size() <= 3); // a name of capitals as wide as they come, on the narrowest card
        CHECK(without_spaces(joined(lines)) == without_spaces(heading)); // nothing cut, no ellipsis
        for (const std::string& line : lines) {
            CHECK(fixed_width(line, em) <= p.title.w() + kEps);
        }
        // The check card's name beside its badge and two buttons, at the title size: two lines at most.
        const int step = tok::wide_step(win.w);
        const float card_w = tok::popup_width(tok::kDeviceCheckW, step, win.w / u) * u;
        const float hit = tok::kHit * tok::popup_grow(step) * u;
        const float room = card_w - 2.0f * tok::kS3 * u - 2.0f * (hit + tok::kS2 * u) - (hit + tok::kS2 * u);
        const float name_em = tok::popup_type(tok::kBody, step, tok::PopupText::Title) * 0.55f * u;
        const auto name = wrap_report(keyboard, room, [&](const std::string& l) { return fixed_width(l, name_em); });
        CHECK(name.size() <= 2);
        CHECK(joined(name) == keyboard);
    }
}
