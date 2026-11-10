// SPDX-License-Identifier: GPL-3.0-or-later
// Where the library screen's parts go, in ImGui points (its sizes are `lib_tok` in tokens.hpp).
// No ImGui here.
//
// The screen covers everything under the top row. Top to bottom: the search row (the search field
// and the view button), the chips row (Sort, Group, the quick filters), the results, and the
// on-screen keyboard while it is up. On a short screen the chips row steps aside while the keyboard
// is up, so the results keep at least one row of cards. Where even that leaves the results less than
// a card's share (a 7 inch panel, whose keys of a hit area and a dead gap take most of the height),
// the search row holds both: the field, and beside it the results as one compact row, a hit
// tall (`LibraryFrame::strip`; the Get, Select and View buttons wait for the keyboard to close).
#pragma once

#include "face_layout.hpp"
#include "osk_layout.hpp"
#include "tokens.hpp"

#include "etude/library_browse.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace etude::app::ui {

struct LibraryFrame {
    Box search;                 // the search field
    Box get;                    // the free catalogues' button (their credits and the online switch)
    Box select;                 // the Select button (picks songs for the actions on many)
    Box view;                   // the view button: Grid, List or Shelves
    Box chips;                  // the chips row; empty while it steps aside
    Box results;                // the cards
    Box keyboard;               // the on-screen keyboard, empty while it is down
    float keyboard_full = 0.0f; // its height once it is all the way up
    // The results are one compact row beside the field, in the search row: get, select and view are
    // empty then, but for a pick of "Same piece as another song" (`keep_chips`), whose way out takes the view's
    // place.
    bool strip = false;
    Box hint; // in the strip with a pick of "Same piece as another song": its instruction, left of the way out
};

// The search field's share of the row in `LibraryFrame::strip`, never under four hit areas, and the
// width of the pick's instruction there (design points: "Tap the song" over "it joins").
inline constexpr float kStripFieldShare = 0.36f;
inline constexpr float kStripHintW = 76.0f;

// The screen's parts in `area` (everything under the top row), with the keyboard `keyboard` of
// the way up (0..1, eased). `keep_chips` holds the chips row on a short screen with the keyboard up
// (the row then carries the bar of "Same piece as another song", which has the way out).
inline LibraryFrame library_frame(const Box& area, float u, float keyboard,
                                  bool keep_chips = false) {
    LibraryFrame f;
    const float pad = lib_tok::kSidePad * u;
    const float hit = tok::kHit * u;
    const float gap = tok::kS2 * u;
    float y = area.y0 + gap;
    f.search = {area.x0 + pad, y, area.x1 - pad - 3.0f * hit - 3.0f * gap, y + hit};
    f.get = {area.x1 - pad - 3.0f * hit - 2.0f * gap, y, area.x1 - pad - 2.0f * hit - 2.0f * gap,
             y + hit};
    f.select = {area.x1 - pad - 2.0f * hit - gap, y, area.x1 - pad - hit - gap, y + hit};
    f.view = {area.x1 - pad - hit, y, area.x1 - pad, y + hit};
    const bool short_screen = area.h() < lib_tok::kShortScreen * u;
    // The keyboard leaves the results at least part of a row of cards.
    const float least_results = lib_tok::kCardH * lib_tok::kLeastResultsShare * u;
    const bool chips_aside = short_screen && !keep_chips;
    // Keys of a hit area and a gap (the keyboard's least) with the rows above them: where the
    // results would then be under their least, the row of the field holds them instead.
    const float keys_least = keyboard_height(area.w(), u, 0.0f);
    const float stacked = 2.0f * gap + hit + (chips_aside ? 0.0f : hit + gap) + keys_least;
    // The row sits a gap under the top row's buttons, which stand inside it: a few points under
    // the area's top. Where not even that and the keys fit, the keys keep their size and the results
    // give way, as before.
    const float strip_top = tok::kS1 * u;
    const bool strip_fits = area.h() >= strip_top + hit + keys_least;
    if (short_screen && keyboard > 0.0f && area.h() - stacked < least_results && strip_fits) {
        y = area.y0 + strip_top;
        f.strip = true;
        const float inner = area.w() - 2.0f * pad;
        const float field_w = std::max(4.0f * hit, inner * kStripFieldShare);
        const float right = area.x1 - pad;
        f.search = {area.x0 + pad, y, area.x0 + pad + field_w, y + hit};
        f.get = f.select = {right, y, right, y};
        f.view = keep_chips ? Box{right - hit, y, right, y + hit} : Box{right, y, right, y};
        float strip_x1 = right;
        if (keep_chips) {
            // The way out, and the instruction left of it (two short lines).
            f.hint = {f.view.x0 - gap - kStripHintW * u, y, f.view.x0 - gap, y + hit};
            strip_x1 = f.hint.x0 - gap;
        }
        f.results = {f.search.x1 + gap, y, strip_x1, y + hit};
        f.chips = {area.x0 + pad, y + hit, area.x1 - pad, y + hit};
        f.keyboard_full = keyboard_height(area.w(), u, area.y1 - (y + hit));
        const float kb_h = f.keyboard_full * std::clamp(keyboard, 0.0f, 1.0f);
        f.keyboard = {area.x0, area.y1 - kb_h, area.x1, area.y1};
        return f;
    }
    y += hit + gap;
    const float kb_room = area.y1 - y - (chips_aside ? 0.0f : hit + gap) - least_results;
    f.keyboard_full = keyboard_height(area.w(), u, kb_room);
    const float kb_h = f.keyboard_full * std::clamp(keyboard, 0.0f, 1.0f);
    if (!(chips_aside && keyboard > 0.0f)) {
        f.chips = {area.x0 + pad, y, area.x1 - pad, y + hit};
        y += hit + gap;
    } else {
        f.chips = {area.x0 + pad, y, area.x1 - pad, y};
    }
    f.keyboard = {area.x0, area.y1 - kb_h, area.x1, area.y1};
    f.results = {area.x0, y, area.x1, std::max(y, area.y1 - kb_h)};
    return f;
}

// A card's height and the least width of the grid's cards: a card that has versions of its piece
// carries a row of chips (lib_tok::kVersionsH more), and while some piece has versions the cards
// are wider so a few chips fit across.
// A card's controls keep `tok::kHitGap` between them: its chips' row sits a gap above Play.
inline float card_height(float u, bool versions) {
    return (lib_tok::kCardH + (versions ? lib_tok::kVersionsH : 0.0f)) * u;
}
inline float card_min_width(float u, bool versions) {
    return (versions ? lib_tok::kVersionsCardMinW : lib_tok::kCardMinW) * u;
}

// A grid card `card`'s controls and its own tap area: the heart in the top corner and Play in the
// bottom one (a hit area each, `kS1` in from the edge), the chips' row over Play where the piece has
// versions (`chips_top` is its top), and the card's tap area, which opens the details: the card less
// the heart's column and a dead gap, and less the chips' row and a gap, so it never reaches over a
// button. While songs are being picked (`selecting`) the whole card picks and has no heart or Play.
struct CardParts {
    Box heart;
    Box play;
    Box tap;
    float chips_top = 0.0f;
};
inline CardParts card_parts(const Box& card, float u, bool selecting, bool versioned) {
    CardParts p;
    const float hit = tok::kHit * u;
    const float gap = tok::kHitGap * u;
    const float edge = tok::kS1 * u;
    p.heart = {card.x1 - edge - hit, card.y0 + edge, card.x1 - edge, card.y0 + edge + hit};
    p.play = {card.x1 - edge - hit, card.y1 - edge - hit, card.x1 - edge, card.y1 - edge};
    p.chips_top = card.y1 - edge - 2.0f * hit - gap;
    p.tap = {card.x0, card.y0, selecting ? card.x1 : p.heart.x0 - gap,
             versioned ? p.chips_top - gap : card.y1};
    return p;
}

// Which version chips a card or the details shows. `with_score` and `names` are the chips' widths
// (a chip that carries its best score, and the name alone), one for each version, easiest first;
// `chosen` is the version the card shows. If all fit across `avail`, they show, with their scores
// when those fit too. Where the chips wrap (the details), the names go on as many rows as they
// need. Else a window of them around the chosen one shows, `more_w` kept at the end for a "+k"
// chip that opens the details.
struct VersionPlan {
    bool scores = false;   // each chip carries its best score
    std::size_t first = 0; // the first chip shown
    std::size_t count = 0; // how many
    bool more = false;     // a "+k" chip at the end
    int rows = 1;
};
inline float chips_width(std::span<const float> widths, std::size_t first, std::size_t count,
                         float gap) {
    float w = 0.0f;
    for (std::size_t i = first; i < first + count; ++i) {
        w += widths[i];
    }
    return count > 0 ? w + gap * static_cast<float>(count - 1) : 0.0f;
}
inline VersionPlan plan_version_chips(std::span<const float> with_score,
                                      std::span<const float> names, std::size_t chosen,
                                      float avail, float gap, float more_w, bool wrap) {
    VersionPlan p;
    const std::size_t n = names.size();
    if (n == 0) {
        p.count = 0;
        return p;
    }
    p.count = n;
    if (with_score.size() == n && chips_width(with_score, 0, n, gap) <= avail) {
        p.scores = true;
        return p;
    }
    if (chips_width(names, 0, n, gap) <= avail) {
        return p;
    }
    if (wrap) {
        float x = 0.0f;
        p.rows = 1;
        for (std::size_t i = 0; i < n; ++i) {
            if (x > 0.0f && x + gap + names[i] > avail) {
                ++p.rows;
                x = 0.0f;
            }
            x += (x > 0.0f ? gap : 0.0f) + names[i];
        }
        return p;
    }
    chosen = std::min(chosen, n - 1);
    for (std::size_t c = n - 1; c >= 1; --c) {
        const std::size_t first = std::clamp<std::size_t>(chosen >= (c - 1) / 2 ? chosen - (c - 1) / 2 : 0, 0, n - c);
        if (chips_width(names, first, c, gap) + gap + more_w <= avail) {
            p.first = first;
            p.count = c;
            p.more = true;
            return p;
        }
    }
    p.first = chosen;
    p.count = 1;
    p.more = true;
    return p;
}

// One row of the List view, `row` its whole box: Play at the right end, the heart left of it,
// then the hands and the length over the level's stars; the title over the composer takes the
// rest. A row too narrow for all of it drops the hands, then the facts, so the title keeps
// `lib_tok::kListLeastTextW`.
struct ListRow {
    Box tap;     // the row's own tap area: the row less the controls at its right end and a gap
    Box text;
    Box stars;   // the level's stars just before the heart, in the strip only; empty when dropped
    Box version; // the chip of the version shown, for a piece that has versions; else empty
    Box facts;   // empty when dropped
    Box hands;   // empty when dropped
    Box heart;
    Box play;
};
inline constexpr float kStripLeastTitleW = 72.0f; // the title's least width in a strip's row, design points
inline constexpr float kStarsW = 5.0f * 2.0f * lib_tok::kLevelStar + 4.0f * lib_tok::kLevelStarGap;
// `version_w`: the width of the version chip (0 for a song on its own); it comes before the hands
// and the facts. Where the title would not keep its least width beside the chip with its word, the
// chip shrinks to its icon (a round chip `tok::kHit` wide), so the next version stays reachable;
// it goes only where not even that fits.
//
// `with_stars` (the strip beside the search field): the level's five stars stand just before the heart
// and go before the title does: the title shortens, to `kStripLeastTitleW`, while the stars stay; where
// even that does not fit the stars are dropped. The stars are not pressable (the row's own tap area
// covers them), so only the heart and Play are hit areas, a gap apart as ever.
inline ListRow list_row(const Box& row, float u, float version_w = 0.0f, bool with_stars = false) {
    ListRow r;
    const float hit = tok::kHit * u;
    const float gap = tok::kHitGap * u;
    const float cy = row.cy();
    const float right = row.x1 - tok::kS1 * u;
    r.play = {right - hit, cy - hit * 0.5f, right, cy + hit * 0.5f};
    r.heart = {r.play.x0 - gap - hit, r.play.y0, r.play.x0 - gap, r.play.y1};
    const float hands_w = lib_tok::kHand * u * lib_tok::kHandPairShare;
    const float facts_w = lib_tok::kListFactsW * u;
    const float text_x = row.x0 + tok::kS3 * u;
    const float least = lib_tok::kListLeastTextW * u;
    float end = r.heart.x0 - gap;
    r.stars = {end, cy, end, cy};
    if (with_stars) {
        const float stars_w = kStarsW * u;
        if (end - stars_w - gap - text_x >= kStripLeastTitleW * u) {
            r.stars = {end - stars_w, cy - lib_tok::kLevelStar * u, end, cy + lib_tok::kLevelStar * u};
            end = r.stars.x0 - gap;
        }
    }
    r.version = {end, cy, end, cy};
    if (version_w > 0.0f) {
        const float w = end - version_w - gap - text_x >= least ? version_w : hit;
        if (end - w - gap - text_x >= least) {
            r.version = {end - w, cy - hit * 0.5f, end, cy + hit * 0.5f};
            end = r.version.x0 - gap;
        }
    }
    if (end - hands_w - gap - facts_w - gap - text_x >= least) {
        r.hands = {end - hands_w, row.y0, end, row.y1};
        end = r.hands.x0 - gap;
    } else {
        r.hands = {end, cy, end, cy};
    }
    if (end - facts_w - gap - text_x >= least) {
        r.facts = {end - facts_w, row.y0, end, row.y1};
        end = r.facts.x0 - gap;
    } else {
        r.facts = {end, cy, end, cy};
    }
    r.text = {text_x, row.y0, std::max(text_x, end), row.y1};
    // The row's own tap area ends a gap before the first of its controls (the chip, else the heart).
    const float controls_x = r.version.w() > 0.0f ? r.version.x0 : r.heart.x0;
    r.tap = {row.x0, row.y0, std::max(row.x0, controls_x - gap), row.y1};
    return r;
}

// The List's columns in a results area `width` wide: as many rows of at least `kListRowMinW` as
// fit, two at most, and one where two do not (a narrow window, or a large UI scale).
inline GridColumns list_columns(float width, float u) {
    const float pad = lib_tok::kSidePad * u;
    const float gap = lib_tok::kListGap * u;
    GridColumns g = grid_columns(width, lib_tok::kListRowMinW * u, gap, pad);
    if (g.columns > lib_tok::kListColumnsMax) {
        g.columns = lib_tok::kListColumnsMax;
        const float inner = std::max(0.0f, width - 2.0f * pad);
        g.card_w =
            (inner - gap * static_cast<float>(g.columns - 1)) / static_cast<float>(g.columns);
    }
    return g;
}

// A shelf's card in the Shelves view: a share of the grid's width, so the next card peeks in.
inline float shelf_card_width(float grid_card_w, float u) {
    return std::max(lib_tok::kCardMinW * u * lib_tok::kShelfCardShare,
                    grid_card_w * lib_tok::kShelfCardShare);
}

// Where the results were scrolled to, kept across the library closing and across a restart (pure,
// tested). The position is in design points. `want_dp` is the live position while the player
// scrolls; it reaches the settings (`observe`'s answer) only once the scrolling has stopped, since
// each write of the settings copies the model. After the screen opens the position is asked for
// again each frame (`restore_frame`) while the list is still filling and cannot reach it yet, for
// at most kRestoreS seconds; a position cut short by the list's end is not taken as the player's.
// Only the plain list (the All filter, the whole library, no search) is kept.
struct ScrollKeeper {
    static constexpr float kRestoreS = 2.0f; // the longest a filling list is waited for
    static constexpr float kSettleS = 0.3f;  // the scroll has stood this long: write it down
    float want_dp = 0.0f;
    float restore_left_s = 0.0f;
    float idle_s = 0.0f;
    bool moved = false; // the player scrolled since the last write

    bool restoring() const { return restore_left_s > 0.0f; }
    // Start putting `dp` back (the screen opened, or the last run's value was read).
    void begin_restore(float dp) {
        want_dp = dp;
        moved = false;
        restore_left_s = dp > 0.0f ? kRestoreS : 0.0f;
    }
    // After asking for `want_px`: `got_px` and `max_px` are what the scroll area says now.
    void restore_frame(float got_px, float max_px, float want_px, bool filling, bool finger,
                       bool plain, float dt) {
        restore_left_s -= dt;
        const bool reached = std::abs(got_px - want_px) < 1.0f;
        const bool cannot = max_px + 1.0f < want_px && !filling; // the list is whole and too short
        if (finger || !plain || reached || cannot || restore_left_s <= 0.0f) {
            restore_left_s = 0.0f;
        }
    }
    // The scroll `px` of a frame that is not restoring: the value to write to the settings once
    // the player has stopped, else none.
    std::optional<int> observe(float px, float unit, float dt, bool plain, int most) {
        if (!plain) {
            return std::nullopt;
        }
        const float dp = px / unit;
        if (std::abs(dp - want_dp) >= 0.5f) {
            want_dp = dp;
            moved = true;
            idle_s = 0.0f;
            return std::nullopt;
        }
        idle_s += dt;
        if (!moved || idle_s < kSettleS) {
            return std::nullopt;
        }
        moved = false;
        return std::clamp(static_cast<int>(std::lround(want_dp)), 0, most);
    }
};

} // namespace etude::app::ui
