// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Learn cards' drill controls go (#373), in ImGui points: the tempo stepper, the direction,
// octave and scale / arpeggio buttons, and the foot row (the hands, the chord card's inversion
// stepper, Listen and Practice). No ImGui here. learn_layout.hpp puts them on the card.
//
// The left column has three forms, by the room the body has (`learn_choose_column`):
//   Full     the tempo over the name over the picker, then a row for each setting (direction,
//            octaves, scale / arpeggio on the scale card), the picker's look switch at the foot row's
//            level on a row of its own.
//   Compact  the picker, the direction row and one row of octaves, scale / arpeggio and the look
//            switch; the tempo and the name move to the header.
//   Popover  the twelve keys as buttons, then as many of the settings under them as the room holds, a
//            row each in the order tempo, direction, octaves, scale / arpeggio (the chord card's
//            inversion in the last place), and under the last of them one button, Options, that
//            opens a card with the rest. When all of them fit there is no button.
#pragma once

#include "face_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace etude::app::ui {

enum class LearnColumn : std::uint8_t { Full, Compact, Popover };

// The boxes of the drill's settings. A box that is not on the card is empty.
struct DrillBoxes {
    Box tempo;                    // the stepper's face: - the number +
    Box tempo_minus;
    Box tempo_plus;
    std::array<Box, 3> dir;       // up, down, up and down
    std::array<Box, 2> oct;       // 1 octave, 2 octaves
    std::array<Box, 2> kind;      // scale, arpeggio (the scale card only)
    Box stepper;                  // the chord card's inversion stepper, when it is not in the foot row
    bool oct_words = false;       // "1 Octave", else "1 oct"
    bool kind_words = false;      // the icon and "Scale", else the icon alone
};

// `N` equal cells across [x0, x1], `tight` between, in a row `h` tall from `y0`.
template <std::size_t N>
inline std::array<Box, N> drill_cells(float x0, float x1, float y0, float h, float tight) {
    std::array<Box, N> out{};
    const float w = (x1 - x0 - static_cast<float>(N - 1) * tight) / static_cast<float>(N);
    for (std::size_t i = 0; i < N; ++i) {
        const float cx = x0 + static_cast<float>(i) * (w + tight);
        out[i] = {cx, y0, cx + w, y0 + h};
    }
    return out;
}

// The tempo stepper in `face`: a minus and a plus a hit wide at its ends, the number between.
inline void drill_place_tempo(DrillBoxes& d, const Box& face, float hit) {
    d.tempo = face;
    d.tempo_minus = {face.x0, face.y0, face.x0 + hit, face.y1};
    d.tempo_plus = {face.x1 - hit, face.y0, face.x1, face.y1};
}

// What a card's settings are: a scale card has the scale / arpeggio pair, a chord card does not.
inline int drill_rows(bool kinds) { return kinds ? 3 : 2; }

// The rows stacked from `y` down in [x0, x1]: direction, octaves and (on the scale card) scale /
// arpeggio, each `hit` tall. Returns the y under the last one.
inline float drill_place_rows(DrillBoxes& d, float x0, float x1, float y, float hit, float tight,
                              float u, bool kinds) {
    d.dir = drill_cells<3>(x0, x1, y, hit, tight);
    y += hit + tight;
    d.oct = drill_cells<2>(x0, x1, y, hit, tight);
    d.oct_words = d.oct[0].w() >= learn_tok::kOctWordsMin * u;
    y += hit + tight;
    if (kinds) {
        d.kind = drill_cells<2>(x0, x1, y, hit, tight);
        d.kind_words = d.kind[0].w() >= learn_tok::kKindWordsMin * u;
        y += hit + tight;
    }
    return y - tight;
}

// The compact column's last row: the octaves (chips with words, so at least a chip wide each), then
// the scale / arpeggio pair and the look's switch (a hit each), `tight` between.
struct DrillMergedRow {
    float oct_w = 0.0f;
    float rest_w = 0.0f;
    int rest = 0; // the cells after the octaves: 3 (scale, arpeggio, switch) or 1 (the switch)
    float width() const {
        return 2.0f * oct_w + static_cast<float>(rest) * rest_w;
    }
};

inline DrillMergedRow drill_merged_row(float hit, float u, bool kinds) {
    DrillMergedRow r;
    r.oct_w = std::max(hit, tok::kChipMinW * u);
    r.rest_w = hit;
    r.rest = kinds ? 3 : 1;
    return r;
}

// The columns' vertical need, in points: what stands above the picker's square, and below it. `big`
// is the foot row's height, which the column's last row is level with.
struct LearnColumnNeed {
    float above = 0.0f;
    float below = 0.0f;
};

inline LearnColumnNeed learn_column_need(LearnColumn mode, float hit, float gap, float tight,
                                         float big, bool kinds) {
    const float rows = static_cast<float>(drill_rows(kinds)) * hit +
                       static_cast<float>(drill_rows(kinds) - 1) * tight;
    switch (mode) {
    case LearnColumn::Full:
        // The tempo, a gap, the name, a gap | the square | a gap, the rows, a gap, the look's row.
        return {2.0f * hit + 2.0f * gap, gap + rows + gap + big};
    case LearnColumn::Compact:
        // The square | a gap, the direction row, the octaves' row (level with the foot row).
        return {0.0f, gap + hit + tight + big};
    case LearnColumn::Popover:
        break;
    }
    // The buttons (the grid takes its own room) | a dead gap, the button that opens the settings (the
    // rows that fit stand between, `learn_popover_plan`).
    return {0.0f, tight + hit};
}

// The Popover column's stack: how many of the settings' rows (`kDrillSettingRows`) stand under the
// key buttons, whether the Options button follows them, and the stack's height. As many rows as fit
// with the button under them (or all of them, with no button).
inline constexpr int kDrillSettingRows = 4;
struct PopoverPlan {
    int inline_rows = 0;
    bool options = true;
    float stack_h = 0.0f;
};

inline PopoverPlan learn_popover_plan(float room, float grid_h, float hit, float gap, float tight) {
    PopoverPlan p;
    for (int n = kDrillSettingRows; n >= 0; --n) {
        p.inline_rows = n;
        p.options = n < kDrillSettingRows;
        p.stack_h = grid_h +
                    (n > 0 ? gap + static_cast<float>(n) * hit + static_cast<float>(n - 1) * tight : 0.0f) +
                    (p.options ? tight + hit : 0.0f);
        if (p.stack_h <= room + 1e-3f) {
            break;
        }
    }
    return p;
}

struct LearnColumnChoice {
    LearnColumn mode = LearnColumn::Popover;
    float side = 0.0f; // the picker's square (the grid's width when the ring does not fit)
    bool ring_fits = false;
};

// The fullest form whose picker still holds a ring of keys of a hit in `room` (the body's height);
// else the Popover form, whose picker is the twelve keys as buttons.
inline LearnColumnChoice learn_choose_column(float room, float ring_min, float cap, float hit,
                                             float gap, float tight, float big, bool kinds) {
    LearnColumnChoice out;
    for (const LearnColumn mode : {LearnColumn::Full, LearnColumn::Compact}) {
        const LearnColumnNeed need = learn_column_need(mode, hit, gap, tight, big, kinds);
        const float side = std::min(room - need.above - need.below, cap);
        if (side >= ring_min) {
            out.mode = mode;
            out.side = side;
            out.ring_fits = true;
            return out;
        }
    }
    out.mode = LearnColumn::Popover;
    out.side = 4.0f * hit + 3.0f * tight;
    return out;
}

// The foot row's controls: how wide each is, and whether Listen and Practice carry their words.
struct FootPlan {
    float hand = 0.0f;    // each hand's toggle, a square this wide
    float stepper = 0.0f; // the inversion stepper (0: none in the row)
    float listen = 0.0f;
    float practise = 0.0f;
    bool words = true;
};

inline float foot_total(const FootPlan& p, float tight, float gap) {
    return 2.0f * p.hand + tight + (p.stepper > 0.0f ? gap + p.stepper : 0.0f) + gap + p.listen +
           gap + p.practise;
}

// What fits in `room`: the words and the full widths where they fit (and the window is wide enough for
// them: `words_allowed`); else the two buttons are their
// icons (`kFootIconShare` times the row's height wide); else the stepper gives up its words down to
// two hits and a number's room; else the hands and the buttons shrink to a hit each, no less.
inline FootPlan learn_foot_plan(float room, float foot_h, float hit, float gap, float tight, float u,
                                float stepper_pref, bool words_allowed) {
    FootPlan p;
    p.hand = foot_h;
    p.stepper = stepper_pref > 0.0f ? stepper_pref * u : 0.0f;
    p.listen = learn_tok::kListenW * u;
    p.practise = learn_tok::kPracticeW * u;
    if (words_allowed && foot_total(p, tight, gap) <= room) {
        return p;
    }
    p.words = false;
    p.listen = p.practise = learn_tok::kFootIconShare * foot_h;
    if (foot_total(p, tight, gap) <= room) {
        return p;
    }
    if (p.stepper > 0.0f) {
        p.stepper = std::min(p.stepper, 2.0f * hit + learn_tok::kTempoValueW * u * 0.6f);
        if (foot_total(p, tight, gap) <= room) {
            return p;
        }
    }
    p.hand = hit;
    p.listen = p.practise = hit;
    const float rest = foot_total(p, tight, gap);
    const float spare = std::max(0.0f, room - rest) * 0.5f;
    p.listen += spare;
    p.practise += spare;
    return p;
}

} // namespace etude::app::ui
