// SPDX-License-Identifier: GPL-3.0-or-later
// The practice list's card, top to bottom, with no ImGui in it. Sizes in pixels (the kit's `s()` already applied).
//
// Rules:
// - The card is as tall as its content needs and no taller than the room under the band
//   (`max_h`); it never grows past the keyboard.
// - Three parts: the title (dropped when the room is short), the list that scrolls (the items, the
//   list's own buttons, the item's settings), and the footer with Start or Stop and the two skips.
//   The footer is never inside the scrolling part, so it is always whole and on the card.
// - The list gets whatever the title and the footer leave, and at least one whole row of it
//   (`hit`) while the room allows: the title goes first when the room is tight.
#pragma once

#include <algorithm>

namespace etude::app::ui {

struct PlanListIn {
    float max_h = 0.0f;     // the room for the card under the band
    float pad = 0.0f;       // the card's padding, top and bottom
    float title_h = 0.0f;   // the title's text height
    float gap = 0.0f;       // the space above the footer and under the title
    float hit = 0.0f;       // a row's and a button's hit height
    float content_h = 0.0f; // the scrolling part's full height; 0 when not known yet (first frame)
};

struct PlanListLayout {
    bool title = false;   // the title is drawn
    float title_h = 0.0f; // the title's block, with the space under it (0 without the title)
    float scroll_h = 0.0f; // the scrolling part's height
    float footer_h = 0.0f; // the footer's block: the space above and the button row
    float h = 0.0f;        // the card's height
};

// The least room the card works in: the padding, the footer and one whole row. Under it the list
// stays closed, so no card ever shows less than a row.
inline float plan_list_min_h(float pad, float gap, float hit) { return 2.0f * pad + gap + 2.0f * hit; }

// `in.max_h` is at least `plan_list_min_h`: the caller closes the list otherwise.
inline PlanListLayout plan_list_layout(const PlanListIn& in) {
    PlanListLayout l;
    l.footer_h = in.gap + in.hit;
    const float inner = std::max(0.0f, in.max_h - 2.0f * in.pad);
    const float title_block = in.title_h + in.gap;
    const float content = in.content_h > 0.0f ? in.content_h : inner;
    // One whole row stays visible, or the whole content when it is shorter than that.
    const float wanted = std::min(content, in.hit);
    l.title = inner - title_block - l.footer_h >= wanted;
    l.title_h = l.title ? title_block : 0.0f;
    const float room = std::max(0.0f, inner - l.title_h - l.footer_h);
    l.scroll_h = std::min(content, room);
    l.h = std::min(in.max_h, 2.0f * in.pad + l.title_h + l.scroll_h + l.footer_h);
    return l;
}

} // namespace etude::app::ui
