// SPDX-License-Identifier: GPL-3.0-or-later
// Where the section band's parts go, with no ImGui in it, so tests/band_layout_test.cpp checks the
// numbers the band draws from. Left to right: the loop toggle; the track (the song's sections as
// named bands, the cuts between them and the playhead); the time; then the tools: cut at the
// playhead, the practice list, suggested cuts, the lane and the full section editor. Sizes in
// points; `u` is the kit's unit (points per design point).
//
// Rules:
// - Every tool is `tok::kHit` square at least, on the band's lower edge (the row above, the bar's,
//   keeps its own margin, so the two rows' hit areas are a dead gap apart), and the band is never
//   shorter than `tok::kHit`.
// - Tools are a hit area and a dead gap (`tok::kHitGap`) apart, the loop toggle and the track too.
// - The track gets what the tools leave. Where it would be under `kBandTrackMinPt` the time goes
//   first, then the tools whose job is done elsewhere (suggested cuts: the editor has them; the
//   lane: its button's job falls to the section editor, as where the lane has no room, but an
//   open lane keeps its button, the one that closes it); the cut, the practice list and the
//   editor stay, and no tool shrinks.
// - A trouble-bar run adds two more tools, skip and end, between the cut and the practice list.
#pragma once

#include "tokens.hpp"

#include "etude/screen_layout.hpp"

#include <algorithm>
#include <iterator>

namespace etude::app::ui {

inline constexpr float kBandTrackMinPt = 160.0f;
// A tool's face on the band: the kit's face, at most the band less its inset.
inline constexpr float kBandFace = tok::kFace;
// The track's sections: this tall, centred on the band (the slim band holds them too).
inline constexpr float kBandSegmentPt = 30.0f;
// The playhead's line and a cut's line across the track, and the grip on top of a cut.
inline constexpr float kBandPlayheadPt = 2.0f;
inline constexpr float kBandCutPt = 1.5f;
inline constexpr float kBandGripPt = 7.0f;
// The practice list's count, a badge in the corner of its button.
inline constexpr float kBandBadgePt = 16.0f;
// The popovers under the band: the practice list and a section's menu, at most this wide.
inline constexpr float kBandListW = 400.0f;
inline constexpr float kBandMenuW = 340.0f;

struct BandTools {
    RectF skip;    // while a trouble-bar run plays: give this bar up and go on (empty otherwise)
    RectF end;     // and end the run
    RectF cut;     // a cut at the playhead
    RectF list;    // the practice list
    RectF suggest; // suggested cuts (opens the lane with them shown)
    RectF lane;    // the lane: expand, or collapse
    RectF editor;  // the full section editor
};

struct BandLayout {
    RectF band;
    RectF loop;
    RectF track;
    RectF time; // empty when the track needs its room
    BandTools tools;
};

// The band at `band` (points), its time label `time_w` points wide as measured. `run` adds the two
// controls of a trouble-bar run, skip and end, left of the practice list's button. `lane_open`
// keeps the lane's button, the lane's only close button: the track gives up its cell while the
// lane shows.
inline BandLayout band_layout(const RectF& band, float time_w, float u, bool run = false,
                              bool lane_open = false) {
    BandLayout l;
    l.band = band;
    const float hit = tok::kHit * u;
    const float pad = tok::kS2 * u;
    const float gap = tok::kHitGap * u;
    // A hit tall, on the band's lower edge.
    const float h = hit;
    const float y = band.y + band.h - h;
    const auto cell = [&](float x) { return RectF{x, y, hit, h}; };
    float x = band.x + pad;
    l.loop = cell(x);
    x += hit + gap;
    // The tools from the right end. A narrow band gives up the suggested cuts, then the lane, so
    // the track keeps `kBandTrackMinPt` beside the loop toggle and the tools that stay.
    RectF* const tools[] = {&l.tools.editor, &l.tools.lane, &l.tools.suggest, &l.tools.list,
                            &l.tools.end,    &l.tools.skip, &l.tools.cut};
    bool shown[] = {true, true, true, true, run, run, true};
    const float cell_w = hit + gap;
    const auto room_left = [&] {
        float used = x;
        for (std::size_t i = 0; i < std::size(tools); ++i) {
            used += shown[i] ? cell_w : 0.0f;
        }
        return band.right() - pad - used;
    };
    for (const std::size_t giving_up : {std::size_t{2}, std::size_t{1}}) {
        if (giving_up == 1 && lane_open) {
            continue;
        }
        if (room_left() < kBandTrackMinPt * u) {
            shown[giving_up] = false;
        }
    }
    float right = band.right() - pad;
    for (std::size_t i = 0; i < std::size(tools); ++i) {
        if (!shown[i]) {
            continue;
        }
        right -= hit;
        *tools[i] = cell(right);
        right -= gap;
    }
    const float time = std::max(0.0f, time_w);
    if (time > 0.0f && right - time - gap - x >= kBandTrackMinPt * u) {
        l.time = RectF{right - time, y, time, h};
        right -= time + gap;
    }
    l.track = RectF{x, y, std::max(0.0f, right - x), h};
    return l;
}

} // namespace etude::app::ui
