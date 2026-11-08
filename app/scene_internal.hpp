// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of scene.cpp share. Not part of the app's interface.
#pragma once

#include "scene.hpp"

#include "frame_log.hpp"
#include "etude/beat_lines.hpp"
#include "etude/grade_palette.hpp"
#include "etude/grade_words.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

namespace etude::app {

namespace scene_detail {

inline const SDL_FColor kWhite = rgba(255, 255, 255);

inline const SDL_FColor kBlack = rgba(0, 0, 0);
// A wrong press flashes its key this red (whatever the colour scheme), and a played note outside
// the scale overlay's scale is this red too.
inline const SDL_FColor kWrongRed = rgba(235, 64, 72);

inline SDL_Rect pixel_rect(const RectF& r) {
    return {static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w),
            static_cast<int>(r.h)};
}

inline int note_index(int note) { return std::clamp(note, 0, 127); }

// Once a frame has spent this long making name textures (in every scene that draws in it), the
// names it has not made yet are drawn from the same text at another size, stretched, or left out,
// and made in later frames (piece_slot, name_slot).
inline constexpr std::int64_t kRasterBudgetNs = 3'000'000;

struct RasterBudget {
    bool framed = false; // begin_frame() was called: there is a budget
    std::int64_t spent_ns = 0;
};

inline RasterBudget& raster_budget() {
    static RasterBudget budget;
    return budget;
}

inline bool raster_budget_spent() {
    const RasterBudget& b = raster_budget();
    return b.framed && b.spent_ns >= kRasterBudgetNs;
}

} // namespace scene_detail

} // namespace etude::app
