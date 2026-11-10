// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Configs page puts its parts, in ImGui points (no ImGui here): three cards side by side, each a hit area
// of at least 12 mm with a preview over its name, a dead gap of 2 mm between them, and under them
// the row of the other profiles, a pill for each that wraps onto a new row when the row is full.
#pragma once

#include "crash_layout.hpp"   // wrap_report
#include "face_layout.hpp"
#include "tokens.hpp"

#include "etude/look.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace etude::app::ui {

namespace configs_tok {
// A card's preview is this share of its width tall, between the least and the most.
inline constexpr float kPreviewAspect = 0.5f;
inline constexpr float kPreviewMinH = 54.0f;
inline constexpr float kPreviewMaxH = 150.0f;
// Stacked cards (a page too narrow for three across): the preview is a strip this tall at most.
inline constexpr float kStackedPreviewH = 64.0f;
// ... and the buttons go right of the tap area while it is at least this wide, else under it.
inline constexpr float kStackedTapMinW = 160.0f;
// The line the name sits in under the preview.
inline constexpr float kNameH = 28.0f;
// The avatar in a profile's pill, across. The pill is as wide as the name needs, never cut.
inline constexpr float kAvatar = 28.0f;
} // namespace configs_tok

// A line of the page's words, in ImGui points at body size `u`: its height.
inline float configs_words_line_h(float u) { return tok::kBody * 1.25f * u; }

// The line(s) a card's name sits in under the preview: one line's room, and a line more for each
// further line the name needs at its measured width (never an ellipsis).
inline float configs_name_h(int lines, float u) {
    return configs_tok::kNameH * u +
           static_cast<float>(std::max(1, lines) - 1) * configs_words_line_h(u);
}

// A card: the tap area (the preview over the name; a tap applies the config, or saves into an empty
// card) and, a dead gap under it, the row of its three icon buttons (Rename, Save current here,
// Clear), each a hit square, a dead gap apart, centred under the tap area. The three cards stand
// side by side while the three buttons fit under one; on a narrower page the cards stack, one above
// the other across the whole width, with the buttons beside the tap area (below it when even that
// leaves the tap area too narrow).
struct ConfigsCards {
    int columns = 3;        // 3 across, or 1: stacked
    float w = 0.0f;         // a card's width
    float h = 0.0f;         // its height: the tap area, the gap and the buttons
    float gap = 0.0f;       // the dead gap between two cards, and under the tap area
    float pad = 0.0f;       // the tap area's inside margin
    float preview_w = 0.0f; // the preview, from the tap area's top left corner inside `pad`
    float preview_h = 0.0f;
    float name_h = 0.0f; // the name's line under the preview
    float tap_w = 0.0f;  // the tap area's width: the card's, or less with the buttons beside it
    float tap_h = 0.0f;  // the tap area's height
    float btn = 0.0f;    // a button's hit square
    float btns_y = 0.0f; // the buttons' top, from the card's top
    float btns_w = 0.0f; // the three and the gaps between
    float btns_x = 0.0f; // the first button's left, from the card's left
    bool beside = false; // stacked: the buttons are right of the tap area, not under it
    bool buttons_fit = false; // the row is no wider than the card
    float x(int i) const { return columns == 1 ? 0.0f : static_cast<float>(i) * (w + gap); }
    float y(int i) const { return columns == 1 ? static_cast<float>(i) * (h + gap) : 0.0f; }
    // The whole block of the three cards.
    float total_h() const {
        return columns == 1 ? h * static_cast<float>(kUserPresetSlots) +
                                  gap * static_cast<float>(kUserPresetSlots - 1)
                            : h;
    }
    // Button `i` (0 Rename, 1 Save current here, 2 Clear): left from the card's left.
    float button_x(int i) const { return btns_x + static_cast<float>(i) * (btn + gap); }
};

inline constexpr int kConfigButtons = 3;

namespace configs_detail {
// The card for `columns` across; `beside` puts the buttons right of the tap area (columns 1 only).
inline ConfigsCards cards_of(float avail_w, float u, int columns, bool beside, int name_lines) {
    ConfigsCards c;
    c.columns = columns;
    c.beside = beside;
    c.gap = tok::kHitGap * u;
    c.pad = tok::kS2 * u;
    c.btn = tok::kHit * u;
    c.btns_w =
        c.btn * static_cast<float>(kConfigButtons) + c.gap * static_cast<float>(kConfigButtons - 1);
    const float n = static_cast<float>(columns);
    c.w = std::floor((avail_w - c.gap * (n - 1.0f)) / n);
    c.tap_w = beside ? c.w - c.btns_w - c.gap - c.pad : c.w;
    c.preview_w = std::max(1.0f, c.tap_w - 2.0f * c.pad);
    // Stacked cards are wide: their preview is a strip, not a share of the width.
    const float max_h = columns == 1 ? configs_tok::kStackedPreviewH : configs_tok::kPreviewMaxH;
    c.preview_h = std::round(std::clamp(c.preview_w * configs_tok::kPreviewAspect,
                                        configs_tok::kPreviewMinH * u, max_h * u));
    c.name_h = configs_name_h(name_lines, u);
    c.tap_h = c.pad + c.preview_h + c.name_h + tok::kS1 * u;
    if (beside) { // the buttons are right of the tap area, centred on its height
        c.btns_x = c.w - c.pad - c.btns_w;
        c.btns_y = std::floor((c.tap_h - c.btn) * 0.5f);
        c.h = c.tap_h;
    } else {
        c.btns_x = std::floor((c.w - c.btns_w) * 0.5f);
        c.btns_y = c.tap_h + c.gap;
        c.h = c.btns_y + c.btn;
    }
    c.buttons_fit = c.btns_w <= c.w;
    return c;
}
} // namespace configs_detail

// The three cards in `avail_w`, at `u` points per design point: across when the buttons fit under a
// card, else stacked.
// `name_lines`: the most lines any card's name needs (configs_name_room says how wide they may be).
inline ConfigsCards configs_cards(float avail_w, float u, int name_lines = 1) {
    const ConfigsCards across =
        configs_detail::cards_of(avail_w, u, kUserPresetSlots, false, name_lines);
    if (across.buttons_fit) {
        return across;
    }
    const ConfigsCards beside = configs_detail::cards_of(avail_w, u, 1, true, name_lines);
    return beside.tap_w >= configs_tok::kStackedTapMinW * u
               ? beside
               : configs_detail::cards_of(avail_w, u, 1, false, name_lines);
}

// The room a card's name has: the tap area less a side pad each end.
inline float configs_name_room(const ConfigsCards& c) {
    return std::max(1.0f, c.tap_w - 2.0f * c.pad);
}

// A pill of the copy row: the avatar, then the name's `text_w` points of words, in a hit area at
// least a hit wide.
inline float configs_pill_width(float text_w, float u) {
    const float w = (tok::kS2 + configs_tok::kAvatar + tok::kS2) * u + text_w + tok::kS3 * u;
    return std::max(tok::kHit * u, std::round(w));
}

struct PillSpot {
    float x = 0.0f; // from the row's left edge
    float y = 0.0f; // from the row's top
};

// Places pills of `widths` side by side a dead gap apart, starting a new row (a hit tall, a dead
// gap under it) where the next would pass `avail_w`. Returns how many rows it used.
inline int configs_place_pills(std::span<const float> widths, float avail_w, float u,
                               std::span<PillSpot> out) {
    const float gap = tok::kHitGap * u;
    const float row_step = tok::kHit * u + gap;
    float x = 0.0f;
    int row = 0;
    for (std::size_t i = 0; i < widths.size() && i < out.size(); ++i) {
        if (x > 0.0f && x + widths[i] > avail_w) {
            x = 0.0f;
            ++row;
        }
        out[i] = {x, static_cast<float>(row) * row_step};
        x += widths[i] + gap;
    }
    return widths.empty() ? 0 : row + 1;
}

// The copy row's height for `rows` rows of pills.
inline float configs_pills_height(int rows, float u) {
    return rows <= 0 ? 0.0f
                     : (tok::kHit * static_cast<float>(rows) +
                        tok::kHitGap * static_cast<float>(rows - 1)) *
                           u;
}

// --- Words that must not be cut ----------------------------------------------------------------
// The page's words (an empty card's "Save current", the Reset all row's) take the lines their width
// needs: the page breaks them at spaces by the width they measure at the real font in the room they
// have (`wrap_report`), never by a constant, so none is cut with an ellipsis or by its box.

// The room an empty card's words have: its preview less a side pad each end.
inline float configs_empty_words_room(const ConfigsCards& c, float u) {
    return std::max(1.0f, c.preview_w - 2.0f * tok::kS2 * u);
}

// The room the Reset all row's words have: the row less its icon, the air round it and a side pad.
inline float configs_reset_words_room(float row_w, float u) {
    return std::max(1.0f, row_w - (tok::kIconSm + tok::kS2 + 2.0f * tok::kS3) * u);
}

// The Reset all row's height for `lines` lines of words: a hit for one, room for the lines else.
inline float configs_reset_row_h(int lines, float u) {
    const float words = static_cast<float>(std::max(1, lines)) * configs_words_line_h(u);
    return std::max(tok::kHit * u, words + 2.0f * tok::kS2 * u);
}

// --- The copy's confirm card ------------------------------------------------------------------
// "Copy Dani's config?" over the page: the profile's avatar, the question, one line on what it
// does, and Cancel and Copy side by side. It is a popup, so its sizes come from the popup rule
// (tokens.hpp: popup_type, popup_grow, popup_width) and it is centred in the window.

namespace configs_tok {
inline constexpr float kConfirmBaseW = 344.0f; // the card's width at step 0, design points
inline constexpr float kConfirmPad = tok::kS4;
inline constexpr float kConfirmAvatar = 40.0f;     // the avatar's diameter at step 0
inline constexpr float kConfirmAvatarWide = 52.0f; // and from step 1
inline constexpr float kConfirmTitleLine = 1.25f;  // line heights, over the type size
inline constexpr float kConfirmDetailLine = 1.35f;
} // namespace configs_tok

// The width the card's words wrap to, in ImGui points.
inline float configs_confirm_text_w(float display_w, float u) {
    const int step = tok::wide_step(display_w);
    const float card = tok::popup_width(configs_tok::kConfirmBaseW, step, display_w / u) * u;
    const float fit = std::min(card, display_w - 2.0f * tok::kS2 * u);
    return fit - 2.0f * configs_tok::kConfirmPad * u;
}

struct ConfigsConfirmPlan {
    int step = 0;
    float title_pt = 0.0f; // design points, from the popup rule
    float detail_pt = 0.0f;
    float button_pt = 0.0f;
    float face_h = 0.0f;   // a button's face, design points (0: the kit's own)
    float title_lh = 0.0f; // line heights in ImGui points
    float detail_lh = 0.0f;
    Box card;
    Box avatar;
    Box title;  // `title_lines` lines tall
    Box detail; // `detail_lines` lines tall
    Box cancel; // the hit areas
    Box copy;
};

// The card for a window `display_w` by `display_h` points, with the question wrapped to
// `title_lines` lines and the sentence to `detail_lines`.
inline ConfigsConfirmPlan plan_configs_confirm(float display_w, float display_h, float u,
                                               int title_lines, int detail_lines) {
    ConfigsConfirmPlan p;
    p.step = tok::wide_step(display_w);
    p.title_pt = tok::popup_type(tok::kTitle, p.step, tok::PopupText::Title);
    p.detail_pt = tok::popup_type(tok::kBody, p.step, tok::PopupText::Detail);
    p.button_pt = tok::popup_type(tok::kLabel, p.step, tok::PopupText::Button);
    const float grow = tok::popup_grow(p.step);
    p.face_h = p.step > 0 ? tok::kFace * grow : 0.0f;
    p.title_lh = p.title_pt * configs_tok::kConfirmTitleLine * u;
    p.detail_lh = p.detail_pt * configs_tok::kConfirmDetailLine * u;
    const float pad = configs_tok::kConfirmPad * u;
    const float edge = tok::kS2 * u;
    const float w =
        std::min(tok::popup_width(configs_tok::kConfirmBaseW, p.step, display_w / u) * u,
                 display_w - 2.0f * edge);
    const float avatar =
        (p.step > 0 ? configs_tok::kConfirmAvatarWide : configs_tok::kConfirmAvatar) * u;
    const float btn_h = tok::kHit * grow * u;
    const float title_h = static_cast<float>(std::max(1, title_lines)) * p.title_lh;
    const float detail_h = static_cast<float>(std::max(1, detail_lines)) * p.detail_lh;
    const float h = pad + avatar + tok::kS3 * u + title_h + tok::kS2 * u + detail_h + tok::kS4 * u +
                    btn_h + pad;
    const float x0 = std::round((display_w - w) * 0.5f);
    const float y0 = std::max(edge, std::round((display_h - h) * 0.5f));
    p.card = {x0, y0, x0 + w, y0 + h};
    float y = y0 + pad;
    p.avatar = {x0 + (w - avatar) * 0.5f, y, x0 + (w + avatar) * 0.5f, y + avatar};
    y += avatar + tok::kS3 * u;
    p.title = {x0 + pad, y, x0 + w - pad, y + title_h};
    y += title_h + tok::kS2 * u;
    p.detail = {x0 + pad, y, x0 + w - pad, y + detail_h};
    y += detail_h + tok::kS4 * u;
    const float gap = tok::kHitGap * u;
    const float bw = std::floor((w - 2.0f * pad - gap) * 0.5f);
    p.cancel = {x0 + pad, y, x0 + pad + bw, y + btn_h};
    p.copy = {x0 + w - pad - bw, y, x0 + w - pad, y + btn_h};
    return p;
}

} // namespace etude::app::ui
