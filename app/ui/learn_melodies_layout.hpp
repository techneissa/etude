// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Melodies page of the Learn screen puts its parts (#375), in ImGui points. No ImGui here, so tests/learn_melodies_layout_test.cpp checks the real numbers at every
// window size.
//
// The header is the Learn cards' (back arrow, the page's name, the four tabs). Under it one section
// for each tutorial path: a header row (the path's icon, its name over "3 of 6", the next piece in
// words, and a Continue button) and under it the row of the path's pieces, one tile each, side by
// side. A tile is one pressable that opens its piece: the number (or a check once passed) and the
// title at the top, the goal in a line under it when the tile is tall enough, and along the bottom
// the hands (each with its key count under it), the difficulty stars between them and Play, which
// wears the progress ring. The stars sit there at every size. Where the tiles of a path do not fit
// across, its row scrolls sideways (the next tile showing at the edge says so); where the sections
// do not fit down, the page scrolls.
#pragma once

#include "learn_card_layout.hpp"
#include "tokens.hpp"

#include "etude/melody_manifest.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude::app::ui {

// What the app measures in the font and reads from the manifest, in design points and counts.
struct MelodyNeeds {
    LearnTabs tabs{{82.0f, 85.0f, 66.0f, 96.0f}, 3};
    std::array<int, etude::kMelodyMaxPaths> pieces{}; // the pieces in each path
    int paths = 0;
    // The "From your library" row under the paths: how many songs it shows, or -1 for no row (the
    // library is not there). With none shown it is its header alone.
    int library = -1;
};

struct MelodySectionBoxes {
    Box whole;  // the header row and the tiles' row
    Box icon;   // the path's icon chip
    Box title;  // its name over its count
    Box next;   // "Next · the piece: its goal"; empty when there is no room for it
    Box button; // Continue
    Box row;    // the tiles' view: its tiles side by side from the left, the rest scrolled away
    float content_w = 0.0f; // all the tiles, side by side
    bool scrolls = false;   // they are wider than the view
    bool button_words = true; // the button carries its words (else its icon alone)
};

struct MelodiesLayout {
    Box header;
    Box back;
    Box title;
    std::array<Box, kLearnTabs> tabs;
    std::array<bool, kLearnTabs> tab_label{};
    Box body;                  // under the header: everything below is clipped to it
    std::array<MelodySectionBoxes, etude::kMelodyMaxPaths> section;
    MelodySectionBoxes library; // the "From your library" section, under the last path's
    bool has_library = false;
    int sections = 0;
    float tile_w = 0.0f;
    float tile_h = 0.0f;
    bool goal = false;         // the tiles have their goal line
    float page_h = 0.0f;       // the sections' height, from the body's top
    float max_scroll = 0.0f;   // how far the page scrolls down (0: it fits)
    float scroll = 0.0f;       // as laid out: `scroll` clamped to 0..max_scroll
};

// One section of the page from its top `y0`: the header row (the icon, the title, the next piece in
// words and the button) and under it the row of `pieces` tiles `row_h` tall.
inline void melody_place_section(MelodySectionBoxes& s, const Box& area, const LearnCardRoom& r, float y0,
                                 int pieces, float tile_w, float row_h, float u) {
    using namespace melody_tok;
    const float pad = learn_tok::kPad * u;
    const float hit = r.hit;
    const float gap = kTileGap * u;
    const float header_row = hit + gap; // the header and the dead gap under it
    const float across = area.w() - 2.0f * pad;
    s.whole = {area.x0 + pad, y0, area.x1 - pad, y0 + header_row + row_h};
    const float cy = y0 + hit * 0.5f;
    const float head_icon = kHeadIcon * u;
    s.icon = {s.whole.x0, cy - head_icon * 0.5f, s.whole.x0 + head_icon, cy + head_icon * 0.5f};
    // The button at the right: its words where the row has room for the title, the next piece in
    // words and it; else the icon alone.
    const float title_w = kHeadTitleW * u;
    const float words_w = kButtonW * u;
    s.title = {s.icon.x1 + r.gap, y0, s.icon.x1 + r.gap + title_w, y0 + hit};
    s.button_words = s.whole.w() >= title_w + words_w + head_icon + 2.0f * r.gap;
    const float button_w = s.button_words ? words_w : std::max(kButtonMinW * u, hit);
    s.button = {s.whole.x1 - button_w, y0, s.whole.x1, y0 + hit};
    const float next_x0 = s.title.x1 + r.gap;
    const float next_x1 = s.button.x0 - r.gap;
    s.next = next_x1 - next_x0 >= kNextMinW * u ? Box{next_x0, y0, next_x1, y0 + hit} : Box{};
    s.content_w = static_cast<float>(pieces) * tile_w + static_cast<float>(std::max(pieces - 1, 0)) * gap;
    s.scrolls = s.content_w > across + 0.5f;
    const float row_y = y0 + header_row;
    // A scrolling row runs to the screen's edge, so the tile cut by it shows there is more.
    s.row = {s.whole.x0, row_y, s.scrolls ? area.x1 : s.whole.x1, row_y + row_h};
}

inline MelodiesLayout melodies_layout(const Box& area, float u, const MelodyNeeds& needs,
                                      float scroll) {
    using namespace melody_tok;
    MelodiesLayout m;
    // The header, as the cards have it.
    LearnCardRoom r;
    r.u = u;
    r.hit = tok::kHit * u;
    r.gap = tok::kS2 * u;
    r.tight = tok::kHitGap * u;
    LearnCardLayout card;
    DrillBoxes unused;
    learn_place_header(card, area, r, needs.tabs, LearnColumn::Full, unused);
    m.header = card.header;
    m.back = card.back;
    m.title = card.title;
    m.tabs = card.tabs;
    m.tab_label = card.tab_label;

    const float pad = learn_tok::kPad * u;
    const float hit = r.hit;
    const float gap = kTileGap * u;
    m.body = {area.x0, m.header.y1 + r.tight, area.x1, area.y1};
    m.sections = std::clamp(needs.paths, 0, kMelodyMaxPaths);
    const float top = m.body.y0 + r.gap;
    const float room = area.y1 - pad - top;
    const float n = static_cast<float>(std::max(m.sections, 1));
    int most = 1;
    for (int i = 0; i < m.sections; ++i) {
        most = std::max(most, needs.pieces[static_cast<std::size_t>(i)]);
    }
    const float across = area.w() - 2.0f * pad;
    // As wide as lets the longest path's tiles fit across, never narrower than `kTileMinW` (then the
    // row scrolls) and never wider than `kTileMaxW`.
    m.tile_w = std::clamp((across - static_cast<float>(most - 1) * gap) / static_cast<float>(most),
                          kTileMinW * u, kTileMaxW * u);
    // As tall as the room gives each section, within the least a tile needs and the most it grows.
    const float header_row = hit + gap; // a path's header and the dead gap under it
    const float per = (room - (n - 1.0f) * gap) / n - header_row;
    m.tile_h = std::clamp(per, kTileMinH * u, kTileMaxH * u);
    m.goal = m.tile_h >= kTileGoalH * u;
    const float section_h = header_row + m.tile_h;
    // The library's row under the paths: its header, and its tiles when it has any.
    m.has_library = needs.library >= 0;
    const float library_h = m.has_library ? gap + header_row + (needs.library > 0 ? m.tile_h : 0.0f) : 0.0f;
    m.page_h = r.gap + n * section_h + (n - 1.0f) * gap + library_h + pad;
    m.max_scroll = std::max(0.0f, m.page_h - (area.y1 - m.body.y0));
    m.scroll = std::clamp(scroll, 0.0f, m.max_scroll);

    for (int i = 0; i < m.sections; ++i) {
        const float y0 = top + static_cast<float>(i) * (section_h + gap) - m.scroll;
        melody_place_section(m.section[static_cast<std::size_t>(i)], area, r, y0,
                             needs.pieces[static_cast<std::size_t>(i)], m.tile_w, m.tile_h, u);
    }
    if (m.has_library) {
        const float y0 = top + static_cast<float>(m.sections) * (section_h + gap) - m.scroll;
        melody_place_section(m.library, area, r, y0, std::max(needs.library, 0), m.tile_w,
                             needs.library > 0 ? m.tile_h : 0.0f, u);
    }
    return m;
}

// ---- One tile ---------------------------------------------------------------------------------

struct MelodyTileBoxes {
    Box badge;     // the number, or the check
    Box title;     // up to two lines, beside the badge
    Box goal;      // one line under them (empty when the tile is too short for it)
    Box hand_left; // each hand's glyph with its key count under it (empty when the piece has none)
    Box hand_right;
    Box stars;     // the five difficulty stars, between the hands and Play
    Box play;      // the Play disc with its ring round it
    Box mark;      // a library song's small library mark in the top right corner (a tile with no goal line)
};

// The parts of the tile `tile`. `hands`: bit 0 the left hand has notes, bit 1 the right. `library`: a
// song of the player's library, which has the library's mark (beside the title where there is no goal
// line to carry it).
inline MelodyTileBoxes melody_tile_boxes(const Box& tile, bool goal, int hands, float u,
                                         bool library = false) {
    using namespace melody_tok;
    MelodyTileBoxes t;
    const float pad = kTilePad * u;
    const float line = (tok::kLabel * 1.2f) * u;
    const Box in{tile.x0 + pad, tile.y0 + pad * 0.6f, tile.x1 - pad, tile.y1 - pad * 0.6f};
    const float ring = (kPlay + 2.0f * (kPlayGap + kRing)) * u;
    // The bottom row: Play at the right, the hands at the left, the stars in the room between.
    t.play = {in.x1 - ring, in.y1 - ring, in.x1, in.y1};
    const float cell = kHandCell * u;
    float x = in.x0;
    const float row_h = std::max(ring, cell * 1.4f);
    const float cy = in.y1 - row_h * 0.5f;
    const float hand_h = (kHandIcon + tok::kS1 + kCountPt) * u;
    if ((hands & 1) != 0) {
        t.hand_left = {x, cy - hand_h * 0.5f, x + cell, cy + hand_h * 0.5f};
        x += cell + tok::kS1 * u;
    }
    if ((hands & 2) != 0) {
        t.hand_right = {x, cy - hand_h * 0.5f, x + cell, cy + hand_h * 0.5f};
        x += cell + tok::kS1 * u;
    }
    const float stars_w = (5.0f * 2.0f * kStar + 4.0f * kStarGap) * u;
    const float room_x0 = x;
    const float room_x1 = t.play.x0 - tok::kS1 * u;
    const float sx = room_x0 + std::max(0.0f, (room_x1 - room_x0 - stars_w) * 0.5f);
    t.stars = {sx, cy - kStar * u, sx + stars_w, cy + kStar * u};
    // The top: the badge, and the title beside it, two lines tall.
    const float badge = kBadge * u;
    const float title_h = std::max(badge, 2.0f * line);
    t.badge = {in.x0, in.y0 + (title_h - badge) * 0.5f, in.x0 + badge, in.y0 + (title_h + badge) * 0.5f};
    t.title = {t.badge.x1 + tok::kS2 * u, in.y0, in.x1, in.y0 + title_h};
    if (library && !goal) {
        const float mark = tok::kIconSm * u;
        t.mark = {in.x1 - mark, in.y0, in.x1, in.y0 + mark};
        t.title.x1 = t.mark.x0 - tok::kS1 * u;
    }
    if (goal) {
        const float y0 = in.y0 + title_h + tok::kS1 * u;
        t.goal = {in.x0, y0, in.x1, y0 + kGoalLine * u};
    }
    return t;
}

// The tile at `place` of a row whose view starts at `row`, scrolled `offset` points.
inline Box melody_tile_at(const Box& row, int place, float tile_w, float tile_h, float u, float offset) {
    const float x0 = row.x0 + static_cast<float>(place) * (tile_w + melody_tok::kTileGap * u) - offset;
    return {x0, row.y0, x0 + tile_w, row.y0 + tile_h};
}

} // namespace etude::app::ui
