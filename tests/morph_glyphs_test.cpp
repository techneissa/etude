// SPDX-License-Identifier: GPL-3.0-or-later
// Every icon the app draws parses whole, fits the drawing buffers, and every morph the app runs
// settles exactly on its target without allocating.
#include "../app/ui/ink_measure.hpp"
#include "../app/ui/morph_glyphs.hpp"

#include "etude/icon_motion.hpp"

#include "alloc_count.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

using namespace etude;
using etude::app::ui::glyph_ok;
using etude::app::ui::kMaxCurvePoints;
using etude::app::ui::kMaxCurveSteps;
using etude::app::ui::kNumberStroke;
using etude::app::ui::kNumberWindow;
using etude::app::ui::kSkipMax;
using etude::app::ui::kSkipMin;
using etude::app::ui::morph_data;
using etude::app::ui::MorphGlyph;
using etude::app::ui::number_data;

namespace {

constexpr MorphGlyph kAll[] = {
    MorphGlyph::Play,      MorphGlyph::Pause,    MorphGlyph::Mute,     MorphGlyph::SoundOther,
    MorphGlyph::SoundBoth, MorphGlyph::Settings, MorphGlyph::Close,    MorphGlyph::LeftHand,
    MorphGlyph::RightHand, MorphGlyph::Chevron,  MorphGlyph::BarStrip, MorphGlyph::BarGroups,
    MorphGlyph::BarDock,   MorphGlyph::Pinned,   MorphGlyph::Unpinned, MorphGlyph::Replay,
    MorphGlyph::Ensemble, MorphGlyph::EnsemblePartial, MorphGlyph::EnsembleOff,
    MorphGlyph::Voice, MorphGlyph::VoicePartial, MorphGlyph::VoiceOff,
    MorphGlyph::ChevronDown, MorphGlyph::ChevronUp,
    MorphGlyph::ViewOff, MorphGlyph::ViewOn, MorphGlyph::ViewFull};

// The bar styles and the chevron pair, which the app morphs between in any order.
constexpr MorphGlyph kBarStyles[] = {MorphGlyph::BarStrip, MorphGlyph::BarGroups,
                                     MorphGlyph::BarDock};

// The morphs the app runs: play and pause, the song sound's three states in turn, the gear and
// its close cross, the bar's pin and its slashed twin. Each runs both ways. The bar styles (every
// pair) are tested below; the chevron turns and the skip steps roll, which icon_motion_test.cpp
// covers.
constexpr std::pair<MorphGlyph, MorphGlyph> kMorphs[] = {
    {MorphGlyph::Play, MorphGlyph::Pause},           {MorphGlyph::Mute, MorphGlyph::SoundOther},
    {MorphGlyph::SoundOther, MorphGlyph::SoundBoth}, {MorphGlyph::SoundBoth, MorphGlyph::Mute},
    {MorphGlyph::Settings, MorphGlyph::Close},       {MorphGlyph::Pinned, MorphGlyph::Unpinned},
};

// Runs a morph to its end: it moves, settles within two seconds and lands exactly on `to`.
void check_settles(MorphIcon& m, const MorphIconData& from, const MorphIconData& to) {
    m.set(from);
    m.morph_to(to);
    REQUIRE(m.moving());
    CHECK(m.frame().count == std::max(from.shape.count, to.shape.count));
    int frames = 0;
    while (m.step(1.0f / 60.0f)) {
        REQUIRE(++frames < 120);
    }
    REQUIRE(m.frame().count == to.shape.count);
    for (int j = 0; j < to.shape.count; ++j) {
        const auto& got = m.frame().subpaths[static_cast<std::size_t>(j)].pts;
        const auto& want = to.shape.subpaths[static_cast<std::size_t>(j)].pts;
        CHECK(std::equal(got.begin(), got.end(), want.begin(),
                         [](auto p, auto q) { return p.x == q.x && p.y == q.y; }));
    }
}

// The outline's leftmost and rightmost x (the outline is a hand's first subpath).
std::pair<float, float> outline_span(MorphGlyph g) {
    const MorphSubpath& s = morph_data(g).shape.subpaths[0];
    float lo = s.pts[0].x;
    float hi = lo;
    for (const MorphPoint& p : s.pts) {
        lo = std::min(lo, p.x);
        hi = std::max(hi, p.x);
    }
    return {lo, hi};
}

} // namespace

TEST_CASE("glyphs: every icon parses whole and fits the drawing buffers") {
    static_assert(std::size(kAll) == static_cast<std::size_t>(MorphGlyph::Count));
    for (const MorphGlyph g : kAll) {
        CAPTURE(static_cast<int>(g));
        const MorphIconData& d = morph_data(g);
        CHECK(d.error == PathError::None);
        CHECK(glyph_ok(g));
        CHECK(d.path.subpath_count > 0);
        CHECK(d.path.subpath_count < kMaxSubpaths);
        CHECK(d.path.cubic_count < kMaxCubics); // not cut off at the limit
        CHECK(d.shape.count == d.path.subpath_count);
        for (int i = 0; i < d.path.subpath_count; ++i) {
            // A subpath is drawn as its start and up to kMaxCurveSteps points per cubic.
            const CubicSubpath& s = d.path.subpaths[static_cast<std::size_t>(i)];
            CHECK(1 + s.count * kMaxCurveSteps < kMaxCurvePoints);
        }
    }
}

TEST_CASE("glyphs: no icon is an empty icon that is never drawn") {
    CHECK_FALSE(glyph_ok(MorphGlyph::Count));
    CHECK(morph_data(MorphGlyph::Count).shape.count == 0);
    CHECK(morph_data(MorphGlyph::Count).path.subpath_count == 0);
}

// The y of the outline's leftmost (or rightmost) point: where the thumb's tip is.
float tip_y(MorphGlyph g, bool leftmost) {
    const MorphSubpath& s = morph_data(g).shape.subpaths[0];
    MorphPoint best = s.pts[0];
    for (const MorphPoint& p : s.pts) {
        if (leftmost ? p.x < best.x : p.x > best.x) {
            best = p;
        }
    }
    return best.y;
}

TEST_CASE("glyphs: the hands are seen from the back, both thumbs pointing inward") {
    // The thumb reaches out past the palm on the inner side: the left hand's on its right, the
    // right hand's on its left, its tip about mid-height. The two are mirror images, each centred
    // on its ink like every icon.
    const auto [left_lo, left_hi] = outline_span(MorphGlyph::LeftHand);
    const auto [right_lo, right_hi] = outline_span(MorphGlyph::RightHand);
    CHECK(tip_y(MorphGlyph::LeftHand, false) == doctest::Approx(12.0f).epsilon(0.25));
    CHECK(tip_y(MorphGlyph::RightHand, true) == doctest::Approx(12.0f).epsilon(0.25));
    CHECK((left_lo + left_hi) * 0.5f == doctest::Approx(12.0f).epsilon(1e-3));
    CHECK((right_lo + right_hi) * 0.5f == doctest::Approx(12.0f).epsilon(1e-3));
    CHECK(left_lo == doctest::Approx(24.0f - right_hi).epsilon(1e-3));
    CHECK(left_hi == doctest::Approx(24.0f - right_lo).epsilon(1e-3));
}

TEST_CASE("glyphs: every icon is centred on its ink, a play triangle right of centre by 8%") {
    for (const MorphGlyph g : kAll) {
        CAPTURE(static_cast<int>(g));
        const MorphIconData& d = morph_data(g);
        float x0 = 1e9f;
        float x1 = -1e9f;
        float y0 = 1e9f;
        float y1 = -1e9f;
        for (int i = 0; i < d.shape.count; ++i) {
            for (const MorphPoint& p : d.shape.subpaths[static_cast<std::size_t>(i)].pts) {
                x0 = std::min(x0, p.x);
                x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y);
                y1 = std::max(y1, p.y);
            }
        }
        const float want_dx =
            g == MorphGlyph::Play ? etude::app::ui::kPlayOpticalShare * (x1 - x0 + 2.0f) : 0.0f;
        // The samples approximate the curves: a few hundredths of a grid unit.
        CHECK(std::fabs((x0 + x1) * 0.5f - 12.0f - want_dx) < 0.05f);
        CHECK(std::fabs((y0 + y1) * 0.5f - 12.0f) < 0.05f);
    }
}

TEST_CASE("glyphs: every morph the app runs settles exactly on its target") {
    auto m = std::make_unique<MorphIcon>();
    for (const auto& [a, b] : kMorphs) {
        for (const bool back : {false, true}) {
            CAPTURE(static_cast<int>(back ? b : a));
            CAPTURE(static_cast<int>(back ? a : b));
            check_settles(*m, morph_data(back ? b : a), morph_data(back ? a : b));
        }
    }
}

TEST_CASE("glyphs: the bar styles morph into one another in every order") {
    auto m = std::make_unique<MorphIcon>();
    for (const MorphGlyph a : kBarStyles) {
        for (const MorphGlyph b : kBarStyles) {
            if (a != b) {
                CAPTURE(static_cast<int>(a));
                CAPTURE(static_cast<int>(b));
                check_settles(*m, morph_data(a), morph_data(b));
            }
        }
    }
}

TEST_CASE("glyphs: the skip steps 1 to 30 parse whole, fit the buffers and sit inside the ring") {
    for (int n = kSkipMin; n <= kSkipMax; ++n) {
        CAPTURE(n);
        const MorphIconData& d = number_data(n);
        CHECK(d.error == PathError::None);
        CHECK(d.path.subpath_count > 0);
        CHECK(d.path.subpath_count < kMaxSubpaths);
        CHECK(d.path.cubic_count < kMaxCubics);
        CHECK(d.shape.count == d.path.subpath_count);
        for (int i = 0; i < d.path.subpath_count; ++i) {
            const CubicSubpath& s = d.path.subpaths[static_cast<std::size_t>(i)];
            CHECK(1 + s.count * kMaxCurveSteps < kMaxCurvePoints);
        }
        // The ring's inner radius is about 10 units around (12, 12): the digits stay within 9.
        for (int i = 0; i < d.shape.count; ++i) {
            for (const MorphPoint& p : d.shape.subpaths[static_cast<std::size_t>(i)].pts) {
                CHECK(std::hypot(p.x - 12.0f, p.y - 12.0f) < 9.0f);
                // Whole inside the window a rolling number is cut to, stroke included.
                CHECK(std::fabs(p.x - 12.0f) + kNumberStroke * 0.5f <= kNumberWindow.x);
                CHECK(std::fabs(p.y - 12.0f) + kNumberStroke * 0.5f <= kNumberWindow.y);
            }
        }
    }
    // Out-of-range steps clamp instead of reading past the table.
    CHECK(&number_data(0) == &number_data(kSkipMin));
    CHECK(&number_data(99) == &number_data(kSkipMax));
}

TEST_CASE("glyphs: every skip step rolls into every other, showing whole numbers all the way") {
    RollIcon roll;
    for (int a = kSkipMin; a <= kSkipMax; ++a) {
        for (int b = kSkipMin; b <= kSkipMax; ++b) {
            if (a == b) {
                continue;
            }
            CAPTURE(a);
            CAPTURE(b);
            roll.set(a);
            roll.roll_to(b);
            REQUIRE(roll.moving());
            int frames = 0;
            while (roll.step(1.0f / 60.0f)) {
                REQUIRE(++frames < 120);
                // Two readable numbers crossfading: the leaving one and the arriving one, within
                // one travel of the middle, one of them always at least an eighth opaque.
                REQUIRE(roll.count() == 2);
                float strongest = 0.0f;
                for (int i = 0; i < roll.count(); ++i) {
                    CHECK(roll.alpha(i) >= 0.0f);
                    CHECK(roll.alpha(i) <= 1.0f);
                    CHECK(std::fabs(roll.offset(i)) <= 1.0f);
                    strongest = std::max(strongest, roll.alpha(i));
                    const MorphIconData& d = number_data(roll.layer_value(i));
                    CHECK(d.error == PathError::None);
                }
                CHECK(strongest >= 0.124f);
            }
            CHECK(roll.count() == 1);
            CHECK(roll.value() == b);
            CHECK(roll.offset(0) == 0.0f);
        }
    }
}

TEST_CASE("glyphs: the app's morphs and their interruptions allocate nothing") {
    for (const MorphGlyph g : kAll) {
        (void)morph_data(g); // built once, on first use
    }
    (void)number_data(kSkipMin);
    auto m = std::make_unique<MorphIcon>();
    test::start_counting_allocations();
    for (const auto& [a, b] : kMorphs) {
        m->set(morph_data(a));
        m->morph_to(morph_data(b));
        for (int i = 0; i < 4; ++i) {
            m->step(1.0f / 60.0f);
        }
        m->morph_to(morph_data(a)); // interrupted: planned again from the frame on screen
        while (m->step(1.0f / 60.0f)) {
        }
    }
    for (const MorphGlyph a : kBarStyles) {
        for (const MorphGlyph b : kBarStyles) {
            m->set(morph_data(a));
            m->morph_to(morph_data(b));
            while (m->step(1.0f / 60.0f)) {
            }
        }
    }
    // Chevrons turning, and a held stepper: the step changes every few frames, each roll cut short
    // by the next, up and then down.
    TurnIcon turn;
    turn.set(0.0f);
    for (int i = 0; i < 6; ++i) {
        turn.turn_to(i % 2 == 0 ? 1.5707964f : 0.0f);
        for (int j = 0; j < 4; ++j) {
            turn.step(1.0f / 60.0f);
        }
    }
    while (turn.step(1.0f / 60.0f)) {
    }
    RollIcon roll;
    roll.set(9);
    for (int n = 10; n <= kSkipMax; ++n) {
        roll.roll_to(n);
        for (int i = 0; i < 3; ++i) {
            roll.step(1.0f / 60.0f);
        }
    }
    for (int n = kSkipMax - 1; n >= kSkipMin; --n) {
        roll.roll_to(n);
        for (int i = 0; i < 3; ++i) {
            roll.step(1.0f / 60.0f);
        }
    }
    while (roll.step(1.0f / 60.0f)) {
    }
    const int allocations = test::stop_counting_allocations();
    CHECK(allocations == 0);
}
