// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the medal wall go (medal_wall.hpp): pure geometry from the body under the Progress
// page's header, the UI unit and the view, so the layout tests can check every size without ImGui.
//
// Two views of the same 24 medals (#396), one icon toggles them:
// - Category: the five categories as tabs along the top, the medals of the chosen one large (ribbon
//   and all) in one row of up to six with their names and counts, and under them the strip of the
//   achievements nearest their next tier (given up first when the body is short).
// - Rows: every category on one screen, a row each: the category at the left, its medals small along
//   the row with their names under them.
// A tap on a medal in either opens the ladder: a card over the wall with the eight tiers of that
// achievement (their materials, titles and what each asks for), planned by `plan_ladder`.
#pragma once

#include "face_layout.hpp"
#include "tokens.hpp"

#include "etude/session_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace etude::app::ui {

namespace wall_tok {
// Design points (times the UI unit `u`).
inline constexpr float kGap = tok::kS2;
inline constexpr float kPad = tok::kS3;
inline constexpr float kTabNamedW = 132.0f;   // a tab this wide shows its category's name beside the icon (the
                                               // widest, "Practice time", bold, with its icon and padding)
inline constexpr float kStripH = 72.0f;       // "closest to next tier": its title and a card of this
inline constexpr float kNearMaxW = 260.0f;    // one card of the strip, as wide as it gets
inline constexpr float kNearMedal = 36.0f;    // the small medal in one
inline constexpr float kNameH = 22.0f;        // a medal's name line (bold, body size)
inline constexpr float kCaptionH = 18.0f;     // ... and the line under it (caption size)
inline constexpr float kCaptionMin = 9.0f;    // the least size, in pixels, a line under a medal shrinks to before it
                                              // wraps (the least a label is read at: `kMinLabelFontPx`)
inline constexpr float kLargeMax = 148.0f;    // a large medal's disc, at most
inline constexpr float kLargeMin = 40.0f;
// The large medal's frame, over the disc's diameter (medal_art.hpp: 192 x 272 around a disc of 168).
inline constexpr float kLargeW = 192.0f / 168.0f;
inline constexpr float kLargeAbove = 174.0f / 168.0f; // from the frame's top to the disc's centre
inline constexpr float kLargeH = 272.0f / 168.0f;
inline constexpr float kSmallMax = 52.0f;     // a small medal's disc in a row
inline constexpr float kSmallMin = 30.0f;
inline constexpr float kSmallFrame = 64.0f / 58.0f; // the small frame over its disc
inline constexpr float kRowHeadW = 132.0f;    // a row's category (icon, name, points)
inline constexpr float kRowHeadIconW = 52.0f;  // ... only its icon, on a narrow body (where the medals need the width)
inline constexpr float kRowHeadNarrow = 640.0f; // a body narrower than this (design points) has the icon-only head
inline constexpr float kRowNameH = 16.0f;     // a name under a small medal
inline constexpr float kRowStackedH = 76.0f;  // from this row height the head has three lines, else one
inline constexpr float kMaxPerCategory = 6.0f;
// The ladder card.
inline constexpr float kLadderMaxW = 760.0f;
inline constexpr float kLadderRungMinW = 76.0f; // from this width a rung goes eight across, else two rows of four
inline constexpr float kLadderPad = tok::kS2;     // inside the card
inline constexpr float kLadderHead = 52.0f;
inline constexpr float kLadderHeadShort = 28.0f; // a body too short for both lines: the name and the rule on one     // the achievement's name over its rule
inline constexpr float kLadderRungText = 2.0f * kCaptionH; // a rung's title and number
inline constexpr float kRungMax = 56.0f;
inline constexpr float kRungMin = 26.0f;
} // namespace wall_tok

// One medal of the wall and the room it has: `cell` the whole slot (the hit area), the medal's centre
// and its disc across, then the lines under it.
struct WallSlot {
    Box cell;
    float cx = 0.0f;
    float cy = 0.0f;
    float d = 0.0f;
    Box name;
    Box caption;
};

// How many lines the names and the captions under the medals take (one each unless a long one wraps):
// every medal of the category gets that room, so the row stays level.
struct WallLines {
    int name = 1;
    int caption = 1;
    bool operator==(const WallLines& o) const { return name == o.name && caption == o.caption; }
};

struct WallLayout {
    Box body;
    WallLines under; // the lines under each large medal: what was asked for, or one each when a medal would shrink too far
    // Category view.
    std::array<Box, 5> tab{};
    bool tab_names = false;           // every tab shows its name (else only the chosen one)
    std::array<WallSlot, 6> slot{};
    int slots = 0;
    Box strip;                        // the title and cards of the closest medals; empty when given up
    Box strip_title;
    std::array<Box, 3> near{};
    int nears = 0;
    // Rows view.
    struct Row {
        Box row;
        Box head;
        std::array<WallSlot, 6> slot{};
        int slots = 0;
    };
    std::array<Row, 5> row{};
    int rows = 0;
    bool row_stacked = false;         // rows tall enough for the head's icon, name and count in three lines
    bool row_names = true;            // rows tall enough for the names under their small medals
    bool row_head_icon = false;       // a narrow body: the head is the category's icon alone
};

// ---- The words under a large medal, measured at the size they are drawn at ----
// The kit draws a label at a whole point (a size under 40 pt is rounded), which can be wider than the
// size it was asked for; a line that fits here fits where it is drawn. `width(text, pt)` is the width of
// `text` at `pt` in the font it is drawn with.

// The size a label asked for at `size` is drawn at; a size that is not above 0 stays as it is.
inline float wall_drawn_pt(float size) {
    constexpr float kRoundBelow = 40.0f;
    if (!(size > 0.0f)) {
        return size;
    }
    return size < kRoundBelow ? std::max(1.0f, std::round(size)) : size;
}

// The width of a cell the medals of one category share, and the room a line has in it: the medal's hit
// area, so words never run under a neighbour's.
inline float wall_cell_w(const Box& body, int medals) { return body.w() / static_cast<float>(std::max(medals, 4)); }
inline float wall_text_room(float cell_w, float u) { return std::max(0.0f, cell_w - tok::kHitGap * u); }

// One or two lines of words and the whole size they are drawn at.
struct WallText {
    float pt = 0.0f;
    int lines = 1;
    std::array<std::string, 2> line;
};

// `size` (as drawn) a point smaller at a time while `width(pt)` is over `room`, down to `floor`.
template <class Width>
float wall_line_size(float size, float room, float floor, const Width& width) {
    float pt = wall_drawn_pt(size);
    const float least = std::min(pt, std::max(1.0f, std::round(floor)));
    while (pt > least && width(pt) > room) {
        pt -= 1.0f;
    }
    return pt;
}

// `text` in two lines at `pt`, cut at a space, each within `room`; false when no cut does it.
template <class Width>
bool wall_wrap(const std::string& text, float pt, float room, const Width& width, WallText& out) {
    std::size_t cut = std::string::npos;
    for (std::size_t i = text.find(' '); i != std::string::npos; i = text.find(' ', i + 1)) {
        if (width(text.substr(0, i), pt) > room) {
            break;
        }
        cut = i;
    }
    if (cut == std::string::npos || width(text.substr(cut + 1), pt) > room) {
        return false;
    }
    out.pt = pt;
    out.lines = 2;
    out.line = {text.substr(0, cut), text.substr(cut + 1)};
    return true;
}

// A caption: one line at `size`, smaller to `floor` while it is too wide, then in two lines at the
// floor (when `max_lines` is 2). Only a line that fits in neither is left for the caller to cut.
template <class Width>
WallText wall_caption(const std::string& text, float size, float room, float floor, const Width& width,
                      int max_lines = 2) {
    WallText out;
    out.pt = wall_line_size(size, room, floor, [&](float pt) { return width(text, pt); });
    out.line[0] = text;
    if (max_lines >= 2 && width(text, out.pt) > room) {
        WallText two;
        if (wall_wrap(text, out.pt, room, width, two)) {
            return two;
        }
    }
    return out;
}

// A name: one line at `body` size when it fits, else two lines at `body`, else one line at `caption`
// size or smaller (to `floor`), else two lines there. Only a line that fits in none is left to cut.
template <class Width>
WallText wall_name(const std::string& text, float body, float caption, float room, float floor, const Width& width,
                   int max_lines = 2) {
    WallText out;
    out.pt = wall_drawn_pt(body);
    out.line[0] = text;
    if (width(text, out.pt) <= room || (max_lines >= 2 && wall_wrap(text, out.pt, room, width, out))) {
        return out;
    }
    return wall_caption(text, caption, room, floor, width, max_lines);
}

namespace wall_detail {

// A large medal's slot in `cell`: the medal with its ribbon on top, the name and the count under, each
// `under` lines tall.
inline WallSlot large_slot(const Box& cell, float u, WallLines under = {}) {
    namespace w = wall_tok;
    WallSlot s;
    s.cell = cell;
    const float name_h = w::kNameH * static_cast<float>(under.name) * u;
    const float caption_h = w::kCaptionH * static_cast<float>(under.caption) * u;
    const float lines = name_h + caption_h + w::kGap * u;
    const float by_h = (cell.h() - lines) / w::kLargeH;
    const float by_w = cell.w() * 0.92f / w::kLargeW;
    s.d = std::max(0.0f, std::min({by_h, by_w, w::kLargeMax * u}));
    const float used = s.d * w::kLargeH + lines;
    const float top = cell.y0 + std::max(0.0f, (cell.h() - used) * 0.5f);
    s.cx = cell.cx();
    s.cy = top + s.d * w::kLargeAbove;
    const float below = top + s.d * w::kLargeH + w::kGap * u;
    s.name = {cell.x0, below, cell.x1, below + name_h};
    s.caption = {cell.x0, s.name.y1, cell.x1, s.name.y1 + caption_h};
    return s;
}

// A small medal's slot in `cell`: the disc centred in the upper part, the name under it (when `named`).
inline WallSlot small_slot(const Box& cell, float u, bool named) {
    namespace w = wall_tok;
    WallSlot s;
    s.cell = cell;
    const float line = named ? w::kRowNameH * u : 0.0f;
    const float by_h = (cell.h() - line - (named ? w::kGap * u * 0.5f : 0.0f)) / w::kSmallFrame;
    const float by_w = cell.w() * 0.9f / w::kSmallFrame;
    s.d = std::max(0.0f, std::min({by_h, by_w, w::kSmallMax * u}));
    const float used = s.d * w::kSmallFrame + (named ? w::kGap * u * 0.5f : 0.0f) + line;
    const float top = cell.y0 + std::max(0.0f, (cell.h() - used) * 0.5f);
    s.cx = cell.cx();
    s.cy = top + s.d * w::kSmallFrame * 0.5f;
    const float below = top + s.d * w::kSmallFrame + (named ? w::kGap * u * 0.5f : 0.0f);
    s.name = {cell.x0, below, cell.x1, below + line};
    s.caption = {cell.x0, below + line, cell.x1, below + line};
    return s;
}

// The rows view: five rows share `body`, each a category at the left and its medals small along the row.
inline void plan_rows(WallLayout& l, const Box& body, float u, const std::array<int, 5>& per_category) {
    namespace w = wall_tok;
    const float gap = w::kGap * u;
    const float row_gap = gap;
    const float rows_top = body.y0;
    const float row_h = (body.y1 - rows_top - row_gap * 4.0f) / 5.0f;
    l.row_stacked = row_h >= w::kRowStackedH * u;
    l.row_names = row_h >= (w::kSmallMin * w::kSmallFrame + w::kRowNameH + w::kGap * 0.5f) * u;
    l.row_head_icon = body.w() < w::kRowHeadNarrow * u;
    l.rows = 5;
    for (std::size_t r = 0; r < 5; ++r) {
        WallLayout::Row& out = l.row[r];
        const float y = rows_top + static_cast<float>(r) * (row_h + row_gap);
        out.row = {body.x0, y, body.x1, y + row_h};
        out.head = {out.row.x0, y, out.row.x0 + (l.row_head_icon ? w::kRowHeadIconW : w::kRowHeadW) * u, y + row_h};
        const int n = std::clamp(per_category[r], 0, 6);
        const float room = out.row.x1 - out.head.x1 - gap;
        const float cell_w = room / w::kMaxPerCategory;
        out.slots = n;
        for (int i = 0; i < n; ++i) {
            const float x = out.head.x1 + gap + cell_w * static_cast<float>(i);
            out.slot[static_cast<std::size_t>(i)] = wall_detail::small_slot({x, y, x + cell_w, y + row_h}, u, l.row_names);
        }
    }
}

} // namespace wall_detail

// Whether the rows view has the room for its five rows, each a hit tall, and for six medals across,
// each a hit wide with the dead gap between neighbours (a medal in it is pressed to open its ladder): a
// short or narrow body (800x480 at 1.25 or more) shows only the category view, and the page has no
// toggle there.
inline bool wall_rows_fit(const Box& body, float u) {
    const float gap = wall_tok::kGap * u;
    const float head = (body.w() < wall_tok::kRowHeadNarrow * u ? wall_tok::kRowHeadIconW : wall_tok::kRowHeadW) * u;
    const float cell_w = (body.w() - head - gap) / wall_tok::kMaxPerCategory;
    return (body.h() - 4.0f * gap) / 5.0f >= tok::kHit * u - 1e-3f && cell_w >= tok::kCell * u - 1e-3f;
}

// The wall in `body`. `per_category[i]` is how many medals category i holds (at most six);
// `chosen` the category shown in the Category view; `near_wanted` how many closest-to-next cards
// there are to show (at most three).
inline WallLayout plan_wall(const Box& body, float u, BadgeWallView view,
                            const std::array<int, 5>& per_category, int chosen, int near_wanted,
                            WallLines under = {}) {
    namespace w = wall_tok;
    WallLayout l;
    l.body = body;
    if (body.w() <= 0.0f || body.h() <= 0.0f) {
        return l;
    }
    const float hit = tok::kHit * u;
    const float gap = w::kGap * u;
    const float top_h = hit;
    const float cell = tok::kCell * u; // an icon-only tab: a hit area and the dead gap
    if (view == BadgeWallView::Category || !wall_rows_fit(body, u)) {
        const float tab_w = body.w() / 5.0f;
        l.tab_names = tab_w >= w::kTabNamedW * u;
        // Too narrow for five names: the tabs not chosen are icons a hit wide and the chosen one has the rest.
        const std::size_t on = static_cast<std::size_t>(std::clamp(chosen, 0, 4));
        float tab_x = body.x0;
        for (std::size_t i = 0; i < 5; ++i) {
            const float width = l.tab_names ? tab_w : (i == on ? body.w() - 4.0f * cell : cell);
            l.tab[i] = {tab_x, body.y0, tab_x + width, body.y0 + top_h};
            tab_x += width;
        }
        float below = body.y0 + top_h + gap;
        const int n = std::clamp(per_category[static_cast<std::size_t>(std::clamp(chosen, 0, 4))], 0, 6);
        // The strip goes first when the medals would get too little.
        const float strip_h = std::min(near_wanted, 3) > 0 ? w::kStripH * u + gap : 0.0f;
        float cells_bottom = body.y1;
        const float least_h = (w::kLargeMin * w::kLargeH + w::kNameH * static_cast<float>(under.name) +
                               w::kCaptionH * static_cast<float>(under.caption)) * u + gap;
        if (strip_h > 0.0f && body.y1 - below - strip_h >= least_h) {
            l.strip = {body.x0, body.y1 - w::kStripH * u, body.x1, body.y1};
            cells_bottom = l.strip.y0 - gap;
            l.nears = std::min(near_wanted, 3);
            const float title_h = w::kCaptionH * u;
            l.strip_title = {l.strip.x0, l.strip.y0, l.strip.x1, l.strip.y0 + title_h};
            const float card_w = std::min(w::kNearMaxW * u, (l.strip.w() - gap * 2.0f) / 3.0f);
            const float card_top = l.strip_title.y1 + gap * 0.5f;
            for (int i = 0; i < l.nears; ++i) {
                const float x = l.strip.x0 + static_cast<float>(i) * (card_w + gap);
                l.near[static_cast<std::size_t>(i)] = {x, card_top, x + card_w, l.strip.y1};
            }
        }
        const float cell_w = wall_cell_w(body, n);
        const float group_w = cell_w * static_cast<float>(n);
        const float x0 = body.x0 + (body.w() - group_w) * 0.5f;
        l.slots = n;
        // More lines under the medals are asked for only while the medal keeps its least size.
        const Box first{x0, below, x0 + cell_w, cells_bottom};
        if (wall_detail::large_slot(first, u, under).d < w::kLargeMin * u) {
            under = {};
        }
        l.under = under;
        for (int i = 0; i < n; ++i) {
            const float x = x0 + cell_w * static_cast<float>(i);
            l.slot[static_cast<std::size_t>(i)] =
                wall_detail::large_slot({x, below, x + cell_w, cells_bottom}, u, under);
        }
        return l;
    }
    wall_detail::plan_rows(l, body, u, per_category);
    return l;
}

// The ladder card over the wall: the achievement's name and rule, then its eight tiers, each a
// medal over its title and what it asks for; eight across when the card is wide enough, else two
// rows of four.
struct LadderLayout {
    Box card;
    Box close;                  // a hit square at the card's top right
    Box title;
    Box rule;
    std::array<WallSlot, 8> rung{};
    bool two_rows = false;
    bool one_line = false;      // the head is one line (the name, then the rule): `rule` is empty
};

inline LadderLayout plan_ladder(const Box& body, float u) {
    namespace w = wall_tok;
    LadderLayout l;
    const float pad = w::kLadderPad * u;
    const float hit = tok::kHit * u;
    const float card_w = std::min(w::kLadderMaxW * u, body.w());
    const float inner_w = card_w - 2.0f * pad;
    l.two_rows = inner_w / 8.0f < w::kLadderRungMinW * u;
    const int per_row = l.two_rows ? 4 : 8;
    const int rows = l.two_rows ? 2 : 1;
    const float cell_w = inner_w / static_cast<float>(per_row);
    const float text = w::kLadderRungText * u;
    const float gap = w::kGap * u;
    // Both head lines when the rungs keep their least size under them, else the name and rule on one.
    const float rows_least = (w::kRungMin * w::kSmallFrame * u + gap * 0.5f + text) * static_cast<float>(rows) +
                             gap * static_cast<float>(rows - 1);
    l.one_line = body.h() < 2.0f * pad + w::kLadderHead * u + rows_least;
    const float head = (l.one_line ? w::kLadderHeadShort : w::kLadderHead) * u;
    const float room = body.h() - 2.0f * pad - head;
    const float row_room = (room - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    const float d_max = std::min({cell_w * 0.9f / w::kSmallFrame, w::kRungMax * u,
                                  (row_room - text - gap * 0.5f) / w::kSmallFrame});
    const float d = std::max(w::kRungMin * u, d_max);
    const float row_h = d * w::kSmallFrame + gap * 0.5f + text;
    const float card_h = 2.0f * pad + head + row_h * static_cast<float>(rows) + gap * static_cast<float>(rows - 1);
    const float x0 = body.cx() - card_w * 0.5f;
    const float y0 = body.y0 + std::max(0.0f, (body.h() - card_h) * 0.5f);
    l.card = {x0, y0, x0 + card_w, y0 + card_h};
    l.close = {l.card.x1 - hit, l.card.y0, l.card.x1, l.card.y0 + hit};
    if (l.one_line) {
        l.title = {l.card.x0 + pad, l.card.y0 + pad, l.close.x0, l.card.y0 + pad + head};
    } else {
        l.title = {l.card.x0 + pad, l.card.y0 + pad, l.close.x0, l.card.y0 + pad + head * 0.5f};
        l.rule = {l.card.x0 + pad, l.title.y1, l.card.x1 - pad, l.card.y0 + pad + head};
    }
    for (int i = 0; i < 8; ++i) {
        const int r = i / per_row;
        const int c = i % per_row;
        const float x = l.card.x0 + pad + cell_w * static_cast<float>(c);
        const float y = l.card.y0 + pad + head + (row_h + gap) * static_cast<float>(r);
        WallSlot s;
        s.cell = {x, y, x + cell_w, y + row_h};
        s.d = d;
        s.cx = s.cell.cx();
        s.cy = y + d * w::kSmallFrame * 0.5f;
        const float below = y + d * w::kSmallFrame + gap * 0.5f;
        s.name = {s.cell.x0, below, s.cell.x1, below + w::kCaptionH * u};
        s.caption = {s.cell.x0, s.name.y1, s.cell.x1, s.name.y1 + w::kCaptionH * u};
        l.rung[static_cast<std::size_t>(i)] = s;
    }
    return l;
}

} // namespace etude::app::ui
