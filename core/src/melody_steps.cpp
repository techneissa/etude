// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/melody_steps.hpp"

namespace etude {

int suggest_melody_step(const MelodyStepFacts& f) {
    using namespace melody_cut;
    // A song not read, with no notes for the right hand (a left-hand part alone is not a step of
    // these tutorials), too short to be a piece, or too long to be one to learn in one go.
    if (f.half_stars <= 0 || !f.right || f.bars > kMaxBars || f.bars < kMinBars || f.notes < kMinNotes) {
        return kMelodyNoStep;
    }
    if (!f.left) {
        if (f.half_stars <= kStep1HalfStars && f.right_keys <= kStep1Keys) {
            return 1;
        }
        if (f.half_stars <= kStep2HalfStars && f.right_keys <= kStep2Keys) {
            return 2;
        }
        return kMelodyNoStep;
    }
    if (f.half_stars <= kStep34HalfStars) {
        return f.left_keys + f.right_keys <= kStep3Keys ? 3 : 4;
    }
    return f.half_stars <= kStep5HalfStars ? 5 : kMelodyNoStep;
}

const char* melody_step_words(int step) {
    switch (step) {
    case 1:
        return "right hand, a few keys";
    case 2:
        return "right hand, a wider range";
    case 3:
        return "both hands, a simple left hand";
    case 4:
        return "both hands moving";
    case 5:
        return "both hands, a wide range";
    default:
        return "";
    }
}

} // namespace etude
