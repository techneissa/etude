// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/icon_morph.hpp"

#include "alloc_count.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

using namespace etude;

namespace {

constexpr float kPi = 3.14159265f;
constexpr std::size_t kN = kMorphSamples;

// Index by the int counts and subpath numbers the plan uses.
std::size_t at(int i) { return static_cast<std::size_t>(i); }

// Lucide icons (ISC licence), as test shapes.
constexpr const char* kChevronRight = "m9 18 6-6-6-6";
constexpr const char* kChevronDown = "m6 9 6 6 6-6";
constexpr const char* kPlay =
    "M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 "
    "2 0 0 1 5 19z";
// Lucide's pause is two rounded rects; the same outlines written as paths.
constexpr const char* kPause =
    "M15 3h3a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1h-3a1 1 0 0 1-1-1V4a1 1 0 0 1 "
    "1-1zM6 3h3a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V4a1 1 0 "
    "0 1 1-1z";

float dist(MorphPoint a, MorphPoint b) { return std::hypot(a.x - b.x, a.y - b.y); }

bool near(MorphPoint a, MorphPoint b, float eps = 1e-4f) { return dist(a, b) <= eps; }

MorphPoint point_on(const Cubic& c, float t) {
    const float u = 1.0f - t;
    return {
        u * u * u * c.p0.x + 3 * u * u * t * c.c1.x + 3 * u * t * t * c.c2.x + t * t * t * c.p3.x,
        u * u * u * c.p0.y + 3 * u * u * t * c.c1.y + 3 * u * t * t * c.c2.y + t * t * t * c.p3.y};
}

// The heavy structs live on the heap so the tests keep a small stack.
std::unique_ptr<MorphIconData> icon(const char* d) {
    auto out = std::make_unique<MorphIconData>();
    out->error = parse_svg_path(d, out->path);
    resample(out->path, out->shape);
    return out;
}

// Within `eps` of one of the subpath's samples.
bool close_to_sample(const MorphSubpath& s, MorphPoint p, float eps) {
    for (const MorphPoint& q : s.pts) {
        if (near(p, q, eps)) {
            return true;
        }
    }
    return false;
}

bool has_sample(const MorphSubpath& s, MorphPoint p) {
    for (const MorphPoint& q : s.pts) {
        if (q.x == p.x && q.y == p.y) {
            return true;
        }
    }
    return false;
}

void require_same_shape(const MorphShape& got, const MorphShape& want, float eps) {
    REQUIRE(got.count == want.count);
    for (std::size_t j = 0; j < at(want.count); ++j) {
        for (std::size_t i = 0; i < kN; ++i) {
            CHECK(near(got.subpaths[j].pts[i], want.subpaths[j].pts[i], eps));
        }
    }
}

} // namespace

// --- Parsing -----------------------------------------------------------------------------------

TEST_CASE("icon path: a line is a cubic with its controls at a third and two thirds") {
    CubicPath p;
    REQUIRE(parse_svg_path("M3 6L9 12", p) == PathError::None);
    REQUIRE(p.subpath_count == 1);
    REQUIRE(p.cubic_count == 1);
    const Cubic& c = p.cubics[0];
    CHECK(near(c.p0, {3, 6}));
    CHECK(near(c.c1, {5, 8}));
    CHECK(near(c.c2, {7, 10}));
    CHECK(near(c.p3, {9, 12}));
    CHECK_FALSE(p.subpaths[0].closed);
}

TEST_CASE("icon path: pairs after a move are lines, relative to the current point") {
    CubicPath p;
    REQUIRE(parse_svg_path(kChevronRight, p) == PathError::None);
    REQUIRE(p.cubic_count == 2);
    CHECK(near(p.cubics[0].p0, {9, 18}));
    CHECK(near(p.cubics[0].p3, {15, 12}));
    CHECK(near(p.cubics[1].p3, {9, 6}));
}

TEST_CASE("icon path: H and V, absolute and relative") {
    CubicPath p;
    REQUIRE(parse_svg_path("M1 2H5v3h-2V1", p) == PathError::None);
    REQUIRE(p.cubic_count == 4);
    CHECK(near(p.cubics[0].p3, {5, 2}));
    CHECK(near(p.cubics[1].p3, {5, 5}));
    CHECK(near(p.cubics[2].p3, {3, 5}));
    CHECK(near(p.cubics[3].p3, {3, 1}));
}

TEST_CASE("icon path: Z closes with a line back to the start and marks the loop") {
    CubicPath p;
    REQUIRE(parse_svg_path("M0 0h4v4z", p) == PathError::None);
    REQUIRE(p.subpath_count == 1);
    CHECK(p.subpaths[0].closed);
    REQUIRE(p.cubic_count == 3);
    CHECK(near(p.cubics[2].p3, {0, 0}));

    // Already back at the start: no extra segment.
    CubicPath q;
    REQUIRE(parse_svg_path("M0 0h4v4H0V0Z", q) == PathError::None);
    CHECK(q.cubic_count == 4);
    CHECK(q.subpaths[0].closed);
}

TEST_CASE("icon path: several subpaths, and a drawing command after Z starts a new one") {
    CubicPath p;
    REQUIRE(parse_svg_path("M18 6 6 18M6 6l12 12", p) == PathError::None);
    CHECK(p.subpath_count == 2);
    CubicPath q;
    REQUIRE(parse_svg_path("M0 0h4v4zl2 2", q) == PathError::None);
    REQUIRE(q.subpath_count == 2);
    CHECK(q.subpaths[0].closed);
    CHECK_FALSE(q.subpaths[1].closed);
    CHECK(near(q.cubics[at(q.subpaths[1].first)].p0, {0, 0}));
}

TEST_CASE("icon path: packed numbers, exponents and packed arc flags") {
    CubicPath p;
    REQUIRE(parse_svg_path("M.5.25l1e1-2.5E-1", p) == PathError::None);
    CHECK(near(p.cubics[0].p0, {0.5f, 0.25f}));
    CHECK(near(p.cubics[0].p3, {10.5f, 0.0f}));
    CubicPath q;
    REQUIRE(parse_svg_path("M0 0a1 1 0 012 0", q) == PathError::None);
    CHECK(near(q.cubics[at(q.cubic_count - 1)].p3, {2, 0}));
}

TEST_CASE("icon path: quadratics are elevated exactly, T and S reflect the last control") {
    CubicPath p;
    REQUIRE(parse_svg_path("M0 0Q3 6 6 0T12 0", p) == PathError::None);
    REQUIRE(p.cubic_count == 2);
    CHECK(near(p.cubics[0].c1, {2, 4}));
    CHECK(near(p.cubics[0].c2, {4, 4}));
    // The quadratic's midpoint (t = 0.5) is (3, 3); the cubic has it too.
    CHECK(near(point_on(p.cubics[0], 0.5f), {3, 3}));
    // T reflects (3, 6) about (6, 0) to (9, -6).
    CHECK(near(p.cubics[1].c1, {8, -4}));

    CubicPath s;
    REQUIRE(parse_svg_path("M0 0C1 2 3 2 4 0S7 -2 8 0", s) == PathError::None);
    CHECK(near(s.cubics[1].c1, {5, -2}));
}

TEST_CASE("icon path: an arc lies on its circle, sliced at 90 degrees at most") {
    CubicPath p;
    // A half circle of radius 5 around (5, 0), through (5, 5) (y down, sweep 1 is clockwise).
    REQUIRE(parse_svg_path("M0 0A5 5 0 0 0 10 0", p) == PathError::None);
    REQUIRE(p.cubic_count == 2);
    CHECK(near(p.cubics[1].p3, {10, 0}));
    for (std::size_t j = 0; j < 2; ++j) {
        for (float t = 0.0f; t <= 1.0f; t += 0.125f) {
            CHECK(dist(point_on(p.cubics[j], t), {5, 0}) == doctest::Approx(5.0f).epsilon(1e-3));
        }
    }
    CHECK(point_on(p.cubics[0], 1.0f).y == doctest::Approx(5.0f).epsilon(1e-4));

    // The large arc the long way round: three quarters of a circle in three slices.
    CubicPath big;
    REQUIRE(parse_svg_path("M5 0A5 5 0 1 1 0 5", big) == PathError::None);
    CHECK(big.cubic_count == 3);
    CHECK(near(big.cubics[2].p3, {0, 5}));
}

TEST_CASE("icon path: an arc too small for its endpoints grows to fit") {
    CubicPath p;
    REQUIRE(parse_svg_path("M0 0A1 1 0 0 1 10 0", p) == PathError::None);
    REQUIRE(p.cubic_count == 2);
    CHECK(dist(point_on(p.cubics[0], 1.0f), {5, 0}) == doctest::Approx(5.0f).epsilon(1e-3));
}

TEST_CASE("icon path: circles and rounded rectangles") {
    CubicPath c;
    REQUIRE(add_circle(c, 12, 12, 3) == PathError::None);
    REQUIRE(c.subpath_count == 1);
    CHECK(c.subpaths[0].closed);
    CHECK(c.cubic_count == 4);
    for (std::size_t j = 0; j < 4; ++j) {
        CHECK(dist(point_on(c.cubics[j], 0.5f), {12, 12}) == doctest::Approx(3.0f).epsilon(1e-3));
    }
    CubicPath r;
    REQUIRE(add_rect(r, 14, 3, 5, 18, 1) == PathError::None);
    CHECK(r.cubic_count == 8);
    CHECK(r.subpaths[0].closed);
    CubicPath sq;
    REQUIRE(add_rect(sq, 0, 0, 4, 4, 0) == PathError::None);
    CHECK(sq.cubic_count == 4);
}

TEST_CASE("icon path: errors") {
    CubicPath p;
    CHECK(parse_svg_path("M0 0X4 4", p) == PathError::Syntax);
    CHECK(parse_svg_path("5 5", p) == PathError::Syntax);
    CHECK(parse_svg_path("M0", p) == PathError::Syntax);
    CHECK(parse_svg_path("M0 0a1 1 0 2 1 4 4", p) == PathError::Syntax); // flag must be 0 or 1
    CubicPath many;
    CHECK(parse_svg_path("M0 0h1M0 2h1M0 4h1M0 6h1M0 8h1M0 10h1M0 12h1M0 14h1M0 16h1", many) ==
          PathError::TooManySubpaths);
    CHECK(many.subpath_count == kMaxSubpaths);
    CubicPath lines;
    char d[2048] = "M0 0";
    for (int i = 0; i <= kMaxCubics; ++i) {
        std::strcat(d, "h1");
    }
    CHECK(parse_svg_path(d, lines) == PathError::TooManyCubics);
}

TEST_CASE("icon path: an error keeps what parsed before it, the open subpath too") {
    CubicPath p;
    REQUIRE(parse_svg_path("M0 0h4zM5 5h4v4X1", p) == PathError::Syntax);
    REQUIRE(p.subpath_count == 2);
    CHECK(p.subpaths[0].closed);
    CHECK(p.subpaths[1].count == 2);
    CHECK_FALSE(p.subpaths[1].closed);
    CHECK(near(p.cubics[at(p.cubic_count - 1)].p3, {9, 9}));

    CubicPath many;
    std::string d = "M0 0";
    for (int i = 0; i <= kMaxCubics; ++i) {
        d += "h1";
    }
    REQUIRE(parse_svg_path(d, many) == PathError::TooManyCubics);
    REQUIRE(many.subpath_count == 1);
    CHECK(many.subpaths[0].count == kMaxCubics);
}

TEST_CASE("icon path: a segment of no length adds nothing") {
    CubicPath p;
    REQUIRE(parse_svg_path("M2 2h0v0l3 0", p) == PathError::None);
    CHECK(p.cubic_count == 1);
    CubicPath q;
    REQUIRE(parse_svg_path("M2 2h0M4 4h1", q) == PathError::None);
    CHECK(q.subpath_count == 1);
}

// --- Resampling --------------------------------------------------------------------------------

TEST_CASE("resample: a line is sampled at equal arc-length steps, ends exact") {
    const auto i = icon("M0 0h63");
    const MorphSubpath& s = i->shape.subpaths[0];
    for (std::size_t k = 0; k < kN; ++k) {
        CHECK(s.pts[k].x == doctest::Approx(static_cast<float>(k)).epsilon(1e-4));
        CHECK(s.pts[k].y == doctest::Approx(0.0f));
    }
    CHECK(s.length == doctest::Approx(63.0f));
    CHECK(near(s.centroid, {31.5f, 0}));
}

TEST_CASE("resample: a corner is an exact sample and each arm is evenly sampled") {
    const auto i = icon(kChevronRight);
    const MorphSubpath& s = i->shape.subpaths[0];
    CHECK(has_sample(s, {15, 12}));
    CHECK(s.pts.front().x == 9.0f);
    CHECK(s.pts.front().y == 18.0f);
    CHECK(s.pts.back().x == 9.0f);
    CHECK(s.pts.back().y == 6.0f);
    // Two arms of the same length share the 63 intervals as 32 and 31.
    int corner = -1;
    for (std::size_t k = 0; k < kN; ++k) {
        if (s.pts[k].x == 15.0f && s.pts[k].y == 12.0f) {
            corner = static_cast<int>(k);
        }
    }
    CHECK((corner == 31 || corner == 32));
    const float step = dist(s.pts[0], s.pts[1]);
    for (std::size_t k = 1; k < at(corner); ++k) {
        CHECK(dist(s.pts[k], s.pts[k + 1]) == doctest::Approx(step).epsilon(1e-3));
    }
}

TEST_CASE("resample: a loop keeps every corner and closes on its first sample") {
    const auto i = icon("M0 0h8v8H0z");
    const MorphSubpath& s = i->shape.subpaths[0];
    CHECK(s.closed);
    CHECK(near(s.pts.front(), s.pts.back(), 0.0f));
    for (const MorphPoint c :
         {MorphPoint{0, 0}, MorphPoint{8, 0}, MorphPoint{8, 8}, MorphPoint{0, 8}}) {
        CHECK(has_sample(s, c));
    }
    CHECK(s.length == doctest::Approx(32.0f));
    // 63 intervals over four equal sides: three get 16, one 15, so the centroid sits a hair off.
    CHECK(near(s.centroid, {4, 4}, 0.1f));
}

TEST_CASE("resample: a loop's samples do not depend on where its path starts") {
    const auto a = icon("M0 0h8v8H0z");
    const auto b = icon("M8 8H0V0h8z");
    for (const MorphPoint& p : b->shape.subpaths[0].pts) {
        CHECK(close_to_sample(a->shape.subpaths[0], p, 1e-4f));
    }
}

TEST_CASE("resample: a smooth curve is measured by its arc length") {
    CubicPath c;
    REQUIRE(add_circle(c, 0, 0, 5) == PathError::None);
    MorphShape s;
    resample(c, s);
    CHECK(s.subpaths[0].length == doctest::Approx(2.0f * kPi * 5.0f).epsilon(1e-3));
    for (const MorphPoint& p : s.subpaths[0].pts) {
        CHECK(dist(p, {0, 0}) == doctest::Approx(5.0f).epsilon(1e-3));
    }
    // Equal steps all round.
    const float step = dist(s.subpaths[0].pts[0], s.subpaths[0].pts[1]);
    for (std::size_t k = 0; k + 1 < kN; ++k) {
        CHECK(dist(s.subpaths[0].pts[k], s.subpaths[0].pts[k + 1]) ==
              doctest::Approx(step).epsilon(1e-2));
    }
}

TEST_CASE("resample: a rounded corner is a smooth turn, not a corner") {
    // Lucide's play: three rounded corners joined by lines. No joint is sharp, so the triangle's
    // samples spread by length alone.
    const auto i = icon(kPlay);
    REQUIRE(i->error == PathError::None);
    const MorphSubpath& s = i->shape.subpaths[0];
    const float step = s.length / (kMorphSamples - 1);
    for (std::size_t k = 0; k + 1 < kN; ++k) {
        CHECK(dist(s.pts[k], s.pts[k + 1]) <= step * 1.05f);
    }
}

// --- Procrustes --------------------------------------------------------------------------------

TEST_CASE("procrustes: recovers a rotation, scale and translation") {
    const auto i = icon(kPlay);
    const MorphSubpath& a = i->shape.subpaths[0];
    std::array<MorphPoint, kMorphSamples> b{};
    const float angle = 0.7f;
    const float scale = 1.6f;
    for (std::size_t k = 0; k < kN; ++k) {
        const MorphPoint p = a.pts[k];
        b[k] = {scale * (std::cos(angle) * p.x - std::sin(angle) * p.y) + 3.0f,
                scale * (std::sin(angle) * p.x + std::cos(angle) * p.y) - 2.0f};
    }
    const Procrustes f = procrustes(a.pts.data(), b.data(), kMorphSamples);
    CHECK(f.angle == doctest::Approx(angle).epsilon(1e-4));
    CHECK(f.scale == doctest::Approx(scale).epsilon(1e-4));
    CHECK(f.residual < 1e-3f);
}

TEST_CASE("procrustes: different shapes leave a residual") {
    const auto a = icon(kChevronRight);
    const auto b = icon("M4 12h16");
    const Procrustes f =
        procrustes(a->shape.subpaths[0].pts.data(), b->shape.subpaths[0].pts.data(), kMorphSamples);
    CHECK(f.residual > 0.1f);
}

// --- Planning and evaluation -------------------------------------------------------------------

TEST_CASE("morph: exact at both ends, also when the subpath counts differ") {
    const auto play = icon(kPlay);
    const auto pause = icon(kPause);
    REQUIRE(pause->shape.count == 2);
    for (const bool forward : {true, false}) {
        const MorphShape& a = forward ? play->shape : pause->shape;
        const MorphShape& b = forward ? pause->shape : play->shape;
        auto plan = std::make_unique<MorphPlan>();
        plan_morph(a, b, *plan);
        CHECK(plan->count == 2);
        auto frame = std::make_unique<MorphShape>();
        // At 0 every pair shows its source subpath; at 1 its target.
        evaluate_morph(*plan, 0.0f, *frame);
        for (std::size_t j = 0; j < at(plan->count); ++j) {
            const MorphSubpath& want = a.subpaths[at(plan->pairs[j].from)];
            for (const MorphPoint& p : frame->subpaths[j].pts) {
                CHECK(close_to_sample(want, p, 1e-4f));
            }
        }
        evaluate_morph(*plan, 1.0f, *frame);
        for (std::size_t j = 0; j < at(plan->count); ++j) {
            const MorphSubpath& want = b.subpaths[at(plan->pairs[j].to)];
            for (const MorphPoint& p : frame->subpaths[j].pts) {
                CHECK(close_to_sample(want, p, 1e-4f));
            }
        }
    }
}

TEST_CASE("morph: one subpath into two duplicates the source, two into one share the target") {
    const auto play = icon(kPlay);
    const auto pause = icon(kPause);
    MorphPlan plan;
    plan_morph(play->shape, pause->shape, plan);
    REQUIRE(plan.count == 2);
    CHECK(plan.pairs[0].from == 0);
    CHECK(plan.pairs[1].from == 0);
    CHECK(plan.pairs[0].to != plan.pairs[1].to);
    plan_morph(pause->shape, play->shape, plan);
    REQUIRE(plan.count == 2);
    CHECK(plan.pairs[0].to == 0);
    CHECK(plan.pairs[1].to == 0);
    CHECK(plan.pairs[0].from != plan.pairs[1].from);
}

TEST_CASE("morph: subpaths pair by position and length, not by their order in the path") {
    const auto a = icon("M2 4v16M20 4v16");
    const auto b = icon("M19 4v16M3 4v16"); // the same bars listed right first
    MorphPlan plan;
    plan_morph(a->shape, b->shape, plan);
    REQUIRE(plan.count == 2);
    for (std::size_t j = 0; j < 2; ++j) {
        CHECK(plan.pairs[j].to == 1 - plan.pairs[j].from);
    }
    // The duplicated source is the nearest: a short bar near the right joins the right one.
    const auto one = icon("M21 8v8");
    plan_morph(one->shape, a->shape, plan);
    REQUIRE(plan.count == 2);
    // Both pairs come from the only source subpath.
    CHECK(plan.pairs[0].from == 0);
    CHECK(plan.pairs[1].from == 0);
    const auto two_to_one = icon("M2 4v16M20 4v16");
    plan_morph(two_to_one->shape, one->shape, plan);
    CHECK(plan.pairs[0].to == 0);
    CHECK(plan.pairs[1].to == 0);
}

TEST_CASE("morph: when the duplicated side is larger, the leftover goes to the nearest") {
    const auto a = icon("M2 4v4M20 4v4");
    const auto b = icon("M2 4v4M20 4v4M21 10v4");
    MorphPlan plan;
    plan_morph(a->shape, b->shape, plan);
    REQUIRE(plan.count == 3);
    CHECK(plan.pairs[0].from == 0);
    CHECK(plan.pairs[1].from == 1);
    CHECK(plan.pairs[2].from == 1); // the third bar splits off the right one
}

TEST_CASE("morph: a congruent icon turns rigidly, never travelling the chord") {
    const auto right = icon(kChevronRight);
    const auto down = icon(kChevronDown);
    MorphPlan plan;
    plan_morph(right->shape, down->shape, plan);
    CHECK(plan.rigid);
    CHECK(std::fabs(plan.pairs[0].angle) == doctest::Approx(kPi / 2).epsilon(1e-3));
    MorphShape mid;
    evaluate_morph(plan, 0.5f, mid);
    // The arms keep their length mid-turn.
    const MorphSubpath& a = right->shape.subpaths[0];
    const MorphSubpath& m = mid.subpaths[0];
    for (std::size_t k = 0; k + 8 < kN; k += 8) {
        CHECK(dist(m.pts[k], m.pts[k + 8]) ==
              doctest::Approx(dist(a.pts[k], a.pts[k + 8])).epsilon(1e-3));
    }
}

TEST_CASE("morph: a congruent icon of several subpaths keeps the distances between them") {
    // An arrow: a shaft and a head, turned by 90 degrees about (12, 12).
    const auto right = icon("M5 12h14m-7-7 7 7-7 7");
    const auto down = icon("M12 5v14m7-7-7 7-7-7");
    MorphPlan plan;
    plan_morph(right->shape, down->shape, plan);
    REQUIRE(plan.count == 2);
    CHECK(plan.rigid);
    MorphShape start;
    MorphShape mid;
    evaluate_morph(plan, 0.0f, start);
    evaluate_morph(plan, 0.5f, mid);
    // A point on the shaft and one on the head stay as far apart as at the start.
    for (std::size_t k = 0; k < kN; k += 9) {
        CHECK(dist(mid.subpaths[0].pts[k], mid.subpaths[1].pts[k]) ==
              doctest::Approx(dist(start.subpaths[0].pts[k], start.subpaths[1].pts[k]))
                  .epsilon(1e-3));
    }
}

TEST_CASE("morph: of two equal fits the shorter turn wins") {
    const auto flat = icon("M4 12h16");
    const auto slant = icon("M6 6l12 12");
    MorphPlan plan;
    plan_morph(flat->shape, slant->shape, plan);
    CHECK(plan.pairs[0].angle == doctest::Approx(kPi / 4).epsilon(1e-3));
}

TEST_CASE("morph: a loop pairs with a congruent loop at any start without turning") {
    const auto a = icon("M0 0h8v8H0z");
    const auto b = icon("M8 8H0V0h8z");
    MorphPlan plan;
    plan_morph(a->shape, b->shape, plan);
    CHECK(std::fabs(plan.pairs[0].angle) < 1e-3f);
    MorphShape mid;
    evaluate_morph(plan, 0.5f, mid);
    // Every frame is the square itself.
    for (const MorphPoint& p : mid.subpaths[0].pts) {
        const bool on_edge = std::fabs(p.x) < 1e-3f || std::fabs(p.x - 8) < 1e-3f ||
                             std::fabs(p.y) < 1e-3f || std::fabs(p.y - 8) < 1e-3f;
        CHECK(on_edge);
    }
    CHECK(mid.subpaths[0].closed);
}

TEST_CASE("morph: scale moves log-linearly, so halfway between 1x and 4x is 2x") {
    const auto small = icon("M10 12h4");
    const auto big = icon("M4 12h16");
    MorphPlan plan;
    plan_morph(small->shape, big->shape, plan);
    MorphShape mid;
    evaluate_morph(plan, 0.5f, mid);
    CHECK(mid.subpaths[0].pts.back().x - mid.subpaths[0].pts.front().x ==
          doctest::Approx(8.0f).epsilon(1e-3));
}

TEST_CASE("morph: a loop into an open path flies open") {
    const auto loop = icon("M0 0h8v8H0z");
    const auto line = icon("M0 4h8");
    MorphPlan plan;
    plan_morph(loop->shape, line->shape, plan);
    CHECK_FALSE(plan.pairs[0].closed);
}

TEST_CASE("morph: an empty side plans nothing") {
    const auto a = icon(kChevronRight);
    MorphShape empty;
    MorphPlan plan;
    plan_morph(a->shape, empty, plan);
    CHECK(plan.count == 0);
}

// --- Spring ------------------------------------------------------------------------------------

TEST_CASE("spring: smooth settles without overshoot, snappy overshoots a little") {
    for (const bool snappy : {false, true}) {
        SpringState s{0.0f, 0.0f};
        float peak = 0.0f;
        float t = 0.0f;
        while (!step_spring(s, snappy ? kSpringSnappy : kSpringSmooth, 1.0f / 60.0f)) {
            peak = std::max(peak, s.x);
            t += 1.0f / 60.0f;
            REQUIRE(t < 2.0f);
        }
        CHECK(s.x == 1.0f);
        CHECK(s.v == 0.0f);
        CHECK(t < 0.8f);
        if (snappy) {
            CHECK(peak > 1.005f);
            CHECK(peak < 1.06f);
        } else {
            CHECK(peak <= 1.0f);
        }
    }
}

TEST_CASE("spring: the frame rate does not change the motion") {
    SpringState fast{0.0f, 0.0f};
    SpringState slow{0.0f, 0.0f};
    for (int i = 0; i < 6; ++i) {
        step_spring(slow, kSpringSnappy, 1.0f / 30.0f);
        for (int k = 0; k < 4; ++k) {
            step_spring(fast, kSpringSnappy, 1.0f / 120.0f);
        }
    }
    CHECK(fast.x == doctest::Approx(slow.x).epsilon(1e-3));
}

TEST_CASE("spring: a long stall counts as a quarter second") {
    SpringState stalled{0.0f, 0.0f};
    SpringState quarter{0.0f, 0.0f};
    step_spring(stalled, kSpringSmooth, 30.0f);
    step_spring(quarter, kSpringSmooth, 0.25f);
    CHECK(stalled.x == quarter.x);
    CHECK(stalled.v == quarter.v);
}

// --- MorphIcon ---------------------------------------------------------------------------------

TEST_CASE("morph icon: the first target shows at once, the same target again does nothing") {
    const auto play = icon(kPlay);
    auto m = std::make_unique<MorphIcon>();
    m->morph_to(*play);
    CHECK_FALSE(m->moving());
    CHECK(m->target() == play.get());
    m->morph_to(*play);
    CHECK_FALSE(m->moving());
}

TEST_CASE("morph icon: settles on the exact target with its own subpath count") {
    const auto play = icon(kPlay);
    const auto pause = icon(kPause);
    auto m = std::make_unique<MorphIcon>();
    m->set(*play);
    m->morph_to(*pause);
    CHECK(m->moving());
    CHECK(m->frame().count == 2);
    m->step(0.05f);
    CHECK(m->moving());
    int frames = 0;
    while (m->step(1.0f / 60.0f)) {
        REQUIRE(++frames < 120);
    }
    require_same_shape(m->frame(), pause->shape, 0.0f);
    m->morph_to(*play);
    while (m->step(1.0f / 60.0f)) {
    }
    CHECK(m->frame().count == 1); // the duplicate is gone at rest
    require_same_shape(m->frame(), play->shape, 0.0f);
}

TEST_CASE("morph icon: an interrupted morph starts where it is and keeps its velocity") {
    const auto play = icon(kPlay);
    const auto pause = icon(kPause);
    auto m = std::make_unique<MorphIcon>();
    m->set(*play);
    m->morph_to(*pause);
    m->step(0.06f);
    REQUIRE(m->moving());
    auto before = std::make_unique<MorphShape>(m->frame());
    const float v = m->velocity();
    REQUIRE(v > 0.5f);
    m->morph_to(*play);
    CHECK(m->moving());
    CHECK(m->progress() == 0.0f);
    CHECK(m->velocity() == doctest::Approx(std::min(v, kSpringMaxSpeed)));
    // No jump: the first frame of the new morph is the last of the old one.
    REQUIRE(m->frame().count == before->count);
    for (std::size_t j = 0; j < at(before->count); ++j) {
        for (std::size_t i = 0; i < kN; ++i) {
            CHECK(close_to_sample(before->subpaths[j], m->frame().subpaths[j].pts[i], 1e-3f));
        }
    }
    while (m->step(1.0f / 60.0f)) {
    }
    require_same_shape(m->frame(), play->shape, 0.0f);
}

TEST_CASE("morph icon: go without animation switches at once, also mid-flight") {
    const auto play = icon(kPlay);
    const auto pause = icon(kPause);
    auto m = std::make_unique<MorphIcon>();
    m->go(*play, false);
    m->go(*pause, true);
    m->step(0.03f);
    REQUIRE(m->moving());
    m->go(*pause, false);
    CHECK_FALSE(m->moving());
    require_same_shape(m->frame(), pause->shape, 0.0f);
    m->go(*play, false);
    CHECK_FALSE(m->moving());
    require_same_shape(m->frame(), play->shape, 0.0f);
}

TEST_CASE("morph: parsing, planning and every frame allocate nothing") {
    auto a = std::make_unique<MorphIconData>();
    auto b = std::make_unique<MorphIconData>();
    auto m = std::make_unique<MorphIcon>();
    test::start_counting_allocations();
    *a = make_icon(kPlay);
    *b = make_icon(kPause);
    m->set(*a);
    m->morph_to(*b);
    for (int i = 0; i < 5; ++i) {
        m->step(1.0f / 60.0f);
    }
    m->morph_to(*a); // an interruption plans again
    while (m->step(1.0f / 60.0f)) {
    }
    const int allocations = test::stop_counting_allocations();
    CHECK(allocations == 0);
    CHECK(a->error == PathError::None);
    CHECK(b->error == PathError::None);
}
