// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_ring_layout.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;

namespace {

constexpr std::array<float, 3> kScales{1.0f, 1.25f, 1.5f};
constexpr float kEps = 1e-3f;

float distance(const KeyRingPoint& a, const KeyRingPoint& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// The squares a ring may be given, in design points: from the least that holds keys of a hit up to
// a whole screen's height.
constexpr std::array<float, 6> kSides{260.0f, 280.0f, 300.0f, 360.0f, 480.0f, 700.0f};

bool inside(const Box& inner, const Box& outer) {
    return inner.x0 >= outer.x0 - kEps && inner.y0 >= outer.y0 - kEps &&
           inner.x1 <= outer.x1 + kEps && inner.y1 <= outer.y1 + kEps;
}

} // namespace

TEST_CASE("key ring: the keys are placed in fifths, clockwise from C at the top") {
    CHECK(key_ring_place(0) == 0);  // C
    CHECK(key_ring_place(7) == 1);  // G
    CHECK(key_ring_place(2) == 2);  // D
    CHECK(key_ring_place(9) == 3);  // A
    CHECK(key_ring_place(4) == 4);  // E
    CHECK(key_ring_place(11) == 5); // B
    CHECK(key_ring_place(6) == 6);  // F#
    CHECK(key_ring_place(1) == 7);  // Db
    CHECK(key_ring_place(8) == 8);  // Ab
    CHECK(key_ring_place(3) == 9);  // Eb
    CHECK(key_ring_place(10) == 10); // Bb
    CHECK(key_ring_place(5) == 11); // F
    std::set<int> seen;
    for (int pc = 0; pc < 12; ++pc) {
        seen.insert(key_ring_place(pc));
        CHECK(key_ring_pitch_class(key_ring_place(pc)) == pc);
    }
    CHECK(seen.size() == 12);
    CHECK(key_ring_place(12) == key_ring_place(0));
    CHECK(key_ring_place(-5) == key_ring_place(7));
}

TEST_CASE("key ring: every key is at least a hit across and none touches another") {
    for (const float u : kScales) {
        for (const float side : kSides) {
            CAPTURE(u);
            CAPTURE(side);
            const Box area{0.0f, 0.0f, side * u, side * u};
            const KeyRingLayout lay = key_ring_layout(area, u);
            CHECK(lay.fits);
            CHECK(lay.key >= tok::kHit * u - kEps);
            CHECK(lay.key <= key_ring_tok::kMaxKey * u + kEps);
            for (int place = 0; place < 12; ++place) {
                const KeyRingPoint& a = lay.centre[static_cast<std::size_t>(key_ring_pitch_class(place))];
                const KeyRingPoint& b =
                    lay.centre[static_cast<std::size_t>(key_ring_pitch_class((place + 1) % 12))];
                CHECK(distance(a, b) >= lay.key + key_ring_tok::kGap * u - kEps);
                // The circles are the hit areas: a gap of 2 mm (kHitGap points) between any two.
                CHECK(distance(a, b) - lay.key >= tok::kHitGap * u - kEps);
            }
            for (int pc = 0; pc < 12; ++pc) {
                for (int other = pc + 1; other < 12; ++other) {
                    CHECK(distance(lay.centre[static_cast<std::size_t>(pc)],
                                   lay.centre[static_cast<std::size_t>(other)]) >= lay.key - kEps);
                }
            }
        }
    }
}

TEST_CASE("key ring: the ring and its keys stay inside the area, centred") {
    for (const float u : kScales) {
        for (const float side : kSides) {
            CAPTURE(u);
            CAPTURE(side);
            // A wide area and a tall one: the ring takes the square of the shorter side.
            for (const Box& area : {Box{10.0f, 20.0f, 10.0f + side * u, 20.0f + side * u},
                                    Box{0.0f, 0.0f, side * u * 1.8f, side * u},
                                    Box{5.0f, 5.0f, 5.0f + side * u, 5.0f + side * u * 1.6f}}) {
                const KeyRingLayout lay = key_ring_layout(area, u);
                CHECK(inside(lay.bounds, area));
                CHECK(lay.bounds.cx() == doctest::Approx(area.cx()));
                CHECK(lay.bounds.cy() == doctest::Approx(area.cy()));
                for (int pc = 0; pc < 12; ++pc) {
                    CHECK(inside(key_ring_key_box(lay, pc), lay.bounds));
                    CHECK(inside(key_ring_key_box(lay, pc), area));
                }
            }
        }
    }
}

TEST_CASE("key ring: keys grow with the room up to the most allowed, and larger when the caller "
          "allows more") {
    const Box big{0.0f, 0.0f, 700.0f, 700.0f};
    CHECK(key_ring_layout(big, 1.0f).key == doctest::Approx(key_ring_tok::kMaxKey));
    CHECK(key_ring_layout(big, 1.0f, 64.0f).key == doctest::Approx(64.0f));
    const Box snug{0.0f, 0.0f, 270.0f, 270.0f};
    const float snug_key = key_ring_layout(snug, 1.0f).key;
    CHECK(snug_key >= tok::kHit);
    CHECK(snug_key < key_ring_tok::kMaxKey);
    // A caller's most below a hit never makes keys smaller than one.
    CHECK(key_ring_layout(big, 1.0f, 20.0f).key == doctest::Approx(tok::kHit));
}

TEST_CASE("key ring: min_side is the least square that holds keys of a given size") {
    for (const float u : kScales) {
        for (const float key_pt : {tok::kHit, 52.0f, 64.0f}) {
            CAPTURE(u);
            CAPTURE(key_pt);
            const float side = key_ring_min_side(key_pt, u);
            const KeyRingLayout at = key_ring_layout({0.0f, 0.0f, side, side}, u, key_pt);
            CHECK(at.fits);
            CHECK(at.key == doctest::Approx(key_pt * u).epsilon(1e-3));
            // A little less and the keys, pinned at a hit when the size asked is a hit, no longer
            // fit: for a hit the ring reports it.
            if (key_pt == tok::kHit) {
                CHECK_FALSE(key_ring_layout({0.0f, 0.0f, side - 2.0f, side - 2.0f}, u, key_pt).fits);
            }
        }
    }
}

TEST_CASE("key ring: an area too small for keys of a hit keeps them a hit and says so") {
    const Box tiny{0.0f, 0.0f, 150.0f, 150.0f};
    const KeyRingLayout lay = key_ring_layout(tiny, 1.0f);
    CHECK_FALSE(lay.fits);
    CHECK(lay.key == doctest::Approx(tok::kHit));
    // The keys still do not touch: the ring overflows instead.
    for (int place = 0; place < 12; ++place) {
        const KeyRingPoint& a = lay.centre[static_cast<std::size_t>(key_ring_pitch_class(place))];
        const KeyRingPoint& b =
            lay.centre[static_cast<std::size_t>(key_ring_pitch_class((place + 1) % 12))];
        CHECK(distance(a, b) >= lay.key - kEps);
    }
    CHECK(lay.bounds.w() > tiny.w());
    // Nothing negative or NaN comes out of an empty area.
    const KeyRingLayout none = key_ring_layout({0.0f, 0.0f, 0.0f, 0.0f}, 1.0f);
    CHECK_FALSE(none.fits);
    CHECK(std::isfinite(none.bounds.x0));
}

TEST_CASE("key ring: the name in the middle sits inside the keys, clear of every one") {
    for (const float u : kScales) {
        for (const float side : kSides) {
            CAPTURE(u);
            CAPTURE(side);
            const KeyRingLayout lay = key_ring_layout({0.0f, 0.0f, side * u, side * u}, u);
            CHECK(lay.middle.w() > 0.0f);
            CHECK(lay.middle.h() > 0.0f);
            // The box is wide enough for a key's name and mode.
            CHECK(lay.middle.w() >= tok::kHit * u);
            // The round keys stay clear of the box: from each centre to the nearest point of it
            // is at least the key's radius.
            for (int pc = 0; pc < 12; ++pc) {
                const KeyRingPoint& c = lay.centre[static_cast<std::size_t>(pc)];
                const float nx = std::clamp(c.x, lay.middle.x0, lay.middle.x1);
                const float ny = std::clamp(c.y, lay.middle.y0, lay.middle.y1);
                CHECK(std::hypot(c.x - nx, c.y - ny) >= lay.key * 0.5f - kEps);
            }
        }
    }
}

TEST_CASE("key ring: a press goes to the key it lands in, and none from between keys, the middle "
          "or outside") {
    for (const float u : kScales) {
        const KeyRingLayout lay = key_ring_layout({0.0f, 0.0f, 300.0f * u, 300.0f * u}, u);
        for (int pc = 0; pc < 12; ++pc) {
            const KeyRingPoint& c = lay.centre[static_cast<std::size_t>(pc)];
            CHECK(key_ring_nearest(lay, c.x, c.y) == pc);
            // The edge of the key's circle, towards the ring's middle, is still the key's.
            const float half = lay.key * 0.5f - 1.0f;
            const float dx = lay.bounds.cx() - c.x;
            const float dy = lay.bounds.cy() - c.y;
            const float len = std::hypot(dx, dy);
            CHECK(key_ring_nearest(lay, c.x + dx / len * half, c.y + dy / len * half) == pc);
        }
        // Halfway between two neighbours is the dead gap: no key takes it.
        for (int place = 0; place < 12; ++place) {
            const int a = key_ring_pitch_class(place);
            const int b = key_ring_pitch_class((place + 1) % 12);
            const KeyRingPoint& pa = lay.centre[static_cast<std::size_t>(a)];
            const KeyRingPoint& pb = lay.centre[static_cast<std::size_t>(b)];
            CHECK(key_ring_nearest(lay, (pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f) == -1);
        }
        CHECK(key_ring_nearest(lay, lay.bounds.cx(), lay.bounds.cy()) == -1);
        CHECK(key_ring_nearest(lay, lay.bounds.x0 - 100.0f * u, lay.bounds.y0 - 100.0f * u) == -1);
    }
}

TEST_CASE("key ring: the ring's size at the windows the app runs in leaves keys of a hit") {
    // What a screen gives it: 800x480 at scale 1 leaves a square about as tall as the screen less
    // its header and a row (design points), and the wider windows more.
    struct Case {
        float side;
        float u;
    };
    for (const Case c : {Case{260.0f, 1.0f}, Case{300.0f, 1.0f}, Case{330.0f, 1.0f},
                         Case{300.0f, 1.25f}, Case{300.0f, 1.5f}}) {
        const KeyRingLayout lay = key_ring_layout({0.0f, 0.0f, c.side * c.u, c.side * c.u}, c.u);
        CHECK(lay.fits);
        CHECK(lay.key >= tok::kHit * c.u - kEps);
    }
}

TEST_CASE("key ring: the name and the mode are larger than a caption, grow with the ring, and sit "
          "inside the inner circle with room to spare at every size") {
    for (const float u : kScales) {
        float last_name = 0.0f;
        for (const float side : kSides) {
            CAPTURE(u);
            CAPTURE(side);
            const KeyRingLayout lay = key_ring_layout({0.0f, 0.0f, side * u, side * u}, u);
            const KeyRingMiddle m = key_ring_middle(lay, u);
            // At least the display size for the name and the body size, bold, for the mode.
            CHECK(m.name_size >= tok::kDisplay * u * 0.99f);
            CHECK(m.mode_size >= tok::kBody * u * 0.99f);
            CHECK(m.name_size > m.mode_size);
            // They grow with the ring (never shrink as it gets larger) up to their caps.
            CHECK(m.name_size >= last_name - kEps);
            last_name = m.name_size;
            CHECK(m.name_size <= key_ring_tok::kNameMax * u + kEps);
            // The block is inside the inner circle with its padding: its top corners (the name's
            // width) and its bottom edge (the word's chord).
            const float inner = lay.radius - lay.key * 0.5f - key_ring_tok::kGap * u;
            CHECK(m.radius == doctest::Approx(inner - key_ring_tok::kMiddlePad * u));
            const float half_h = m.block_h * 0.5f;
            const float name_half_w = m.name_size * key_ring_tok::kNameWide * 0.5f;
            CHECK(std::hypot(name_half_w, half_h) <= m.radius + kEps);
            CHECK(m.name_cy < lay.bounds.cy());
            CHECK(m.mode_cy > lay.bounds.cy() - kEps);
            CHECK(m.name_cy > lay.bounds.cy() - half_h);
            CHECK(m.mode_cy < lay.bounds.cy() + half_h);
            // The word has room for "major" and "minor" at its size, and a long one shrinks to
            // what room there is (down to the floor) rather than leaving the circle.
            CHECK(m.word_room >= 5.0f * 0.6f * m.mode_size);
            CHECK(m.word_room <= 2.0f * m.radius + kEps);
            CHECK(key_ring_tok::kModeFloor * u <= m.mode_size + kEps);
        }
    }
    // The ring's middle in a window's smallest ring at the plain unit: still a clear name.
    const KeyRingLayout smallest = key_ring_layout({0.0f, 0.0f, 232.0f, 232.0f}, 1.0f);
    CHECK(key_ring_middle(smallest, 1.0f).name_size > 30.0f);
}
