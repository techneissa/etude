// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/color.hpp"

#include <array>
#include <string_view>

namespace etude {

// The colours of the grades, one place for every screen that shows one: the results card's counts
// and hand figures, the check-in and the summary of a trouble-bar run, and the grade words over
// the hit line. Perfect is green, Good teal, Early blue, Late amber, Miss red, Wrong magenta and
// Combo gold. Each is a short gradient, light at the top of a figure to deeper at its foot, and
// `grade_colour` is its middle where one flat colour is wanted.
//
// The rules the tests hold:
// - text in any of them reads at `kGradeMinContrast` (4.5:1) on the dark surface the cards and the
//   scene's grade words sit on (and on the idle fill of the counts row over it), and at
//   `kGradeHighContrastMinContrast` (7:1) with High contrast on, where the cards leave that fill
//   out so the text sits on the surface itself;
// - any two grades, at the top, the middle and the foot of their gradients, are at least
//   `kGradeMinDistance` (OKLab) apart for typical vision, protanopia and deuteranopia, High
//   contrast included (red, green, amber and gold meet in hue for those, so their lightness is
//   what tells them apart);
// - colour is never the only cue: `grade_name` is the word that goes with it everywhere.
enum class Grade : std::uint8_t { Perfect, Good, Early, Late, Miss, Wrong, Combo };
inline constexpr int kGradeCount = 7;
inline constexpr std::array<Grade, kGradeCount> kAllGrades{
    Grade::Perfect, Grade::Good,  Grade::Early, Grade::Late,
    Grade::Miss,    Grade::Wrong, Grade::Combo};

inline constexpr double kGradeMinContrast = 4.5;
inline constexpr double kGradeHighContrastMinContrast = 7.0;
inline constexpr double kGradeMinDistance = 0.08;

// The dark surface a grade colour is measured against: the results card's face (the UI's surface
// colour, opaque).
inline constexpr RGBA kGradeSurface{22, 25, 34, 255};

struct GradeGradient {
    RGBA top;    // the lighter end, at the top of a figure
    RGBA bottom; // the deeper end, at its foot
    friend bool operator==(const GradeGradient&, const GradeGradient&) = default;
};

// The named tokens, light top to deep foot.
namespace grade_tok {
inline constexpr GradeGradient kPerfect{{0xC6, 0xFF, 0xD4, 255}, {0x55, 0xF0, 0x8E, 255}};
inline constexpr GradeGradient kGood{{0x00, 0xC7, 0xC3, 255}, {0x00, 0xA4, 0xA1, 255}};
inline constexpr GradeGradient kEarly{{0x88, 0xAB, 0xFF, 255}, {0x5F, 0x89, 0xF3, 255}};
inline constexpr GradeGradient kLate{{0xFF, 0xAC, 0x40, 255}, {0xDE, 0x8C, 0x00, 255}};
inline constexpr GradeGradient kMiss{{0xFE, 0x8C, 0x90, 255}, {0xE8, 0x60, 0x6B, 255}};
inline constexpr GradeGradient kWrong{{0xF4, 0xCE, 0xFF, 255}, {0xE9, 0x9A, 0xFF, 255}};
inline constexpr GradeGradient kCombo{{0xFF, 0xDC, 0x7D, 255}, {0xE9, 0xB9, 0x00, 255}};
// With High contrast on: the same hues lifted to 7:1 and spread so they still tell apart; at that
// lightness Wrong is the palest, a near-white pink.
inline constexpr GradeGradient kPerfectHc{{0x9A, 0xFC, 0xAE, 255}, {0x6B, 0xEA, 0x8B, 255}};
inline constexpr GradeGradient kGoodHc{{0x7D, 0xE7, 0xE0, 255}, {0x41, 0xD3, 0xCC, 255}};
inline constexpr GradeGradient kEarlyHc{{0xA1, 0xC3, 0xFF, 255}, {0x7D, 0xAC, 0xFF, 255}};
inline constexpr GradeGradient kLateHc{{0xFF, 0xAE, 0x46, 255}, {0xEC, 0x95, 0x00, 255}};
inline constexpr GradeGradient kMissHc{{0xFE, 0xA4, 0x9D, 255}, {0xF2, 0x87, 0x80, 255}};
inline constexpr GradeGradient kWrongHc{{0xFF, 0xFD, 0xFF, 255}, {0xF8, 0xDB, 0xFF, 255}};
inline constexpr GradeGradient kComboHc{{0xFF, 0xE0, 0x57, 255}, {0xEB, 0xC8, 0x00, 255}};
} // namespace grade_tok

GradeGradient grade_gradient(Grade grade, bool high_contrast = false);
// The middle of the gradient.
RGBA grade_colour(Grade grade, bool high_contrast = false);
// The word that always goes with the colour: "Perfect", "Good", "Early", "Late", "Miss", "Wrong",
// "Combo".
std::string_view grade_name(Grade grade);

// The grade whose colour an accuracy figure is drawn in (a hand's percentage): 90% and over is
// Perfect, 70% and over Good, below that Miss; -1 (nothing graded) is none and gets no colour.
inline constexpr int kGradePerfectPct = 90;
inline constexpr int kGradeGoodPct = 70;
bool accuracy_grade(int pct, Grade* out);

} // namespace etude
