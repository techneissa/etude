// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the Progress page go (progress_page.hpp): pure geometry from the library
// screen's area, the UI unit and what the page holds, so the layout tests can check every size
// without ImGui. Two views of one page: "Progress" (the practice calendar, the accuracy and speed
// graph and the best run of each song in the left column; the profile, the time practised against
// the goal, the songs and the household rank, the medals and "Practise today" in the right one),
// "Household" (a card for each profile side by side) and "Medals" (the wall, medal_wall_layout.hpp).
//
// A column is a stack of cards: each has the height it likes and the least it can be drawn in; a
// column too short for all of them gives up, from the last in its order, what it cannot hold, and
// the cards that can shrink (the best runs, the practise spots) give rows before one is dropped.
#pragma once

#include "face_layout.hpp"
#include "medal_wall_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace etude::app::ui {

namespace progress_tok {
// Design points (times the UI unit `u`).
inline constexpr float kPad = tok::kS3;        // round the page, and inside a card
inline constexpr float kGap = tok::kS2;        // between cards
inline constexpr float kWideAt = 900.0f;       // the page width from which the right column is wide
inline constexpr float kRightW = 360.0f;       // the right column, wide
inline constexpr float kRightNarrowW = 296.0f; // and on a narrow window
inline constexpr float kRightMaxShare = 0.45f; // of the page, whatever the scale
inline constexpr float kModeW = 104.0f;        // one of the Progress | Household | Medals cells
inline constexpr float kTitleH = 20.0f;        // a card's title line
// Left column.
inline constexpr float kCalendarMinH = 150.0f;
inline constexpr float kCalendarMaxH = 232.0f;
inline constexpr float kGraphMinH = 104.0f;
inline constexpr float kGraphH = 190.0f;
inline constexpr float kBestRowH = 40.0f;
inline constexpr float kBestRowsMin = 2.0f;
// Right column.
inline constexpr float kProfileH = 64.0f;      // avatar, name, streak, the pencil
inline constexpr float kAvatar = 44.0f;
inline constexpr float kProfileShortH = 56.0f; // the narrow window's
inline constexpr float kRingCardH = 88.0f;     // the time practised, its ring and the goal
inline constexpr float kRing = 56.0f;
inline constexpr float kTilesH = 64.0f;        // songs and household rank
inline constexpr float kTilesShortH = 56.0f;
inline constexpr float kMedalsH = 92.0f;       // the medals card: the ring and the title
inline constexpr float kMedalsRing = 56.0f;
inline constexpr float kSpotH = 52.0f;         // a "Practise today" row: its words and the play button
inline constexpr float kContinueH = tok::kHit;
inline constexpr float kSpotsMax = 3.0f;
// Inside the cards (progress_charts.cpp).
inline constexpr float kCalendarLabelW = 30.0f; // the weekday names left of the calendar
inline constexpr float kCellMin = 6.0f;         // the calendar draws no cell smaller than this
inline constexpr float kCellGap = 2.0f;
inline constexpr float kCellRound = 3.0f;
inline constexpr float kLegendCell = 11.0f;     // a cell of the calendar's "Less ... More" legend
inline constexpr float kAxisW = 38.0f;          // the graph's percent labels
inline constexpr float kAxisHalfH = 8.0f;
inline constexpr float kTicksH = 18.0f;         // the speed ticks under the graph
inline constexpr float kPlotMinH = 24.0f;
inline constexpr float kPlotMinW = 40.0f;
inline constexpr float kLine = 2.0f;            // the accuracy line
inline constexpr float kDotR = 4.0f;            // its last point
inline constexpr float kTickW = 4.0f;
inline constexpr float kPointsW = 64.0f;        // a best run's columns, from the right
inline constexpr float kPillW = 44.0f;
inline constexpr float kPillHalfH = 11.0f;
inline constexpr float kAccW = 44.0f;
inline constexpr float kSpeedW = 88.0f;
inline constexpr float kTrendW = 96.0f;
inline constexpr float kTrendInset = 10.0f;
inline constexpr float kTrendLine = 1.6f;
inline constexpr float kBestTitleMinW = 150.0f; // the song's name keeps this; the trend, then the speed, go first
inline constexpr float kRingStroke = 6.0f;
inline constexpr float kTileHead = tok::kLabel + 2.0f; // a tile's bold line
inline constexpr float kIconBox = 36.0f;        // the square behind a tile's or a spot's icon
inline constexpr float kCaptionMinW = 90.0f;    // a card's side caption shows from this width
inline constexpr float kRankW = 40.0f;          // "1st" at a household card's corner
// The Household cards.
inline constexpr float kHouseRowMaxH = 132.0f;  // a ranked row, as tall as it gets
inline constexpr float kHouseRowTallH = 72.0f;  // from this height a row has a second line
inline constexpr float kHouseSlimH = tok::kHit + tok::kS1; // a hit tall at least
inline constexpr float kHouseAvatar = 52.0f;
inline constexpr float kHouseRankW = 44.0f;     // the rank's column
inline constexpr float kHouseTextW = 220.0f;    // the name and streak
inline constexpr float kHouseName = tok::kTitle; // a household row's name, bold
inline constexpr float kHouseStreak = tok::kBody; // ... and its streak line
inline constexpr float kHouseNameLine = 1.35f;  // the name's line pitch over its size
inline constexpr float kHouseStreakLine = 1.5f; // the streak's
inline constexpr float kHousePointsD = 76.0f;   // the circle the points are in
inline constexpr float kBadgeTextShare = 0.30f; // the number's size, of the circle's
inline constexpr float kBadgeInkShare = 0.84f;  // and the share of its width the number may take
inline constexpr float kHouseRing = 64.0f;      // the medal ring, at the row's right end
inline constexpr float kTitleMedal = 28.0f;    // the title's small medal on the medals card
inline constexpr float kTitleGap = 0.3f;     // the space between a name and its title, in the name's size
inline constexpr float kLineRoom = 1.7f;       // a line's slot in a row, in its type size
inline constexpr float kHouseBarH = 12.0f;      // the week's minutes, as a bar
inline constexpr float kBarMinW = 72.0f;        // a minutes bar narrower than this is left out
inline constexpr float kHouseMinutesW = 96.0f;  // a slim row's minutes, right of its bar
inline constexpr float kHouseRingAt = 620.0f;   // a row this wide (in design points) shows the medal ring
} // namespace progress_tok

enum class ProgressTab : unsigned char { Me, Household, Medals };

// What the page holds, as far as its layout goes.
struct ProgressContent {
    ProgressTab view = ProgressTab::Me;
    int best_rows = 0;     // songs with a best run (at most core's kBestSongs)
    int spots = 0;         // "Practise today" bars named (at most 3)
    bool can_continue = false;
    int profiles = 1;      // the household's size, for its view
};

struct ProgressLayout {
    bool wide = false;
    Box area;
    Box back;
    Box title;
    Box mode_me;        // Progress | Household | Medals
    Box mode_household;
    Box mode_medals;
    Box toggle;         // the Medals view's switch between the wall's two views
    Box wall;           // the Medals view's body
    // The Me view: each card's box, empty when it was given up.
    Box calendar;
    Box graph;
    Box best;
    std::array<Box, 8> best_row{};
    int best_rows = 0; // rows that fit
    Box profile;
    Box edit;          // the pencil, a hit square in the profile card
    Box ring;
    Box tiles;
    Box tile_songs;
    Box tile_rank;
    Box medals;
    Box practise;
    std::array<Box, 3> spot{}; // a row each: the whole row presses (it opens the bars)
    int spots = 0;
    Box continue_row;  // empty with nothing to continue
    // The Household view.
    std::array<Box, 16> house{};
    int houses = 0;
    bool house_slim = false; // rows too short for a second line: the name alone
    bool tiles_short = false; // the narrow window's profile and tiles are a row shorter
};

namespace progress_detail {

struct Item {
    float min_h = 0.0f;
    float pref_h = 0.0f;
    bool grow = false; // takes the room left over after every card has its preferred height
    bool keep = true;
    float h = 0.0f;
    float alt_min = 0.0f; // when even the kept cards at their least do not fit: this least (0: none)
};

// Gives each kept card its least height, drops what does not fit in the order `drop` names (indices
// of `items`; a card not named stays), then hands out the room that is left: first up to each
// card's preferred height in the order `fill` names, then the rest to the cards that grow.
template <std::size_t N, std::size_t D>
inline void stack(std::array<Item, N>& items, float room, float gap,
                  const std::array<std::size_t, D>& drop, const std::array<std::size_t, N>& fill) {
    const auto used = [&] {
        float sum = 0.0f;
        int n = 0;
        for (const Item& it : items) {
            if (it.keep) {
                sum += it.min_h;
                ++n;
            }
        }
        return sum + gap * static_cast<float>(std::max(0, n - 1));
    };
    for (const std::size_t i : drop) {
        if (used() > room) {
            items[i].keep = false;
        }
    }
    if (used() > room) {
        for (Item& it : items) {
            if (it.keep && it.alt_min > 0.0f) {
                it.min_h = it.alt_min;
            }
        }
    }
    float left = room - used();
    for (Item& it : items) {
        it.h = it.keep ? it.min_h : 0.0f;
    }
    for (const std::size_t i : fill) {
        Item& it = items[i];
        if (it.keep && left > 0.0f) {
            const float more = std::min(left, it.pref_h - it.min_h);
            if (more > 0.0f) {
                it.h += more;
                left -= more;
            }
        }
    }
    for (Item& it : items) {
        if (it.keep && it.grow && left > 0.0f) {
            it.h += left;
            left = 0.0f;
        }
    }
}

// Stacks the kept cards from `x0`, `y0`, `w` wide, writing each one's box.
template <std::size_t N>
inline void place_column(const std::array<Item, N>& items, float x0, float y0, float w, float gap,
                         const std::array<Box*, N>& out) {
    float y = y0;
    for (std::size_t i = 0; i < N; ++i) {
        if (items[i].keep) {
            *out[i] = {x0, y, x0 + w, y + items[i].h};
            y += items[i].h + gap;
        }
    }
}

// The Household view: one ranked row for each profile across the width, as tall as the page lets
// them be (up to `kHouseRowMaxH`), so a household of four fills the page. A household too big for
// one column goes in as many columns as the least row height needs.
inline void place_household(ProgressLayout& l, const Box& body, float u, int profiles) {
    namespace pg = progress_tok;
    const float gap = pg::kGap * u;
    const int n = std::clamp(profiles, 1, static_cast<int>(l.house.size()));
    const float least = pg::kHouseSlimH * u;
    int cols = 1;
    if (static_cast<float>(n) * (least + gap) - gap > body.h()) {
        const int fit_rows = std::max(1, static_cast<int>((body.h() + gap) / (least + gap)));
        cols = (n + fit_rows - 1) / fit_rows;
    }
    const int rows = (n + cols - 1) / cols;
    const float room = (body.h() + gap) / static_cast<float>(rows) - gap;
    const float h = std::clamp(room, least, pg::kHouseRowMaxH * u);
    const float w = (body.w() - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    l.house_slim = h < pg::kHouseRowTallH * u;
    for (int i = 0; i < n; ++i) {
        const float x = body.x0 + static_cast<float>(i / rows) * (w + gap);
        const float y = body.y0 + static_cast<float>(i % rows) * (h + gap);
        l.house[static_cast<std::size_t>(i)] = {x, y, x + w, y + h};
    }
    l.houses = n;
}

// The left column: calendar (wide only), graph, best runs. The calendar is given up first, then the
// best runs, then the graph; the best-run card gives rows before it goes.
inline void place_left(ProgressLayout& l, const Box& left, float u, int best_rows) {
    namespace pg = progress_tok;
    const float gap = pg::kGap * u;
    const float title = pg::kTitleH * u;
    const float best_pad = 2.0f * pg::kPad * u;
    const float row_h = pg::kBestRowH * u;
    const int rows_wanted = std::clamp(best_rows, 0, 8);
    const float best_min = title + best_pad + static_cast<float>(std::min(rows_wanted, 2)) * row_h;
    const float best_pref = title + best_pad + static_cast<float>(rows_wanted) * row_h;
    std::array<Item, 3> col{};
    col[0] = {pg::kCalendarMinH * u, pg::kCalendarMaxH * u, false, l.wide, 0.0f};
    col[1] = {pg::kGraphMinH * u, pg::kGraphH * u, true, true, 0.0f};
    col[2] = {best_min, std::max(best_min, best_pref), false, true, 0.0f};
    stack(col, left.h(), gap, std::array<std::size_t, 3>{0, 2, 1}, std::array<std::size_t, 3>{0, 2, 1});
    place_column(col, left.x0, left.y0, left.w(), gap,
                 std::array<Box*, 3>{&l.calendar, &l.graph, &l.best});
    if (l.best.h() <= 0.0f) {
        return;
    }
    const float rows_room = l.best.h() - title - best_pad;
    l.best_rows = std::clamp(static_cast<int>(rows_room / row_h + 1e-3f), 0, rows_wanted);
    for (int i = 0; i < l.best_rows; ++i) {
        const float y = l.best.y0 + pg::kPad * u + title + static_cast<float>(i) * row_h;
        l.best_row[static_cast<std::size_t>(i)] = {l.best.x0 + pg::kPad * u, y,
                                                   l.best.x1 - pg::kPad * u, y + row_h};
    }
}

// The cards of "Practise today" inside its box: the spots that fit (one at least when the card
// holds any room for them), then Continue at the bottom.
inline void place_practise(ProgressLayout& l, float u, int spots_wanted, bool can_continue) {
    namespace pg = progress_tok;
    const float gap = pg::kGap * u;
    const float pad = pg::kPad * u;
    const float head = pg::kTitleH * u + pad;
    const float continue_h = can_continue ? pg::kContinueH * u + gap : 0.0f;
    const float pitch = pg::kSpotH * u + gap;
    const float room = l.practise.h() - 2.0f * pad - head - continue_h;
    l.spots = std::clamp(static_cast<int>((room + gap) / pitch + 1e-3f), 0, spots_wanted);
    float y = l.practise.y0 + pad + head;
    for (int i = 0; i < l.spots; ++i) {
        l.spot[static_cast<std::size_t>(i)] = {l.practise.x0 + pad, y, l.practise.x1 - pad,
                                               y + pg::kSpotH * u};
        y += pitch;
    }
    if (can_continue) {
        const float hit = tok::kHit * u;
        l.continue_row = {l.practise.x0 + pad, l.practise.y1 - pad - hit, l.practise.x1 - pad,
                          l.practise.y1 - pad};
    }
}

// The right column. Wide: profile, ring, tiles, medals, practise (the calendar is in the left
// column). Narrow: profile, the two tiles (the week's time and the songs), practise, medals; no
// ring, no calendar. Medals go first, then the tiles; "Practise today" is last to give up room.
inline void place_right(ProgressLayout& l, const Box& right, float u, const ProgressContent& c) {
    namespace pg = progress_tok;
    const float gap = pg::kGap * u;
    const float pad = pg::kPad * u;
    const float hit = tok::kHit * u;
    l.tiles_short = !l.wide;
    const float profile_h = (l.tiles_short ? pg::kProfileShortH : pg::kProfileH) * u;
    const float tiles_h = (l.tiles_short ? pg::kTilesShortH : pg::kTilesH) * u;
    const int spots_wanted = std::clamp(c.spots, 0, 3);
    const float head = pg::kTitleH * u + pad;
    const float continue_h = c.can_continue ? pg::kContinueH * u + gap : 0.0f;
    const float pitch = pg::kSpotH * u + gap;
    const float least = 2.0f * pad + head + continue_h; // no spot, only Continue
    const float one = least + static_cast<float>(std::min(spots_wanted, 1)) * pitch;
    const float pref = least + static_cast<float>(spots_wanted) * pitch;
    const Item practise{one, std::max(one, pref), true, true, 0.0f, least};
    const Item medals{pg::kMedalsH * u * 0.88f, pg::kMedalsH * u, false, true, 0.0f};
    const Item profile{profile_h, profile_h, false, true, 0.0f};
    if (l.wide) {
        std::array<Item, 5> col{profile,
                                {pg::kRingCardH * u * 0.9f, pg::kRingCardH * u, false, true, 0.0f},
                                {tiles_h * 0.9f, tiles_h, false, true, 0.0f},
                                medals,
                                practise};
        stack(col, right.h(), gap, std::array<std::size_t, 4>{3, 1, 2, 0},
              std::array<std::size_t, 5>{0, 1, 2, 3, 4});
        place_column(col, right.x0, right.y0, right.w(), gap,
                     std::array<Box*, 5>{&l.profile, &l.ring, &l.tiles, &l.medals, &l.practise});
    } else {
        std::array<Item, 4> col{profile, {tiles_h, tiles_h, false, true, 0.0f}, practise, medals};
        stack(col, right.h(), gap, std::array<std::size_t, 2>{3, 1},
              std::array<std::size_t, 4>{0, 1, 2, 3});
        place_column(col, right.x0, right.y0, right.w(), gap,
                     std::array<Box*, 4>{&l.profile, &l.tiles, &l.practise, &l.medals});
    }
    if (l.profile.h() > 0.0f) {
        const float ex = std::round(l.profile.x1 - hit);
        const float ey = std::round(l.profile.cy() - hit * 0.5f);
        l.edit = {ex, ey, ex + std::round(hit), ey + std::round(hit)};
    }
    if (l.tiles.h() > 0.0f) {
        const float half = (l.tiles.w() - gap) * 0.5f;
        l.tile_songs = {l.tiles.x0, l.tiles.y0, l.tiles.x0 + half, l.tiles.y1};
        l.tile_rank = {l.tiles.x1 - half, l.tiles.y0, l.tiles.x1, l.tiles.y1};
    }
    if (l.practise.h() > 0.0f) {
        place_practise(l, u, spots_wanted, c.can_continue);
    }
}

} // namespace progress_detail

// The page in `area` (the library screen's, under the top row). `u` is the UI unit.
inline ProgressLayout plan_progress(const Box& area, float u, const ProgressContent& c) {
    namespace pg = progress_tok;
    ProgressLayout l;
    l.area = area;
    const float hit = tok::kHit * u;
    const float pad = pg::kPad * u;
    const float gap = pg::kGap * u;
    l.wide = area.w() >= pg::kWideAt * u;
    // The header: Back, the title, and the two views at the right. The icon button sits on whole
    // pixels, so its icon comes out centred.
    const float top = area.y0 + pad;
    const float snap_hit = std::round(hit);
    l.back = {std::round(area.x0 + pad), std::round(top), std::round(area.x0 + pad) + snap_hit,
              std::round(top) + snap_hit};
    const float mode_w = pg::kModeW * u;
    l.mode_medals = {area.x1 - pad - mode_w, top, area.x1 - pad, top + hit};
    l.mode_household = {l.mode_medals.x0 - mode_w, top, l.mode_medals.x0, top + hit};
    l.mode_me = {l.mode_household.x0 - mode_w, top, l.mode_household.x0, top + hit};
    const float title_end = c.view == ProgressTab::Medals ? l.mode_me.x0 - gap - snap_hit - gap : l.mode_me.x0 - gap;
    if (c.view == ProgressTab::Medals) {
        l.toggle = {std::round(l.mode_me.x0 - gap - snap_hit), std::round(top), std::round(l.mode_me.x0 - gap),
                    std::round(top) + snap_hit};
    }
    l.title = {l.back.x1 + gap, top, title_end, top + hit};
    const Box body{area.x0 + pad, top + hit + gap, area.x1 - pad, area.y1 - pad};
    if (body.h() <= 0.0f || body.w() <= 0.0f) {
        return l;
    }
    if (c.view == ProgressTab::Household) {
        progress_detail::place_household(l, body, u, c.profiles);
        return l;
    }
    if (c.view == ProgressTab::Medals) {
        l.wall = body;
        if (!wall_rows_fit(body, u)) { // only the category view fits: no toggle
            l.toggle = {};
            l.title.x1 = l.mode_me.x0 - gap;
        }
        return l;
    }
    const float right_w =
        std::min((l.wide ? pg::kRightW : pg::kRightNarrowW) * u, body.w() * pg::kRightMaxShare);
    const float split = body.x1 - right_w;
    progress_detail::place_left(l, {body.x0, body.y0, split - gap, body.y1}, u, c.best_rows);
    progress_detail::place_right(l, {split, body.y0, body.x1, body.y1}, u, c);
    return l;
}

} // namespace etude::app::ui
