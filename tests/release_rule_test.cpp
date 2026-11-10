// SPDX-License-Identifier: GPL-3.0-or-later
// The key ring decides a press itself (`key_ring_release`): a key fires only when the finger lifts
// on the key it landed on, and sliding off cancels. Driven through press, slide and lift with the
// core tap rule.
#include "ui/key_ring_layout.hpp"

#include "etude/tap_rule.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <initializer_list>

using namespace etude;
using namespace etude::app::ui;

namespace {

struct Pt {
    float x;
    float y;
};

constexpr std::array<float, 2> kUnits{1.0f, 1.425f}; // a 96 dpi screen, and the 7 inch panel's

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

} // namespace

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
