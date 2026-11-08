// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/combo_display.hpp"

#include "etude/look.hpp"

#include <cmath>
#include <initializer_list>

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr double kFrame = 1.0 / 60.0;

// Runs `seconds` of frames at 60 Hz with the same inputs.
void run(ComboDisplay& d, double seconds, int combo, bool reduced = false) {
    for (double t = 0.0; t < seconds; t += kFrame) {
        d.update(kFrame, combo, false, reduced);
    }
}

// A display that has shown `combo` for two seconds, so every animation has settled.
ComboDisplay settled(int combo, bool reduced = false) {
    ComboDisplay d;
    run(d, 2.0, combo, reduced);
    return d;
}

float digit_value(const ComboView& v) {
    int n = 0;
    for (int c = 0; c < v.columns; ++c) {
        n = n * 10 + v.column[static_cast<std::size_t>(c)].now.digit;
    }
    return static_cast<float>(n);
}

bool moving(const ComboView& v) {
    for (int c = 0; c < v.columns; ++c) {
        const ComboColumn& col = v.column[static_cast<std::size_t>(c)];
        if (col.out.digit >= 0 || col.now.dy != 0.0f || col.now.alpha != 1.0f) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("combo display: nothing shows below the threshold") {
    ComboDisplay d;
    for (int combo = 0; combo < kComboShowAt; ++combo) {
        d.update(kFrame, combo, false, false);
        CHECK_FALSE(d.view().shown());
        CHECK(d.view().columns == 0);
    }
}

TEST_CASE("combo display: the number appears at 10, its digits easing in") {
    ComboDisplay d;
    run(d, 0.5, 9);
    d.update(kFrame, 10, true, false);
    const ComboView first = d.view();
    CHECK(first.shown());
    CHECK(first.columns == 2);
    // Both digits come in from below with no old digit to replace.
    for (int c = 0; c < 2; ++c) {
        const ComboColumn& col = first.column[static_cast<std::size_t>(c)];
        CHECK(col.now.dy > 0.0f);
        CHECK(col.now.alpha < 1.0f);
        CHECK(col.out.digit == -1);
    }
    CHECK(first.alpha < 1.0f);
    CHECK(digit_value(first) == 10.0f);
    run(d, 0.5, 10);
    CHECK(d.view().alpha == 1.0f);
    CHECK_FALSE(moving(d.view()));
}

TEST_CASE("combo display: only the digit that changed animates") {
    ComboDisplay d = settled(10);
    d.update(kFrame, 11, false, false);
    const ComboView v = d.view();
    REQUIRE(v.columns == 2);
    // The tens digit (1) stays; the ones goes 0 -> 1: the new one slides up, the old one lifts out.
    CHECK(v.column[0].now.digit == 1);
    CHECK(v.column[0].now.dy == 0.0f);
    CHECK(v.column[0].now.alpha == 1.0f);
    CHECK(v.column[0].out.digit == -1);
    CHECK(v.column[1].now.digit == 1);
    CHECK(v.column[1].now.dy > 0.0f);
    CHECK(v.column[1].now.alpha < 1.0f);
    CHECK(v.column[1].out.digit == 0);
    CHECK(v.column[1].out.dy < 0.0f);
    CHECK(v.column[1].out.alpha > 0.0f);
    // The number itself does not pulse or fade.
    CHECK(v.alpha == 1.0f);
    CHECK(v.scale == 1.0f);
    CHECK(v.glow == 0.0f);
    // It settles within kComboEnterS and leaves nothing behind.
    run(d, kComboEnterS + 0.05, 11);
    CHECK_FALSE(moving(d.view()));
    CHECK(d.view().column[1].out.digit == -1);
}

TEST_CASE("combo display: a change eases monotonically, within the slide") {
    ComboDisplay d = settled(14);
    d.update(kFrame, 15, false, false);
    float last_alpha = d.view().column[1].now.alpha;
    float last_dy = d.view().column[1].now.dy;
    for (int i = 0; i < 20; ++i) {
        d.update(kFrame, 15, false, false);
        const ComboGlyph& g = d.view().column[1].now;
        CHECK(g.alpha >= last_alpha);
        CHECK(g.dy <= last_dy);
        CHECK(g.dy >= 0.0f);
        CHECK(g.dy <= kComboSlide);
        last_alpha = g.alpha;
        last_dy = g.dy;
    }
    CHECK(last_alpha == 1.0f);
    CHECK(last_dy == 0.0f);
}

TEST_CASE("combo display: both digits animate when both change; a new place comes in alone") {
    ComboDisplay d = settled(19);
    d.update(kFrame, 20, false, false);
    CHECK(d.view().column[0].out.digit == 1);
    CHECK(d.view().column[1].out.digit == 9);
    d = settled(99);
    d.update(kFrame, 100, false, false);
    const ComboView v = d.view();
    REQUIRE(v.columns == 3);
    // The hundreds place has nothing to replace.
    CHECK(v.column[0].now.digit == 1);
    CHECK(v.column[0].out.digit == -1);
    CHECK(v.column[0].now.dy > 0.0f);
    CHECK(v.column[1].out.digit == 9);
    CHECK(v.column[2].out.digit == 9);
}

TEST_CASE("combo display: a milestone pulses the number a little, once") {
    ComboDisplay d = settled(24);
    d.update(kFrame, 25, true, false);
    float peak = 1.0f;
    float peak_glow = 0.0f;
    for (double t = 0.0; t < kComboPulseS + 0.1; t += kFrame) {
        peak = std::max(peak, d.view().scale);
        peak_glow = std::max(peak_glow, d.view().glow);
        d.update(kFrame, 25, false, false);
    }
    CHECK(peak > 1.02f);
    CHECK(peak <= 1.0f + kComboPulseGrow + 0.001f);
    CHECK(peak_glow > 0.5f);
    CHECK(d.view().scale == 1.0f);
    CHECK(d.view().glow == 0.0f);
    // Without a milestone a change never pulses.
    d = settled(30);
    for (int combo = 31; combo < 40; ++combo) {
        d.update(kFrame, combo, false, false);
        CHECK(d.view().scale == 1.0f);
    }
}

TEST_CASE("combo display: a lost combo fades the last number out, with no motion") {
    ComboDisplay d = settled(30);
    float last = d.view().alpha;
    CHECK(last == 1.0f);
    bool dropped = false;
    for (double t = 0.0; t < kComboFadeOutS + 0.1; t += kFrame) {
        d.update(kFrame, 0, false, false);
        const ComboView& v = d.view();
        if (!v.shown()) {
            dropped = true;
            break;
        }
        // Still the lost number, in place, getting fainter.
        CHECK(digit_value(v) == 30.0f);
        CHECK(v.alpha <= last);
        CHECK(v.scale == 1.0f);
        CHECK(v.glow == 0.0f);
        for (int c = 0; c < v.columns; ++c) {
            CHECK(v.column[static_cast<std::size_t>(c)].now.dy == 0.0f);
            CHECK(v.column[static_cast<std::size_t>(c)].out.digit == -1);
        }
        last = v.alpha;
    }
    CHECK(dropped);
    CHECK(d.view().columns == 0);
}

TEST_CASE("combo display: it fades rather than vanishes, taking about kComboFadeOutS") {
    ComboDisplay d = settled(30);
    d.update(kFrame, 0, false, false);
    CHECK(d.view().shown());
    CHECK(d.view().alpha > 0.9f);
    run(d, kComboFadeOutS * 0.5, 0);
    CHECK(d.view().shown());
    CHECK(d.view().alpha < 0.8f);
    run(d, kComboFadeOutS, 0);
    CHECK_FALSE(d.view().shown());
}

TEST_CASE("combo display: building up again after a loss brings every digit in") {
    ComboDisplay d = settled(30);
    run(d, kComboFadeOutS * 0.5, 0);
    REQUIRE(d.view().shown());
    d.update(kFrame, 10, false, false);
    const ComboView v = d.view();
    CHECK(digit_value(v) == 10.0f);
    // Not an in-place change from 30: the number starts over.
    CHECK(v.column[0].out.digit == -1);
    CHECK(v.column[1].out.digit == -1);
    CHECK(v.column[0].now.dy > 0.0f);
}

TEST_CASE("combo display: a combo that stops short of the threshold never shows") {
    ComboDisplay d;
    run(d, 0.3, 7);
    d.update(kFrame, 0, false, false);
    CHECK_FALSE(d.view().shown());
}

TEST_CASE("combo display: reduced effects just follow the combo") {
    ComboDisplay d;
    d.update(kFrame, 9, false, true);
    CHECK_FALSE(d.view().shown());
    d.update(kFrame, 10, true, true);
    ComboView v = d.view();
    CHECK(v.reduced);
    CHECK(v.shown());
    CHECK(v.alpha == 1.0f);
    CHECK(v.scale == 1.0f);
    CHECK(v.glow == 0.0f);
    CHECK_FALSE(moving(v));
    d.update(kFrame, 11, false, true);
    v = d.view();
    CHECK(digit_value(v) == 11.0f);
    CHECK_FALSE(moving(v));
    d.update(kFrame, 25, true, true);
    CHECK(d.view().scale == 1.0f);
    CHECK(digit_value(d.view()) == 25.0f);
    // A loss takes the number away at once.
    d.update(kFrame, 0, false, true);
    CHECK_FALSE(d.view().shown());
}

TEST_CASE("combo display: switching to reduced effects stops what was moving") {
    ComboDisplay d = settled(24);
    d.update(kFrame, 25, true, false);
    CHECK(d.view().scale > 1.0f);
    d.update(kFrame, 25, false, true);
    CHECK(d.view().scale == 1.0f);
    CHECK_FALSE(moving(d.view()));
}

TEST_CASE("combo display: the number is capped at five digits") {
    ComboDisplay d = settled(250000);
    CHECK(d.view().columns == kComboMaxDigits);
    CHECK(digit_value(d.view()) == static_cast<float>(kComboMaxShown));
}

TEST_CASE("combo display: a long frame plays only a short step of the animation") {
    ComboDisplay d = settled(10);
    d.update(5.0, 11, false, false);
    CHECK(d.view().column[1].out.digit == 0);
    ComboDisplay e = settled(10);
    e.update(-1.0, 11, false, false);
    CHECK(e.view().column[1].out.digit == 0);
}

TEST_CASE("combo display: reset forgets the number at once, with no fade") {
    ComboDisplay d = settled(40);
    d.reset();
    d.update(kFrame, 0, false, false);
    CHECK_FALSE(d.view().shown());
}

TEST_CASE("combo display: a new song starts the number over, with no fade across songs") {
    ComboDisplay d;
    for (double t = 0.0; t < 2.0; t += kFrame) {
        d.update(kFrame, 40, false, false, 7);
    }
    REQUIRE(d.view().shown());
    // The next song is loaded: its combo is 0 and the old number must not fade over it.
    d.update(kFrame, 0, false, false, 8);
    CHECK_FALSE(d.view().shown());
    CHECK(d.view().columns == 0);
    // Within one song the same drop fades (the epoch did not change).
    ComboDisplay e;
    for (double t = 0.0; t < 2.0; t += kFrame) {
        e.update(kFrame, 40, false, false, 7);
    }
    e.update(kFrame, 0, false, false, 7);
    CHECK(e.view().shown());
}

TEST_CASE("combo display: a break fades quickly, in about kComboFadeOutS") {
    CHECK(kComboFadeOutS <= 0.2f);
    CHECK(kComboFadeOutS >= 0.15f);
    ComboDisplay d = settled(30);
    int frames = 0;
    while (d.view().shown() && frames < 60) {
        d.update(kFrame, 0, false, false);
        ++frames;
    }
    CHECK(static_cast<double>(frames) * kFrame <= kComboFadeOutS + 2.0 * kFrame);
}

TEST_CASE("place combo: the top-right corner of the note area") {
    const RectF area{0.0f, 100.0f, 1000.0f, 400.0f};
    const RectF box = place_combo(area, 0.0f, 1.0f, 90.0f, 30.0f);
    CHECK(box.right() == doctest::Approx(1000.0f - kComboMarginPt));
    CHECK(box.y == doctest::Approx(100.0f + kComboTopPt));
    CHECK(box.w == 90.0f);
    CHECK(box.h == 30.0f);
}

TEST_CASE("place combo: under the count band and the section band") {
    const RectF area{0.0f, 100.0f, 1000.0f, 400.0f};
    const RectF box = place_combo(area, 44.0f, 1.0f, 90.0f, 30.0f);
    CHECK(box.y == doctest::Approx(100.0f + 44.0f + kComboTopPt));
}

TEST_CASE("place combo: steps under a card it would touch, and leaves it alone otherwise") {
    const RectF area{0.0f, 100.0f, 1000.0f, 400.0f};
    // The card in the middle: far from the corner.
    const RectF middle{330.0f, 110.0f, 340.0f, 120.0f};
    const RectF clear = place_combo(area, 0.0f, 1.0f, 90.0f, 30.0f, middle);
    CHECK(clear.y == doctest::Approx(100.0f + kComboTopPt));
    // A card wide enough to reach the corner: the number goes under it.
    const RectF wide{100.0f, 110.0f, 860.0f, 120.0f};
    const RectF under = place_combo(area, 0.0f, 1.0f, 90.0f, 30.0f, wide);
    CHECK(under.y >= wide.bottom());
    CHECK(under.bottom() <= area.bottom());
    // No room under it: nothing is drawn rather than something over the card.
    const RectF tall{100.0f, 110.0f, 860.0f, 380.0f};
    CHECK(place_combo(area, 0.0f, 1.0f, 90.0f, 30.0f, tall).empty());
}

TEST_CASE("place combo: keeps clear of the right-edge arrows' strip") {
    const RectF area{0.0f, 100.0f, 1000.0f, 400.0f};
    // The arrows' strip is 30 1080p units wide at the edge, `unit` pixels each.
    for (const float unit : {0.45f, 0.71f, 1.0f, 1.5f}) {
        const float strip = 30.0f * unit;
        for (const float u : {0.75f, 1.0f, 2.0f, 3.2f}) {
            CAPTURE(unit);
            CAPTURE(u);
            const RectF box = place_combo(area, 0.0f, u, 90.0f, 30.0f, {}, strip);
            REQUIRE_FALSE(box.empty());
            CHECK(box.right() <= area.right() - strip);
            // Never closer to the edge than the margin either.
            CHECK(box.right() <= area.right() - kComboMarginPt * u + 1e-3f);
        }
    }
    // No arrows' strip: the margin alone.
    CHECK(place_combo(area, 0.0f, 1.0f, 90.0f, 30.0f).right() ==
          doctest::Approx(1000.0f - kComboMarginPt));
}

TEST_CASE("place combo: empty when the area is empty or the box does not fit") {
    CHECK(place_combo({}, 0.0f, 1.0f, 90.0f, 30.0f).empty());
    CHECK(place_combo({0, 0, 80, 400}, 0.0f, 1.0f, 90.0f, 30.0f).empty());
    CHECK(place_combo({0, 0, 1000, 20}, 0.0f, 1.0f, 90.0f, 30.0f).empty());
    CHECK(place_combo({0, 0, 1000, 400}, 0.0f, 0.0f, 90.0f, 30.0f).empty());
}

TEST_CASE("place combo: fits the real note area from 4:3 to 21:9 at UI scales 1.0 to 2.0") {
    struct Size {
        float w;
        float h;
    };
    const LookSettings look;
    constexpr float kPpmm = 96.0f / 25.4f;
    for (const Size size : {Size{800, 600}, Size{1024, 768}, Size{1366, 768}, Size{1920, 1080},
                            Size{2560, 1080}, Size{3440, 1440}}) {
        for (const float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
            for (const bool pinned : {false, true}) {
                CAPTURE(size.w);
                CAPTURE(size.h);
                CAPTURE(scale);
                CAPTURE(pinned);
                const PinnedBar bar{pinned, BarStyle::Strip, true, false};
                const ScreenLayout lay =
                    compute_layout(size.w, size.h, kPpmm * scale, look, bar, 1.0f, 0.0f);
                const float u = ui_scale(size.w, size.h, kPpmm * scale);
                // A band hanging from the revealed bar covers the top of an unpinned note area.
                const float hang = pinned ? 0.0f : bar_under_row_pt(true, true) * u;
                const float h = 28.0f * u * 0.72f;
                const float w = 100.0f * u;
                const RectF box = place_combo(lay.note_area, hang, u, w, h);
                REQUIRE_FALSE(box.empty());
                CHECK(box.x >= lay.note_area.x);
                CHECK(box.right() <= lay.note_area.right());
                CHECK(box.y >= lay.note_area.y + hang);
                CHECK(box.bottom() <= lay.note_area.bottom());
                // Never over the rows above the note area: top row, band, lane, chord label.
                for (const RectF* above :
                     {&lay.top_row, &lay.bar_under, &lay.lane, &lay.chord_label}) {
                    CHECK_FALSE((box.y < above->bottom() && box.bottom() > above->y &&
                                 box.x < above->right() && box.right() > above->x));
                }
                // The key bed and keys are below it.
                CHECK(box.bottom() <= lay.keyboard.y);
            }
        }
    }
}
