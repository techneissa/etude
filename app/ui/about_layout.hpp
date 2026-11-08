// SPDX-License-Identifier: GPL-3.0-or-later
// Where everything on the About page goes, with no ImGui in it, so tests/about_layout_test.cpp
// checks the rules with the real tokens. Sizes in points, from the page body's top left; `u` is the
// kit's unit (points per design point).
//
// Rules:
// - From the top, at every window size: one row with the app's icon, its name and its version line
//   at the left and the links (the GitHub mark with its owner's name, and the Buy Me a Coffee cup)
//   at the right end; then the Updates card.
// - Every link is at least `tok::kHit` wide and tall, with at least `tok::kHitGap` between them,
//   and the row stays inside the page's width.
// - The Updates are two boxes: the status box (the status row, what is new and "Update") and the
//   settings box ("Check automatically", then "Check now" with the time of the last check). From
//   `kAboutWideW` wide they sit side by side, else the settings box is under the status box.
// - The status row is `kStatusH` tall; its one action (Retry, Cancel) is at the right, `tok::kHit`
//   tall at least, clear of the words, which take what the action and the reason's icon leave. A
//   failure's reason that does not fit the line opens from the status icon itself, which is then a
//   touch target (`tok::kHit`) with `tok::kHitGap` before the words; else it is a plain icon.
// - A download's bar sits under the status words, inside the row's side padding.
// - "Check now" is at the row's right end, the time of the last check at its left, in the room the
//   button leaves.
// - "Update" is `tok::kHit` tall at least and as wide as its words want; where the card is one
//   column it may fill the card's width instead.
// - The QR sheet that replaces the page's body for a link has Back at the left, the title beside
//   it, then the code in a white tile (as large as the height allows, within `kQrMax`) and under it
//   the address and a line saying what to do; a body too short for that puts the code and the words
//   side by side.
#pragma once

#include "add_songs_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace about_tok {
inline constexpr float kIconSide = 64.0f;   // the app icon
inline constexpr float kTop = tok::kS2;     // above the icon
inline constexpr float kNameH = tok::kTitle * phone_tok::kLineRatio;
inline constexpr float kVersionH = tok::kBody * phone_tok::kLineRatio;
inline constexpr float kMark = 22.0f;       // the GitHub mark drawn in its link
inline constexpr float kCup = 28.0f;        // the Buy Me a Coffee tile drawn in its link
inline constexpr float kLinkGap = tok::kS3; // between the links (past the dead gap)
inline constexpr float kStatusH = tok::kHit + tok::kS2;
inline constexpr float kBarH = 6.0f;        // a download's bar
inline constexpr float kActionW = 112.0f;   // Retry, Cancel
inline constexpr float kCheckW = 128.0f;    // Check now
inline constexpr float kQrMax = 280.0f;     // the sheet's code, across
inline constexpr float kQrMin = 120.0f;
inline constexpr float kSheetAddressH = tok::kTitle * phone_tok::kLineRatio;
inline constexpr float kSheetHintH = tok::kBody * phone_tok::kLineRatio;
inline constexpr float kNotesCaptionH = tok::kCaption * phone_tok::kLineRatio;
inline constexpr float kNotesLineH = tok::kLabel * phone_tok::kLineRatio;
} // namespace about_tok

// The links' hit areas from the row's top, for a GitHub name `name_w` wide.
struct AboutLinks {
    Rect github;
    Rect coffee;
    float height = 0.0f;
};

inline AboutLinks about_links(float w, float u, float name_w) {
    AboutLinks g;
    const float hit = tok::kHit * u;
    const float github_w =
        std::max(hit, 2.0f * tok::kS3 * u + about_tok::kMark * u + tok::kS2 * u + name_w);
    const float gap = about_tok::kLinkGap * u;
    const float total = github_w + gap + hit;
    const float x = std::max(0.0f, (w - total) * 0.5f);
    g.github = {x, 0.0f, github_w, hit};
    g.coffee = {g.github.right() + gap, 0.0f, hit, hit};
    g.height = hit + tok::kS2 * u;
    return g;
}

// The top of the page in one piece: one row with the icon and the words at the left and the links
// at the right end.
struct AboutTop {
    Rect icon;
    Rect name;
    Rect version;
    Rect github;
    Rect coffee;
    float height = 0.0f;
};

inline constexpr float kAboutRowIcon = 48.0f; // the icon in the top row
inline constexpr float kAboutRowPad = tok::kS2;

// `text_w` is the wider of the name and the version line, `name_w` the GitHub name's width. A page
// too narrow for all of it gives the words what the links leave.
inline AboutTop about_top(float w, float u, float text_w, float name_w) {
    AboutTop g;
    const AboutLinks links = about_links(w, u, name_w);
    const float icon = kAboutRowIcon * u;
    const float pad = tok::kS3 * u;
    const float row_h = icon + 2.0f * kAboutRowPad * u;
    const float text_x = pad + icon + tok::kS3 * u;
    const float words_h = (about_tok::kNameH + about_tok::kVersionH) * u;
    const float hit = tok::kHit * u;
    const float coffee_x = w - pad - links.coffee.w;
    const float github_x = std::max(text_x, coffee_x - about_tok::kLinkGap * u - links.github.w);
    const float words_w = std::clamp(github_x - tok::kS3 * u - text_x, 0.0f, text_w);
    g.icon = {pad, (row_h - icon) * 0.5f, icon, icon};
    g.name = {text_x, (row_h - words_h) * 0.5f, words_w, about_tok::kNameH * u};
    g.version = {text_x, g.name.bottom(), words_w, about_tok::kVersionH * u};
    g.github = {github_x, (row_h - hit) * 0.5f, links.github.w, hit};
    g.coffee = {coffee_x, (row_h - hit) * 0.5f, links.coffee.w, hit};
    g.height = row_h;
    return g;
}

// The status row of the Updates card: a state icon, the words, an action at the right; under it a
// download's bar, then a line of detail.
struct AboutStatus {
    Rect row;
    Rect icon;   // `tok::kHit` wide when it is the reason's button (`with_info`)
    Rect text;
    Rect action; // no width when there is none
    Rect bar;    // no height when none
    Rect detail; // no height when none
    float height = 0.0f;
};

inline AboutStatus about_status(float w, float u, bool with_action, bool with_bar,
                                bool with_detail, bool with_info = false) {
    AboutStatus g;
    const float h = about_tok::kStatusH * u;
    const float pad = tok::kS3 * u;
    g.row = {0.0f, 0.0f, w, h};
    const float icon_w = (with_info ? tok::kHit : tok::kIcon) * u;
    g.icon = {pad, with_info ? (h - tok::kHit * u) * 0.5f : 0.0f, icon_w,
              with_info ? tok::kHit * u : h};
    const float action_w = with_action ? std::min(about_tok::kActionW * u, w * 0.4f) : 0.0f;
    g.action = {w - pad - action_w, (h - tok::kHit * u) * 0.5f, action_w, tok::kHit * u};
    const float text_x = g.icon.right() + (with_info ? tok::kHitGap : tok::kS3) * u;
    const float text_right = with_action ? g.action.x - tok::kS2 * u : w - pad;
    g.text = {text_x, 0.0f, std::max(0.0f, text_right - text_x), h};
    float y = h;
    g.bar = {text_x, y, std::max(0.0f, w - pad - text_x), 0.0f};
    if (with_bar) {
        g.bar.h = about_tok::kBarH * u;
        y = g.bar.bottom() + tok::kS2 * u;
    }
    g.detail = {text_x, y, std::max(0.0f, w - pad - text_x), 0.0f};
    if (with_detail) {
        g.detail.h = tok::kLabel * phone_tok::kLineRatio * u;
        y = g.detail.bottom() + tok::kS1 * u;
    }
    g.height = y;
    return g;
}

// What an update that is out adds under the status row: "What's new" and the notes' lines, then
// "Update" below, `button_w` wide.
struct AboutUpdate {
    Rect caption;
    Rect notes; // `lines` lines, one `kNotesLineH` each
    Rect button;
    float height = 0.0f; // from `y`, to the bottom of the button and the card's padding
};

// `button_w` 0: the button fills the card; else it is that wide (at least a touch target).
inline AboutUpdate about_update(float w, float u, float y, int lines, float button_w = 0.0f) {
    AboutUpdate g;
    const float pad = tok::kS3 * u;
    const float x = pad;
    const float room = std::max(0.0f, w - 2.0f * pad);
    g.caption = {x, y, room, 0.0f};
    g.notes = {x, y, room, 0.0f};
    if (lines > 0) {
        g.caption.h = about_tok::kNotesCaptionH * u;
        g.notes = {x, g.caption.bottom(), room,
                   static_cast<float>(lines) * about_tok::kNotesLineH * u};
        y = g.notes.bottom() + tok::kS2 * u;
    }
    const float width = button_w > 0.0f ? std::clamp(button_w, tok::kHit * u, room) : room;
    g.button = {x, y, width, tok::kHit * u};
    g.height = g.button.bottom() + pad - g.caption.y;
    return g;
}

// The settings box's rows, in the box's own coordinates, for a box `w` wide: "Check automatically",
// then the check row ("Last checked" and "Check now"), each `tok::kHit` tall.
struct AboutSettings {
    Rect switch_row;
    Rect check_row;
    float height = 0.0f; // the box, padding included
};

inline AboutSettings about_settings(float w, float u) {
    AboutSettings g;
    const float top = tok::kS1 * u; // centres the first row on a status row beside it
    const float hit = tok::kHit * u;
    g.switch_row = {0.0f, top, w, hit};
    g.check_row = {0.0f, g.switch_row.bottom() + tok::kS2 * u, w, hit};
    g.height = g.check_row.bottom() + top;
    return g;
}

// The two boxes in the page's coordinates: side by side from `kAboutWideW` wide (the settings box at
// the right, `kSettingsMinW` to `kSettingsMaxW` wide, both as tall as the taller), else the settings
// box under the status box. `status_h` and `settings_h` are their heights. At the break the status
// row has about 155 points for a failure's short form once Retry and (!) take their room, which
// holds every short form on two lines (`wrap_words`).
inline constexpr float kAboutWideW = 760.0f;
inline constexpr float kSettingsMinW = 360.0f;
inline constexpr float kSettingsMaxW = 440.0f;

struct AboutBoxes {
    Rect status;
    Rect settings;
    bool side_by_side = false;
    float height = 0.0f;
};

inline bool about_side_by_side(float w, float u) { return w >= kAboutWideW * u; }

inline AboutBoxes about_boxes(float w, float u, float status_h, float settings_h) {
    AboutBoxes g;
    const float gap = tok::kS3 * u;
    g.side_by_side = about_side_by_side(w, u);
    if (g.side_by_side) {
        const float sw = std::clamp(w * 0.42f, kSettingsMinW * u, kSettingsMaxW * u);
        // Side by side the boxes are as tall as the taller one.
        g.height = std::max(status_h, settings_h);
        g.status = {0.0f, 0.0f, w - gap - sw, g.height};
        g.settings = {w - sw, 0.0f, sw, g.height};
    } else {
        g.status = {0.0f, 0.0f, w, status_h};
        g.settings = {0.0f, status_h + gap, w, settings_h};
        g.height = g.settings.bottom();
    }
    return g;
}

// `text` cut at its spaces into at most `max_lines` lines no wider than `max_w` by `width(line)`,
// each as full as it goes; the last line takes what is left (it may be wider: the caller fits it). A
// word wider than a line stays whole on its own line.
template <class Width>
std::vector<std::string> wrap_words(const std::string& text, float max_w, int max_lines,
                                    const Width& width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find(' ', at);
        end = end == std::string::npos ? text.size() : end;
        const std::string word = text.substr(at, end - at);
        at = end + 1;
        const std::string joined = line.empty() ? word : line + " " + word;
        const bool last = static_cast<int>(lines.size()) + 1 >= max_lines;
        if (line.empty() || last || width(joined) <= max_w) {
            line = joined;
        } else {
            lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// "Last checked" at the left, "Check now" at the right end of one row `tok::kHit` tall.
struct AboutCheckRow {
    Rect row;
    Rect text;
    Rect button;
};

inline AboutCheckRow about_check_row(float w, float u) {
    AboutCheckRow g;
    const float h = tok::kHit * u;
    const float pad = tok::kS3 * u;
    g.row = {0.0f, 0.0f, w, h};
    const float button_w = std::min(about_tok::kCheckW * u, w * 0.4f);
    g.button = {w - pad - button_w, 0.0f, button_w, h};
    const float text_x = pad + tok::kIcon * u + pad;
    g.text = {text_x, 0.0f, std::max(0.0f, g.button.x - tok::kS2 * u - text_x), h};
    return g;
}

// The code, the address and a line of what to do, on the link's sheet, in a body `w` by `body_h`.
struct AboutSheet {
    SubviewGeometry head;
    Rect qr;
    Rect address;
    Rect hint;
    bool side_by_side = false;
};

inline AboutSheet about_sheet(float w, float body_h, float u) {
    AboutSheet g;
    g.head = subview_geometry(w, body_h, u, false);
    const Rect body = g.head.body;
    const float gap = tok::kS3 * u;
    const float address_h = about_tok::kSheetAddressH * u;
    const float hint_h = about_tok::kSheetHintH * u;
    const float words_h = address_h + hint_h;
    const float stacked_q = std::min({about_tok::kQrMax * u, body.w, body.h - words_h - gap});
    g.side_by_side = stacked_q < about_tok::kQrMin * u && body.w > body.h;
    if (g.side_by_side) {
        const float q = std::clamp(std::min(about_tok::kQrMax * u, body.h), 0.0f, body.w * 0.5f);
        g.qr = {body.x, body.y + (body.h - q) * 0.5f, q, q};
        const float x = g.qr.right() + gap;
        const float room = std::max(0.0f, body.right() - x);
        const float top = body.y + (body.h - words_h) * 0.5f;
        g.address = {x, top, room, address_h};
        g.hint = {x, g.address.bottom(), room, hint_h};
        return g;
    }
    const float q = std::max(stacked_q, 0.0f);
    const float top = body.y + std::max(0.0f, (body.h - q - gap - words_h) * 0.5f);
    g.qr = {body.x + (body.w - q) * 0.5f, top, q, q};
    g.address = {body.x, g.qr.bottom() + gap, body.w, address_h};
    g.hint = {body.x, g.address.bottom(), body.w, hint_h};
    return g;
}

} // namespace etude::app::ui
