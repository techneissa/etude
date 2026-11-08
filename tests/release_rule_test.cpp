// SPDX-License-Identifier: GPL-3.0-or-later
// The release rule on the real geometry of the controls: a control fires when the finger lifts
// inside the control the press started in, sliding off cancels, a small move under the slop still
// fires. No test links ImGui (the kit needs fonts, the audit and a renderer), so a press cannot be
// driven through a drawn control without an SDL window. What can be tested is what the rule rests
// on: the settings row, the library card and list row and the on-screen keys fire through ImGui's
// button (it fires on the release over the item the press began on), so a slide off is a cancel
// exactly when no hit area reaches over another, and a small move stays a tap when a hit area is
// wider than the slop; those are checked on the boxes the controls draw from (`row_tap_parts`,
// `card_parts`, `list_row`, `keyboard_key_hit`). The key ring decides itself (`key_ring_release`)
// and is driven through press, slide and lift. That nothing fires on the press is the guard in
// tests/touch_fire_guard_test.cpp; the tap rule (slop, hold, cancel) is core's TapTracker
// (tap_rule_test.cpp).
#include "ui/key_ring_layout.hpp"
#include "ui/library_layout.hpp"
#include "ui/osk_layout.hpp"
#include "ui/touch_rules.hpp"

#include "etude/tap_rule.hpp"

#include <doctest/doctest.h>

#include <array>
#include <initializer_list>
#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

struct Pt {
    float x;
    float y;
};

constexpr std::array<float, 2> kUnits{1.0f, 1.425f}; // a 96 dpi screen, and the 7 inch panel's
constexpr float kEps = 1e-3f;

bool has(const Box& b, Pt p) { return p.x >= b.x0 && p.x <= b.x1 && p.y >= b.y0 && p.y <= b.y1; }
Pt centre(const Box& b) { return {b.cx(), b.cy()}; }

// A press that lands at `down`, goes through `path`, and lifts at `up`, fast enough to be a tap.
TapTracker press(Pt down, Pt up, std::initializer_list<Pt> path = {}) {
    TapTracker t;
    t.down(down.x, down.y, 0.0);
    double now = 0.02;
    for (const Pt& p : path) {
        t.move(p.x, p.y, now);
        now += 0.02;
    }
    t.up(up.x, up.y, now);
    return t;
}

// The pitch class a press fires on the key ring, or -1: the tap rule and the key rule together, as
// the ring draws them.
int ring_fires(const KeyRingLayout& lay, Pt down, Pt up, float u,
               std::initializer_list<Pt> path = {}) {
    const TapTracker t = press(down, up, path);
    if (!t.tap(TapRuleConfig::scaled(u))) {
        return -1;
    }
    return key_ring_release(lay, down.x, down.y, up.x, up.y);
}

// What lets a press end in the control it began in and nowhere else: every hit area at least a hit
// across (twice the slop, so a move under it stays inside), none over another and a dead gap between
// any two, and the middle of each in no other. `what` names the control in a failure.
void check_pressables(const std::vector<Box>& boxes, float u, const char* what) {
    const float slop = TapRuleConfig::scaled(u).slop;
    const float gap = tok::kHitGap * u;
    CAPTURE(what);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        CAPTURE(i);
        CHECK(boxes[i].w() >= tok::kHit * u - kEps);
        CHECK(boxes[i].h() >= tok::kHit * u - kEps);
        // A move under the slop from the middle stays inside.
        CHECK(boxes[i].w() * 0.5f > slop);
        CHECK(boxes[i].h() * 0.5f > slop);
        for (std::size_t j = i + 1; j < boxes.size(); ++j) {
            CAPTURE(j);
            CHECK_FALSE(has(boxes[j], centre(boxes[i])));
            CHECK_FALSE(has(boxes[i], centre(boxes[j])));
            const float dx = std::max(boxes[j].x0 - boxes[i].x1, boxes[i].x0 - boxes[j].x1);
            const float dy = std::max(boxes[j].y0 - boxes[i].y1, boxes[i].y0 - boxes[j].y1);
            CHECK(std::max(dx, dy) >= gap - kEps); // a dead gap on at least one axis
        }
    }
}

} // namespace

TEST_CASE("release rule: a settings row's parts are a hit each and a dead gap clear of the info "
          "button, so a lift on the button or the other part is a cancel") {
    for (const float u : kUnits) {
        CAPTURE(u);
        const Box row{0.0f, 0.0f, 600.0f * u, tok::kHit * u};
        const float hit = tok::kHit * u;
        const Box info{200.0f * u - hit * 0.5f, 0.0f, 200.0f * u + hit * 0.5f, hit};
        const RowTapParts parts = row_tap_parts(row, &info, tok::kHitGap * u);
        REQUIRE(parts.count == 2);
        check_pressables({parts.part[0], parts.part[1], info}, u, "row parts and info button");
        // A row with no info button is one part, the whole row.
        const RowTapParts whole = row_tap_parts(row, nullptr, tok::kHitGap * u);
        REQUIRE(whole.count == 1);
        check_pressables({whole.part[0]}, u, "whole row");
    }
}

TEST_CASE("release rule: a key ring key fires only when the finger lifts on the key it landed on") {
    for (const float u : kUnits) {
        CAPTURE(u);
        const KeyRingLayout lay = key_ring_layout({0.0f, 0.0f, 300.0f * u, 300.0f * u}, u);
        REQUIRE(lay.fits);
        const float slop = TapRuleConfig::scaled(u).slop;
        for (int pc = 0; pc < 12; ++pc) {
            CAPTURE(pc);
            const KeyRingPoint& c = lay.centre[static_cast<std::size_t>(pc)];
            const int next = key_ring_pitch_class((key_ring_place(pc) + 1) % 12);
            const KeyRingPoint& n = lay.centre[static_cast<std::size_t>(next)];
            // Press and lift on the key.
            CHECK(ring_fires(lay, {c.x, c.y}, {c.x, c.y}, u) == pc);
            // A small move under the slop, still on the key (towards the middle, away from the
            // neighbours' dead gap).
            const float dx = lay.bounds.cx() - c.x;
            const float dy = lay.bounds.cy() - c.y;
            const float len = std::hypot(dx, dy);
            CHECK(ring_fires(lay, {c.x, c.y}, {c.x + dx / len * slop * 0.8f, c.y + dy / len * slop * 0.8f},
                             u) == pc);
            // Slide off onto the next key: cancelled.
            CHECK(ring_fires(lay, {c.x, c.y}, {n.x, n.y}, u, {{(c.x + n.x) * 0.5f, (c.y + n.y) * 0.5f}}) ==
                  -1);
            // Slide into the gap or the middle, and lift there: cancelled.
            CHECK(ring_fires(lay, {c.x, c.y}, {(c.x + n.x) * 0.5f, (c.y + n.y) * 0.5f}, u) == -1);
            CHECK(ring_fires(lay, {c.x, c.y}, {lay.bounds.cx(), lay.bounds.cy()}, u) == -1);
        }
        // A press that lands between keys fires nothing, whatever it lifts on.
        const KeyRingPoint& a = lay.centre[0];
        const KeyRingPoint& b = lay.centre[static_cast<std::size_t>(key_ring_pitch_class(1))];
        CHECK(ring_fires(lay, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, {a.x, a.y}, u) == -1);
    }
}

TEST_CASE("release rule: a library card's tap area, heart, Play and chips row never reach over "
          "each other") {
    for (const float u : kUnits) {
        for (const bool versioned : {false, true}) {
            CAPTURE(u);
            CAPTURE(versioned);
            const Box card{0.0f, 0.0f, 220.0f * u, card_height(u, versioned)};
            const CardParts parts = card_parts(card, u, false, versioned);
            std::vector<Box> boxes{parts.tap, parts.heart, parts.play};
            if (versioned) {
                // The chips' row over Play, a hit tall, the card's width less its inset.
                boxes.push_back({card.x0 + tok::kS2 * u, parts.chips_top, card.x1 - tok::kS2 * u,
                                 parts.chips_top + tok::kHit * u});
            }
            check_pressables(boxes, u, "library card");
        }
        // While songs are picked the whole card is the tap area, and there is no heart or Play.
        const Box card{0.0f, 0.0f, 220.0f * u, card_height(u, false)};
        CHECK(card_parts(card, u, true, false).tap.x1 == doctest::Approx(card.x1));
    }
}

TEST_CASE("release rule: a list row's tap area, version chip, heart and Play never reach over each "
          "other, in the list and in the search strip") {
    for (const float u : kUnits) {
        for (const bool strip : {false, true}) {
            CAPTURE(u);
            CAPTURE(strip);
            const Box row{0.0f, 0.0f, (strip ? 331.0f : 420.0f) * u, (strip ? tok::kHit : lib_tok::kRowH) * u};
            const ListRow lay = list_row(row, u, 80.0f * u, strip);
            std::vector<Box> boxes{lay.heart, lay.play};
            if (lay.version.w() > 0.0f) {
                boxes.push_back(lay.version);
            }
            check_pressables(boxes, u, "list row controls");
            // The row's own tap area is clear of them by a dead gap, and holds a point to press.
            CHECK(lay.tap.w() > 0.0f);
            for (const Box& b : boxes) {
                CHECK(lay.tap.x1 <= b.x0 - tok::kHitGap * u + kEps);
            }
            // The stars are not pressable and keep out of the controls' way.
            if (lay.stars.w() > 0.0f) {
                CHECK(lay.stars.x1 <= lay.heart.x0 - tok::kHitGap * u + kEps);
            }
        }
    }
}

TEST_CASE("release rule: the on-screen keys are a hit each and a dead gap apart, in every layer") {
    for (const float u : kUnits) {
        CAPTURE(u);
        const Box area{0.0f, 0.0f, 800.0f * u, keyboard_height(800.0f * u, u)};
        for (const Layer layer : {Layer::Letters, Layer::Symbols}) {
            const std::span<const KeyRow> rows = keyboard_rows(layer);
            std::vector<Box> keys;
            for (std::size_t r = 0; r < rows.size(); ++r) {
                for (std::size_t i = 0; i < rows[r].count; ++i) {
                    keys.push_back(
                        keyboard_key_hit(keyboard_key(area, u, layer, r, i), osk_tok::kKeyGap * u));
                }
            }
            check_pressables(keys, u, "on-screen keys");
        }
    }
}
