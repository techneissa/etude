// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Ear card's parts go (#374), in ImGui points. No ImGui here.
//
// The header is the Learn cards' (back arrow, name, the four tabs). Under it two columns. The left
// has the streak (a flame and its number, the ten dots under it), the interval's name in a pill (a
// question mark until answered), the replay disc, and the settings: in the full form the range and
// the any-octave switch on one row and the largest interval on a row of its own, in the short form
// (a window too low for them) one Settings button that opens them in a card, with the pill beside
// the streak. The right has
// the four modes (Note, Interval up, Interval down, Together) as chips in a row, or a strip that
// scrolls sideways when their words do not fit, the keyboard, and the foot row with Hint and Next.
// The keyboard is as wide as the column, its keys never taller than the natural proportion and each
// at least a hit wide; how many white keys it shows (8 to 15) follows from that, and the notes asked
// are on them. The right column's stack is centred in the block the left column makes.
#pragma once

#include "ear_session.hpp"
#include "learn_card_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude::app::ui {

inline constexpr int kEarModes = 4;

// What the app measures in the font, in design points: the tabs' widths and each mode chip's (icon,
// words, pads). The defaults are only for tests.
struct EarNeeds {
    LearnTabs tabs{{82.0f, 85.0f, 66.0f, 96.0f}, 2};
    std::array<float, kEarModes> mode{learn_tok::kEarModeNeed - 28.0f, learn_tok::kEarModeNeed + 14.0f,
                                      learn_tok::kEarModeNeed + 36.0f, learn_tok::kEarModeNeed + 8.0f};
};

struct EarLayout {
    Box header;
    Box back;
    Box title;
    std::array<Box, kLearnTabs> tabs;
    std::array<bool, kLearnTabs> tab_label{};
    bool short_form = false; // one Settings button and its card, the pill beside the streak
    float ub = 1.0f;         // the body's unit
    // The left column.
    Box column;
    Box streak;   // the flame and the number
    Box dots;     // the ten dots, under it
    Box pill;     // the interval's name
    Box ring;     // the replay disc's ring: the square it and its marks are drawn in
    Box disc;     // the disc, a circle in it: the replay button
    Box range;    // the range's stepper
    Box any;      // the any-octave switch
    Box size;     // the largest interval's stepper (its own row)
    Box settings; // short form: the button that opens the three above, in `popover`
    Box popover;
    float dot = 0.0f;
    // The right column.
    std::array<Box, kEarModes> mode; // the chips in a row (empty when the strip holds them)
    bool strip = false;              // they scroll sideways in `mode_strip`
    Box mode_strip;
    Box keyboard;
    int whites = 0;                  // white keys shown
    float white_w = 0.0f;
    Box hint;
    Box next;
    bool foot_words = true;
};

// The most white keys a keyboard of width `w` shows with each at least `hit` wide, 8 to 15.
inline int ear_whites_for(float w, float hit, int least, int most) {
    return std::clamp(static_cast<int>(std::floor(w / std::max(hit, 1.0f))), least, most);
}

namespace ear_detail {

// What the two columns are laid out from: the body's room in the card's unit.
struct Room {
    LearnCardRoom r;
    float room = 0.0f;
    float big = 0.0f;      // the modes' and the foot row's height
    float side = 0.0f;     // the left column's width
    float between = 0.0f;  // the gap between the columns
    float ring = 0.0f;     // the disc's ring
    float left_h = 0.0f;   // the left column's height
    float right_h = 0.0f;  // the right column's stack
    float key_h = 0.0f;
    float block_top = 0.0f;
    float block_h = 0.0f;
};

inline Room plan(EarLayout& e, const Box& area, float u) {
    using namespace learn_tok;
    Room p;
    LearnCardRoom& r = p.r;
    const float ub = learn_card_unit(area, u);
    e.ub = ub;
    r.u = ub;
    r.hit = tok::kHit * ub;
    r.gap = tok::kS2 * ub;
    r.tight = tok::kHitGap * ub; // a dead gap between hit areas
    r.pad = kPad * ub;
    r.top = area.y0 + tok::kHitGap * u + tok::kHit * u + r.gap; // under the header
    r.bottom = area.y1 - r.pad;
    r.left = area.x0 + r.pad;
    r.right = area.x1 - r.pad;
    p.room = r.bottom - r.top;
    // The foot row and the modes are `kBigScale` times a hit tall where the keyboard still has its
    // least height, else a hit.
    p.big = p.room - 2.0f * kBigScale * r.hit - 2.0f * r.gap >= kKeyboardMinH * ub ? kBigScale * r.hit
                                                                                   : r.hit;
    // The left column is at most `kEarSideMax` and a share of the width, but gives way to keys a hit
    // wide (at the least, the octave) as far as its Settings button and the disc allow.
    const float body_w = r.right - r.left;
    const float side_floor = 3.0f * r.hit + 2.0f * r.tight;
    const float octave_w = static_cast<float>(kEarMinWhites) * r.hit;
    p.between = std::clamp(body_w - side_floor - octave_w, r.gap, r.pad);
    p.side = std::min(std::min(kEarSideMax * ub, kEarSideShare * body_w),
                      std::max(side_floor, body_w - p.between - octave_w));
    e.dot = kEarDot * ub;
    const float dots_h = e.dot + 2.0f * r.tight;
    const float streak_h = kEarStreakShare * r.hit;
    const float pill_h = kEarPillShare * r.hit;
    // Full form: streak over its dots, the pill, the disc, the two rows of settings. Short form: the
    // pill beside the streak, one button of settings.
    const float full_fixed = streak_h + dots_h + pill_h + 2.0f * r.tight + r.gap + 2.0f * r.hit +
                             r.tight + r.gap;
    const float short_fixed = streak_h + dots_h + r.gap + r.hit + r.gap;
    e.short_form = std::min(p.room - full_fixed, p.side) < kEarDiscMin * ub;
    const float fixed = e.short_form ? short_fixed : full_fixed;
    p.ring = std::max(std::min(p.room - fixed, p.side), r.hit * 1.2f);
    p.left_h = fixed + p.ring;
    // The right column's stack: the modes, the keyboard, the foot row.
    const float width = std::max(0.0f, r.right - (r.left + p.side + p.between));
    e.whites = ear_whites_for(width, r.hit, kEarMinWhites, kEarMaxWhites);
    e.white_w = width / static_cast<float>(e.whites);
    p.key_h = std::min(e.white_w * kKeyAspect, std::max(0.0f, p.room - 2.0f * p.big - 2.0f * r.gap));
    p.right_h = p.big + r.gap + p.key_h + r.gap + p.big;
    p.block_h = std::min(std::max(p.left_h, p.right_h), p.room);
    p.block_top = r.top + (p.room - p.block_h) * 0.5f;
    return p;
}

// The left column from the top of the block.
inline void place_left(EarLayout& e, const Room& p) {
    using namespace learn_tok;
    const LearnCardRoom& r = p.r;
    const float dots_h = e.dot + 2.0f * r.tight;
    const float dots_w = static_cast<float>(kEarDots) * e.dot + (kEarDots - 1) * kEarDotGap * e.ub;
    const float streak_h = kEarStreakShare * r.hit;
    const float pill_h = kEarPillShare * r.hit;
    const float bottom = p.block_top + p.block_h;
    const float cx = r.left + p.side * 0.5f;
    e.column = {r.left, p.block_top, r.left + p.side, bottom};
    float y = p.block_top;
    if (e.short_form) {
        const float half = p.side * 0.5f;
        const float pill_y = y + (streak_h - pill_h) * 0.5f;
        e.streak = {r.left, y, r.left + half, y + streak_h};
        e.pill = {r.left + half, pill_y, r.left + p.side, pill_y + pill_h};
        y += streak_h;
        e.dots = {r.left, y, r.left + half, y + dots_h};
        y += dots_h + r.gap;
    } else {
        e.streak = {r.left, y, r.left + p.side, y + streak_h};
        y += streak_h;
        e.dots = {cx - dots_w * 0.5f, y, cx + dots_w * 0.5f, y + dots_h};
        y += dots_h + r.tight;
        e.pill = {r.left, y, r.left + p.side, y + pill_h};
        y += pill_h + r.gap;
    }
    // The ring takes the room the settings leave, centred across the column.
    e.ring = {cx - p.ring * 0.5f, y, cx + p.ring * 0.5f, y + p.ring};
    const float disc = p.ring * kEarDiscShare;
    e.disc = {e.ring.cx() - disc * 0.5f, e.ring.cy() - disc * 0.5f, e.ring.cx() + disc * 0.5f,
              e.ring.cy() + disc * 0.5f};
    // The settings end at the block's bottom: two rows, or the button and, over it, the card.
    float row1 = bottom - 2.0f * r.hit - r.tight; // the top of the range row
    float x0 = r.left;
    float x1 = r.left + p.side;
    if (e.short_form) {
        e.settings = {r.left, bottom - r.hit, r.left + p.side, bottom};
        const float card_w = std::min(kPopoverW * e.ub, r.right - r.left);
        const float card_h = 2.0f * r.hit + r.tight + 2.0f * r.gap;
        e.popover = {r.left, e.settings.y0 - r.tight - card_h, r.left + card_w,
                     e.settings.y0 - r.tight};
        row1 = e.popover.y0 + r.gap;
        x0 = e.popover.x0 + r.gap;
        x1 = e.popover.x1 - r.gap;
    }
    e.any = {x1 - r.hit, row1, x1, row1 + r.hit};
    e.range = {x0, row1, e.any.x0 - r.tight, row1 + r.hit};
    e.size = {x0, row1 + r.hit + r.tight, x1, row1 + 2.0f * r.hit + r.tight};
}

// The right column: the modes, the keyboard and the foot row, a stack centred in the block.
inline void place_right(EarLayout& e, const Room& p, const EarNeeds& needs, const Box& area, float u) {
    using namespace learn_tok;
    const LearnCardRoom& r = p.r;
    const float x0 = r.left + p.side + p.between;
    const float width = std::max(0.0f, r.right - x0);
    const float top = p.block_top + (p.block_h - p.right_h) * 0.5f;
    const float bottom = top + p.right_h;
    float need_sum = static_cast<float>(kEarModes - 1) * r.tight;
    for (const float n : needs.mode) {
        need_sum += n * e.ub;
    }
    if (need_sum <= width) {
        const float extra = (width - need_sum) / static_cast<float>(kEarModes);
        float x = x0;
        for (std::size_t i = 0; i < e.mode.size(); ++i) {
            const float w = needs.mode[i] * e.ub + extra;
            e.mode[i] = {x, top, x + w, top + p.big};
            x += w + r.tight;
        }
    } else {
        e.strip = true;
        e.mode_strip = {x0, top, x0 + width, top + p.big};
    }
    const float keys_y = top + p.big + r.gap;
    e.keyboard = {x0, keys_y, x0 + width, keys_y + p.key_h};
    e.foot_words = area.w() / u >= kFootWordsMinW;
    const float hint_w = e.foot_words ? kEarHintW * e.ub : kFootIconShare * p.big;
    const float next_w = e.foot_words ? kEarNextW * e.ub : kFootIconShare * p.big;
    const float span = hint_w + r.gap + next_w;
    const float fx = x0 + (width - span) * 0.5f;
    e.hint = {fx, bottom - p.big, fx + hint_w, bottom};
    e.next = {fx + hint_w + r.gap, bottom - p.big, fx + span, bottom};
}

} // namespace ear_detail

inline EarLayout ear_layout(const Box& area, float u, const EarNeeds& needs = {}) {
    EarLayout e;
    const ear_detail::Room p = ear_detail::plan(e, area, u);
    // The header, in the window's unit.
    LearnCardRoom h = p.r;
    h.u = u;
    h.hit = tok::kHit * u;
    h.gap = tok::kS2 * u;
    h.tight = tok::kHitGap * u;
    LearnCardLayout head;
    DrillBoxes unused;
    learn_place_header(head, area, h, needs.tabs, LearnColumn::Full, unused);
    e.header = head.header;
    e.back = head.back;
    e.title = head.title;
    e.tabs = head.tabs;
    e.tab_label = head.tab_label;
    ear_detail::place_left(e, p);
    ear_detail::place_right(e, p, needs, area, u);
    return e;
}

// ---- The keys -----------------------------------------------------------------------------------

// A note of a black key (C#, D#, F#, G#, A#).
inline bool ear_is_black(int note) {
    const int pc = ((note % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// One key as drawn (`box`) and as pressed (`hit`). The keyboard is one pressable surface and a press is
// given to the key under it by `ear_key_at`: a black key first, within a hit's width of its middle (a
// black key drawn narrower than a hit still takes one), then the white key under it. So no two hit
// areas overlap; a white key's own part is the full width below the black keys, at least a hit tall.
struct EarKeyBox {
    int note = 60;
    bool black = false;
    Box box;
    Box hit;
};
inline constexpr std::size_t kEarMaxKeys = 32;
struct EarKeys {
    std::array<EarKeyBox, kEarMaxKeys> key;
    std::size_t count = 0;
};

inline EarKeys ear_keys_layout(const Box& keyboard, float white_w, int low_note, int top_note, float hit) {
    using namespace learn_tok;
    EarKeys out;
    const float black_w = white_w * kBlackShare;
    const float black_h = keyboard.h() * kBlackHeightShare;
    int white = 0;
    for (int n = low_note; n <= top_note && out.count < out.key.size(); ++n) {
        EarKeyBox& k = out.key[out.count++];
        k.note = n;
        k.black = ear_is_black(n);
        const float x = keyboard.x0 + static_cast<float>(white) * white_w;
        if (k.black) {
            k.box = {x - black_w * 0.5f, keyboard.y0, x + black_w * 0.5f, keyboard.y0 + black_h};
            const float wide = std::max(black_w, hit) * 0.5f;
            k.hit = {x - wide, k.box.y0, x + wide, k.box.y1};
        } else {
            k.box = {x, keyboard.y0, x + white_w, keyboard.y1};
            k.hit = k.box;
            ++white;
        }
    }
    return out;
}

// The note of the key under (x, y), or -1: a black key's hit first, then a white key's box.
inline int ear_key_at(const EarKeys& keys, float x, float y) {
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < keys.count; ++i) {
            const EarKeyBox& k = keys.key[i];
            if (k.black != (pass == 0)) {
                continue;
            }
            if (x >= k.hit.x0 && x < k.hit.x1 && y >= k.hit.y0 && y < k.hit.y1) {
                return k.note;
            }
        }
    }
    return -1;
}

} // namespace etude::app::ui
