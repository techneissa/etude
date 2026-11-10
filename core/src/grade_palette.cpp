// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/grade_palette.hpp"

#include <cstddef>

namespace etude {

GradeGradient grade_gradient(Grade grade, bool high_contrast) {
    using namespace grade_tok;
    static constexpr std::array<GradeGradient, kGradeCount> kNormal{kPerfect, kGood,  kEarly, kLate,
                                                                    kMiss,    kWrong, kCombo};
    static constexpr std::array<GradeGradient, kGradeCount> kHigh{
        kPerfectHc, kGoodHc, kEarlyHc, kLateHc, kMissHc, kWrongHc, kComboHc};
    const auto i = static_cast<std::size_t>(grade);
    if (i >= kNormal.size()) {
        return kNormal[0];
    }
    return high_contrast ? kHigh[i] : kNormal[i];
}

std::string_view grade_name(Grade grade) {
    switch (grade) {
    case Grade::Perfect:
        return "Perfect";
    case Grade::Good:
        return "Good";
    case Grade::Early:
        return "Early";
    case Grade::Late:
        return "Late";
    case Grade::Miss:
        return "Miss";
    case Grade::Wrong:
        return "Wrong";
    case Grade::Combo:
        return "Combo";
    }
    return "";
}

bool accuracy_grade(int pct, Grade* out) {
    if (pct < 0) {
        return false;
    }
    if (out != nullptr) {
        *out = pct >= kGradePerfectPct ? Grade::Perfect
               : pct >= kGradeGoodPct  ? Grade::Good
                                       : Grade::Miss;
    }
    return true;
}

} // namespace etude
