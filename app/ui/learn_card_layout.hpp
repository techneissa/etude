// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Learn cards' parts go (the scale card and the chord card share them), in ImGui points.
// No ImGui here, so tests/learn_layout_test.cpp checks the real numbers at every window size.
//
// A card is one structure at every size, drawn in a unit that grows with the window. The header has
// the back arrow, the page's name and the four tabs. The left column is the tempo over the name
// over the key picker (the ring of fifths or the twelve keys), then a row for each drill setting
// (drill_layout: learn_drill_layout.hpp has its three forms for the room). The column at its right has
// the types over the keyboard over the foot row: the types in rows when each fits its words, else
// one strip that scrolls sideways (chip_strip.hpp); the keyboard as wide as the column, its keys
// never taller than a natural proportion, the right hand's fingers above it and the left hand's
// below; the foot row, centred under the keyboard, with the hands' toggles (left, then right, as in
// the player's top bar), the chord card's inversion stepper, Listen and Practice. The types and the
// foot are 1.3 times a hit tall, the keyboard giving way.
#pragma once

#include "face_layout.hpp"
#include "key_ring_layout.hpp"
#include "learn_drill_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace etude::app::ui {

// The Learn cards, and the tabs that go between them: Scales, Chords, Ear and Melodies.
inline constexpr int kLearnCards = 4;
inline constexpr int kLearnTabs = kLearnCards;

inline constexpr int kLearnTypes = 14;    // kAllScaleTypes
inline constexpr int kLearnMaxCells = 24; // the most buttons a card's types hold (the chords' 23)

// What a card's types are made of: the buttons (a scale type or a chord quality each), how many to
// a row when they are rows, the least cell that holds their longest words, whether the card has the
// scale / arpeggio pair, and the chord card's inversion stepper (its preferred width, 0 for none).
// Sizes in design points.
struct LearnCells {
    int count = kLearnTypes;
    int per_row = 7;
    float cell_min = learn_tok::kTypeCellMin;
    bool kinds = true;
    float stepper_w = 0.0f;
};
inline constexpr LearnCells kScaleCells{};
inline constexpr LearnCells kChordCells{23, 8, learn_tok::kChordCellMin, false,
                                        learn_tok::kStepperW};

struct LearnCardLayout {
    bool rows = false;              // the types are in rows (else one strip)
    Box header;
    Box back;
    Box title;                      // the page's name
    std::array<Box, kLearnTabs> tabs;  // Scales, Chords, Ear, Melodies
    std::array<bool, kLearnTabs> tab_label{}; // the tab shows its name beside its icon
    LearnColumn column = LearnColumn::Full;
    Box slot;                       // the key picker's square (the buttons: their grid)
    Box toggle;                     // its look switch (empty when the ring does not fit)
    bool ring_fits = true;          // false: the body is too short for a ring of keys of a hit, so
                                    // the slot holds the twelve keys as buttons whatever the look,
                                    // and there is no switch
    Box name;                       // the scale's name: over the picker, or in the header
    bool name_in_header = false;
    DrillBoxes drill;               // the settings where they are on the card (Full, Compact)
    int inline_rows = 0;            // Popover: how many settings stand under the keys, in `drill`
    bool options = false;           // and whether the Options button follows them
    Box settings;                   // the Options button that opens the rest
    Box popover;                    // and their card, with those settings in `pop`
    DrillBoxes pop;
    std::array<Box, kLearnMaxCells> type; // rows: each button's cell, `per_row` to a row
    int type_cells = 0;             // the buttons' count with rows, else 0
    Box strip;                      // else: the strip of all the types, scrolling sideways
    Box play;                       // the foot row: Listen,
    Box practise;                   // then Practice,
    Box stepper;                    // the chord card's inversion stepper (empty when not in the row),
    Box hand_right;                 // and the hands' toggles square before them,
    Box hand_left;                  // the group centred under the keyboard
    bool foot_words = true;         // Listen and Practice carry their words (else their icons)
    Box keyboard;                   // the keys, white keys across
    Box fingers_right;              // the row of badges over the keyboard
    Box fingers_left;               // and the row under it
    float badge = 0.0f;             // a finger badge's diameter
    float ring_side = 0.0f;         // the picker's square
};

// What the card's pieces are laid out from: the room under the header and the sizes in pixels.
struct LearnCardRoom {
    float u = 1.0f;
    float hit = 0.0f;
    float gap = 0.0f;   // tok::kS2
    float tight = 0.0f; // tok::kHitGap: between two hit areas
    float pad = 0.0f;
    float top = 0.0f; // the body: under the header
    float bottom = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
    float col_x0 = 0.0f; // where the picker's column ends and the rest begins
    float side_w = 0.0f; // the left column's width
};

// What the header's tabs need, in design points: each one's width with its icon and name beside it
// (the app measures the name in the font; these are what the font gave at the body size: Scales, Chords,
// Ear, Melodies), and the tab that is shown, which keeps its name when the others have to be icons alone.
struct LearnTabs {
    std::array<float, kLearnTabs> need{82.0f, 85.0f, 66.0f, 96.0f};
    int selected = 0;
};

// The header: the back arrow, and at the right the tabs, each an icon with its name beside it when
// all four fit with room left for the page's name (`kTitleRoom`, or `kTitleNeed` when the scale's
// name is in the header too) and, in the Compact column, the tempo; else only the shown tab keeps its
// name and the others are their icons. `d` gets the tempo stepper of the Compact column.
inline void learn_place_header(LearnCardLayout& c, const Box& area, const LearnCardRoom& r,
                               const LearnTabs& tabs, LearnColumn mode, DrillBoxes& d) {
    // The header stands a dead gap under the top row's buttons.
    const float head_y = area.y0 + r.tight;
    c.header = {area.x0, head_y, area.x1, head_y + r.hit};
    c.back = {area.x0 + r.gap, head_y, area.x0 + r.gap + r.hit, head_y + r.hit};
    const float tabs_right = area.x1 - r.gap;
    const bool in_header = mode != LearnColumn::Full;
    const float tempo_w =
        mode == LearnColumn::Compact ? 2.0f * r.hit + learn_tok::kTempoValueW * r.u + r.gap : 0.0f;
    const float title_need = (in_header ? learn_tok::kTitleNeed : learn_tok::kTitleRoom) * r.u;
    const float room_left = c.back.x1 + title_need + tempo_w;
    float all = static_cast<float>(kLearnTabs - 1) * r.tight;
    for (const float need : tabs.need) {
        all += need * r.u;
    }
    const bool every = tabs_right - all >= room_left;
    float x1 = tabs_right;
    for (int i = kLearnTabs - 1; i >= 0; --i) {
        const std::size_t n = static_cast<std::size_t>(i);
        const bool label = every || i == tabs.selected;
        c.tab_label[n] = label;
        const float w = label ? std::max(tabs.need[n] * r.u, r.hit) : r.hit;
        c.tabs[n] = {x1 - w, head_y, x1, head_y + r.hit};
        x1 -= w + r.tight;
    }
    float title_right = c.tabs[0].x0 - r.gap;
    if (mode == LearnColumn::Compact) {
        const float w = tempo_w - r.gap;
        drill_place_tempo(d, {title_right - w, head_y, title_right, head_y + r.hit}, r.hit);
        title_right = d.tempo.x0 - r.gap;
    }
    c.title = {c.back.x1, c.header.y0, title_right, c.header.y1};
    if (in_header) {
        // The page's word, then the scale's or chord's name after it.
        const float word_w = std::min(learn_tok::kTitleWordW * r.u, c.title.w());
        c.name = {c.title.x0 + word_w, c.title.y0, c.title.x1, c.title.y1};
        c.title.x1 = c.name.x0;
        c.name_in_header = true;
    }
}

// The twelve keys as buttons are four across and three down, or three across and four down:
// whichever gives the larger button in the room (`w` by `h`, with `tight` between), four across
// when they tie (a square room).
inline int learn_grid_columns(float w, float h, float tight) {
    const auto cell = [&](int columns, int rows) {
        return std::min((w - static_cast<float>(columns - 1) * tight) / static_cast<float>(columns),
                        (h - static_cast<float>(rows - 1) * tight) / static_cast<float>(rows));
    };
    return cell(3, 4) > cell(4, 3) + 1e-3f ? 3 : 4;
}

// The left column's form and picker square for the body `r` and the card's cells: the picker's square
// as large as the room allows up to its cap, and never below what keys of a hit need, else the twelve
// keys as buttons (the Popover form, where the settings fold into one button).
inline LearnColumnChoice learn_place_picker(const LearnCardRoom& r, bool kinds) {
    const float ring_min = key_ring_min_side(tok::kHit, r.u);
    const float cap = learn_tok::kRingMax * r.u;
    const float big = learn_tok::kBigScale * r.hit;
    LearnColumnChoice choice =
        learn_choose_column(r.bottom - r.top, ring_min, cap, r.hit, r.gap, r.tight, big, kinds);
    return choice;
}

// The foot row, `foot_h` tall and ending at `bottom`: the hands' toggles (square) then, on the chord
// card, the inversion stepper, then Listen and Practice, in the order of the player's top bar (left
// hand, right hand, then the buttons), as one group centred on `cx`.
inline void learn_place_foot(LearnCardLayout& c, const LearnCardRoom& r, float cx, float room_w,
                             float bottom, float foot_h, float stepper_pref, bool words_allowed) {
    const FootPlan p =
        learn_foot_plan(room_w, foot_h, r.hit, r.gap, r.tight, r.u, stepper_pref, words_allowed);
    c.foot_words = p.words;
    const float total = foot_total(p, r.tight, r.gap);
    const float cy = bottom - foot_h * 0.5f;
    const float h = p.hand;
    const float y0 = cy - h * 0.5f;
    float x = std::max(cx - total * 0.5f, cx - room_w * 0.5f);
    c.hand_left = {x, y0, x + p.hand, y0 + h};
    x += p.hand + r.tight;
    c.hand_right = {x, y0, x + p.hand, y0 + h};
    x += p.hand + r.gap;
    if (p.stepper > 0.0f) {
        c.stepper = {x, y0, x + p.stepper, y0 + h};
        x += p.stepper + r.gap;
    } else {
        c.stepper = {};
    }
    c.play = {x, y0, x + p.listen, y0 + h};
    x += p.listen + r.gap;
    c.practise = {x, y0, x + p.practise, y0 + h};
}

// Whether the types fit in rows: a cell (`cell` wide) is at least `cell_min` design points (the app
// measures the longest words in the font and gives what holds them), and the column is tall enough
// for the `rows` of them, a keyboard of at least its least height, and the foot, the
// rows and the foot at their full height.
inline bool learn_types_fit_rows(float cell, float room, const LearnCardRoom& r, float frow, int rows,
                                 float cell_min) {
    const float big = learn_tok::kBigScale * r.hit;
    const float rest = static_cast<float>(rows) * big + static_cast<float>(rows - 1) * r.tight +
                       2.0f * r.gap + 2.0f * frow + learn_tok::kKeyboardMinH * r.u + big;
    return cell >= cell_min * r.u && room >= rest;
}

// The column at the right of the picker: the types (rows of `per_row`, or one strip), the keyboard
// with its rows of badges, and the foot row, a stack that ends at `block_bottom` (the foot row's
// level, which the left column's last row shares) and is `used` tall. The types and the foot are
// `kBigScale` times a hit tall, the keyboard giving way to them: it is as wide as the column and its
// keys are never taller than `kKeyAspect` times their width, but a column too short for that takes
// what height is left. Where the types are a strip (the room is short) the strip and the foot are
// as tall as leaves the keyboard its least height, at least a hit. Returns the stack's height, and
// places it only when `place` (the left column needs the height first).
inline float learn_place_column(LearnCardLayout& c, const LearnCardRoom& r, const LearnCells& cells,
                                LearnColumn mode, float block_bottom, bool words_allowed, bool place) {
    using namespace learn_tok;
    const float x0 = r.col_x0;
    const float x1 = r.right;
    const float width = std::max(0.0f, x1 - x0);
    const float room = r.bottom - r.top;
    const float badge = std::min(kBadge * r.u, kBadgeShare * width / 9.0f);
    const float frow = badge + r.tight;
    const int per_row = cells.per_row;
    const int row_count = (cells.count + per_row - 1) / per_row;
    const float cell = (width - static_cast<float>(per_row - 1) * r.gap) / static_cast<float>(per_row);
    const bool rows = learn_types_fit_rows(cell, room, r, frow, row_count, cells.cell_min);
    float big = kBigScale * r.hit;
    if (!rows) {
        // One strip row and the foot row: they grow from a hit towards `big` while the keyboard keeps
        // its least height.
        const float spare = room - 2.0f * r.hit - 2.0f * r.gap - 2.0f * frow - kKeyboardMinH * r.u;
        big = r.hit + std::clamp(spare * 0.5f, 0.0f, big - r.hit);
    }
    // Rows of types a dead gap apart.
    const float types_h =
        rows ? static_cast<float>(row_count) * big + static_cast<float>(row_count - 1) * r.tight
             : big;
    const float keys_room = std::max(0.0f, room - types_h - 2.0f * r.gap - 2.0f * frow - big);
    const float key_h = std::min(width / 8.0f * kKeyAspect, keys_room);
    const float used = types_h + r.gap + 2.0f * frow + key_h + r.gap + big;
    if (!place) {
        return used;
    }
    c.badge = badge;
    c.rows = rows;
    const float top = block_bottom - used;
    if (rows) {
        for (int i = 0; i < cells.count; ++i) {
            const float cx = x0 + static_cast<float>(i % per_row) * (cell + r.gap);
            const float cy = top + static_cast<float>(i / per_row) * (big + r.tight);
            c.type[static_cast<std::size_t>(i)] = {cx, cy, cx + cell, cy + big};
        }
        c.type_cells = cells.count;
    } else {
        c.strip = {x0, top, x1, top + big};
        c.type_cells = 0;
    }
    const float kb_top = top + types_h + r.gap;
    c.fingers_right = {x0, kb_top, x1, kb_top + frow};
    c.keyboard = {x0, c.fingers_right.y1, x1, c.fingers_right.y1 + key_h};
    c.fingers_left = {x0, c.keyboard.y1, x1, c.keyboard.y1 + frow};
    learn_place_foot(c, r, c.keyboard.cx(), width, c.fingers_left.y1 + r.gap + big, big,
                     mode == LearnColumn::Popover ? 0.0f : cells.stepper_w, words_allowed);
    return used;
}

// The settings' card of the Popover column: the settings that did not fit under the keys (the rows
// from `inline_rows` on), two to a row as the full card has them (tempo and direction over octaves and
// scale / arpeggio or the chord card's inversion), from the body's top left, as wide as the room allows.
inline void learn_place_popover(LearnCardLayout& c, const LearnCardRoom& r, const LearnCells& cells,
                                int inline_rows) {
    const float left_w = 2.0f * r.hit + learn_tok::kTempoValueW * r.u;
    const float right_w = 3.0f * r.hit + 2.0f * r.tight;
    const float inner_w = left_w + r.gap + right_w;
    const float w = std::max(inner_w + 2.0f * r.gap, learn_tok::kPopoverW * r.u);
    const int first = inline_rows / 2; // a row whose settings all stand under the keys is not in it
    const float rows = static_cast<float>(2 - first);
    const float h = rows * r.hit + (rows - 1.0f) * r.tight + 2.0f * r.gap;
    c.popover = {r.left, r.top, std::min(r.left + w, r.right), r.top + h};
    const float x0 = c.popover.x0 + r.gap;
    const float rx = x0 + left_w + r.gap;
    const auto row_y = [&](int setting) {
        return c.popover.y0 + r.gap + static_cast<float>(setting / 2 - first) * (r.hit + r.tight);
    };
    DrillBoxes& d = c.pop;
    if (inline_rows <= 0) {
        drill_place_tempo(d, {x0, row_y(0), x0 + left_w, row_y(0) + r.hit}, r.hit);
    }
    if (inline_rows <= 1) {
        d.dir = drill_cells<3>(rx, rx + right_w, row_y(1), r.hit, r.tight);
    }
    if (inline_rows <= 2) {
        d.oct = drill_cells<2>(x0, x0 + left_w, row_y(2), r.hit, r.tight);
        d.oct_words = d.oct[0].w() >= learn_tok::kOctWordsMin * r.u;
    }
    if (inline_rows <= 3) {
        if (cells.kinds) {
            d.kind = drill_cells<2>(rx, rx + right_w, row_y(3), r.hit, r.tight);
            d.kind_words = d.kind[0].w() >= learn_tok::kKindWordsMin * r.u;
        } else if (cells.stepper_w > 0.0f) {
            d.stepper = {rx, row_y(3), rx + right_w, row_y(3) + r.hit};
        }
    }
}

// The settings that stand under the keys in the Popover column, each a row `hit` tall across [x0, x1]
// from `y` down (the first `n` of the four): tempo, direction, octaves, scale / arpeggio or the chord
// card's inversion. Returns the y under the last.
inline float learn_place_inline(LearnCardLayout& c, const LearnCardRoom& r, const LearnCells& cells,
                                int n, float x0, float x1, float y) {
    DrillBoxes& d = c.drill;
    for (int i = 0; i < n; ++i) {
        switch (i) {
        case 0:
            drill_place_tempo(d, {x0, y, x1, y + r.hit}, r.hit);
            break;
        case 1:
            d.dir = drill_cells<3>(x0, x1, y, r.hit, r.tight);
            break;
        case 2:
            d.oct = drill_cells<2>(x0, x1, y, r.hit, r.tight);
            d.oct_words = d.oct[0].w() >= learn_tok::kOctWordsMin * r.u;
            break;
        default:
            if (cells.kinds) {
                d.kind = drill_cells<2>(x0, x1, y, r.hit, r.tight);
                d.kind_words = d.kind[0].w() >= learn_tok::kKindWordsMin * r.u;
            } else {
                d.stepper = {x0, y, x1, y + r.hit};
            }
            break;
        }
        y += r.hit + r.tight;
    }
    return n > 0 ? y - r.tight : y;
}

// The size the types' words are drawn at, as a share of the body size, in a cell `cell_pt` wide
// (design points) when the widest of them is `need_pt` wide at the body size, pad included: the
// largest up to `kTypeLabelScale` that holds them, never below the body size (a cell narrower than
// `need_pt` is a strip's, not rows').
inline float learn_label_scale(float cell_pt, float need_pt) {
    if (!(need_pt > 0.0f)) {
        return learn_tok::kTypeLabelScale;
    }
    return std::clamp(cell_pt / need_pt, 1.0f, learn_tok::kTypeLabelScale);
}

// The unit the card's body (everything under the header) is laid out and drawn in: the window's
// unit grown by how much larger the window is than the 1366x768 the card is first drawn for (never
// less), so a larger window shows the same card at a larger size rather than the same size with
// air around it.
inline float learn_card_unit(const Box& area, float u) {
    const float across = area.w() / (learn_tok::kGrowW * u);
    const float down = area.h() / (learn_tok::kGrowH * u);
    return u * std::clamp(std::min(across, down), 1.0f, learn_tok::kGrowMax);
}

// The twelve keys as buttons, the picker's other look: four across and three down, or three across
// and four down (`learn_grid_columns`: whichever is larger in the slot), in the order of a keyboard
// (C, Db, D, Eb, E, F, F#, G, Ab, A, Bb, B), by pitch class.
inline std::array<Box, 12> learn_key_grid(const Box& slot, float u) {
    const float tight = tok::kHitGap * u; // a dead gap between hit areas
    const int columns = learn_grid_columns(slot.w(), slot.h(), tight);
    const int rows = 12 / columns;
    const float cell_w = (slot.w() - static_cast<float>(columns - 1) * tight) /
                         static_cast<float>(columns);
    const float cell_h = std::min(cell_w, (slot.h() - static_cast<float>(rows - 1) * tight) /
                                              static_cast<float>(rows));
    const float top = slot.y0 + (slot.h() - static_cast<float>(rows) * cell_h -
                                 static_cast<float>(rows - 1) * tight) *
                                    0.5f;
    std::array<Box, 12> out{};
    for (int pc = 0; pc < 12; ++pc) {
        const float x0 = slot.x0 + static_cast<float>(pc % columns) * (cell_w + tight);
        const float y0 = top + static_cast<float>(pc / columns) * (cell_h + tight);
        out[static_cast<std::size_t>(pc)] = {x0, y0, x0 + cell_w, y0 + cell_h};
    }
    return out;
}

// The left column, from the block's top: its parts by the form the room gave it, the last row level
// with the foot row (already placed). A column shorter than the block (the chord card has no scale /
// arpeggio row) spreads what is left between its parts.
inline void learn_place_left(LearnCardLayout& c, const LearnCardRoom& r, const LearnCells& cells,
                             const LearnColumnChoice& choice, float width, float block_top,
                             float block_h, float left_h, float grid_h) {
    const float foot_cy = c.play.cy();
    const float extra = std::max(0.0f, block_h - left_h);
    const float top = block_top;
    const float x0 = r.left;
    const float x1 = r.left + width;
    const float side = choice.side;
    const float cx = x0 + width * 0.5f;
    switch (c.column) {
    case LearnColumn::Full: {
        const float e = extra / 3.0f;
        drill_place_tempo(c.drill, {x0, top, x1, top + r.hit}, r.hit);
        c.name = {x0, top + r.hit + r.gap + e, x1, top + 2.0f * r.hit + r.gap + e};
        c.slot = {x0, c.name.y1 + r.gap, x0 + side, c.name.y1 + r.gap + side};
        drill_place_rows(c.drill, x0, x1, c.slot.y1 + r.gap + e, r.hit, r.tight, r.u, cells.kinds);
        c.toggle = {cx - r.hit * 0.5f, foot_cy - r.hit * 0.5f, cx + r.hit * 0.5f,
                    foot_cy + r.hit * 0.5f};
        break;
    }
    case LearnColumn::Compact: {
        const float my = foot_cy - r.hit * 0.5f;
        const float sy = top + extra * 0.5f;
        c.slot = {x0 + (width - side) * 0.5f, sy, x0 + (width - side) * 0.5f + side, sy + side};
        c.drill.dir = drill_cells<3>(x0, x1, my - r.tight - r.hit, r.hit, r.tight);
        const DrillMergedRow row = drill_merged_row(r.hit, r.u, cells.kinds);
        const float span = row.width() + static_cast<float>(row.rest + 1) * r.tight;
        float x = x0 + (width - span) * 0.5f;
        const auto next = [&](float w) {
            const Box b{x, my, x + w, my + r.hit};
            x += w + r.tight;
            return b;
        };
        c.drill.oct = {next(row.oct_w), next(row.oct_w)};
        if (cells.kinds) {
            c.drill.kind = {next(row.rest_w), next(row.rest_w)};
        }
        c.toggle = next(row.rest_w);
        c.drill.oct_words = false;
        c.drill.kind_words = false;
        break;
    }
    case LearnColumn::Popover: {
        // The keys' buttons, then the settings that fit, then Options right under the last of them,
        // the stack centred in the block.
        const float y_top = top + std::max(0.0f, (block_h - left_h) * 0.5f);
        c.slot = {x0, y_top, x0 + width, y_top + grid_h};
        c.toggle = {};
        float bottom = c.slot.y1;
        if (c.inline_rows > 0) {
            bottom = learn_place_inline(c, r, cells, c.inline_rows, x0, x1, c.slot.y1 + r.gap);
        }
        if (c.options) {
            c.settings = {x0, bottom + r.tight, x1, bottom + r.tight + r.hit};
            learn_place_popover(c, r, cells, c.inline_rows);
        }
        break;
    }
    }
}

inline LearnCardLayout learn_card_layout(const Box& area, float u,
                                         const LearnCells& cells = kScaleCells,
                                         const LearnTabs& tabs = {}) {
    LearnCardLayout c;
    LearnCardRoom r;
    // The body in its own unit (the header in the window's).
    const float ub = learn_card_unit(area, u);
    r.u = ub;
    r.hit = tok::kHit * ub;
    r.gap = tok::kS2 * ub;
    r.tight = tok::kHitGap * ub;
    r.pad = learn_tok::kPad * ub;
    r.top = area.y0 + tok::kHitGap * u + tok::kHit * u + r.gap; // under the header
    r.bottom = area.y1 - r.pad;
    r.left = area.x0 + r.pad;
    r.right = area.x1 - r.pad;
    const float big = learn_tok::kBigScale * r.hit;
    // Listen and Practice carry their words only on a window wide enough for them (not the 7 inch 800x480 panel).
    const bool words = area.w() / u >= learn_tok::kFootWordsMinW;
    const LearnColumnChoice choice = learn_place_picker(r, cells.kinds);
    c.column = choice.mode;
    c.ring_fits = choice.ring_fits;
    c.ring_side = choice.side;
    // The header in the window's unit: its own hit, gap and tight.
    LearnCardRoom h = r;
    h.u = u;
    h.hit = tok::kHit * u;
    h.gap = tok::kS2 * u;
    h.tight = tok::kHitGap * u;
    learn_place_header(c, area, h, tabs, c.column, c.drill);
    // The left column's width: the picker's square, or what its widest row of settings needs.
    float width = choice.side;
    if (c.column == LearnColumn::Compact) {
        // The scale card's row, whichever card: the keys start at the same place on both, so they
        // do not jump when a tab is tapped.
        const DrillMergedRow row = drill_merged_row(r.hit, r.u, true);
        width = std::max(width, row.width() + static_cast<float>(row.rest + 1) * r.tight);
    }
    r.side_w = width;
    const float between = c.ring_fits ? r.pad : r.gap;
    r.col_x0 = r.left + width + between;
    // The Popover column: the keys' buttons, as many settings under them as the room holds and the
    // Options button after them (`learn_popover_plan`). A body too short for the buttons and that
    // button (no setting fits) is given the margin below it that they need, down to three quarters of
    // a gap (the margin is the screen's edge, not a gap between hit areas).
    PopoverPlan plan;
    if (c.column == LearnColumn::Popover) {
        plan = learn_popover_plan(r.bottom - r.top, std::min(3.0f * r.hit + 2.0f * r.tight, r.bottom - r.top),
                                  r.hit, r.gap, r.tight);
        c.inline_rows = plan.inline_rows;
        c.options = plan.options;
        if (plan.inline_rows == 0) {
            const float deficit = 4.0f * r.hit + 3.0f * r.tight - (r.bottom - r.top);
            r.bottom += std::clamp(deficit, 0.0f, std::max(0.0f, r.pad - 0.75f * r.gap));
        }
    }
    // The two columns share one block, ending at the foot row.
    const float room = r.bottom - r.top;
    const LearnColumnNeed need =
        learn_column_need(c.column, r.hit, r.gap, r.tight, big, cells.kinds);
    const float grid_h = std::min(3.0f * r.hit + 2.0f * r.tight, room);
    const float left_h = c.column == LearnColumn::Popover ? plan.stack_h
                                                           : need.above + choice.side + need.below;
    const float used = learn_place_column(c, r, cells, c.column, 0.0f, words, false);
    const float block_h = std::min(std::max(left_h, used), room);
    const float block_bottom = r.top + (room - block_h) * 0.5f + block_h;
    learn_place_column(c, r, cells, c.column, block_bottom, words, true);
    learn_place_left(c, r, cells, choice, width, block_bottom - block_h, block_h, left_h, grid_h);
    if (c.column == LearnColumn::Popover) {
        // The twelve keys: their grid, a hit across at the least, in the square the column gives.
        c.ring_side = width;
    }
    return c;
}

} // namespace etude::app::ui
