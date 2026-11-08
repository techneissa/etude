// SPDX-License-Identifier: GPL-3.0-or-later
#include "atkinson_metrics.hpp"
#include "learn_drill_util.hpp"
#include "ui/key_ring_layout.hpp"
#include "ui/learn_layout.hpp"
#include "ui/learn_types.hpp"

#include "etude/panel_size.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace etude;
using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;
namespace lt = etude::app::ui::learn_tok;

namespace {

struct Window {
    float w;
    float h;
    float u; // the kit's unit
};

// The windows the app runs in: the 7 inch 800x480 panel, the usual screens, and the denser ones at their
// grown unit. A window smaller in design points than 800x480 (a 7 inch panel at a taste scale
// above 1) cannot keep 12 mm controls with 2 mm gaps on this card, and is not supported.
constexpr std::array<Window, 6> kWindows{{{800, 480, 1.0f},
                                          {1024, 600, 1.0f},
                                          {1280, 720, 1.0f},
                                          {1366, 768, 1.0f},
                                          {1920, 1080, 1.4f},
                                          {1920, 1080, 1.0f}}};
constexpr float kEps = 1e-3f;

// The screen under a top row about 15 mm tall.
Box area_for(const Window& s) { return {0.0f, 57.0f * s.u, s.w, s.h}; }

bool inside(const Box& inner, const Box& outer) {
    return inner.x0 >= outer.x0 - kEps && inner.y0 >= outer.y0 - kEps &&
           inner.x1 <= outer.x1 + kEps && inner.y1 <= outer.y1 + kEps;
}

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

// Whether two boxes are at least the dead gap apart (both directions count: the larger gap).
bool apart(const Box& a, const Box& b, float u) {
    const float dx = std::max(a.x0 - b.x1, b.x0 - a.x1);
    const float dy = std::max(a.y0 - b.y1, b.y0 - a.y1);
    return std::max(dx, dy) >= tok::kHitGap * u - kEps;
}

bool at_least_a_hit(const Box& b, float u) {
    return b.w() >= tok::kHit * u - kEps && b.h() >= tok::kHit * u - kEps;
}

bool empty(const Box& b) { return b.w() <= 0.0f || b.h() <= 0.0f; }

// Every control of a scale card: what the layout must keep a hit across and apart.
std::vector<std::pair<std::string, Box>> controls_of(const LearnCardLayout& c) {
    std::vector<std::pair<std::string, Box>> controls;
    controls.push_back({"back", c.back});
    for (const Box& tab : c.tabs) {
        controls.push_back({"tab", tab});
    }
    if (c.ring_fits) {
        controls.push_back({"toggle", c.toggle});
    }
    controls.push_back({"slot", c.slot});
    controls.push_back({"listen", c.play});
    controls.push_back({"practise", c.practise});
    controls.push_back({"hand right", c.hand_right});
    controls.push_back({"hand left", c.hand_left});
    learn_test::add_drill_controls(controls, c);
    for (int i = 0; i < c.type_cells; ++i) {
        controls.push_back({"type " + std::to_string(i), c.type[static_cast<std::size_t>(i)]});
    }
    if (!c.rows) {
        controls.push_back({"strip", c.strip});
    }
    return controls;
}

} // namespace

TEST_CASE("learn gallery: a 2x2 grid of tiles and the Continue row inside the screen, none "
          "touching another, at every size") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnGalleryLayout g = learn_gallery_layout(area, s.u);
        CHECK(g.columns == 2);
        CHECK(g.rows == 2);
        CHECK(inside(g.header, area));
        CHECK(inside(g.back, area));
        CHECK(at_least_a_hit(g.back, s.u));
        CHECK(g.title.w() > 0.0f);
        CHECK_FALSE(overlap(g.title, g.back));
        // Scales and Chords over Ear and Melodies: the cards in reading order.
        CHECK(g.tiles[0].tile.y0 == doctest::Approx(g.tiles[1].tile.y0));
        CHECK(g.tiles[0].tile.x1 < g.tiles[1].tile.x0);
        CHECK(g.tiles[2].tile.y0 > g.tiles[0].tile.y1);
        CHECK(g.tiles[2].tile.x0 == doctest::Approx(g.tiles[0].tile.x0));
        CHECK(g.tiles[3].tile.x0 == doctest::Approx(g.tiles[1].tile.x0));
        CHECK(g.tiles[3].tile.y0 == doctest::Approx(g.tiles[2].tile.y0));
        for (std::size_t i = 0; i < g.tiles.size(); ++i) {
            const LearnTileBoxes& t = g.tiles[i];
            CHECK(inside(t.tile, area));
            CHECK(at_least_a_hit(t.tile, s.u));
            CHECK(inside(t.preview, t.tile));
            CHECK(inside(t.icon, t.tile));
            CHECK(inside(t.text, t.tile));
            CHECK(inside(t.chevron, t.tile));
            // The footer's parts are side by side.
            CHECK_FALSE(overlap(t.icon, t.text));
            CHECK_FALSE(overlap(t.text, t.chevron));
            CHECK_FALSE(overlap(t.icon, t.preview));
            CHECK_FALSE(overlap(t.text, t.preview));
            CHECK_FALSE(overlap(t.chevron, t.preview));
            // The tiles are two millimetres or more apart.
            for (std::size_t j = i + 1; j < g.tiles.size(); ++j) {
                CHECK_FALSE(overlap(t.tile, g.tiles[j].tile));
            }
            CHECK_FALSE(overlap(t.tile, g.header));
            if (!empty(g.cont)) {
                CHECK_FALSE(overlap(t.tile, g.cont));
            }
        }
        if (empty(g.cont)) {
            continue;
        }
        CHECK(inside(g.cont, area));
        CHECK(at_least_a_hit(g.cont, s.u));
        CHECK(inside(g.cont_icon, g.cont));
        CHECK(inside(g.cont_chevron, g.cont));
        CHECK(inside(g.cont_text, g.cont));
        CHECK_FALSE(overlap(g.cont_icon, g.cont_text));
        CHECK_FALSE(overlap(g.cont_text, g.cont_chevron));
    }
}

TEST_CASE("learn gallery: a tile hugs its picture and title row, and the grid is centred in the "
          "room left") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnGalleryLayout g = learn_gallery_layout(area, s.u);
        const float gap = tok::kS2 * s.u;
        for (const LearnTileBoxes& t : g.tiles) {
            if (t.preview.h() <= 0.0f || t.side) {
                continue;
            }
            // No air between the picture and the title row beyond the tile's own gap, and none under
            // the title row: the picture's top and the title row's bottom are a gap from the edges.
            CHECK(t.preview.y1 <= t.icon.y0 + gap + kEps);
            CHECK(t.tile.y1 - t.icon.y1 == doctest::Approx(gap));
            CHECK(t.preview.y0 - t.tile.y0 == doctest::Approx(gap));
            // The keys keep their natural proportion: the picture is no taller than eight white keys
            // across its width make it (with the margins the picture keeps).
            const float keys_w = t.preview.w() - 2.0f * tok::kS3 * s.u;
            const float natural = keys_w / 8.0f * lt::kPreviewKeyAspect + 2.0f * tok::kS3 * s.u;
            CHECK(t.preview.h() <= natural + kEps);
        }
        // Centred: the room over the block (under the header) and under it are the same.
        const float block_top = g.tiles[0].tile.y0;
        const float block_bottom = empty(g.cont) ? g.tiles[3].tile.y1 : g.cont.y1;
        const float above = block_top - (g.header.y1 + gap);
        const float below = (area.y1 - lt::kPad * s.u) - block_bottom;
        CHECK(above == doctest::Approx(below).epsilon(0.01).scale(1.0));
        // And across: the grid sits in the middle of the width.
        CHECK(g.tiles[0].tile.x0 - area.x0 == doctest::Approx(area.x1 - g.tiles[1].tile.x1));
    }
}

TEST_CASE("learn gallery: the keys are large at every size: the Continue row is kept where the keys "
          "stay large and dropped where it would shrink them") {
    for (const Window& s : {Window{800, 480, 1.0f}, Window{1366, 768, 1.0f}, Window{1920, 1080, 1.0f}}) {
        CAPTURE(s.w);
        const LearnGalleryLayout g = learn_gallery_layout(area_for(s), s.u);
        for (const LearnTileBoxes& t : g.tiles) {
            // Keys of at least 30 points across (8 of them are the picture's height over 3 tall).
            CHECK(t.preview.h() / lt::kPreviewKeyAspect >= 30.0f * s.u - kEps);
            // The picture takes the tile's width less its gaps; its keys fill its height.
            CHECK(t.preview.w() == doctest::Approx(t.tile.w() - 2.0f * tok::kS2 * s.u));
        }
        // Continue stays where the tiles keep keys of `kTilePicGood` with it, as at 1366x768.
        CHECK(empty(g.cont) == (s.w < 1000.0f));
    }
}

TEST_CASE("learn gallery: a window too short for tiles of a useful height drops the Continue row") {
    const Box tiny{0.0f, 57.0f, 800.0f, 330.0f};
    const LearnGalleryLayout g = learn_gallery_layout(tiny, 1.0f);
    CHECK(empty(g.cont));
    for (const LearnTileBoxes& t : g.tiles) {
        CHECK(inside(t.tile, tiny));
    }
}

TEST_CASE("learn card: every control is at least a hit, inside the screen, and none touches "
          "another") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnCardLayout c = learn_card_layout(area, s.u);

        const std::vector<std::pair<std::string, Box>> controls = controls_of(c);
        CHECK(c.type_cells == (c.rows ? kLearnTypes : 0));
        for (const auto& [name, box] : controls) {
            CAPTURE(name);
            CHECK(inside(box, area));
            CHECK(at_least_a_hit(box, s.u));
        }
        // The type buttons are chips: as wide as the kit's least chip.
        for (int i = 0; i < c.type_cells; ++i) {
            CHECK(c.type[static_cast<std::size_t>(i)].w() >= tok::kChipMinW * s.u - kEps);
        }
        for (std::size_t i = 0; i < controls.size(); ++i) {
            for (std::size_t j = i + 1; j < controls.size(); ++j) {
                INFO(controls[i].first << " and " << controls[j].first);
                CHECK(apart(controls[i].second, controls[j].second, s.u));
            }
        }
        // The keys, with the rows of badges, apart from every control.
        const Box keys{c.fingers_right.x0, c.fingers_right.y0, c.fingers_right.x1,
                       c.fingers_left.y1};
        CHECK(inside(keys, area));
        CHECK(c.keyboard.h() > 0.0f);
        // A white key is wide enough for a name and a finger badge (it is not pressable).
        CHECK(c.keyboard.w() / 9.0f >= 28.0f * s.u);
        CHECK_FALSE(overlap(keys, c.slot));
        CHECK_FALSE(overlap(keys, c.toggle));
        CHECK_FALSE(overlap(keys, c.play));
        CHECK_FALSE(overlap(keys, c.practise));
        CHECK_FALSE(overlap(keys, c.name));
        for (const Box& other : {c.strip, c.hand_right, c.hand_left}) {
            CHECK_FALSE(overlap(keys, other));
            CHECK_FALSE(overlap(c.name, other));
        }
        for (int i = 0; i < c.type_cells; ++i) {
            CHECK_FALSE(overlap(keys, c.type[static_cast<std::size_t>(i)]));
        }
        // The name is clear of Listen and the header's controls.
        CHECK(inside(c.name, area));
        CHECK_FALSE(overlap(c.name, c.play));
        CHECK_FALSE(overlap(c.name, c.slot));
        CHECK_FALSE(overlap(c.name, c.toggle));
        CHECK(c.name_in_header == (c.column != LearnColumn::Full));
        for (int i = 0; i < c.type_cells; ++i) {
            CHECK_FALSE(overlap(c.name, c.type[static_cast<std::size_t>(i)]));
        }
        // The page's name in the header is clear of what is round it.
        CHECK(c.title.w() > 0.0f);
        CHECK_FALSE(overlap(c.title, c.back));
        CHECK_FALSE(overlap(c.title, c.toggle));
        CHECK_FALSE(overlap(c.name, c.back));
        CHECK_FALSE(overlap(c.name, c.drill.tempo));
        for (const Box& tab : c.tabs) {
            CHECK_FALSE(overlap(c.title, tab));
            CHECK_FALSE(overlap(c.name, tab));
        }
    }
}

TEST_CASE("learn card: the picker's square holds a ring of keys of a hit, and so do the twelve "
          "keys as buttons") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnCardLayout c = learn_card_layout(area, s.u);
        CHECK(inside(c.slot, area));
        if (c.ring_fits) {
            CHECK(c.slot.w() == doctest::Approx(c.slot.h()));
            CHECK(c.slot.w() >= key_ring_min_side(tok::kHit, s.u) - kEps);
            const KeyRingLayout ring = key_ring_layout(c.slot, s.u);
            CHECK(ring.fits);
            CHECK(ring.key >= tok::kHit * s.u - kEps);
            CHECK(inside(ring.bounds, c.slot));
        } else {
            CHECK(empty(c.toggle));
        }
        // The twelve keys: each a hit, inside the square, none touching another.
        const std::array<Box, 12> grid = learn_key_grid(c.slot, s.u);
        for (std::size_t i = 0; i < grid.size(); ++i) {
            CHECK(inside(grid[i], c.slot));
            CHECK(at_least_a_hit(grid[i], s.u));
            for (std::size_t j = i + 1; j < grid.size(); ++j) {
                CHECK(apart(grid[i], grid[j], s.u));
            }
        }
    }
}

TEST_CASE("learn card: the picker's square and its switch do not move with the look") {
    // The layout takes nothing of the look: the square and the switch are the same boxes whether the
    // ring or the keys fill it (the layout is one function of the window).
    const Window s{1366, 768, 1.0f};
    const LearnCardLayout a = learn_card_layout(area_for(s), s.u);
    const LearnCardLayout b = learn_card_layout(area_for(s), s.u);
    CHECK(a.slot.x0 == b.slot.x0);
    CHECK(a.slot.y1 == b.slot.y1);
    CHECK(a.toggle.x0 == b.toggle.x0);
    // At 1366x768 the ring has its largest square and the keys of the ring nearly 56 points across
    // (a dead gap between two takes a little of the square).
    CHECK(a.ring_side <= lt::kRingMax + kEps);
    CHECK(a.ring_side >= 0.97f * lt::kRingMax);
    const float key = key_ring_layout(a.slot, 1.0f).key;
    CHECK(key <= key_ring_tok::kMaxKey + kEps);
    CHECK(key >= 0.97f * key_ring_tok::kMaxKey);
}

TEST_CASE("learn card: at 1024x600 the column is compact: the ring keeps keys of a hit, the tempo and "
          "the name move to the header, the types are a strip and the hands come before Listen") {
    const Window s{1024, 600, 1.0f};
    const LearnCardLayout c = learn_card_layout(area_for(s), s.u);
    CHECK(c.column == LearnColumn::Compact);
    CHECK_FALSE(c.rows);
    CHECK(c.type_cells == 0);
    CHECK(c.ring_side <= lt::kRingMax + kEps);
    CHECK(c.ring_side >= key_ring_min_side(tok::kHit, 1.0f));
    const KeyRingLayout ring = key_ring_layout(c.slot, 1.0f);
    CHECK(ring.key >= tok::kHit);
    // The strip is above the keys, the foot row under them, the keyboard the rest of the width.
    CHECK(c.strip.y1 <= c.fingers_right.y0 + kEps);
    CHECK(c.strip.x0 == doctest::Approx(c.keyboard.x0));
    CHECK(c.strip.x1 == doctest::Approx(c.keyboard.x1));
    CHECK(c.strip.h() >= tok::kHit); // a hit tall, taller where the room allows
    CHECK(c.play.y0 >= c.fingers_left.y1 - kEps);
    // The hands sit before Listen and Practice in the top bar's order, left hand then right hand,
    // all in one row, Listen and Practice with their words.
    CHECK(c.hand_right.cy() == doctest::Approx(c.play.cy()));
    CHECK(c.hand_left.cy() == doctest::Approx(c.play.cy()));
    CHECK(c.practise.cy() == doctest::Approx(c.play.cy()));
    CHECK(c.hand_left.x1 <= c.hand_right.x0 + kEps);
    CHECK(c.hand_right.x1 <= c.play.x0 - tok::kS2 * s.u + kEps);
    CHECK(c.play.x1 <= c.practise.x0 - tok::kS2 * s.u + kEps);
    CHECK(c.foot_words);
    // The name is in the header, after the page's word, and the tempo is there too.
    CHECK(c.name_in_header);
    CHECK(inside(c.name, c.header));
    CHECK(inside(c.drill.tempo, c.header));
    CHECK(c.name.x0 >= c.title.x1 - kEps);
    // The keys start where the left column ends (the freed width is theirs).
    CHECK(c.keyboard.x0 >= c.slot.x1 + lt::kPad * s.u - kEps);
}

TEST_CASE("learn card: the types are two rows of seven wherever each fits its words, else a strip") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnCardLayout c = learn_card_layout(area, s.u);
        const float ub = learn_card_unit(area, s.u);
        if (!c.rows) {
            CHECK(c.type_cells == 0);
            CHECK(c.strip.w() > 0.0f);
            CHECK(c.strip.y1 <= c.fingers_right.y0 + kEps);
            continue;
        }
        CHECK(c.type_cells == kLearnTypes);
        CHECK(empty(c.strip));
        for (int i = 0; i < kLearnTypes; ++i) {
            const Box& cell = c.type[static_cast<std::size_t>(i)];
            CAPTURE(i);
            CHECK(cell.w() >= lt::kTypeCellMin * ub - kEps);
            CHECK(cell.h() == doctest::Approx(lt::kBigScale * tok::kHit * ub)); // bigger than a hit
            CHECK(cell.y0 == doctest::Approx(c.type[static_cast<std::size_t>(i / 7 * 7)].y0));
            CHECK(cell.y1 <= c.fingers_right.y0 + kEps);
            CHECK(cell.x0 >= c.keyboard.x0 - kEps);
            CHECK(cell.x1 <= c.keyboard.x1 + kEps);
            if (i % 7 != 0) {
                CHECK(cell.x0 > c.type[static_cast<std::size_t>(i - 1)].x1);
            }
        }
        CHECK(c.type[7].y0 >= c.type[0].y1 - kEps); // the second row under the first
        CHECK(c.type[6].x1 == doctest::Approx(c.keyboard.x1));
    }
    // The windows with room for them: 1280x720 and up, not 1024x600 and the 7 inch 800x480.
    for (const Window s : {Window{1280, 720, 1.0f}, Window{1366, 768, 1.0f}, Window{1920, 1080, 1.0f},
                           Window{1920, 1080, 1.4f}}) {
        CAPTURE(s.w);
        CHECK(learn_card_layout(area_for(s), s.u).rows);
    }
    for (const Window s : {Window{800, 480, 1.0f}, Window{1024, 600, 1.0f}}) {
        CAPTURE(s.w);
        CHECK_FALSE(learn_card_layout(area_for(s), s.u).rows);
    }
}

TEST_CASE("learn card: the foot row is the hands (square), Listen and Practice, as tall as each other, "
          "one group centred under the keyboard, at every size") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnCardLayout c = learn_card_layout(area, s.u);
        const float ub = learn_card_unit(area, s.u);
        for (const Box* b : {&c.hand_left, &c.hand_right, &c.play, &c.practise}) {
            CHECK(b->cy() == doctest::Approx(c.play.cy()));
        }
        CHECK(c.hand_left.w() == doctest::Approx(c.hand_left.h())); // a square
        CHECK(c.hand_right.w() == doctest::Approx(c.hand_right.h()));
        CHECK(c.hand_left.h() == doctest::Approx(c.play.h()));
        CHECK(c.hand_left.x1 <= c.hand_right.x0 + kEps);
        CHECK(c.hand_right.x1 <= c.play.x0 + kEps);
        CHECK(c.play.x1 <= c.practise.x0 + kEps);
        CHECK(c.play.y0 >= c.fingers_left.y1 - kEps);
        // The group is centred under the keyboard, and inside its column.
        CHECK((c.hand_left.x0 + c.practise.x1) * 0.5f == doctest::Approx(c.keyboard.cx()).epsilon(0.001));
        CHECK(c.hand_left.x0 >= c.keyboard.x0 - kEps);
        CHECK(c.practise.x1 <= c.keyboard.x1 + kEps);
        // Bigger than a hit wherever the room allows, and always a hit.
        CHECK(c.play.h() >= tok::kHit * ub - kEps);
        CHECK(c.play.h() <= lt::kBigScale * tok::kHit * ub + kEps);
        CHECK(c.play.w() >= tok::kHit * ub - kEps);
        CHECK(c.practise.w() >= tok::kHit * ub - kEps);
        if (c.rows) {
            CHECK(c.play.h() == doctest::Approx(lt::kBigScale * tok::kHit * ub));
        }
    }
    // With the room, both buttons carry their words at their full widths; the 7 inch panel shows icons.
    for (const Window s : {Window{1366, 768, 1.0f}, Window{1920, 1080, 1.0f}}) {
        const Box area = area_for(s);
        const LearnCardLayout c = learn_card_layout(area, s.u);
        const float ub = learn_card_unit(area, s.u);
        CHECK(c.foot_words);
        CHECK(c.play.w() == doctest::Approx(lt::kListenW * ub));
        CHECK(c.practise.w() == doctest::Approx(lt::kPracticeW * ub));
        CHECK(c.play.h() == doctest::Approx(lt::kBigScale * tok::kHit * ub));
    }
    // At 800x480 they carry their words too (the foot row holds them); at a taste scale above 1 the
    // window is under 800 design points wide, and they are icons.
    CHECK(learn_card_layout(area_for({800, 480, 1.0f}), 1.0f).foot_words);
    CHECK_FALSE(learn_card_layout(area_for({800, 480, 1.25f}), 1.25f).foot_words);
}

TEST_CASE("learn card: one keyboard as wide as its column, its keys never taller than natural, and "
          "the foot row level with the left column's last row") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnCardLayout c = learn_card_layout(area, s.u);
        const float ub = learn_card_unit(area, s.u);
        CHECK(c.keyboard.x0 >= c.slot.x1 + tok::kS2 * ub - kEps);
        CHECK(c.keyboard.x1 == doctest::Approx(area.x1 - lt::kPad * ub));
        CHECK(c.keyboard.h() <= c.keyboard.w() / 8.0f * lt::kKeyAspect + kEps);
        CHECK(c.fingers_right.y1 <= c.keyboard.y0 + kEps);
        CHECK(c.fingers_left.y0 >= c.keyboard.y1 - kEps);
        // The foot row ends the body (or ends the block both columns share, centred in it).
        CHECK(c.play.y1 <= area.y1 - lt::kPad * ub + kEps);
        // The left column's last row, the look's switch, is level with it (the Popover column has no
        // row at the foot: its settings stand under the keys).
        if (c.column != LearnColumn::Popover) {
            CHECK(c.toggle.cy() == doctest::Approx(c.play.cy()));
        }
    }
}

TEST_CASE("learn card: at 1920x1080 the card is the 1366x768 card grown to the window") {
    const Window big{1920, 1080, 1.0f};
    const Box area = area_for(big);
    const LearnCardLayout c = learn_card_layout(area, 1.0f);
    const float ub = learn_card_unit(area, 1.0f);
    CHECK(ub > 1.3f);
    CHECK(ub < 1.5f);
    // The ring is the largest its column lets it be, with keys of a hit.
    CHECK(c.ring_side >= 0.9f * lt::kRingMax * ub);
    CHECK(key_ring_layout(c.slot, ub).key >= tok::kHit);
    // Its chips are as tall as 1366's chips, grown, and its keys as natural as the keyboard at 800.
    REQUIRE(c.rows);
    CHECK(c.type[0].h() == doctest::Approx(lt::kBigScale * tok::kHit * ub));
    CHECK(c.keyboard.h() >= 0.85f * c.keyboard.w() / 8.0f * lt::kKeyAspect);
}

TEST_CASE("learn card: the left column holds the tempo over the name over the picker, then the "
          "settings' rows, where there is room for all of them") {
    for (const Window s : {Window{1366, 768, 1.0f}, Window{1920, 1080, 1.0f}}) {
        CAPTURE(s.w);
        const Box area = area_for(s);
        const LearnCardLayout c = learn_card_layout(area, s.u);
        REQUIRE(c.column == LearnColumn::Full);
        CHECK_FALSE(c.name_in_header);
        const float ub = learn_card_unit(area, s.u);
        // Top to bottom: tempo, name, the square, direction, octaves, scale / arpeggio, the switch.
        CHECK(c.drill.tempo.y1 <= c.name.y0 + kEps);
        CHECK(c.name.y1 <= c.slot.y0 + kEps);
        CHECK(c.slot.y1 <= c.drill.dir[0].y0 + kEps);
        CHECK(c.drill.dir[0].y1 <= c.drill.oct[0].y0 + kEps);
        CHECK(c.drill.oct[0].y1 <= c.drill.kind[0].y0 + kEps);
        CHECK(c.drill.kind[0].y1 <= c.toggle.y0 + kEps);
        // Each row is the column wide, its buttons a hit at the least, words where they fit.
        for (const Box* row : {&c.drill.dir.front(), &c.drill.oct.front(), &c.drill.kind.front()}) {
            CHECK(row->x0 == doctest::Approx(c.drill.tempo.x0));
        }
        CHECK(c.drill.dir.back().x1 == doctest::Approx(c.drill.tempo.x1));
        CHECK(c.drill.oct.back().x1 == doctest::Approx(c.drill.tempo.x1));
        CHECK(c.drill.kind.back().x1 == doctest::Approx(c.drill.tempo.x1));
        CHECK(c.drill.oct_words);
        CHECK(c.drill.tempo.h() >= tok::kHit * ub - kEps);
        // The switch sits under the settings, centred on the column, level with the foot row.
        CHECK(c.toggle.cx() == doctest::Approx(c.drill.tempo.cx()));
    }
}

TEST_CASE("learn card: a window too short for two rows of types and a keyboard gets the strip "
          "whatever its width") {
    const Box area{0.0f, 57.0f, 1300.0f, 57.0f + 380.0f};
    CHECK_FALSE(learn_card_layout(area, 1.0f).rows);
}

TEST_CASE("learn card: the left column is full where the body holds every row and a ring of keys of "
          "a hit, compact where it holds the ring and two rows, else the keys as buttons and one "
          "settings button") {
    CHECK(learn_card_layout(area_for({1366, 768, 1.0f}), 1.0f).column == LearnColumn::Full);
    // 12 mm hits and 2 mm gaps need more room than 4 mm ones did: the ring, two rows and the foot row
    // fit from 1024x600 down to 1280x720, and the 800x480 body holds only the buttons and one button.
    CHECK(learn_card_layout(area_for({1280, 720, 1.0f}), 1.0f).column == LearnColumn::Compact);
    CHECK(learn_card_layout(area_for({1024, 600, 1.0f}), 1.0f).column == LearnColumn::Compact);
    CHECK(learn_card_layout(area_for({800, 480, 1.0f}), 1.0f).column == LearnColumn::Popover);
    // The compact column: the ring over the direction row over one row of octaves, scale and
    // arpeggio and the look's switch, the tempo in the header.
    const Window s{1024, 600, 1.0f};
    const LearnCardLayout c = learn_card_layout(area_for(s), s.u);
    CHECK(c.slot.y1 <= c.drill.dir[0].y0 + kEps);
    CHECK(c.drill.dir[0].y1 <= c.drill.oct[0].y0 + kEps);
    CHECK(c.drill.oct[1].x1 <= c.drill.kind[0].x0 + kEps);
    CHECK(c.drill.kind[1].x1 <= c.toggle.x0 + kEps);
    CHECK(c.drill.oct[0].cy() == doctest::Approx(c.toggle.cy()));
    CHECK(c.drill.oct[0].cy() == doctest::Approx(c.drill.kind[0].cy()));
    CHECK(inside(c.drill.tempo, c.header));
    CHECK(c.slot.w() >= key_ring_min_side(tok::kHit, s.u) - kEps);
}

namespace {

// The settings that stand under the keys in the Popover column, in order: tempo, direction, octaves,
// scale / arpeggio (or the chord card's inversion), as named boxes.
learn_test::Controls inline_settings(const LearnCardLayout& c) {
    learn_test::Controls out;
    if (!empty(c.drill.tempo)) {
        out.push_back({"tempo", c.drill.tempo});
    }
    for (std::size_t i = 0; i < c.drill.dir.size(); ++i) {
        if (!empty(c.drill.dir[i])) {
            out.push_back({"dir " + std::to_string(i), c.drill.dir[i]});
        }
    }
    for (std::size_t i = 0; i < c.drill.oct.size(); ++i) {
        if (!empty(c.drill.oct[i])) {
            out.push_back({"oct " + std::to_string(i), c.drill.oct[i]});
        }
    }
    for (std::size_t i = 0; i < c.drill.kind.size(); ++i) {
        if (!empty(c.drill.kind[i])) {
            out.push_back({"kind " + std::to_string(i), c.drill.kind[i]});
        }
    }
    if (!empty(c.drill.stepper)) {
        out.push_back({"stepper", c.drill.stepper});
    }
    return out;
}

learn_test::Controls card_settings(const LearnCardLayout& c) {
    learn_test::Controls out;
    if (!empty(c.pop.tempo)) {
        out.push_back({"tempo", c.pop.tempo});
    }
    for (std::size_t i = 0; i < c.pop.dir.size(); ++i) {
        if (!empty(c.pop.dir[i])) {
            out.push_back({"dir " + std::to_string(i), c.pop.dir[i]});
        }
    }
    for (std::size_t i = 0; i < c.pop.oct.size(); ++i) {
        if (!empty(c.pop.oct[i])) {
            out.push_back({"oct " + std::to_string(i), c.pop.oct[i]});
        }
    }
    for (std::size_t i = 0; i < c.pop.kind.size(); ++i) {
        if (!empty(c.pop.kind[i])) {
            out.push_back({"kind " + std::to_string(i), c.pop.kind[i]});
        }
    }
    if (!empty(c.pop.stepper)) {
        out.push_back({"stepper", c.pop.stepper});
    }
    return out;
}

} // namespace

TEST_CASE("learn card: a body too short for a ring of keys of a hit shows the buttons, as many settings "
          "under them as fit and Options right under the last, the rest in its card") {
    // 800x480 at the scale 1.0, and at the unit the 7 inch panel's size makes.
    const float panel_u = etude::panel_px_per_mm(800.0f, 480.0f, {154.0f, 86.0f}) /
                          etude::kFallbackPxPerMm;
    for (const LearnCells& cells : {kScaleCells, kChordCells}) {
        for (const Window s : {Window{800, 480, 1.0f}, Window{800, 480, panel_u}}) {
            CAPTURE(s.u);
            CAPTURE(cells.kinds);
            const Box area = area_for(s);
            const LearnCardLayout c = learn_card_layout(area, s.u, cells);
            CHECK_FALSE(c.rows);
            CHECK_FALSE(c.ring_fits);
            CHECK(c.column == LearnColumn::Popover);
            CHECK(empty(c.toggle));
            const float ub = learn_card_unit(area, s.u);
            // The buttons fit the slot with each a hit, and the slot ends inside the window.
            CHECK(inside(c.slot, area));
            for (const Box& b : learn_key_grid(c.slot, s.u)) {
                CHECK(at_least_a_hit(b, s.u));
                CHECK(inside(b, c.slot));
            }
            CHECK(c.name_in_header);
            CHECK(inside(c.name, area));
            CHECK(c.strip.w() >= 2.0f * tok::kChipMinW * s.u);
            // The settings that fit stand under the keys in order, each a hit, a gap under the last
            // control above, with no empty column: Options follows the last of them directly.
            const learn_test::Controls inl = inline_settings(c);
            float below = c.slot.y1;
            for (std::size_t i = 0; i < inl.size(); ++i) {
                CAPTURE(inl[i].first);
                CHECK(at_least_a_hit(inl[i].second, ub));
                CHECK(inside(inl[i].second, area));
                CHECK(inl[i].second.y0 >= c.slot.y1 + tok::kS2 * ub - kEps);
                for (std::size_t j = i + 1; j < inl.size(); ++j) {
                    INFO(inl[i].first << " and " << inl[j].first);
                    CHECK(apart(inl[i].second, inl[j].second, ub));
                }
                below = std::max(below, inl[i].second.y1);
            }
            if (c.options) {
                CHECK(at_least_a_hit(c.settings, ub));
                CHECK(inside(c.settings, area));
                CHECK(c.settings.y0 >= below + tok::kHitGap * ub - kEps);
                CHECK(c.settings.y0 <= below + tok::kHitGap * ub + kEps); // right under the last row
                CHECK(c.slot.y1 <= c.settings.y0 + kEps);
                CHECK(inside(c.popover, area));
                // Its card holds the settings that did not fit, each a hit, none touching another.
                const learn_test::Controls pop = card_settings(c);
                CHECK_FALSE(pop.empty());
                for (std::size_t i = 0; i < pop.size(); ++i) {
                    CAPTURE(pop[i].first);
                    CHECK(inside(pop[i].second, c.popover));
                    CHECK(inside(pop[i].second, area));
                    CHECK(at_least_a_hit(pop[i].second, ub));
                    for (std::size_t j = i + 1; j < pop.size(); ++j) {
                        INFO(pop[i].first << " and " << pop[j].first);
                        CHECK(apart(pop[i].second, pop[j].second, ub));
                    }
                }
                // Every setting is in one place: under the keys or in the card.
                CHECK(inl.size() + pop.size() == (cells.kinds ? 1u + 3u + 2u + 2u : 1u + 3u + 2u + 1u));
            } else {
                CHECK(empty(c.settings));
                CHECK(empty(c.popover));
                CHECK(inl.size() == (cells.kinds ? 1u + 3u + 2u + 2u : 1u + 3u + 2u + 1u));
            }
        }
    }
    // The ring still fits where the body holds it with its two rows.
    const Window ok{1024, 600, 1.0f};
    CHECK(learn_card_layout(area_for(ok), ok.u).ring_fits);
}

TEST_CASE("learn card: at 800x480 the tempo and the direction stand under the keys with Options right "
          "under them; on the 7 inch panel, where only the keys and Options fit, Options is under the keys") {
    const LearnCardLayout a = learn_card_layout(area_for({800, 480, 1.0f}), 1.0f);
    CHECK(a.inline_rows == 2);
    CHECK(a.options);
    CHECK_FALSE(empty(a.drill.tempo));
    CHECK_FALSE(empty(a.drill.dir[0]));
    CHECK(empty(a.drill.oct[0])); // octaves and scale / arpeggio are in the card
    CHECK_FALSE(empty(a.pop.oct[0]));
    CHECK_FALSE(empty(a.pop.kind[0]));
    const float panel_u = etude::panel_px_per_mm(800.0f, 480.0f, {154.0f, 86.0f}) /
                          etude::kFallbackPxPerMm;
    const LearnCardLayout p = learn_card_layout(area_for({800, 480, panel_u}), panel_u);
    CHECK(p.inline_rows == 0);
    CHECK(p.options);
    CHECK(p.settings.y0 == doctest::Approx(p.slot.y1 + tok::kHitGap * panel_u));
}

TEST_CASE("learn card: the twelve keys are four across and three down, or three and four, "
          "whichever is larger in the room") {
    const float u = 1.0f;
    const float tight = tok::kHitGap * u;
    // A square room, and a room wider than tall: four across.
    CHECK(learn_grid_columns(264.0f, 264.0f, tight) == 4);
    CHECK(learn_grid_columns(282.0f, 210.0f, tight) == 4);
    // A room taller than wide: three across and four down.
    CHECK(learn_grid_columns(150.0f, 300.0f, tight) == 3);
    CHECK(learn_grid_columns(200.0f, 330.0f, tight) == 3);
    // Whatever the shape, the twelve fit the room with each button as large as the shape allows.
    for (const Box room : {Box{0, 0, 264, 264}, Box{0, 0, 282, 210}, Box{0, 0, 150, 300},
                           Box{0, 0, 200, 330}, Box{10, 20, 310, 200}}) {
        const std::array<Box, 12> grid = learn_key_grid(room, u);
        for (std::size_t i = 0; i < grid.size(); ++i) {
            CHECK(inside(grid[i], room));
            CHECK(grid[i].w() > 0.0f);
            for (std::size_t j = i + 1; j < grid.size(); ++j) {
                CHECK_FALSE(overlap(grid[i], grid[j]));
            }
        }
        // In keyboard order, row by row.
        CHECK(grid[0].y0 <= grid[11].y0);
        CHECK(grid[0].x0 <= grid[1].x0);
    }
    // Three across in the tall room, four in the wide one.
    const std::array<Box, 12> tall = learn_key_grid({0, 0, 150, 300}, u);
    CHECK(tall[2].x0 > tall[1].x0);
    CHECK(tall[3].y0 > tall[0].y0);
    const std::array<Box, 12> wide = learn_key_grid({0, 0, 282, 210}, u);
    CHECK(wide[3].y0 == doctest::Approx(wide[0].y0));
    CHECK(wide[4].y0 > wide[0].y0);
}

TEST_CASE("learn card: the finger badges never touch, for every key and scale type") {
    for (const Window& s : kWindows) {
        const LearnCardLayout c = learn_card_layout(area_for(s), s.u);
        CAPTURE(s.w);
        CAPTURE(s.u);
        CHECK(c.badge > 0.0f);
        for (const ScaleType type : kAllScaleTypes) {
            for (int root = 0; root < 12; ++root) {
                const ScaleDiagram d = scale_diagram(root, type);
                std::vector<Box> badges;
                for (const DiagramKey& key : d.keys) {
                    if (key.right > 0) {
                        badges.push_back(
                            learn_badge_box(c.fingers_right, c.keyboard, key, d.white_keys, c.badge));
                    }
                }
                for (std::size_t i = 0; i < badges.size(); ++i) {
                    CHECK(inside(badges[i], c.fingers_right));
                    for (std::size_t j = i + 1; j < badges.size(); ++j) {
                        CHECK_FALSE(overlap(badges[i], badges[j]));
                    }
                }
                // Every key of the diagram is inside the keyboard, and black keys never overlap.
                for (std::size_t i = 0; i < d.keys.size(); ++i) {
                    const Box a = learn_key_box(c.keyboard, d.keys[i], d.white_keys);
                    CHECK(inside(a, c.keyboard));
                    for (std::size_t j = i + 1; j < d.keys.size(); ++j) {
                        if (d.keys[i].black && d.keys[j].black) {
                            CHECK_FALSE(overlap(a, learn_key_box(c.keyboard, d.keys[j], d.white_keys)));
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("learn types: fourteen names, full and short, in the order of the scale types") {
    CHECK(kLearnTypeBrief.size() == kAllScaleTypes.size());
    for (std::size_t i = 0; i < kAllScaleTypes.size(); ++i) {
        CHECK(std::string(learn_type_words(i)) == scale_type_name(kAllScaleTypes[i]));
        CHECK(std::string(kLearnTypeBrief[i]).size() > 0);
        CHECK(std::string(kLearnTypeBrief[i]).size() <= 11);
    }
}

TEST_CASE("learn card: the rows are used only where the measured widest name fits a cell, in a size "
          "that holds it") {
    // The widest name's need at the body size is the app's measurement in the font; here a few
    // values stand for it. 1366x768 has cells of about 138.6 points, 1280x720 of about 127.
    const Window a{1366, 768, 1.0f};
    const Window b{1280, 720, 1.0f};
    const auto cells = [](float cell_min) {
        LearnCells c = kScaleCells;
        c.cell_min = cell_min;
        return c;
    };
    CHECK(learn_card_layout(area_for(a), a.u, cells(135.2f)).rows);
    CHECK_FALSE(learn_card_layout(area_for(a), a.u, cells(150.0f)).rows);
    CHECK(learn_card_layout(area_for(b), b.u, cells(120.0f)).rows);
    CHECK_FALSE(learn_card_layout(area_for(b), b.u, cells(135.2f)).rows);
    // The size: the largest up to the chips' own that holds the widest name, never below the body
    // size.
    CHECK(learn_label_scale(138.6f, 135.2f) == doctest::Approx(138.6f / 135.2f));
    CHECK(learn_label_scale(200.0f, 100.0f) == doctest::Approx(lt::kTypeLabelScale));
    CHECK(learn_label_scale(130.0f, 135.2f) == doctest::Approx(1.0f));
    // At the size chosen the widest name (scaled with it) is never wider than its cell.
    for (const float need : {100.0f, 120.0f, 135.2f}) {
        for (const float cell : {need, need + 1.0f, 1.3f * need}) {
            CHECK(need * learn_label_scale(cell, need) <= cell + 1e-3f);
        }
    }
}

TEST_CASE("learn gallery: on the 7 inch panel (800x480 at the scale a 12 mm hit makes) a tile is one "
          "row: the keys at natural proportion at the left of the title, a hit tall at least") {
    for (const float u : {1.35f, 1.4f, 1.45f}) {
        const Window s{800, 480, u};
        CAPTURE(u);
        const Box area = area_for(s);
        const LearnGalleryLayout g = learn_gallery_layout(area, s.u);
        for (const LearnTileBoxes& t : g.tiles) {
            CHECK(t.side);
            CHECK(at_least_a_hit(t.tile, s.u));
            CHECK(inside(t.tile, area));
            // The picture: nearly a hit tall (the tile is the hit), the keys at their natural proportion (eight white
            // keys across, `kPreviewKeyAspect` tall to a key's width), inside the tile.
            CHECK(t.preview.h() >= 0.9f * tok::kHit * s.u);
            CHECK(t.preview.h() == doctest::Approx(t.preview.w() / 8.0f * lt::kPreviewKeyAspect));
            CHECK(t.preview.w() <= t.tile.w() * lt::kSidePicShare + kEps);
            CHECK(inside(t.preview, t.tile));
            // Then the title, then the arrow, in one row, none touching.
            CHECK(t.preview.x1 < t.text.x0);
            CHECK(t.text.x1 <= t.chevron.x0);
            CHECK(inside(t.text, t.tile));
            CHECK(inside(t.chevron, t.tile));
            CHECK_FALSE(overlap(t.preview, t.text));
            CHECK_FALSE(overlap(t.text, t.chevron));
            CHECK(t.text.h() >= tok::kHit * s.u - kEps);
            CHECK(empty(t.icon));
            // The title has room beside the picture for the longest name in the title type (the
            // font's advances are an upper bound of what the app draws).
            CHECK(t.text.w() >= etude::test::text_em(true, "Chords") * tok::kTitle * s.u);
        }
        // The Continue row comes back where it fits, and then touches no tile.
        if (!empty(g.cont)) {
            CHECK(inside(g.cont, area));
            for (const LearnTileBoxes& t : g.tiles) {
                CHECK_FALSE(overlap(t.tile, g.cont));
            }
        }
    }
}

TEST_CASE("learn card: on the 7 inch panel (154x86 mm at 800x480) every control is 12 mm and no two are "
          "closer than 2 mm, measured in real millimetres") {
    // The unit the panel's size makes: a design point is 1/96 inch on its real size.
    const float px_per_mm = etude::panel_px_per_mm(800.0f, 480.0f, {154.0f, 86.0f});
    const float u = px_per_mm / etude::kFallbackPxPerMm;
    const Window s{800, 480, u};
    const Box area = area_for(s);
    const LearnCardLayout c = learn_card_layout(area, u);
    // The panel at its real size is 560 design points wide: Listen and Practice are icons there (the
    // words come at 800 design points, as at 800x480 at the scale 1.0).
    CHECK_FALSE(c.foot_words);
    const auto controls = controls_of(c);
    REQUIRE_FALSE(controls.empty());
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const Box& a = controls[i].second;
        if (empty(a)) {
            continue;
        }
        CAPTURE(controls[i].first);
        CHECK(a.w() / px_per_mm >= etude::kMinHitMm - 0.05f);
        CHECK(a.h() / px_per_mm >= etude::kMinHitMm - 0.05f);
        for (std::size_t j = i + 1; j < controls.size(); ++j) {
            const Box& b = controls[j].second;
            if (empty(b)) {
                continue;
            }
            CAPTURE(controls[j].first);
            const float dx = std::max(a.x0 - b.x1, b.x0 - a.x1);
            const float dy = std::max(a.y0 - b.y1, b.y0 - a.y1);
            CHECK(std::max(dx, dy) / px_per_mm >= etude::kMinHitGapMm - 0.05f);
        }
    }
}
