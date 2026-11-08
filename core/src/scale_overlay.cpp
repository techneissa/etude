// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/scale_overlay.hpp"

#include "etude/exercises.hpp"

namespace etude {

namespace {

// The scale's two octaves start on the root at or above this note, so the numbers sit around
// middle C where a hand is.
constexpr int kFingersFrom = 57;

} // namespace

bool scale_has_fingering(ScaleType type) { return !scale_fingers(0, type, Hand::Right, 1).empty(); }

ScaleOverlayMask make_scale_overlay(const ScaleOverlay& overlay) {
    ScaleOverlayMask m;
    if (!overlay.on) {
        return m;
    }
    m.on = true;
    m.root_pc = ((overlay.root_pc % 12) + 12) % 12;
    m.pitch_classes = scale_pitch_class_mask(m.root_pc, overlay.type);
    const Tonic tonic_name = default_tonic(m.root_pc, overlay.type);
    m.root_letter = tonic_name.letter;
    m.root_accidental = tonic_name.accidental;
    if (!overlay.fingers) {
        return m;
    }
    const std::vector<int> fingers = scale_fingers(m.root_pc, overlay.type, Hand::Right, 2);
    const std::span<const int> steps = scale_intervals(overlay.type);
    if (fingers.empty() || steps.empty()) {
        return m;
    }
    const int tonic = kFingersFrom + ((m.root_pc - kFingersFrom) % 12 + 12) % 12;
    const int per_octave = static_cast<int>(steps.size());
    for (int i = 0; i < static_cast<int>(fingers.size()); ++i) {
        const int note =
            tonic + 12 * (i / per_octave) + steps[static_cast<std::size_t>(i % per_octave)];
        if (note >= 0 && note < 128) {
            m.finger[static_cast<std::size_t>(note)] =
                static_cast<std::uint8_t>(fingers[static_cast<std::size_t>(i)]);
        }
    }
    m.fingers_shown = true;
    return m;
}

} // namespace etude
