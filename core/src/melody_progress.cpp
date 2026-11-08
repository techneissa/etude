// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/melody_progress.hpp"

#include "etude/level_stars.hpp"

#include <algorithm>

namespace etude {

MelodyMark melody_mark(int best_permille) {
    if (best_permille < 0) {
        return MelodyMark::New;
    }
    return best_permille >= kMelodyPassPermille ? MelodyMark::Passed : MelodyMark::Started;
}

float melody_share(int best_permille) {
    if (best_permille <= 0) {
        return 0.0f;
    }
    return std::min(1.0f, static_cast<float>(best_permille) / static_cast<float>(kMelodyPassPermille));
}

RGBA melody_fill_colour(float share) {
    const float s = std::clamp(share, 0.0f, 1.0f);
    if (s < 0.5f) {
        return lerp(level_tok::kMiddle, level_tok::kHarder, s * 2.0f);
    }
    return lerp(level_tok::kHarder, level_tok::kEasy, (s - 0.5f) * 2.0f);
}

MelodyPathProgress melody_path_progress(std::span<const int> best_permille) {
    MelodyPathProgress p;
    p.total = static_cast<int>(best_permille.size());
    for (std::size_t i = 0; i < best_permille.size(); ++i) {
        const MelodyMark mark = melody_mark(best_permille[i]);
        p.passed += mark == MelodyMark::Passed ? 1 : 0;
        p.started = p.started || mark != MelodyMark::New;
        if (mark != MelodyMark::Passed && p.next < 0) {
            p.next = static_cast<int>(i);
        }
    }
    return p;
}

} // namespace etude
