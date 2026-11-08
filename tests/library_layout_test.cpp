// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/library_layout.hpp"
#include "ui/osk_focus.hpp"
#include "ui/osk_layout.hpp"
#include "ui/osk_text.hpp"
#include "ui/tempo_entry.hpp"

#include "etude/look.hpp"
#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <array>
#include <optional>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;

namespace {

struct Size {
    float w;
    float h;
};

constexpr std::array<Size, 5> kWindows{
    {{800, 480}, {1024, 600}, {1280, 800}, {1366, 768}, {1920, 1080}}};
constexpr std::array<float, 3> kScales{1.0f, 1.25f, 1.5f};
constexpr float kEps = 1e-3f;

// The screen under a top row as tall as a hit.
Box area_for(Size s, float u) { return {0.0f, tok::kHit * u, s.w, s.h}; }

bool inside(const Box& inner, const Box& outer) {
    return inner.x0 >= outer.x0 - kEps && inner.y0 >= outer.y0 - kEps &&
           inner.x1 <= outer.x1 + kEps && inner.y1 <= outer.y1 + kEps;
}

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

} // namespace

TEST_CASE("library frame: the parts stack top to bottom inside the screen, at every size") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            const Box area = area_for(s, u);
            for (const float kb : {0.0f, 0.5f, 1.0f}) {
                const LibraryFrame f = library_frame(area, u, kb);
                CHECK(inside(f.search, area));
                CHECK(inside(f.view, area));
                CHECK(inside(f.chips, area));
                CHECK(inside(f.results, area));
                CHECK(inside(f.keyboard, area));
                CHECK(inside(f.select, area));
                CHECK_FALSE(overlap(f.search, f.view));
                CHECK_FALSE(overlap(f.search, f.select));
                CHECK_FALSE(overlap(f.select, f.view));
                CHECK(inside(f.get, area));
                CHECK_FALSE(overlap(f.search, f.get));
                CHECK_FALSE(overlap(f.get, f.select));
                CHECK(f.search.w() >= tok::kHit * u * 4.0f); // still a field to type in
                CHECK(f.search.h() >= tok::kHit * u - kEps);
                if (!f.strip) {
                    CHECK(f.get.w() >= tok::kHit * u - kEps);
                    CHECK(f.select.w() >= tok::kHit * u - kEps);
                    CHECK(f.view.w() >= tok::kHit * u - kEps);
                    CHECK(f.results.y0 >= f.search.y1 - kEps);
                    CHECK(f.results.y0 >= f.chips.y1 - kEps);
                } else {
                    // The results stand beside the field, a dead gap from it.
                    CHECK_FALSE(overlap(f.search, f.results));
                    CHECK(f.results.x0 >= f.search.x1 + tok::kHitGap * u - kEps);
                }
                // The keys keep their least size (a hit area and a gap) first, so on a short window
                // the keyboard may take everything under the search row and leave the results
                // nothing; where it fits, the results end where it begins.
                if (f.results.h() > 0.0f) {
                    CHECK(f.results.y1 <= f.keyboard.y0 + kEps);
                }
                CHECK(f.results.h() >= 0.0f);
                CHECK(f.keyboard.h() == doctest::Approx(f.keyboard_full * kb));
            }
            // With the keyboard down the chips are always there.
            CHECK(library_frame(area, u, 0.0f).chips.h() >= tok::kHit * u - kEps);
        }
    }
}

TEST_CASE("library frame: the keyboard always leaves at least one result row, a hit tall") {
    // At the scales a person picks for a screen this size; at 1.5 on 800x480 the keys keep their
    // least size first and the results get what is left.
    for (const Size s : kWindows) {
        for (const float u : {1.0f, 1.25f}) {
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            const LibraryFrame f = library_frame(area_for(s, u), u, 1.0f);
            // A hit tall at the least (a row of results a finger can tap), and where the screen is
            // tall enough, part of a row of cards.
            CHECK(f.results.h() >= tok::kHit * u - kEps);
            if (!f.strip) {
                CHECK(f.results.h() >= lib_tok::kCardH * lib_tok::kLeastResultsShare * u - kEps);
            }
        }
    }
}

TEST_CASE("library frame: on a short screen the chips step aside while the keyboard is up") {
    const float u = 1.0f;
    const LibraryFrame low = library_frame(area_for({800, 480}, u), u, 1.0f);
    CHECK(low.chips.h() == 0.0f);
    const LibraryFrame tall = library_frame(area_for({1920, 1080}, u), u, 1.0f);
    CHECK(tall.chips.h() >= tok::kHit * u - kEps);
}

TEST_CASE("library frame: a row held for the join bar stays on a short screen with the keyboard up") {
    const float u = 1.0f;
    const Box area = area_for({800, 480}, u);
    const LibraryFrame held = library_frame(area, u, 1.0f, true);
    CHECK(held.chips.h() >= tok::kHit * u - kEps);
    CHECK(held.results.y0 >= held.chips.y1 - kEps);
    CHECK(held.results.y1 <= held.keyboard.y0 + kEps);
    CHECK(held.results.h() >= 0.0f);
}

TEST_CASE("on-screen keyboard: every key is a full hit, inside the keyboard, none overlapping") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            const LibraryFrame f = library_frame(area_for(s, u), u, 1.0f);
            for (const Layer layer : {Layer::Letters, Layer::Symbols, Layer::Numbers}) {
                const auto rows = keyboard_rows(layer);
                // The number pad is two rows, so its keyboard is half as tall.
                Box kb = f.keyboard;
                if (layer == Layer::Numbers) {
                    kb.y0 = kb.y1 - keyboard_height(kb.w(), u, 1e9f, 2);
                }
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    float units = 0.0f;
                    for (std::size_t i = 0; i < rows[r].count; ++i) {
                        units += rows[r].keys[i].units;
                        const Box key = keyboard_key(kb, u, layer, r, i);
                        CHECK(key.w() >= tok::kHit * u - kEps);
                        CHECK(key.h() >= tok::kHit * u - kEps);
                        CHECK(inside(key, kb));
                        if (i > 0) {
                            CHECK_FALSE(overlap(key, keyboard_key(kb, u, layer, r, i - 1)));
                        }
                    }
                    CHECK(units <= osk_tok::kKeyUnits + kEps);
                    if (r > 0) {
                        CHECK_FALSE(overlap(keyboard_key(kb, u, layer, r, 0),
                                            keyboard_key(kb, u, layer, r - 1, 0)));
                    }
                }
            }
        }
    }
}

TEST_CASE("on-screen keyboard: the letters and the symbols end in the same bottom row") {
    const auto letters = keyboard_rows(Layer::Letters);
    const auto symbols = keyboard_rows(Layer::Symbols);
    const auto pad = keyboard_rows(Layer::Numbers);
    CHECK(letters.size() == symbols.size());
    CHECK(letters[3].keys == symbols[3].keys);
    CHECK(letters[0].keys[0].text == std::string_view("q"));
    CHECK(symbols[0].keys[0].text == std::string_view("1"));
    CHECK(letters[2].keys[letters[2].count - 1].kind == KeyKind::Backspace);
    CHECK(letters[3].keys[3].kind == KeyKind::Done);
    // The number pad: the digits, then Backspace, Clear and Done; no key types a non-digit.
    REQUIRE(pad.size() == 2);
    CHECK(pad[0].count == 10);
    for (std::size_t r = 0; r < pad.size(); ++r) {
        for (std::size_t i = 0; i < pad[r].count; ++i) {
            const KeySpec& key = pad[r].keys[i];
            if (key.kind == KeyKind::Letter) {
                REQUIRE(key.text != nullptr);
                CHECK(key.text[0] >= '0');
                CHECK(key.text[0] <= '9');
            }
        }
    }
    CHECK(pad[1].keys[pad[1].count - 1].kind == KeyKind::Done);
}

TEST_CASE(
    "on-screen keyboard: its height grows with the width, between its least pitch and its tallest") {
    const float u = 1.0f;
    const float pad = 2.0f * osk_tok::kKeyPad;
    const float rows = static_cast<float>(osk_tok::kKeyRows);
    CHECK(keyboard_height(400.0f, u) == doctest::Approx(osk_tok::kKeyPitchMin * rows + pad));
    // The widest screens keep the tallest keys, no taller.
    CHECK(keyboard_height(5000.0f, u) == doctest::Approx(osk_tok::kKeyPitchMax * rows + pad));
    CHECK(keyboard_height(1366.0f, u) > keyboard_height(800.0f, u));
    // Squeezed, it never goes under a hit area and its gap a row.
    CHECK(keyboard_height(1000.0f, u, 10.0f) == doctest::Approx(tok::kCell * rows + pad));
    // The number pad has half the rows.
    CHECK(keyboard_height(1000.0f, u, 1e9f, 2) < keyboard_height(1000.0f, u));
}

TEST_CASE("name card: never under the keyboard, whole inside the screen, at every size") {
    // The dialog's real area: the window under the top row (the status text is drawn over every
    // window), the keyboard as `name_keyboard_height` makes it, the card allowed to reach over the
    // top row as a last resort. Sizes past the audited ones too.
    std::array<Size, 8> sizes{{{800, 480},
                               {1024, 600},
                               {1280, 800},
                               {1366, 768},
                               {1920, 1080},
                               {640, 360},
                               {800, 400},
                               {1366, 600}}};
    for (const Size s : sizes) {
        for (const float u : {0.75f, 1.0f, 1.25f, 1.5f, 1.6f}) {
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            etude::PinnedBar pb;
            const etude::ScreenLayout lay = etude::compute_layout(
                s.w, s.h, etude::kDefaultPxPerMm * u, etude::LookSettings{}, pb);
            const float top = lay.top_row.bottom();
            const Box area{0.0f, top, s.w, s.h};
            const float full = name_keyboard_height(s.w, s.h, u);
            // The keys keep a hit a row where the window has room for them and the one-row card
            // (272 design points), and give way past their least size where it has not.
            const float least_keys = tok::kHit * 4.0f * u + 2.0f * osk_tok::kKeyPad * u;
            if (s.h >= 272.0f * u) {
                CHECK(full >= least_keys - kEps);
            } else {
                CHECK(full >= 2.0f * tok::kHit * u - kEps);
            }
            for (const bool keys : {false, true}) {
                for (const int quick : {0, 6}) {
                    for (const bool own : {false, true}) {
                        for (const bool top_anchor : {false, true}) {
                            CAPTURE(keys);
                            CAPTURE(quick);
                            NameCardSpec spec;
                            spec.quick = quick;
                            spec.own = own;
                            spec.top = top_anchor;
                            spec.above = top;
                            const float keys_top = keys ? s.h - full : s.h;
                            const NameCardPlan p =
                                plan_name_card(area, u, keys_top, spec, 60.0f * u);
                            CHECK(inside(p.card, Box{0.0f, 0.0f, s.w, s.h}));
                            // Save is never under the keys, and the card is never above the window.
                            CHECK(p.card.y1 <= keys_top + kEps);
                            CHECK(p.save.y1 <= keys_top + kEps);
                            CHECK(p.card.y0 >= -kEps);
                            CHECK(inside(p.field, p.card));
                            CHECK(inside(p.save, p.card));
                            CHECK(inside(p.cancel, p.card));
                            CHECK(p.field.h() >= tok::kHit * u - kEps);
                            CHECK(p.save.h() >= tok::kHit * u - kEps);
                            CHECK(p.cancel.h() >= tok::kHit * u - kEps);
                            CHECK(p.field.w() > 0.0f);
                            // Nothing in the card overlaps.
                            CHECK_FALSE(overlap(p.field, p.save));
                            CHECK_FALSE(overlap(p.field, p.cancel));
                            CHECK_FALSE(overlap(p.save, p.cancel));
                            if (p.quick) {
                                CHECK(inside(p.chips, p.card));
                                CHECK_FALSE(overlap(p.chips, p.field));
                                CHECK_FALSE(overlap(p.chips, p.save));
                            }
                            if (!p.compact && own) {
                                CHECK(inside(p.own, p.card));
                                CHECK_FALSE(overlap(p.own, p.cancel));
                                CHECK_FALSE(overlap(p.own, p.save));
                            }
                            // Over the top row only as the one-row card, and only where the room
                            // below it is short.
                            if (p.card.y0 < top - kEps) {
                                CHECK(p.compact);
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("name card: with the keyboard down the one-tap names show where there is room") {
    NameCardSpec spec;
    spec.quick = 6;
    const Box area{0.0f, 0.0f, 1366.0f, 768.0f};
    const NameCardPlan roomy = plan_name_card(area, 1.0f, area.y1, spec, 60.0f);
    CHECK(roomy.quick);
    CHECK(roomy.chip_rows == 2);
    CHECK_FALSE(roomy.compact);
    // Centred about 42% down.
    CHECK(roomy.card.cy() == doctest::Approx(area.h() * 0.42f).epsilon(0.1));
    // Every one-tap name is a full hit and none overlaps another.
    for (std::size_t i = 0; i < 6; ++i) {
        const Box cell = quick_name_cell(roomy.chips, 1.0f, roomy.chip_gap, i);
        CHECK(cell.h() >= tok::kHit - kEps);
        CHECK(cell.w() >= tok::kHit - kEps);
        CHECK(inside(cell, roomy.chips));
        for (std::size_t j = 0; j < i; ++j) {
            CHECK_FALSE(overlap(cell, quick_name_cell(roomy.chips, 1.0f, roomy.chip_gap, j)));
        }
    }
}

TEST_CASE("name card: the smallest window, no keyboard, still shows the one-tap names, tight") {
    NameCardSpec spec;
    spec.quick = 6;
    spec.own = true;
    // 800x480 at 1.5 times, under a top row 90 pixels tall: the roomy card does not fit.
    const float u = 1.5f;
    const Box area{0.0f, 90.0f, 800.0f, 480.0f};
    const NameCardPlan p = plan_name_card(area, u, area.y1, spec, 60.0f * u);
    CHECK(p.quick);
    CHECK(p.tight);
    CHECK_FALSE(p.compact);
    CHECK(inside(p.card, area));
    // Tight: less padding, but the names' rows keep the dead gap, and every name is a full hit.
    CHECK(p.chip_gap == doctest::Approx(tok::kHitGap * u));
    for (std::size_t i = 0; i < 6; ++i) {
        const Box cell = quick_name_cell(p.chips, u, p.chip_gap, i);
        CHECK(cell.h() >= tok::kHit * u - kEps);
        CHECK(inside(cell, p.chips));
    }
}

TEST_CASE(
    "name card: on a short screen with the keyboard up the names go, then the card is one row") {
    NameCardSpec spec;
    spec.quick = 6;
    // 1366x768 at the keyboard's tallest: the card stays whole, names and all.
    const Box big{0.0f, 0.0f, 1366.0f, 768.0f};
    const float big_kb = keyboard_height(big.w(), 1.0f, 1e9f);
    const NameCardPlan whole = plan_name_card(big, 1.0f, big.y1 - big_kb, spec, 60.0f);
    CHECK_FALSE(whole.compact);
    CHECK(whole.quick);
    // 800x480 at 1.5 times: no room for either.
    const float u = 1.5f;
    const Box small{0.0f, 0.0f, 800.0f, 480.0f};
    const float least_card = (tok::kHit + 2.0f * tok::kS2 + tok::kS4 + tok::kS2) * u;
    const float kb = keyboard_height(small.w(), u, small.h() - least_card);
    const NameCardPlan one_row = plan_name_card(small, u, small.y1 - kb, spec, 60.0f * u);
    CHECK(one_row.compact);
    CHECK_FALSE(one_row.quick);
    CHECK(one_row.card.y1 <= small.y1 - kb + kEps);
    CHECK(one_row.card.h() < whole.card.h());
}

TEST_CASE("typing: one space between words, none first, and a character is never split") {
    using namespace etude::app::ui::osk;
    std::string t;
    type_text(t, " Hello  world ", FieldRules{});
    CHECK(t == "Hello world ");
    t.clear();
    // Control characters are left out.
    type_text(t, "a\tb\nc\x7f", FieldRules{});
    CHECK(t == "abc");
    // A multi-byte character goes in whole or not at all.
    FieldRules tight;
    tight.max_bytes = 4;
    t.clear();
    type_text(t, "ab\xC3\xA9\xC3\xA9", tight); // a, b, then two 2-byte letters
    CHECK(t == "ab\xC3\xA9");
    type_text(t, "z", tight);
    CHECK(t == "ab\xC3\xA9"); // 4 bytes already
    // A full field takes nothing more, and a later short character does not slip past a long one.
    t = "abc";
    type_text(t,
              "\xE2\x82\xAC"
              "d",
              tight);
    CHECK(t == "abc");
}

TEST_CASE("typing: a number field takes digits only, and Backspace takes a whole character") {
    using namespace etude::app::ui::osk;
    FieldRules number;
    number.kind = FieldKind::Number;
    number.max_bytes = 3;
    std::string t;
    type_text(t, "1a2 -.3456", number);
    CHECK(t == "123");
    erase_last(t);
    CHECK(t == "12");
    std::string s = "ab\xC3\xA9";
    erase_last(s);
    CHECK(s == "ab");
    s = "\xE2\x82\xAC";
    erase_last(s);
    CHECK(s.empty());
    erase_last(s); // nothing to take
    CHECK(s.empty());
}

TEST_CASE("list row: Play, the heart, the hands and the facts never overlap, the title keeps room") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            CAPTURE(s.w);
            CAPTURE(u);
            const float pad = lib_tok::kSidePad * u;
            const Box row{pad, 100.0f, s.w - pad, 100.0f + lib_tok::kRowH * u};
            const ListRow r = list_row(row, u);
            CHECK(r.play.w() >= tok::kHit * u - kEps);
            CHECK(r.heart.w() >= tok::kHit * u - kEps);
            CHECK(inside(r.play, row));
            CHECK(inside(r.heart, row));
            CHECK_FALSE(overlap(r.play, r.heart));
            CHECK_FALSE(overlap(r.heart, r.hands));
            CHECK_FALSE(overlap(r.hands, r.facts));
            CHECK_FALSE(overlap(r.facts, r.text));
            CHECK_FALSE(overlap(r.hands, r.text));
            CHECK(r.text.w() >= lib_tok::kListLeastTextW * u - kEps);
            // Every window here has room for all of it.
            CHECK(r.facts.w() > 0.0f);
            CHECK(r.hands.w() > 0.0f);
        }
    }
    // A narrow row drops the hands first, then the facts.
    const Box narrow{0.0f, 0.0f, 330.0f, lib_tok::kRowH};
    const ListRow r = list_row(narrow, 1.0f);
    CHECK(r.hands.w() == 0.0f);
    CHECK(r.text.w() >= lib_tok::kListLeastTextW - kEps);
}

TEST_CASE("list columns: two rows side by side where two fit, one where they do not") {
    const auto cols = [](float w, float u) { return list_columns(w, u); };
    // 800x480 at the usual scale has room for two; at scale 1.5 (533 design points wide) it has
    // not.
    CHECK(cols(800.0f, 1.0f).columns == 2);
    CHECK(cols(800.0f, 1.5f).columns == 1);
    CHECK(cols(800.0f, 1.25f).columns == 1);
    // A wider screen at its own scale (1366 wide at 1.0, 1920 wide at the 1.4 it grows to).
    CHECK(cols(1366.0f, 1.0f).columns == 2);
    CHECK(cols(1920.0f, 1.405f).columns == 2);
    // Never more than two, however wide.
    CHECK(cols(4000.0f, 1.0f).columns == lib_tok::kListColumnsMax);
    // The edge: two rows of the least width, the gap and the two pads fit exactly, and a point less
    // does not.
    for (const float u : kScales) {
        const float exact =
            (2.0f * lib_tok::kListRowMinW + lib_tok::kListGap + 2.0f * lib_tok::kSidePad) * u;
        CHECK(cols(exact + kEps, u).columns == 2);
        CHECK(cols(exact - 1.0f, u).columns == 1);
    }
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            CAPTURE(s.w);
            CAPTURE(u);
            const float pad = lib_tok::kSidePad * u;
            const etude::GridColumns g = list_columns(s.w, u);
            CHECK(g.columns <= lib_tok::kListColumnsMax);
            // The rows fill the width between the pads, with the gap between them.
            const float right = g.x(g.columns - 1) + g.card_w;
            CHECK(right == doctest::Approx(s.w - pad).epsilon(1e-4));
            CHECK(g.x(0) == doctest::Approx(pad));
            // A row is never narrower than the least width, except where one row is all there is.
            if (g.columns > 1) {
                CHECK(g.card_w >= lib_tok::kListRowMinW * u - kEps);
                CHECK(g.x(1) >= g.x(0) + g.card_w + lib_tok::kListGap * u - kEps);
            }
        }
    }
}

TEST_CASE("list rows side by side: no part of one reaches into the other") {
    for (const float u : kScales) {
        CAPTURE(u);
        const etude::GridColumns g = list_columns(2000.0f, u);
        REQUIRE(g.columns == 2);
        const float h = lib_tok::kRowH * u;
        const Box left{g.x(0), 100.0f, g.x(0) + g.card_w, 100.0f + h};
        const Box right{g.x(1), 100.0f, g.x(1) + g.card_w, 100.0f + h};
        CHECK_FALSE(overlap(left, right));
        const ListRow a = list_row(left, u);
        const ListRow b = list_row(right, u);
        for (const Box& p : {a.text, a.facts, a.hands, a.heart, a.play}) {
            for (const Box& q : {b.text, b.facts, b.hands, b.heart, b.play}) {
                CHECK_FALSE(overlap(p, q));
            }
        }
    }
}

TEST_CASE("list row at its least width: nothing overlaps, the title keeps room, hits stay apart") {
    for (const float u : kScales) {
        CAPTURE(u);
        const float w = lib_tok::kListRowMinW * u;
        const float pitch = (lib_tok::kRowH + lib_tok::kListGap) * u;
        const Box first{0.0f, 100.0f, w, 100.0f + lib_tok::kRowH * u};
        const Box second{0.0f, first.y0 + pitch, w, first.y1 + pitch};
        const ListRow a = list_row(first, u);
        const ListRow b = list_row(second, u);
        CHECK(a.play.w() >= tok::kHit * u - kEps);
        CHECK(a.play.h() >= tok::kHit * u - kEps);
        CHECK(a.heart.h() >= tok::kHit * u - kEps);
        CHECK(inside(a.play, first));
        CHECK(inside(a.heart, first));
        CHECK_FALSE(overlap(a.play, a.heart));
        CHECK_FALSE(overlap(a.heart, a.hands));
        CHECK_FALSE(overlap(a.hands, a.facts));
        CHECK_FALSE(overlap(a.facts, a.text));
        // One row's buttons never reach into the next row's.
        CHECK_FALSE(overlap(a.play, b.play));
        CHECK_FALSE(overlap(a.heart, b.heart));
        CHECK(a.text.w() >= lib_tok::kListLeastTextW * u - kEps);
        // The facts are kept at the least width; the hands go first.
        CHECK(a.facts.w() > 0.0f);
    }
}

// ---- Versions of one piece (#164)

TEST_CASE("a card with versions is a chips row taller, and the grid's cards are wider while some have them") {
    for (const float u : kScales) {
        CHECK(card_height(u, false) == doctest::Approx(lib_tok::kCardH * u));
        CHECK(card_height(u, true) == doctest::Approx((lib_tok::kCardH + lib_tok::kVersionsH) * u));
        CHECK(card_min_width(u, true) > card_min_width(u, false));
    }
    // The chips row is a hit tall, so its chips keep the least hit.
    CHECK(lib_tok::kVersionsH >= tok::kHit);
    // With the wider cards, three of the kit's least chips fit across a card at every window size
    // the app is made for and the least scale.
    for (const Size s : kWindows) {
        const etude::GridColumns g = etude::grid_columns(
            s.w, card_min_width(1.0f, true), lib_tok::kCardGap, lib_tok::kSidePad);
        CAPTURE(s.w);
        CHECK(g.card_w - 2.0f * tok::kS2 >=
              3.0f * tok::kChipMinW + 2.0f * tok::kS1);
    }
}

TEST_CASE("version chips: all show when they fit, with their scores when those fit too") {
    const std::array<float, 3> names{56.0f, 70.0f, 56.0f};
    const std::array<float, 3> scores{80.0f, 94.0f, 56.0f};
    // 80 + 94 + 56 + 2 gaps of 4 = 238.
    VersionPlan p = plan_version_chips(scores, names, 1, 240.0f, 4.0f, 56.0f, false);
    CHECK(p.scores);
    CHECK(p.count == 3);
    CHECK(p.first == 0);
    CHECK_FALSE(p.more);
    // Exactly at the edge fits; a point less drops the scores, not the chips.
    p = plan_version_chips(scores, names, 1, 238.0f, 4.0f, 56.0f, false);
    CHECK(p.scores);
    p = plan_version_chips(scores, names, 1, 237.0f, 4.0f, 56.0f, false);
    CHECK_FALSE(p.scores);
    CHECK(p.count == 3);
    CHECK_FALSE(p.more);
    // 56 + 70 + 56 + 8 = 190: at 190 the names fit, at 189 they do not.
    p = plan_version_chips(scores, names, 1, 190.0f, 4.0f, 56.0f, false);
    CHECK(p.count == 3);
    CHECK_FALSE(p.more);
    p = plan_version_chips(scores, names, 1, 189.0f, 4.0f, 56.0f, false);
    CHECK(p.more);
    CHECK(p.count < 3);
}

TEST_CASE("version chips: too many for the card show a window around the chosen one and a +k chip") {
    const std::array<float, 5> names{56.0f, 56.0f, 70.0f, 56.0f, 66.0f};
    // Room for two chips and the +k chip: 56 + 4 + 56 + 4 + 56 = 176.
    for (std::size_t chosen = 0; chosen < names.size(); ++chosen) {
        const VersionPlan p = plan_version_chips({}, names, chosen, 176.0f, 4.0f, 56.0f, false);
        CAPTURE(chosen);
        CHECK(p.more);
        CHECK(p.count >= 1);
        CHECK(chosen >= p.first);
        CHECK(chosen < p.first + p.count); // the chosen one is always shown
        CHECK(p.first + p.count <= names.size());
        CHECK(chips_width(names, p.first, p.count, 4.0f) + 4.0f + 56.0f <= 176.0f);
    }
    // As many as fit: with room for two, the window is two.
    CHECK(plan_version_chips({}, names, 0, 176.0f, 4.0f, 56.0f, false).count == 2);
    // The window slides to keep the chosen one in view.
    const VersionPlan last = plan_version_chips({}, names, 4, 176.0f, 4.0f, 56.0f, false);
    CHECK(last.first + last.count == names.size());
    // A card with no room even for one chip and the +k chip still shows the chosen one.
    const VersionPlan tiny = plan_version_chips({}, names, 3, 60.0f, 4.0f, 56.0f, false);
    CHECK(tiny.count == 1);
    CHECK(tiny.first == 3);
    CHECK(tiny.more);
}

TEST_CASE("version chips: where the details wrap them, they go on as many rows as they need") {
    const std::array<float, 5> names{56.0f, 56.0f, 70.0f, 56.0f, 66.0f};
    // 56 + 56 + 70 + 56 + 66 + 4 gaps of 4 = 320 across: one row.
    VersionPlan p = plan_version_chips({}, names, 0, 320.0f, 4.0f, 56.0f, true);
    CHECK(p.rows == 1);
    CHECK(p.count == 5);
    CHECK_FALSE(p.more);
    // 200 across: 56 + 4 + 56 + 4 + 70 = 190 | 56 + 4 + 66 = 126: two rows.
    p = plan_version_chips({}, names, 0, 200.0f, 4.0f, 56.0f, true);
    CHECK(p.rows == 2);
    CHECK(p.count == 5);
    CHECK_FALSE(p.more);
    // 100 across: 56 | 56 | 70 | 56 | 66: a row each.
    p = plan_version_chips({}, names, 0, 100.0f, 4.0f, 56.0f, true);
    CHECK(p.rows == 5);
    // No versions: nothing.
    CHECK(plan_version_chips({}, {}, 0, 100.0f, 4.0f, 56.0f, false).count == 0);
}

TEST_CASE("list row: a version chip too wide for its word shrinks to its icon and stays reachable") {
    for (const float u : kScales) {
        CAPTURE(u);
        const float word_w = 150.0f * u; // "Medium Lieder", say
        const Box least{0.0f, 0.0f, lib_tok::kListRowMinW * u, lib_tok::kRowH * u};
        const ListRow r = list_row(least, u, word_w);
        // The least row cannot hold the word and the title: the icon alone, a whole hit wide.
        CHECK(r.version.w() == doctest::Approx(tok::kHit * u));
        CHECK(r.version.h() >= tok::kHit * u - kEps);
        CHECK(r.text.w() >= lib_tok::kListLeastTextW * u - kEps);
        CHECK_FALSE(overlap(r.version, r.heart));
        CHECK_FALSE(overlap(r.version, r.text));
        // A wide row keeps the word.
        const Box wide{0.0f, 0.0f, 520.0f * u, lib_tok::kRowH * u};
        CHECK(list_row(wide, u, word_w).version.w() == doctest::Approx(word_w));
    }
}

TEST_CASE("list row: a version chip comes before the hands and the facts, and goes before the title shrinks") {
    for (const float u : kScales) {
        const float chip_w = tok::kChipMinW * u;
        // A wide row: everything fits.
        const Box wide{0.0f, 0.0f, 420.0f * u, lib_tok::kRowH * u};
        const ListRow all = list_row(wide, u, chip_w);
        CHECK(all.version.w() == doctest::Approx(chip_w));
        CHECK(all.version.h() >= tok::kHit * u - kEps);
        CHECK(all.facts.w() > 0.0f);
        CHECK_FALSE(overlap(all.version, all.facts));
        CHECK_FALSE(overlap(all.version, all.heart));
        CHECK_FALSE(overlap(all.version, all.text));
        // The least row keeps the title its least width: the chip stays, the facts and hands go.
        const Box least{0.0f, 0.0f, lib_tok::kListRowMinW * u, lib_tok::kRowH * u};
        const ListRow narrow = list_row(least, u, chip_w);
        CAPTURE(u);
        CHECK(narrow.text.w() >= lib_tok::kListLeastTextW * u - kEps);
        // No chip asked for: nothing of it.
        CHECK(list_row(wide, u).version.w() == doctest::Approx(0.0f));
        // A row too narrow for the chip and the title leaves the chip out.
        const Box tiny{0.0f, 0.0f, 290.0f * u, lib_tok::kRowH * u};
        const ListRow none = list_row(tiny, u, chip_w);
        CHECK(none.version.w() == doctest::Approx(0.0f));
        CHECK(none.text.w() >= lib_tok::kListLeastTextW * u - kEps);
    }
}

TEST_CASE("typing: the fields take what the core keeps of a name, no more") {
    using namespace etude::app::ui::osk;
    // A long section name: the field stops at the core's limit, and the core keeps all of it.
    std::string section;
    type_text(section, std::string(100, 'a'), kSectionNameRules);
    CHECK(section.size() == etude::kMaxSectionNameBytes);
    CHECK(etude::sanitize_section_name(section) == section);
    CHECK(take(section, "b", kSectionNameRules) == Take::Full);
    // A look's: the same through a saved slot.
    std::string look;
    type_text(look, std::string(100, 'b'), kLookNameRules);
    CHECK(look.size() == etude::kMaxPresetNameBytes);
    etude::UserPresets presets;
    REQUIRE(presets.save_slot(0, look, etude::LookSettings{}));
    CHECK(presets.slot_name(0) == look);
    // A multi-byte character at the limit goes whole or not at all, and the core keeps it whole.
    std::string accents(etude::kMaxPresetNameBytes - 1, 'c');
    type_text(accents, "\xC3\xA9", kLookNameRules); // one byte of room: the 2-byte letter stays out
    CHECK(accents.size() == etude::kMaxPresetNameBytes - 1);
    std::string roomy(etude::kMaxPresetNameBytes - 2, 'c');
    type_text(roomy, "\xC3\xA9", kLookNameRules);
    CHECK(roomy.size() == etude::kMaxPresetNameBytes);
    REQUIRE(presets.save_slot(1, roomy, etude::LookSettings{}));
    CHECK(presets.slot_name(1) == roomy);
    // A name that was longer comes in cut at a character boundary.
    CHECK(cut_utf8("ab\xC3\xA9", 3) == "ab");
    CHECK(cut_utf8("ab\xC3\xA9", 4) == "ab\xC3\xA9");
    CHECK(cut_utf8("abc", 10) == "abc");
}

TEST_CASE("typing: a refused key is known before it is pressed") {
    using namespace etude::app::ui::osk;
    FieldRules tiny;
    tiny.max_bytes = 2;
    std::string t = "a";
    CHECK(take(t, "b", tiny) == Take::Yes);
    CHECK(take(t, " ", tiny) == Take::Yes);
    t = "ab";
    CHECK(take(t, "c", tiny) == Take::Full);
    // A space first, or after a space, is no key press.
    CHECK(take("", " ", tiny) == Take::Skip);
    CHECK(take("a ", " ", FieldRules{}) == Take::Skip);
    FieldRules number;
    number.kind = FieldKind::Number;
    number.max_bytes = 3;
    number.max_value = 120;
    CHECK(take("", "0", number) == Take::Skip); // no leading zero
    CHECK(take("", "9", number) == Take::Yes);
    CHECK(take("12", "0", number) == Take::Yes);  // 120
    CHECK(take("12", "1", number) == Take::Skip); // 121 is past it
    CHECK(take("13", "0", number) == Take::Skip); // 130
    CHECK(take("99", "9", number) == Take::Skip); // 999
    std::string n;
    type_text(n, "999", number);
    CHECK(n == "99");
}

TEST_CASE("fields: open the moment they are asked for, and keep the keys and touches") {
    using namespace etude::app::ui::osk;
    auto clear = [] {
        for (const Owner o : {Owner::SectionName, Owner::LookName, Owner::Library, Owner::Tempo}) {
            set_field_open(o, false);
        }
    };
    clear();
    CHECK_FALSE(text_field_open());
    CHECK_FALSE(dialog_open());
    // Keys and text are the UI's with a field open, though ImGui does not say so yet.
    CHECK_FALSE(field_takes_event(true, false, false));
    CHECK(field_takes_event(true, false, true));
    CHECK(field_takes_event(true, true, false));
    // Other events (a finger, the mouse) are never taken by this rule.
    CHECK_FALSE(field_takes_event(false, true, true));
    set_field_open(Owner::SectionName, true);
    CHECK(text_field_open());
    CHECK(dialog_open());
    set_field_open(Owner::SectionName, false);
    CHECK_FALSE(dialog_open());
    // The typed tempo and the library take typing but are not modal: the piano keys under them
    // still take a finger.
    set_field_open(Owner::Tempo, true);
    CHECK(text_field_open());
    CHECK_FALSE(dialog_open());
    set_field_open(Owner::Tempo, false);
    set_field_open(Owner::Library, true);
    CHECK(text_field_open());
    CHECK_FALSE(dialog_open());
    set_field_open(Owner::LookName, true);
    CHECK(dialog_open());
    clear();
    CHECK_FALSE(text_field_open());
}

TEST_CASE("typed tempo: the range the speed allows, and what Done does with a number") {
    // A 60 bpm song at 25% to 200%: 15 to 120.
    const TempoRange r = tempo_range(60.0 * 25 / 100.0, 60.0 * 200 / 100.0);
    CHECK(r.low == 15);
    CHECK(r.high == 120);
    CHECK(tempo_digits(r) == 3);
    // A tempo that is not whole at the ends: the first whole tempo inside, the last inside.
    const TempoRange odd = tempo_range(22.4, 179.9);
    CHECK(odd.low == 23);
    CHECK(odd.high == 179);
    CHECK(tempo_digits(tempo_range(5.0, 95.0)) == 2);
    CHECK(check_tempo_entry("", r) == TempoEntry::Empty);
    CHECK(check_tempo_entry("14", r) == TempoEntry::TooLow);
    CHECK(check_tempo_entry("15", r) == TempoEntry::Ok);
    CHECK(check_tempo_entry("120", r) == TempoEntry::Ok);
    CHECK(check_tempo_entry("121", r) == TempoEntry::TooHigh);
    CHECK(check_tempo_entry("999", r) == TempoEntry::TooHigh);
    CHECK(check_tempo_entry("0", r) == TempoEntry::TooLow);
    // The field itself never lets 999 in: its rules are the range's.
    etude::app::ui::osk::FieldRules rules;
    rules.kind = etude::app::ui::osk::FieldKind::Number;
    rules.max_bytes = static_cast<std::size_t>(tempo_digits(r));
    rules.max_value = r.high;
    std::string typed;
    etude::app::ui::osk::type_text(typed, "999", rules);
    CHECK(check_tempo_entry(typed, r) != TempoEntry::TooHigh);
}

TEST_CASE("scroll keeper: a restore asks again while the list fills, then stops") {
    ScrollKeeper k;
    k.begin_restore(800.0f);
    REQUIRE(k.restoring());
    // The list is still filling and cannot reach it yet: keep asking, for a long time.
    for (int i = 0; i < 100; ++i) {
        k.restore_frame(200.0f, 200.0f, 800.0f, true, false, true, 0.016f);
    }
    CHECK(k.restoring()); // 1.6 s of 2
    // It reaches the place: done.
    k.restore_frame(800.0f, 2000.0f, 800.0f, true, false, true, 0.016f);
    CHECK_FALSE(k.restoring());
    // The wait has a cap.
    k.begin_restore(800.0f);
    for (int i = 0; i < 200; ++i) {
        k.restore_frame(200.0f, 200.0f, 800.0f, true, false, true, 0.016f);
    }
    CHECK_FALSE(k.restoring());
}

TEST_CASE("scroll keeper: a restore ends when the list is whole and too short, or a finger scrolls") {
    ScrollKeeper k;
    k.begin_restore(800.0f);
    k.restore_frame(200.0f, 200.0f, 800.0f, false, false, true, 0.016f);
    CHECK_FALSE(k.restoring());
    k.begin_restore(800.0f);
    k.restore_frame(200.0f, 200.0f, 800.0f, true, true, true, 0.016f);
    CHECK_FALSE(k.restoring()); // a finger took it
    k.begin_restore(800.0f);
    k.restore_frame(200.0f, 200.0f, 800.0f, true, false, false, 0.016f);
    CHECK_FALSE(k.restoring()); // a search or filter replaced the plain list
    k.begin_restore(0.0f);
    CHECK_FALSE(k.restoring()); // nothing to restore at the top
}

TEST_CASE("scroll keeper: the position is written once the scrolling has stopped, for the plain list") {
    ScrollKeeper k;
    int writes = 0;
    int last = -1;
    const auto frame = [&](float px, bool plain) {
        if (const auto v = k.observe(px, 2.0f, 0.016f, plain, 1'000'000)) {
            ++writes;
            last = *v;
        }
    };
    // Scrolling: no write on any step.
    for (int i = 1; i <= 30; ++i) {
        frame(static_cast<float>(i) * 40.0f, true);
    }
    CHECK(writes == 0);
    // It stands: one write, in design points (px / unit).
    for (int i = 0; i < 60; ++i) {
        frame(1200.0f, true);
    }
    CHECK(writes == 1);
    CHECK(last == 600);
    // A search or filter on (not the plain list): scrolling is not kept.
    for (int i = 1; i <= 30; ++i) {
        frame(static_cast<float>(i) * 10.0f, false);
    }
    for (int i = 0; i < 60; ++i) {
        frame(300.0f, false);
    }
    CHECK(writes == 1);
    // Clamped to the most the settings keep.
    ScrollKeeper big;
    big.begin_restore(0.0f);
    std::optional<int> out;
    for (int i = 0; i < 80 && !out; ++i) {
        out = big.observe(8.0e6f, 1.0f, 0.016f, true, 1000);
    }
    REQUIRE(out.has_value());
    CHECK(*out == 1000);
}
