// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hit_line.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

bool note_struck(std::optional<double> struck_start_s, bool key_down, double note_start_s,
                 int grade) {
    if (!key_down || !struck_start_s.has_value()) {
        return false;
    }
    if (grade == static_cast<int>(NoteGrade::Missed)) {
        return false;
    }
    return std::fabs(note_start_s - *struck_start_s) <= kStrikeSpreadS;
}

float wrong_flash_alpha(std::int64_t age_ns, bool fade) {
    if (age_ns < 0) {
        return 1.0f;
    }
    if (age_ns >= kWrongFlashNs) {
        return 0.0f;
    }
    if (!fade) {
        return 1.0f;
    }
    const float t = static_cast<float>(age_ns) / static_cast<float>(kWrongFlashNs);
    return (1.0f - t) * (1.0f - t);
}

float rising_alpha(bool song_open, bool rising_in_song) {
    if (!song_open) {
        return 1.0f;
    }
    return rising_in_song ? kRisingInSongAlpha : 0.0f;
}

void add_wrong_flash(std::array<float, 128>& levels, int key, std::int64_t age_ns, bool fade) {
    float& level = levels[static_cast<std::size_t>(key & 127)];
    level = std::max(level, wrong_flash_alpha(age_ns, fade));
}

} // namespace etude
