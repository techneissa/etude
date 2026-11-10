// SPDX-License-Identifier: GPL-3.0-or-later
// Where everything of the crash notice goes, with no ImGui in it. Points, `u` is the kit's unit.
//
// - The notice is a card centred in the note area: a badge and the words (what happened and that
//   the report stays on the device), and under them "Dismiss" and "Send report" at the right. Both
//   are `tok::kHit` tall at least, with `tok::kHitGap` between them. The card, its badge, type and
//   buttons take the larger steps on a wide window (`tok::wide_step`, `crash_notice_sizes`); the words
//   wrap, they are never cut.
// - "Send report" opens the sheet: Back at the left, the title, the action at the right (the
//   browser or the QR code), and the report under them, which scrolls. The sheet leaves a margin to
//   the screen's edge and stays under the top row.
// - The report's lines are cut at spaces (`wrap_report`) to the body's width, and a word wider than
//   the body is cut where it must be.
#pragma once

#include "about_layout.hpp"
#include "add_songs_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace crash_tok {
inline constexpr float kCardW = 520.0f;     // the notice card, at wide_step 0 (the 800x480 panel's)
inline constexpr float kCardW1 = 900.0f;    // from 1366 points wide: the card's widest, in design points
inline constexpr float kCardW2 = 1100.0f;   // from 1920
inline constexpr float kCardShare1 = 0.55f; // and its share of the note area's width
inline constexpr float kCardShare2 = 0.60f;
inline constexpr float kSheetMaxW = 760.0f; // the sheet of the report and of the code
inline constexpr float kLineRatio = 1.3f;   // a report line's height in its type size
} // namespace crash_tok

// What the notice draws at a `tok::wide_step`: the card's width, the badge's tile and icon, the type
// sizes (steps of the kit's ladder: the title and its detail, the buttons' words), the spacing steps
// and how much taller the buttons are than a hit area. Step 0 is what 800x480 has always drawn; the
// larger steps take the top of the ladder, as the crash screen does, in a card of a share of the
// note area's width.
struct CrashNoticeSizes {
    float card_w;     // design points: step 0's width, the larger steps' widest
    float card_share; // of the note area's width (0: the card is `card_w` wide)
    float tile;     // the badge's drawn square
    float icon;
    float title_pt;
    float detail_pt;
    float button_pt;
    float pad;      // the card's inner margin
    float gap;      // between the badge and the words, and between the words and the buttons
    float grow;     // the buttons' hit and face height over `kHit` and `kFace`
};

inline CrashNoticeSizes crash_notice_sizes(int step) {
    switch (step) {
    case 0:
        return {crash_tok::kCardW, 0.0f, tok::kFace, tok::kIcon, tok::kBody, tok::kLabel, tok::kLabel,
                tok::kS3, tok::kS2, 1.0f};
    case 1:
        return {crash_tok::kCardW1, crash_tok::kCardShare1, 96.0f, tok::kIconHero, tok::kDisplay,
                tok::kTitle, tok::kBody, tok::kS5, tok::kS4, tok::kWideGrow1};
    default:
        return {crash_tok::kCardW2, crash_tok::kCardShare2, 128.0f, tok::kIconHuge, tok::kDisplay,
                tok::kTitle, tok::kTitle, tok::kS6, tok::kS5, tok::kWideGrow2};
    }
}

struct CrashNoticeGeom {
    Rect card;
    Rect badge; // the badge's box: its tile is centred in it
    Rect title;
    Rect detail;
    Rect dismiss;
    Rect send;
};

// The card's width in a note area `area_w` wide.
inline float crash_notice_card_w(float area_w, float u, int step) {
    const CrashNoticeSizes z = crash_notice_sizes(step);
    const float room = std::max(area_w - tok::kS5 * u, 0.0f);
    return std::min({z.card_share > 0.0f ? z.card_share * area_w : z.card_w * u, z.card_w * u, room});
}

// The width the words get beside the badge: what the title and the detail are wrapped to.
inline float crash_notice_text_w(float area_w, float u, int step) {
    const CrashNoticeSizes z = crash_notice_sizes(step);
    const float badge = std::max(tok::kHit, z.tile) * u;
    return std::max(0.0f, crash_notice_card_w(area_w, u, step) - 2.0f * z.pad * u - badge - z.gap * u);
}

// In a note area `area_w` by `area_h` at (`area_x`, `area_y`); the two chips are `dismiss_w` and
// `send_w` wide (their words'); `step` is `tok::wide_step` of the window's width; the title and the
// detail are `title_lines` and `detail_lines` lines wrapped to `crash_notice_text_w`.
inline CrashNoticeGeom crash_notice_geometry(float area_x, float area_y, float area_w, float area_h,
                                             float u, float dismiss_w, float send_w, int step = 0,
                                             int title_lines = 1, int detail_lines = 1) {
    CrashNoticeGeom g;
    const CrashNoticeSizes z = crash_notice_sizes(step);
    const float hit = tok::kHit * u * z.grow;
    const float pad = z.pad * u;
    const float gap = z.gap * u;
    const float badge = std::max(tok::kHit, z.tile) * u;
    const float title_h = z.title_pt * phone_tok::kLineRatio * u * static_cast<float>(std::max(title_lines, 1));
    const float line_h = z.detail_pt * phone_tok::kLineRatio * u * static_cast<float>(std::max(detail_lines, 1));
    const float words_h = title_h + line_h;
    const float row_h = std::max(badge, words_h);
    const float w = crash_notice_card_w(area_w, u, step);
    const float h = pad + row_h + gap + hit + pad;
    const float x = area_x + (area_w - w) * 0.5f;
    const float y = area_y + std::max(0.0f, (area_h - h) * 0.5f);
    g.card = {x, y, w, h};
    g.badge = {x + pad, y + pad + (row_h - badge) * 0.5f, badge, badge};
    const float text_x = g.badge.right() + gap;
    const float text_w = std::max(0.0f, x + w - pad - text_x);
    const float top = y + pad + (row_h - words_h) * 0.5f;
    g.title = {text_x, top, text_w, title_h};
    g.detail = {text_x, g.title.bottom(), text_w, line_h};
    const float chips_y = y + pad + row_h + gap;
    const float hit_gap = std::max(gap, tok::kHitGap * u);
    g.send = {x + w - pad - send_w, chips_y, send_w, hit};
    g.dismiss = {g.send.x - hit_gap - dismiss_w, chips_y, dismiss_w, hit};
    return g;
}

struct CrashSheetGeom {
    Rect sheet;
    Rect back;
    Rect title;
    Rect action;
    Rect body; // under the head
};

// The sheet in a screen `w` by `h` whose top row ends at `top`; `action_w` is the action chip's
// width (its words'). All the rects are in screen points.
inline CrashSheetGeom crash_sheet_geometry(float w, float h, float top, float u, float action_w) {
    CrashSheetGeom g;
    const float margin = tok::kS3 * u;
    const float pad = tok::kS3 * u;
    const float sheet_w = std::min(crash_tok::kSheetMaxW * u, std::max(w - 2.0f * margin, 0.0f));
    const float sheet_h = std::max(h - top - 2.0f * margin, 0.0f);
    g.sheet = {(w - sheet_w) * 0.5f, top + margin, sheet_w, sheet_h};
    const float inner_w = std::max(sheet_w - 2.0f * pad, 0.0f);
    SubviewGeometry head = subview_geometry(inner_w, sheet_h - 2.0f * pad, u, true);
    const float x = g.sheet.x + pad;
    const float y = g.sheet.y + pad;
    const float aw = std::min(action_w, std::max(inner_w - head.back.w, 0.0f));
    g.back = {x + head.back.x, y + head.back.y, head.back.w, head.back.h};
    g.action = {x + inner_w - aw, y, aw, head.back.h};
    const float title_x = g.back.right() + tok::kS2 * u;
    g.title = {title_x, y, std::max(0.0f, g.action.x - tok::kS2 * u - title_x), head.back.h};
    g.body = {x, y + head.body.y, inner_w, head.body.h};
    return g;
}

// `text` cut at its line ends and then at spaces into lines no wider than `max_w` by
// `width(line)`; a word wider than a line is cut at the character (never inside a UTF-8 one).
template <class Width>
std::vector<std::string> wrap_report(const std::string& text, float max_w, const Width& width) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        end = end == std::string::npos ? text.size() : end;
        const std::string hard = text.substr(at, end - at);
        at = end + 1;
        std::string line;
        std::size_t p = 0;
        while (p < hard.size()) {
            std::size_t stop = hard.find(' ', p);
            stop = stop == std::string::npos ? hard.size() : stop;
            std::string word = hard.substr(p, stop - p);
            p = std::min(stop + 1, hard.size() + 1);
            const std::string joined = line.empty() ? word : line + " " + word;
            if (width(joined) <= max_w) {
                line = joined;
                continue;
            }
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            // The word alone: cut it where the line is full.
            while (width(word) > max_w && word.size() > 1) {
                std::size_t n = word.size() - 1;
                while (n > 1 && (width(word.substr(0, n)) > max_w ||
                                 (static_cast<unsigned char>(word[n]) & 0xC0U) == 0x80U)) {
                    --n;
                }
                while (n < word.size() && (static_cast<unsigned char>(word[n]) & 0xC0U) == 0x80U) {
                    ++n; // a character wider than the line still goes whole
                }
                lines.push_back(word.substr(0, n));
                word = word.substr(n);
            }
            line = word;
        }
        lines.push_back(line);
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

} // namespace etude::app::ui
