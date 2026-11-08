// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/level_stars.hpp"

#include <algorithm>

namespace etude {

RGBA level_star_colour(double stars, bool high_contrast) {
    const double s = std::clamp(stars, 1.0, static_cast<double>(kLevelStars));
    RGBA c;
    if (s <= level_tok::kMiddleAt) {
        c = lerp(level_tok::kEasy, level_tok::kMiddle,
                 static_cast<float>((s - 1.0) / (level_tok::kMiddleAt - 1.0)));
    } else if (s <= level_tok::kHarderAt) {
        c = lerp(level_tok::kMiddle, level_tok::kHarder,
                 static_cast<float>((s - level_tok::kMiddleAt) /
                                    (level_tok::kHarderAt - level_tok::kMiddleAt)));
    } else {
        c = lerp(
            level_tok::kHarder, level_tok::kHard,
            static_cast<float>((s - level_tok::kHarderAt) / (kLevelStars - level_tok::kHarderAt)));
    }
    return high_contrast ? lift_to_contrast(c, kLevelStarSurface, kLevelStarHighContrastMinContrast)
                         : c;
}

double level_star_fill(int half_stars, int index) {
    const int left = half_stars - 2 * index;
    return left >= 2 ? 1.0 : left == 1 ? 0.5 : 0.0;
}

} // namespace etude
