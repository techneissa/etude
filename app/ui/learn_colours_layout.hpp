// SPDX-License-Identifier: GPL-3.0-or-later
// Where the View page's Learn colour swatches go, with no ImGui in it. Sizes in pixels (`u` is
// the kit's unit), from the top left of the room. The tab row above them (Scales, Chords, Ear,
// Melodies) is the kit's `segmented`; this is the one row of seven shades under it, for the tab picked.
//
// Rules:
// - Each shade's whole cell is the hit area, `tok::kHit` square, and no two are closer than
//   `tok::kHitGap`, in a line or between one wrapped line and the next.
// - Where all seven fit across the room they share one line and spread over its whole width, the
//   first at its left edge and the last at its right edge, so they line up with the tab track above.
//   Else the shades wrap into lines of as many as fit (down to one), and a cell is never made smaller
//   to fit.
#pragma once

#include "add_songs_layout.hpp"
#include "tokens.hpp"

#include "etude/learn_palette.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace learn_colours_tok {
inline constexpr float kSwatch = 28.0f; // the disc drawn in a cell
inline constexpr float kRingGap = 3.0f; // between the disc and the ring round the chosen one
} // namespace learn_colours_tok

// The tab the View page's Learn colours show: kept while the settings sheet is open, and Scales again
// when the sheet is opened.
struct LearnColoursTab {
    int index = 0;
    void sheet_opened() { index = 0; }
};

struct LearnColoursSizes {
    float w = 0.0f; // the room across
    float u = 1.0f; // the kit's unit
};

struct LearnColoursLayout {
    std::array<Rect, kLearnShadeCount> cells;
    int per_line = 1;    // shades on a line
    float height = 0.0f; // everything, from the top to the bottom
};

inline LearnColoursLayout learn_colours_layout(const LearnColoursSizes& s) {
    LearnColoursLayout g;
    const float hit = tok::kHit * s.u;
    const float gap = tok::kHitGap * s.u;
    const float pitch = hit + gap;
    const int count = static_cast<int>(kLearnShadeCount);
    g.per_line = std::clamp(static_cast<int>((s.w + gap) / pitch), 1, count);
    const int lines = (count + g.per_line - 1) / g.per_line;
    // All on one line: spread over the room, never closer than the dead gap.
    const float step = g.per_line == count ? std::max(pitch, (s.w - hit) / static_cast<float>(count - 1))
                                           : pitch;
    for (std::size_t i = 0; i < g.cells.size(); ++i) {
        const int line = static_cast<int>(i) / g.per_line;
        const int col = static_cast<int>(i) % g.per_line;
        g.cells[i] = {static_cast<float>(col) * step, static_cast<float>(line) * pitch, hit, hit};
    }
    g.height = static_cast<float>(lines) * pitch - gap;
    return g;
}

} // namespace etude::app::ui
