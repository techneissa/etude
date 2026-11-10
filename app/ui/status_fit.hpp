// SPDX-License-Identifier: GPL-3.0-or-later
// Fits the status text of the top row to its room, with no ImGui in it. The keyboard's name and the notes after it (app/status_parts.hpp) are whole
// parts: when the room is short the lowest-ranked note goes first, and so on in the fixed order
// `StatusRank` lists, so the row never shows a cut word or a fragment. Only two things are cut with
// an ellipsis, and never narrower than `min_cut`: the song's title, after every note below it has
// gone, and the keyboard's name when it stands alone and is longer than its room. Below that a
// text is dropped, not cut to a few letters.
#pragma once

#include "status_parts.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

namespace etude::app::ui {

struct StatusFitInput {
    float room = 0.0f;      // from the start of the text to what follows it (the bar, the gear)
    float label_w = 0.0f;   // the keyboard's name, whole; 0 for none
    float gap = 0.0f;       // between the name and the notes
    float group_gap = 0.0f; // between two notes of different groups
    float dot_gap = 0.0f;   // between two notes of one group: the dot and the spaces round it
    float min_cut = 0.0f;   // a name or title is never cut narrower than this
    std::span<const StatusPart> parts;
    std::span<const float> part_w; // each part's own width, as many as `parts`
};

struct StatusFit {
    bool label = false;      // the keyboard's name shows
    float label_w = 0.0f;    // the width it shows at: its own, or the room when it is cut
    std::uint32_t shown = 0; // bit i: part i shows
    int cut = -1;            // the part cut to `cut_w` with an ellipsis, -1 for none
    float cut_w = 0.0f;
    bool shows(int i) const { return (shown >> i & 1u) != 0; }
};

// The width of the parts in `mask` (bit i: part i) side by side, a gap between two.
inline float status_notes_width(const StatusFitInput& in, std::uint32_t mask) {
    float w = 0.0f;
    int prev = -1;
    for (int i = 0; i < static_cast<int>(in.parts.size()); ++i) {
        if ((mask >> i & 1u) == 0) {
            continue;
        }
        if (prev >= 0) {
            w += in.parts[static_cast<std::size_t>(i)].group ==
                         in.parts[static_cast<std::size_t>(prev)].group
                     ? in.dot_gap
                     : in.group_gap;
        }
        w += in.part_w[static_cast<std::size_t>(i)];
        prev = i;
    }
    return w;
}

// The mask of the first `count` parts, all of them.
inline constexpr std::uint32_t all_status_parts(std::size_t count) {
    return count >= 32 ? ~0u : (1u << count) - 1u;
}

inline StatusFit fit_status(const StatusFitInput& in) {
    StatusFit f;
    const bool has_label = in.label_w > 0.0f;
    // The name outranks every note: without room for it (whole, or cut to `min_cut`) the row shows
    // nothing.
    if (has_label && in.room < std::min(in.label_w, in.min_cut)) {
        return f;
    }
    f.label = has_label;
    f.label_w = std::min(in.label_w, in.room);

    const int n = std::min(static_cast<int>(in.parts.size()), kMaxStatusParts);
    std::uint32_t mask = all_status_parts(static_cast<std::size_t>(n));
    const float lead = has_label ? in.label_w + in.gap : 0.0f;
    while (mask != 0) {
        if (lead + status_notes_width(in, mask) <= in.room) {
            break;
        }
        // The lowest-ranked part still shown goes (the later of two of one rank).
        int lowest = -1;
        for (int i = 0; i < n; ++i) {
            if ((mask >> i & 1u) != 0 &&
                (lowest < 0 || in.parts[static_cast<std::size_t>(i)].rank <=
                                   in.parts[static_cast<std::size_t>(lowest)].rank)) {
                lowest = i;
            }
        }
        const auto idx = static_cast<std::size_t>(lowest);
        if (in.parts[idx].rank == StatusRank::Title) {
            // The title is cut before it goes, down to `min_cut`.
            const float rest = status_notes_width(in, mask) - in.part_w[idx];
            const float room_for_title = in.room - lead - rest;
            if (room_for_title >= std::min(in.part_w[idx], in.min_cut)) {
                f.cut = lowest;
                f.cut_w = room_for_title;
                break;
            }
        }
        mask &= ~(1u << lowest);
    }
    f.shown = mask;
    if (mask != 0) {
        f.label_w = in.label_w; // notes show beside the name, so the name is whole
    }
    return f;
}

} // namespace etude::app::ui
