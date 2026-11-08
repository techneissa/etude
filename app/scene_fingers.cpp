// SPDX-License-Identifier: GPL-3.0-or-later
// A drill's finger numbers on the falling blocks (#373): a small dark disc with the number in white
// at the block's top, drawn over the names so the note's name stays at its foot. Read from the list
// draw_song_bars filled; a frame allocates nothing (the digits are cached text slots).
#include "scene.hpp"

#include "scene_internal.hpp"

#include <algorithm>

namespace etude::app {

using namespace scene_detail;

namespace {

// The disc across, in 1080p pixels at most, and the share of the block's width it may take.
constexpr float kFingerMaxPx = 26.0f;
constexpr float kFingerWidthShare = 0.82f;
// Under this many pixels a number is not legible: none is drawn.
constexpr float kFingerMinPx = 12.0f;
constexpr float kFingerDigitShare = 0.62f;

} // namespace

void Scene::draw_finger_marks() {
    if (fingers_.empty()) {
        return;
    }
    const SDL_FColor disc = rgba(12, 16, 26, 200);
    mesh_.clear();
    for (const FingerMark& m : fingers_) {
        const float d = std::min(kFingerMaxPx * unit_, kFingerWidthShare * m.width);
        if (d < kFingerMinPx * unit_) {
            continue;
        }
        mesh_.rounded_rect({m.cx - 0.5f * d, m.top + 2.0f * unit_, d, d}, 0.5f * d, disc, disc, 0.0f);
    }
    mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
    mesh_.clear();
    for (const FingerMark& m : fingers_) {
        const float d = std::min(kFingerMaxPx * unit_, kFingerWidthShare * m.width);
        if (d < kFingerMinPx * unit_) {
            continue;
        }
        const char digit[2] = {static_cast<char>('0' + m.digit), '\0'};
        const float px = kFingerDigitShare * d;
        // The text's lower edge sits a little under the digit's baseline.
        draw_name(digit, px, m.cx, m.top + 2.0f * unit_ + 0.5f * d + 0.56f * px, kWhite, true);
    }
}

} // namespace etude::app
