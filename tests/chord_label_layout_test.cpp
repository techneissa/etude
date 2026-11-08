// SPDX-License-Identifier: GPL-3.0-or-later
// Where the chord label stands (chord_label_layout.hpp): inside its strip at every screen and
// scale, and never over what shares its height.
#include "etude/chord_label_layout.hpp"

#include "etude/look.hpp"
#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <vector>

using namespace etude;

namespace {

bool overlaps(float a0, float a1, float b0, float b1) { return a0 < b1 && a1 > b0; }

constexpr float kCapEm = 0.7f;
constexpr float kCapTopEm = 0.2f;

} // namespace

TEST_CASE("chord label layout: the name, its line and its ticks lie inside the strip") {
    int checked = 0;
    for (const auto& [w, h] : std::vector<std::pair<float, float>>{
             {800, 480}, {1366, 768}, {1920, 1080}, {2560, 1440}}) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            for (const float size : {kMinChordLabelSize, 1.0f, kMaxChordLabelSize}) {
                LookSettings look;
                look.chord_label_size = size;
                const float ppmm = kDefaultPxPerMm * scale;
                const ScreenLayout l = compute_layout(w, h, ppmm, look, {}, 1.0f, 0.0f);
                if (!l.chord_label_visible) {
                    continue;
                }
                ++checked;
                const ChordLabelGeometry g = chord_label_geometry(
                    l.chord_label, l.chord_label_font_px, scale * h / 768.0f, kCapEm, kCapTopEm);
                INFO(w << "x" << h << " x" << scale << " size " << size);
                // The root's capitals start under the strip's top; the line and the ticks end
                // inside its foot.
                CHECK(g.name_top + kCapTopEm * l.chord_label_font_px >= l.chord_label.y);
                CHECK(g.baseline < g.line_y - g.tick_height);
                CHECK(g.line_y + g.line_thickness <= l.chord_label.bottom());
                CHECK(g.line_y - g.tick_height >= l.chord_label.y);
                CHECK(g.line_thickness >= 1.0f);
                CHECK(g.tick_height >= 2.0f);
                CHECK(g.margin > 0.0f);
                CHECK(g.pad > 0.0f);
                // The strip never reaches into the note area.
                CHECK(l.chord_label.bottom() <= l.note_area.y + 0.5f);
            }
        }
    }
    CHECK(checked >= 24);
}

TEST_CASE("chord label layout: the label's strip is smaller than the old banner's") {
    LookSettings look;
    const ScreenLayout l = compute_layout(1366, 768, kDefaultPxPerMm, look, {}, 1.0f, 0.0f);
    REQUIRE(l.chord_label_visible);
    CHECK(l.chord_label.h < 0.07f * 768.0f); // under 54 px at 1366x768, the old strip was 90
    CHECK(l.chord_label_font_px >= kMinChordFontPx);
}

TEST_CASE("chord label layout: only what shares the strip's height blocks it") {
    const RectF strip{0, 100, 1000, 40};
    const std::array<RectF, 5> rects{{
        {800, 90, 100, 30}, // the combo, over the strip's top
        {300, 140, 50, 30}, // under it: touches only the edge
        {500, 141, 50, 30}, // under it
        {100, 50, 60, 40},  // over it
        {0, 0, 0, 0},       // empty
    }};
    std::array<ChordSpan, 8> out{};
    const std::size_t n = chord_label_blockers(strip, rects, 8.0f, out);
    REQUIRE(n == 1);
    CHECK(out[0].x0 == 792.0f);
    CHECK(out[0].x1 == 908.0f);
}

TEST_CASE("chord label layout: a name centres over its keys, inside the margins") {
    const std::array<ChordNameWant, 1> want{{{100.0f, 500.0f, 0}}};
    std::array<ChordNamePlace, 1> out{};
    place_chord_names(want, 14.0f, 1352.0f, 8.0f, {}, out);
    REQUIRE(out[0].placed);
    CHECK(out[0].left == 450.0f);
    // Over the left edge: it keeps its margin.
    const std::array<ChordNameWant, 1> left_edge{{{100.0f, 20.0f, 0}}};
    place_chord_names(left_edge, 14.0f, 1352.0f, 8.0f, {}, out);
    CHECK(out[0].left == 14.0f);
    // Over the right edge.
    const std::array<ChordNameWant, 1> right_edge{{{100.0f, 1350.0f, 0}}};
    place_chord_names(right_edge, 14.0f, 1352.0f, 8.0f, {}, out);
    CHECK(out[0].left + 100.0f == 1352.0f);
}

TEST_CASE(
    "chord label layout: a name steps aside to the nearest free place, never over a blocker") {
    const std::array<ChordSpan, 1> combo{{{470.0f, 530.0f}}};
    const std::array<ChordNameWant, 1> want{{{100.0f, 500.0f, 0}}}; // wants 450..550
    std::array<ChordNamePlace, 1> out{};
    place_chord_names(want, 14.0f, 1352.0f, 8.0f, combo, out);
    REQUIRE(out[0].placed);
    CHECK_FALSE(overlaps(out[0].left, out[0].left + 100.0f, 470.0f, 530.0f));
    // The nearer side: to the left it needs 370, to the right 530; 450 is 80 from 370 and 80 from
    // 530, so the left one wins a tie.
    CHECK((out[0].left == 370.0f || out[0].left == 530.0f));

    // Nearer to the right.
    const std::array<ChordNameWant, 1> right_of{{{100.0f, 520.0f, 0}}}; // wants 470..570
    place_chord_names(right_of, 14.0f, 1352.0f, 8.0f, combo, out);
    CHECK(out[0].left == 530.0f);
    // Nearer to the left.
    const std::array<ChordNameWant, 1> left_of{{{100.0f, 480.0f, 0}}}; // wants 430..530
    place_chord_names(left_of, 14.0f, 1352.0f, 8.0f, combo, out);
    CHECK(out[0].left == 370.0f);
}

TEST_CASE("chord label layout: a blocker at the edge sends the name to the other side") {
    const std::array<ChordSpan, 1> corner{{{1200.0f, 1352.0f}}};
    const std::array<ChordNameWant, 1> want{{{100.0f, 1300.0f, 0}}};
    std::array<ChordNamePlace, 1> out{};
    place_chord_names(want, 14.0f, 1352.0f, 8.0f, corner, out);
    REQUIRE(out[0].placed);
    CHECK(out[0].left + 100.0f <= 1200.0f);
}

TEST_CASE("chord label layout: no free place leaves the name out") {
    const std::array<ChordSpan, 1> wall{{{0.0f, 2000.0f}}};
    const std::array<ChordNameWant, 1> want{{{100.0f, 500.0f, 0}}};
    std::array<ChordNamePlace, 1> out{};
    place_chord_names(want, 14.0f, 1352.0f, 8.0f, wall, out);
    CHECK_FALSE(out[0].placed);
    // A name wider than the window.
    const std::array<ChordNameWant, 1> huge{{{2000.0f, 500.0f, 0}}};
    place_chord_names(huge, 14.0f, 1352.0f, 8.0f, {}, out);
    CHECK_FALSE(out[0].placed);
}

TEST_CASE("chord label layout: two hands' names never overlap, each steps aside from the first") {
    // Both want the same place; the second group moves clear of the first (and its pad).
    const std::array<ChordNameWant, 2> wants{{{100.0f, 500.0f, 0}, {100.0f, 520.0f, 1}}};
    std::array<ChordNamePlace, 2> out{};
    place_chord_names(wants, 14.0f, 1352.0f, 8.0f, {}, out);
    REQUIRE(out[0].placed);
    REQUIRE(out[1].placed);
    CHECK(out[0].left == 450.0f);
    CHECK_FALSE(
        overlaps(out[0].left - 8.0f, out[0].left + 108.0f, out[1].left, out[1].left + 100.0f));
}

TEST_CASE("chord label layout: a hand's two labels crossfading may stand over one another") {
    const std::array<ChordNameWant, 2> wants{{{100.0f, 500.0f, 0}, {100.0f, 500.0f, 0}}};
    std::array<ChordNamePlace, 2> out{};
    place_chord_names(wants, 14.0f, 1352.0f, 8.0f, {}, out);
    CHECK(out[0].left == 450.0f);
    CHECK(out[1].left == 450.0f);
}

TEST_CASE("chord label layout: with real blockers at real screens a name never touches them") {
    for (const auto& [w, h] :
         std::vector<std::pair<float, float>>{{800, 480}, {1366, 768}, {1920, 1080}}) {
        LookSettings look;
        const ScreenLayout l = compute_layout(w, h, kDefaultPxPerMm * 1.0f, look, {}, 1.0f, 0.0f);
        REQUIRE(l.chord_label_visible);
        const float unit = h / 768.0f;
        const ChordLabelGeometry g =
            chord_label_geometry(l.chord_label, l.chord_label_font_px, unit, kCapEm, kCapTopEm);
        // A combo, an offer card and a key label that all stand across the strip's height.
        const std::array<RectF, 3> rects{{
            {w - 160.0f * unit, l.chord_label.y - 4.0f, 150.0f * unit, 50.0f * unit},
            {w * 0.4f, l.chord_label.y, w * 0.2f, 60.0f * unit},
            {10.0f, l.chord_label.y + 2.0f, 90.0f * unit, 20.0f * unit},
        }};
        std::array<ChordSpan, kChordMaxBlockers> blocked{};
        const std::size_t n = chord_label_blockers(l.chord_label, rects, g.pad, blocked);
        REQUIRE(n == 3);
        for (float centre = g.margin; centre < w; centre += 37.0f) {
            std::array<ChordNameWant, 2> wants{
                {{90.0f * unit, centre, 0}, {70.0f * unit, centre, 1}}};
            std::array<ChordNamePlace, 2> out{};
            place_chord_names(wants, g.margin, w - g.margin, g.pad,
                              std::span<const ChordSpan>(blocked.data(), n), out);
            for (std::size_t i = 0; i < 2; ++i) {
                if (!out[i].placed) {
                    continue;
                }
                const float x1 = out[i].left + wants[i].width;
                CHECK(out[i].left >= g.margin);
                CHECK(x1 <= w - g.margin);
                for (const RectF& r : rects) {
                    CHECK_FALSE(overlaps(out[i].left, x1, r.x, r.right()));
                }
            }
            if (out[0].placed && out[1].placed) {
                CHECK_FALSE(overlaps(out[0].left, out[0].left + wants[0].width, out[1].left,
                                     out[1].left + wants[1].width));
            }
        }
    }
}

TEST_CASE("chord label layout: the line is cut round what shares its height and keeps its ends") {
    const std::array<ChordSpan, 1> hole{{{300.0f, 400.0f}}};
    std::array<ChordSpan, 4> runs{};
    std::size_t n = chord_line_runs(200.0f, 600.0f, hole, runs);
    REQUIRE(n == 2);
    CHECK(runs[0].x0 == 200.0f);
    CHECK(runs[0].x1 == 300.0f);
    CHECK(runs[1].x0 == 400.0f);
    CHECK(runs[1].x1 == 600.0f);
    // Nothing in the way: one run. Everything in the way: none.
    n = chord_line_runs(200.0f, 600.0f, {}, runs);
    REQUIRE(n == 1);
    CHECK(runs[0].x0 == 200.0f);
    CHECK(runs[0].x1 == 600.0f);
    const std::array<ChordSpan, 1> all{{{0.0f, 1000.0f}}};
    CHECK(chord_line_runs(200.0f, 600.0f, all, runs) == 0);
    // A run under a pixel is dropped.
    const std::array<ChordSpan, 1> almost{{{200.5f, 700.0f}}};
    CHECK(chord_line_runs(200.0f, 600.0f, almost, runs) == 0);
}
