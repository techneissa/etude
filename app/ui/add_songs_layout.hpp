// SPDX-License-Identifier: GPL-3.0-or-later
// Where everything on the Add songs page goes, with no ImGui in it, so
// tests/add_songs_layout_test.cpp checks the rules with the real tokens. Sizes in points, from the
// page body's top left; `u` is the kit's unit (points per design point).
//
// Rules:
// - The switch sits right after the words "Phone" and its state, which have one width for every
//   state, so it never moves; it and the row around it are at least `tok::kHit` on both sides.
// - The card fills the page's width. The code is `kQrShare` of it (less when the height does not
//   leave that much), square, and the right side is as tall as the code: the address at the top
//   (its type size shrinks to the title size when the code is short), the state at the bottom, the
//   PIN between, with the free height shared between them. A card too
//   narrow for the right side's least width puts the code above it.
// - Under the card, the network folder and the USB stick are two tiles side by side, each at least
//   `tok::kHit` tall, together as wide as the card.
// - A tile opens a view that replaces everything above: Back at the left, a title, one action
// (Import
//   all, Cancel) at the right, then the body; the USB list's rows are `kUsbRowH` tall with the
//   song's name, its size and a button a full touch target wide and tall.
// - Nothing leaves the page's width, and with the body's height given, nothing leaves it either
//   unless the code is already at its least.
#pragma once

#include "tokens.hpp"

#include <algorithm>

namespace etude::app::ui {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
};

// The Phone row: an icon, the title over the state, the switch, left to right.
struct ToggleRowGeometry {
    Rect row; // what a tap on toggles
    Rect icon;
    Rect text;
    Rect toggle; // the switch's own touch area
    Rect track;  // the drawn switch, centred in `toggle`
};

struct InfoGeometry {
    Rect name;
    float name_size = 0.0f; // the address's type size
    Rect number;
    Rect pin_label;
    Rect pin[4];
    Rect status; // the chips, or the file coming in
};

struct AddSongsGeometry {
    ToggleRowGeometry head;
    bool card_shown = false;
    bool stacked = false; // the code above the text
    Rect card;
    Rect qr;
    InfoGeometry info;
    Rect tiles[2];
    float height = 0.0f; // the bottom of the last tile
};

inline ToggleRowGeometry toggle_row_geometry(float u) {
    ToggleRowGeometry g;
    const float h = phone_tok::kRowH * u;
    g.icon = {tok::kS3 * u, 0.0f, tok::kIcon * u, h};
    g.text = {g.icon.right() + tok::kS3 * u, 0.0f, phone_tok::kStatusW * u, h};
    g.toggle = {g.text.right() + tok::kS4 * u, 0.0f, phone_tok::kSwitchW * u, h};
    g.track = {g.toggle.x, (h - phone_tok::kSwitchH * u) * 0.5f, phone_tok::kSwitchW * u,
               phone_tok::kSwitchH * u};
    g.row = {0.0f, 0.0f, g.toggle.right() + tok::kS3 * u, h};
    return g;
}

// `with_card`: the phone page is on and has an address, so the card shows. `below_head` is how much
// the page puts between the Phone row and the tiles when there is no card (its help text), 0 when
// the card is there.
inline AddSongsGeometry add_songs_geometry(float w, float body_h, float u, bool with_card,
                                           float below_head = 0.0f) {
    AddSongsGeometry g;
    g.head = toggle_row_geometry(u);
    const float gap = tok::kS2 * u;
    const float pad = tok::kS4 * u;
    const float tile_h = phone_tok::kTileH * u;
    float y = g.head.row.h + gap;
    g.card_shown = with_card;
    if (with_card) {
        const float card_max = std::max(0.0f, body_h - y - gap - tile_h);
        const float inner_max = std::max(0.0f, card_max - 2.0f * pad);
        float q = std::min(phone_tok::kQrShare * w, inner_max);
        q = std::max(q, phone_tok::kQrMin * u);
        const float right_w = w - 3.0f * pad - q;
        g.stacked = right_w < phone_tok::kInfoMinW * u;
        if (g.stacked) {
            q = std::min({w - 2.0f * pad, phone_tok::kStackedQrMax * u});
        }
        const float info_w = g.stacked ? w - 2.0f * pad : right_w;
        float name_size = std::clamp(info_w / (phone_tok::kAddressChars * phone_tok::kBoldAdvance),
                                     tok::kTitle * u, phone_tok::kAddressSize * u);
        const float number_h = (tok::kS4 + tok::kS1) * u;
        const float status_h = phone_tok::kStatusH * u;
        const float least_gap = tok::kS1 * u;
        if (!g.stacked) {
            // A code the height holds short keeps the right side as tall as it: the address takes
            // what the rest leaves, never under the title size.
            const float rest = number_h + status_h + phone_tok::kPinMinH * u + 3.0f * least_gap;
            name_size = std::max(tok::kTitle * u,
                                 std::min(name_size, (q - rest) / phone_tok::kLineRatio));
        }
        const float name_h = name_size * phone_tok::kLineRatio;
        // The right side as tall as the code (or, stacked, as tall as it needs): the PIN's boxes
        // take what is left after the words and the least gaps, within their own range.
        const float fixed = name_h + number_h + status_h;
        const float want_h = g.stacked ? fixed + phone_tok::kPinMinH * u + 3.0f * least_gap : q;
        const float pin_h = std::clamp(want_h - fixed - 3.0f * least_gap, phone_tok::kPinMinH * u,
                                       phone_tok::kPinMaxH * u);
        const float info_h = std::max(want_h, fixed + pin_h + 3.0f * least_gap);
        const float spare = (info_h - fixed - pin_h) / 3.0f; // between name, PIN and state
        g.card = {0.0f, y, w, 0.0f};
        const float x = g.stacked ? pad : pad + q + pad;
        float top = y + pad;
        g.qr = {g.stacked ? (w - q) * 0.5f : pad, top, q, q};
        if (g.stacked) {
            top = g.qr.bottom() + pad;
        }
        const float info_top = g.stacked ? top : top + (std::max(q, info_h) - info_h) * 0.5f;
        InfoGeometry& in = g.info;
        in.name_size = name_size;
        in.name = {x, info_top, info_w, name_h};
        in.number = {x, in.name.bottom(), info_w, number_h};
        const float pin_y = in.number.bottom() + spare;
        in.pin_label = {x, pin_y, phone_tok::kPinLabelW * u, pin_h};
        const float boxes_w = info_w - in.pin_label.w;
        const float box_w = std::min(phone_tok::kPinMaxW * u, (boxes_w - 3.0f * gap) / 4.0f);
        for (int i = 0; i < 4; ++i) {
            in.pin[i] = {in.pin_label.right() + static_cast<float>(i) * (box_w + gap), pin_y, box_w,
                         pin_h};
        }
        in.status = {x, pin_y + pin_h + spare, info_w, status_h};
        const float content_bottom =
            g.stacked ? in.status.bottom() : std::max(g.qr.bottom(), in.status.bottom());
        g.card.h = content_bottom + pad - y;
        y = g.card.bottom() + gap;
    } else {
        y += below_head;
    }
    const float tile_w = (w - gap) * 0.5f;
    g.tiles[0] = {0.0f, y, tile_w, tile_h};
    g.tiles[1] = {tile_w + gap, y, tile_w, tile_h};
    g.height = g.tiles[0].bottom();
    return g;
}

// The view that replaces the Phone row and the tiles: Back at the left, the title, the action
// (Import all, Cancel) at the right, and the body under them. All of it within `w` and `body_h`.
struct SubviewGeometry {
    Rect back;
    Rect title;
    Rect action; // no width when the view has no action
    Rect body;
};

inline SubviewGeometry subview_geometry(float w, float body_h, float u, bool with_action) {
    SubviewGeometry g;
    const float h = phone_tok::kRowH * u;
    const float gap = tok::kS2 * u;
    g.back = {0.0f, 0.0f, std::min(phone_tok::kBackW * u, w), h};
    const float action_w = with_action ? std::min(phone_tok::kActionW * u, w - g.back.w) : 0.0f;
    g.action = {w - action_w, 0.0f, action_w, h};
    const float title_x = g.back.right() + gap;
    g.title = {title_x, 0.0f, std::max(0.0f, g.action.x - gap - title_x), h};
    g.body = {0.0f, h + gap, w, std::max(0.0f, body_h - h - gap)};
    return g;
}

// A row of the USB list from `y` in a list `w` wide: the name, the size and the button.
struct UsbRowGeometry {
    Rect name;
    Rect size;
    Rect button;
    float step = 0.0f; // the row's height, which is where the next one starts
};

inline UsbRowGeometry usb_row_geometry(float w, float y, float u) {
    UsbRowGeometry g;
    const float pad = tok::kS3 * u;
    const float gap = tok::kS2 * u;
    g.step = phone_tok::kUsbRowH * u;
    const float hit = tok::kHit * u;
    const float button_w = std::min(phone_tok::kUsbButtonW * u, std::max(hit, w * 0.4f));
    g.button = {w - pad - button_w, y + (g.step - hit) * 0.5f, button_w, hit};
    g.size = {g.button.x - gap - phone_tok::kUsbSizeW * u, y, phone_tok::kUsbSizeW * u, g.step};
    g.name = {pad, y, std::max(0.0f, g.size.x - gap - pad), g.step};
    return g;
}

} // namespace etude::app::ui
