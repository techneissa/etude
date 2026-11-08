// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/accidental_glyph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <doctest/doctest.h>

using namespace etude;

namespace {

bool whole(float v) { return v == std::round(v); }

// How many quads cover the point (a point on an edge counts for none).
int covers(const AccidentalShape& s, float px, float py) {
    int n = 0;
    for (std::size_t q = 0; q < s.count; ++q) {
        const GlyphQuad& g = s.quads[q];
        bool inside = true;
        for (std::size_t i = 0; i < 4 && inside; ++i) {
            const std::size_t j = (i + 1) % 4;
            const float cross =
                (g.x[j] - g.x[i]) * (py - g.y[i]) - (g.y[j] - g.y[i]) * (px - g.x[i]);
            const float side =
                (g.x[1] - g.x[0]) * (g.y[2] - g.y[1]) - (g.y[1] - g.y[0]) * (g.x[2] - g.x[1]);
            inside = side > 0.0f ? cross > 0.0f : cross < 0.0f;
        }
        n += inside ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("the accidental pieces of a name map to glyph kinds") {
    CHECK(accidental_kind("#") == AccidentalKind::Sharp);
    CHECK(accidental_kind("b") == AccidentalKind::Flat);
    CHECK(accidental_kind("") == AccidentalKind::None);
    CHECK(accidental_kind("x") == AccidentalKind::None);
    CHECK(accidental_kind("##") == AccidentalKind::None);
}

TEST_CASE("a glyph is at least 6 px tall, sized from the letter's capitals") {
    CHECK(kMinAccidentalGlyphPx == 6.0f);
    CHECK(accidental_glyph_height_px(5.0f) == 6.0f);
    CHECK(accidental_glyph_height_px(0.0f) == 6.0f);
    CHECK(accidental_glyph_height_px(-3.0f) == 6.0f);
    CHECK(accidental_glyph_height_px(std::numeric_limits<float>::quiet_NaN()) == 6.0f);
    CHECK(accidental_glyph_height_px(8.0f) == 6.0f); // 0.75 * 8
    CHECK(accidental_glyph_height_px(12.0f) == 9.0f);
    CHECK(accidental_glyph_height_px(20.0f) == 15.0f);
    // Never shrinks as the letter grows.
    float last = 0.0f;
    for (float cap = 1.0f; cap <= 60.0f; cap += 0.5f) {
        const float h = accidental_glyph_height_px(cap);
        CHECK(h >= last);
        CHECK(whole(h));
        last = h;
    }
}

TEST_CASE("a glyph's width follows its kind and height") {
    CHECK(accidental_glyph_width_px(AccidentalKind::None, 8.0f) == 0.0f);
    CHECK(accidental_glyph_width_px(AccidentalKind::Sharp, 6.0f) == 5.0f);
    CHECK(accidental_glyph_width_px(AccidentalKind::Flat, 6.0f) == 3.0f);
    CHECK(accidental_glyph_width_px(AccidentalKind::Sharp, 12.0f) == 9.0f);
    CHECK(accidental_glyph_width_px(AccidentalKind::Flat, 12.0f) == 6.0f);
    // A height under the minimum is the minimum.
    CHECK(accidental_glyph_width_px(AccidentalKind::Sharp, 2.0f) == 5.0f);
    for (float h = 6.0f; h <= 40.0f; h += 1.0f) {
        CHECK(whole(accidental_glyph_width_px(AccidentalKind::Sharp, h)));
        CHECK(whole(accidental_glyph_width_px(AccidentalKind::Flat, h)));
    }
}

TEST_CASE("every glyph is whole pixels, strokes at least 1 px, inside its box") {
    for (AccidentalKind kind : {AccidentalKind::Sharp, AccidentalKind::Flat}) {
        for (float h = 6.0f; h <= 40.0f; h += 1.0f) {
            const AccidentalShape s = accidental_shape(kind, h);
            INFO("height " << h);
            REQUIRE(s.count > 0);
            CHECK(s.height == h);
            CHECK(s.width == accidental_glyph_width_px(kind, h));
            for (std::size_t q = 0; q < s.count; ++q) {
                const GlyphQuad& g = s.quads[q];
                float min_x = 1e9f;
                float max_x = -1e9f;
                float min_y = 1e9f;
                float max_y = -1e9f;
                for (std::size_t i = 0; i < 4; ++i) {
                    CHECK(whole(g.x[i]));
                    // The glyph's outline is on whole pixels; only a junction with a slanted bar
                    // (a corner inside the glyph) may sit between rows.
                    if (g.y[i] != 0.0f && g.y[i] != s.height && h < 8.0f) {
                        CHECK(whole(g.y[i]));
                    }
                    CHECK(g.x[i] >= 0.0f);
                    CHECK(g.x[i] <= s.width);
                    CHECK(g.y[i] >= 0.0f);
                    CHECK(g.y[i] <= s.height);
                    min_x = std::min(min_x, g.x[i]);
                    max_x = std::max(max_x, g.x[i]);
                    min_y = std::min(min_y, g.y[i]);
                    max_y = std::max(max_y, g.y[i]);
                }
                // A stroke is at least a pixel across in both directions.
                CHECK(max_x - min_x >= 1.0f);
                CHECK(max_y - min_y >= 1.0f);
            }
        }
    }
}

TEST_CASE("the glyph reaches all four edges of its box") {
    for (AccidentalKind kind : {AccidentalKind::Sharp, AccidentalKind::Flat}) {
        for (float h : {6.0f, 8.0f, 10.0f, 15.0f}) {
            const AccidentalShape s = accidental_shape(kind, h);
            float min_x = 1e9f;
            float max_x = -1e9f;
            float min_y = 1e9f;
            float max_y = -1e9f;
            for (std::size_t q = 0; q < s.count; ++q) {
                for (std::size_t i = 0; i < 4; ++i) {
                    min_x = std::min(min_x, s.quads[q].x[i]);
                    max_x = std::max(max_x, s.quads[q].x[i]);
                    min_y = std::min(min_y, s.quads[q].y[i]);
                    max_y = std::max(max_y, s.quads[q].y[i]);
                }
            }
            CHECK(min_x == 0.0f);
            CHECK(max_x == s.width);
            CHECK(min_y == 0.0f);
            CHECK(max_y == s.height);
        }
    }
}

TEST_CASE("the quads do not overlap, so a translucent fill has no seams") {
    for (AccidentalKind kind : {AccidentalKind::Sharp, AccidentalKind::Flat}) {
        for (float h = 6.0f; h <= 24.0f; h += 1.0f) {
            const AccidentalShape s = accidental_shape(kind, h);
            int sampled = 0;
            int double_covered = 0;
            for (float y = 0.05f; y < s.height; y += 0.1f) {
                for (float x = 0.05f; x < s.width; x += 0.1f) {
                    const int n = covers(s, x, y);
                    sampled += n > 0 ? 1 : 0;
                    double_covered += n > 1 ? 1 : 0;
                }
            }
            INFO("kind " << static_cast<int>(kind) << " height " << h);
            CHECK(sampled > 0);
            CHECK(static_cast<float>(double_covered) <= 0.005f * static_cast<float>(sampled));
        }
    }
}

TEST_CASE("a sharp is two strokes and two bars, a flat a stem and a bowl") {
    const AccidentalShape sharp = accidental_shape(AccidentalKind::Sharp, 6.0f);
    // The two bars across, and the strokes in three pieces each, cut where the bars cross.
    CHECK(sharp.count == 8);
    // Between the bars and between the strokes there is a gap: a hole you can see through.
    CHECK(covers(sharp, 2.5f, 3.5f) == 0); // the middle of the crossing
    CHECK(covers(sharp, 1.5f, 3.5f) == 1); // a stroke
    CHECK(covers(sharp, 2.5f, 2.5f) == 1); // a bar
    const AccidentalShape flat = accidental_shape(AccidentalKind::Flat, 6.0f);
    CHECK(flat.count == 4);
    CHECK(covers(flat, 0.5f, 0.5f) == 1); // the stem reaches the top
    CHECK(covers(flat, 2.5f, 0.5f) == 0); // the bowl does not
    CHECK(covers(flat, 2.5f, 3.5f) == 1); // the bowl's right side
}

TEST_CASE("bars are slanted from 8 px up and flat below") {
    const AccidentalShape small = accidental_shape(AccidentalKind::Sharp, 7.0f);
    const AccidentalShape big = accidental_shape(AccidentalKind::Sharp, 12.0f);
    // The first quad is the first bar: corners top-left, top-right, bottom-right, bottom-left.
    CHECK(small.quads[0].y[0] == small.quads[0].y[1]);
    CHECK(big.quads[0].y[0] > big.quads[0].y[1]); // the left end is lower
}

TEST_CASE("None has no shape and a bad height is the minimum") {
    CHECK(accidental_shape(AccidentalKind::None, 10.0f).count == 0);
    CHECK(accidental_shape(AccidentalKind::None, 10.0f).width == 0.0f);
    const AccidentalShape nan =
        accidental_shape(AccidentalKind::Sharp, std::numeric_limits<float>::quiet_NaN());
    CHECK(nan.height == 6.0f);
    CHECK(accidental_shape(AccidentalKind::Flat, -4.0f).height == 6.0f);
    CHECK(accidental_shape(AccidentalKind::Sharp, 5.2f).height == 6.0f);
    CHECK(accidental_shape(AccidentalKind::Sharp, 9.4f).height == 9.0f);
}
