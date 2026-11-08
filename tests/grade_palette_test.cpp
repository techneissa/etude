// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/grade_palette.hpp"

#include "etude/palette.hpp"

#include <doctest/doctest.h>

#include <array>
#include <vector>

using namespace etude;

namespace {

constexpr ColourVision kChecked[] = {ColourVision::Typical, ColourVision::Protanopia,
                                     ColourVision::Deuteranopia};

// One colour per grade at `at` (0 the top of the gradient, 0.5 the middle, 1 the foot).
std::vector<RGBA> at(bool high_contrast, float t) {
    std::vector<RGBA> out;
    for (const Grade g : kAllGrades) {
        const GradeGradient grad = grade_gradient(g, high_contrast);
        out.push_back(lerp(grad.top, grad.bottom, t));
    }
    return out;
}

// What a grade's text sits on at its lightest: the card's surface under the counts row's idle
// fill (`tok::kFillAlpha`, 5.5% white). With High contrast the cards draw those rows with no fill,
// so the text sits on the surface itself.
RGBA lightest_ground(bool high_contrast) {
    return high_contrast ? kGradeSurface : lerp(kGradeSurface, RGBA{255, 255, 255, 255}, 0.055f);
}

} // namespace

TEST_CASE("grades: seven, each with its word") {
    CHECK(kAllGrades.size() == 7);
    CHECK(grade_name(Grade::Perfect) == "Perfect");
    CHECK(grade_name(Grade::Good) == "Good");
    CHECK(grade_name(Grade::Early) == "Early");
    CHECK(grade_name(Grade::Late) == "Late");
    CHECK(grade_name(Grade::Miss) == "Miss");
    CHECK(grade_name(Grade::Wrong) == "Wrong");
    CHECK(grade_name(Grade::Combo) == "Combo");
}

TEST_CASE("grades: Perfect is green, and each hue is the one it is named for") {
    const auto hue = [](Grade g) { return to_hsv(grade_colour(g)).hue; };
    CHECK(hue(Grade::Perfect) > 120.0);
    CHECK(hue(Grade::Perfect) < 160.0);
    CHECK(hue(Grade::Good) > 170.0);
    CHECK(hue(Grade::Good) < 195.0);
    CHECK(hue(Grade::Early) > 205.0);
    CHECK(hue(Grade::Early) < 235.0);
    CHECK(hue(Grade::Late) > 28.0);
    CHECK(hue(Grade::Late) < 45.0);
    CHECK((hue(Grade::Miss) < 8.0 || hue(Grade::Miss) > 350.0));
    CHECK(hue(Grade::Wrong) > 285.0);
    CHECK(hue(Grade::Wrong) < 320.0);
    CHECK(hue(Grade::Combo) > 40.0);
    CHECK(hue(Grade::Combo) < 55.0);
}

TEST_CASE("grades: each is a gradient, lighter at the top than at the foot") {
    for (const bool hc : {false, true}) {
        for (const Grade g : kAllGrades) {
            const GradeGradient grad = grade_gradient(g, hc);
            CHECK(relative_luminance(grad.top) > relative_luminance(grad.bottom));
            CHECK(grad.top.a == 255);
            CHECK(grad.bottom.a == 255);
            const RGBA mid = grade_colour(g, hc);
            CHECK(relative_luminance(mid) < relative_luminance(grad.top));
            CHECK(relative_luminance(mid) > relative_luminance(grad.bottom));
        }
    }
}

TEST_CASE("grades: text reads at 4.5:1 along the whole gradient, 7:1 with High contrast") {
    for (const Grade g : kAllGrades) {
        for (const bool hc : {false, true}) {
            const RGBA ground = lightest_ground(hc);
            const GradeGradient grad = grade_gradient(g, hc);
            const double need = hc ? kGradeHighContrastMinContrast : kGradeMinContrast;
            INFO(grade_name(g), hc ? " (high contrast)" : "");
            // The foot is the darkest end, so it is the one that has to pass.
            CHECK(contrast_ratio(grad.bottom, ground) >= need);
            CHECK(contrast_ratio(grad.top, ground) >= need);
            CHECK(contrast_ratio(grad.bottom, kGradeSurface) >= need);
        }
    }
}

TEST_CASE(
    "grades: any two are 0.08 apart in OKLab, for typical vision, protanopia and deuteranopia") {
    for (const bool hc : {false, true}) {
        for (const float t : {0.0f, 0.5f, 1.0f}) {
            const std::vector<RGBA> colours = at(hc, t);
            for (const ColourVision v : kChecked) {
                INFO("high contrast ", hc, ", at ", t, ", vision ", static_cast<int>(v));
                CHECK(min_distance_seen(colours, v) >= kGradeMinDistance);
            }
        }
    }
}

TEST_CASE("grades: the pairs that meet in hue for protanopia and deuteranopia stay apart") {
    // Red, green, amber and gold are the ones that run together without red-green vision.
    constexpr Grade warm[] = {Grade::Perfect, Grade::Late, Grade::Miss, Grade::Combo};
    for (const bool hc : {false, true}) {
        for (const ColourVision v : {ColourVision::Protanopia, ColourVision::Deuteranopia}) {
            for (const Grade a : warm) {
                for (const Grade b : warm) {
                    if (a == b) {
                        continue;
                    }
                    CHECK(perceptual_distance(simulate_vision(grade_colour(a, hc), v),
                                              simulate_vision(grade_colour(b, hc), v)) >=
                          kGradeMinDistance);
                }
            }
        }
    }
}

TEST_CASE(
    "grades: an accuracy figure takes Perfect from 90%, Good from 70%, else Miss; none for -1") {
    Grade g = Grade::Combo;
    CHECK_FALSE(accuracy_grade(-1, &g));
    CHECK(g == Grade::Combo); // untouched
    REQUIRE(accuracy_grade(100, &g));
    CHECK(g == Grade::Perfect);
    REQUIRE(accuracy_grade(90, &g));
    CHECK(g == Grade::Perfect);
    REQUIRE(accuracy_grade(89, &g));
    CHECK(g == Grade::Good);
    REQUIRE(accuracy_grade(70, &g));
    CHECK(g == Grade::Good);
    REQUIRE(accuracy_grade(69, &g));
    CHECK(g == Grade::Miss);
    REQUIRE(accuracy_grade(0, &g));
    CHECK(g == Grade::Miss);
}
