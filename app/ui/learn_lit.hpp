// SPDX-License-Identifier: GPL-3.0-or-later
// Which keys of the Learn card's keyboard Listen is sounding at a moment: worked out from the
// exercise's own schedule (core exercise_sound.hpp), so a key lights exactly while its note sounds.
// Plain data and arithmetic, no ImGui, so the lit set is tested against the notes sounding. A frame
// fills a fixed record and allocates nothing.
#pragma once

#include "etude/exercise_sound.hpp"
#include "etude/scale_diagram.hpp"
#include "etude/song_schedule.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace etude::app::ui {

// For each key of a diagram, the MIDI note a hand plays on it while it sounds (-1: none) and how
// recently it began (1 at the start, 0 `kLearnPulseS` later), which makes its finger badge pulse.
inline constexpr std::size_t kLearnMaxKeys = 32;
inline constexpr double kLearnPulseS = 0.25;
// The sound is scheduled this far after the tap, so the keys wait the same lead to match it.
inline constexpr double kLearnLeadS = static_cast<double>(kSongSoundLeadNs) / 1e9;
struct LearnLit {
    std::array<std::int16_t, kLearnMaxKeys> right;
    std::array<std::int16_t, kLearnMaxKeys> left;
    std::array<float, kLearnMaxKeys> right_pulse{};
    std::array<float, kLearnMaxKeys> left_pulse{};
    LearnLit() {
        right.fill(-1);
        left.fill(-1);
    }
    bool any() const {
        for (std::size_t i = 0; i < kLearnMaxKeys; ++i) {
            if (right[i] >= 0 || left[i] >= 0) {
                return true;
            }
        }
        return false;
    }
};

// The keys of `d` lit at second `t` of the exercise `spans`: each sounding note lights the key that
// is its distance above the diagram's lowest lit key (a scale's tonic, a chord's bass), in its hand's
// slot. A note a drill takes beyond the diagram (the second octave, the octave above a chord's bass)
// lights the same key an octave or two lower, the one the player's hand is over. A note that falls on
// no lit key of the diagram lights none.
inline LearnLit learn_lit_at(std::span<const ExerciseSpan> spans, const ScaleDiagram& d, double t) {
    LearnLit lit;
    if (d.keys.empty()) {
        return lit;
    }
    std::array<std::uint16_t, 16> sounding{};
    const std::size_t n = spans_sounding_at(spans, t, sounding);
    int base = d.keys.front().note;
    for (const DiagramKey& key : d.keys) {
        if (key.in_scale) {
            base = key.note;
            break;
        }
    }
    const int keys = static_cast<int>(std::min(d.keys.size(), kLearnMaxKeys));
    for (std::size_t i = 0; i < n; ++i) {
        const ExerciseSpan& s = spans[sounding[i]];
        int index = -1;
        for (int octave = 0; octave <= 2 && index < 0; ++octave) {
            const int at = base + s.semitone - 12 * octave - d.keys.front().note;
            if (at >= 0 && at < keys && d.keys[static_cast<std::size_t>(at)].in_scale) {
                index = at;
            }
        }
        if (index < 0) {
            continue;
        }
        const std::size_t at = static_cast<std::size_t>(index);
        const float pulse = static_cast<float>(std::clamp(1.0 - (t - s.start_s) / kLearnPulseS, 0.0, 1.0));
        if (s.hand == Hand::Left) {
            lit.left[at] = s.pitch;
            lit.left_pulse[at] = std::max(lit.left_pulse[at], pulse);
        } else {
            lit.right[at] = s.pitch;
            lit.right_pulse[at] = std::max(lit.right_pulse[at], pulse);
        }
    }
    return lit;
}

} // namespace etude::app::ui
