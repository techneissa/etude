// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/shift_fit.hpp"

#include <algorithm>
#include <cstdlib>

namespace etude {

ShiftFits shift_fits(std::span<const Note> notes, KeyRange keyboard) {
    ShiftFits fits;
    fits.keyboard = keyboard;
    // Notes per pitch, so each of the 25 shifts is a sum over 128 pitches and not a pass over the
    // notes.
    std::array<int, 128> per_pitch{};
    for (const Note& n : notes) {
        if (n.drum || n.accompaniment || n.pitch > 127) {
            continue;
        }
        ++per_pitch[n.pitch];
        ++fits.notes;
        fits.lowest = fits.lowest < 0 ? n.pitch : std::min<int>(fits.lowest, n.pitch);
        fits.highest = std::max<int>(fits.highest, n.pitch);
    }
    for (int shift = -kMaxTransposeSemitones; shift <= kMaxTransposeSemitones; ++shift) {
        int outside = 0;
        for (int pitch = 0; pitch < 128; ++pitch) {
            if (per_pitch[static_cast<std::size_t>(pitch)] > 0 &&
                !keyboard.contains(pitch + shift)) {
                outside += per_pitch[static_cast<std::size_t>(pitch)];
            }
        }
        fits.outside[static_cast<std::size_t>(shift + kMaxTransposeSemitones)] = outside;
    }
    return fits;
}

std::optional<int> suggest_octave_shift(const ShiftFits& fits, int current) {
    current = std::clamp(current, -kMaxTransposeSemitones, kMaxTransposeSemitones);
    const int now = fits.outside_at(current);
    if (fits.notes <= 0 || now < kOfferMinOutsideNotes ||
        static_cast<double>(now) < kOfferMinOutsideShare * static_cast<double>(fits.notes)) {
        return std::nullopt;
    }
    std::optional<int> best;
    int best_outside = now;
    for (int shift = -kMaxTransposeSemitones; shift <= kMaxTransposeSemitones; ++shift) {
        if (shift == current || (shift - current) % 12 != 0) {
            continue;
        }
        const int outside = fits.outside_at(shift);
        const bool fewer = outside < best_outside;
        const bool tie_closer = best && outside == best_outside &&
                                (std::abs(shift - current) < std::abs(*best - current) ||
                                 (std::abs(shift - current) == std::abs(*best - current) &&
                                  std::abs(shift) < std::abs(*best)));
        if (fewer || tie_closer) {
            best = shift;
            best_outside = outside;
        }
    }
    return best;
}

} // namespace etude
