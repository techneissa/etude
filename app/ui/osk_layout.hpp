// SPDX-License-Identifier: GPL-3.0-or-later
// The on-screen keyboard: its keys, where they go, and where the name card sits above it, in ImGui
// points (the sizes are `osk_tok` in tokens.hpp). No ImGui here. One keyboard serves every text field in the app:
// the library's search and names, a section's name, a look's name, the typed tempo.
#pragma once

#include "face_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace etude::app::ui {

// One key of the on-screen keyboard: where it is and what it does.
enum class KeyKind : unsigned char { Letter, Backspace, Layer, Clear, Space, Done };
struct KeySpec {
    const char* text; // the character it types (Letter); null for the others
    float units;      // its width in key units
    KeyKind kind;
};

// The layers: letters, then digits and symbols (four rows each, ending in the same bottom row),
// and the number pad, two rows, for a field that takes a number.
enum class Layer : unsigned char { Letters, Symbols, Numbers };

struct KeyRow {
    const KeySpec* keys;
    std::size_t count;
};

inline std::span<const KeyRow> keyboard_rows(Layer layer) {
    static constexpr KeySpec kL1[] = {{"q", 1, KeyKind::Letter}, {"w", 1, KeyKind::Letter},
                                      {"e", 1, KeyKind::Letter}, {"r", 1, KeyKind::Letter},
                                      {"t", 1, KeyKind::Letter}, {"y", 1, KeyKind::Letter},
                                      {"u", 1, KeyKind::Letter}, {"i", 1, KeyKind::Letter},
                                      {"o", 1, KeyKind::Letter}, {"p", 1, KeyKind::Letter}};
    static constexpr KeySpec kL2[] = {
        {"a", 1, KeyKind::Letter}, {"s", 1, KeyKind::Letter}, {"d", 1, KeyKind::Letter},
        {"f", 1, KeyKind::Letter}, {"g", 1, KeyKind::Letter}, {"h", 1, KeyKind::Letter},
        {"j", 1, KeyKind::Letter}, {"k", 1, KeyKind::Letter}, {"l", 1, KeyKind::Letter}};
    static constexpr KeySpec kL3[] = {
        {"z", 1, KeyKind::Letter}, {"x", 1, KeyKind::Letter}, {"c", 1, KeyKind::Letter},
        {"v", 1, KeyKind::Letter}, {"b", 1, KeyKind::Letter}, {"n", 1, KeyKind::Letter},
        {"m", 1, KeyKind::Letter}, {"'", 1, KeyKind::Letter}, {nullptr, 1.5f, KeyKind::Backspace}};
    static constexpr KeySpec kD1[] = {{"1", 1, KeyKind::Letter}, {"2", 1, KeyKind::Letter},
                                      {"3", 1, KeyKind::Letter}, {"4", 1, KeyKind::Letter},
                                      {"5", 1, KeyKind::Letter}, {"6", 1, KeyKind::Letter},
                                      {"7", 1, KeyKind::Letter}, {"8", 1, KeyKind::Letter},
                                      {"9", 1, KeyKind::Letter}, {"0", 1, KeyKind::Letter}};
    static constexpr KeySpec kD2[] = {
        {"-", 1, KeyKind::Letter}, {".", 1, KeyKind::Letter}, {",", 1, KeyKind::Letter},
        {"&", 1, KeyKind::Letter}, {"(", 1, KeyKind::Letter}, {")", 1, KeyKind::Letter},
        {"/", 1, KeyKind::Letter}, {"#", 1, KeyKind::Letter}, {":", 1, KeyKind::Letter}};
    static constexpr KeySpec kD3[] = {
        {"!", 1, KeyKind::Letter}, {"?", 1, KeyKind::Letter},  {"+", 1, KeyKind::Letter},
        {"=", 1, KeyKind::Letter}, {"\"", 1, KeyKind::Letter}, {";", 1, KeyKind::Letter},
        {"_", 1, KeyKind::Letter}, {"@", 1, KeyKind::Letter},  {nullptr, 1.5f, KeyKind::Backspace}};
    static constexpr KeySpec kBottom[] = {{nullptr, 2.0f, KeyKind::Layer},
                                          {nullptr, 1.5f, KeyKind::Clear},
                                          {nullptr, 4.5f, KeyKind::Space},
                                          {nullptr, 2.0f, KeyKind::Done}};
    static constexpr KeySpec kPadBottom[] = {{nullptr, 3.0f, KeyKind::Backspace},
                                             {nullptr, 3.0f, KeyKind::Clear},
                                             {nullptr, 4.0f, KeyKind::Done}};
    static const std::array<KeyRow, osk_tok::kKeyRows> letters{{{kL1, std::size(kL1)},
                                                                {kL2, std::size(kL2)},
                                                                {kL3, std::size(kL3)},
                                                                {kBottom, std::size(kBottom)}}};
    static const std::array<KeyRow, osk_tok::kKeyRows> symbols{{{kD1, std::size(kD1)},
                                                                {kD2, std::size(kD2)},
                                                                {kD3, std::size(kD3)},
                                                                {kBottom, std::size(kBottom)}}};
    static const std::array<KeyRow, 2> pad{
        {{kD1, std::size(kD1)}, {kPadBottom, std::size(kPadBottom)}}};
    switch (layer) {
    case Layer::Letters:
        return letters;
    case Layer::Symbols:
        return symbols;
    case Layer::Numbers:
        return pad;
    }
    return letters;
}

// The keyboard's height for an area `width` points wide, at most `max_h` (its rows shrink toward
// `tok::kCell`, never below it), for a layer of `rows` rows. The keys fill the whole width, so
// their height follows it: the wider the window, the taller the keys, between the least pitch and
// the tallest.
inline float keyboard_height(float width, float u, float max_h = 1e9f,
                             int rows = osk_tok::kKeyRows) {
    const float pad = 2.0f * osk_tok::kKeyPad * u;
    const auto n = static_cast<float>(rows);
    float pitch = std::clamp(width * osk_tok::kKeyPitchShare, osk_tok::kKeyPitchMin * u,
                             osk_tok::kKeyPitchMax * u);
    if (pitch * n + pad > max_h) {
        pitch = std::max(tok::kCell * u, (max_h - pad) / n);
    }
    return pitch * n + pad;
}

// Key `index` of row `row` in a keyboard in `area`: its cell. The key's face and hit area are the
// cell less half the dead gap (`osk_tok::kKeyGap`) on each side, so two keys' hit areas are a gap
// apart. Every row runs from one edge of the area to the other (less
// the pad): a row whose keys add up to fewer units than the widest has each key stretched to fill
// it, so no row is bunched in the middle.
inline Box keyboard_key(const Box& area, float u, Layer layer, std::size_t row, std::size_t index) {
    const std::span<const KeyRow> rows = keyboard_rows(layer);
    const float pad = osk_tok::kKeyPad * u;
    const float x0 = area.x0 + pad;
    const float unit = (area.w() - 2.0f * pad) / osk_tok::kKeyUnits;
    const float pitch = (area.h() - 2.0f * pad) / static_cast<float>(rows.size());
    const KeyRow& r = rows[row];
    float total = 0.0f;
    for (std::size_t i = 0; i < r.count; ++i) {
        total += r.keys[i].units;
    }
    const float stretch = total > 0.0f ? osk_tok::kKeyUnits / total : 1.0f;
    float x = x0;
    for (std::size_t i = 0; i < index; ++i) {
        x += r.keys[i].units * unit * stretch;
    }
    const float y = area.y0 + pad + pitch * static_cast<float>(row);
    return {x, y, x + r.keys[index].units * unit * stretch, y + pitch};
}

// A key's face and hit area, one box: its cell less half the dead gap (`osk_tok::kKeyGap`) on each
// side, so two keys' hit areas are a gap apart.
inline Box keyboard_key_hit(const Box& cell, float gap) {
    const float half = gap * 0.5f;
    return {cell.x0 + half, cell.y0 + half, cell.x1 - half, cell.y1 - half};
}

// What the keys' text and icons are drawn at for keys `pitch` tall: the least size at the least
// pitch, growing with the key to the tallest. `letter` is a character, `word` ("Clear", "123"),
// `icon` the backspace and enter glyphs.
struct KeyInk {
    float letter = 0.0f;
    float word = 0.0f;
    float icon = 0.0f;
};
inline KeyInk key_ink(float pitch, float u) {
    KeyInk k;
    k.letter = std::clamp(pitch * osk_tok::kKeyLetterShare, osk_tok::kKeyLetter * u,
                          osk_tok::kKeyLetterMax * u);
    k.word = std::clamp(pitch * 0.30f, tok::kLabel * u, osk_tok::kKeyWordMax * u);
    k.icon = std::clamp(pitch * 0.42f, tok::kIcon * u, osk_tok::kKeyIconMax * u);
    return k;
}

// The keyboard of a name dialog over a `display_w` by `display_h` window: it leaves the card's
// one-row form room above it (the card may reach over the top row), its keys shrinking to their
// least size (a hit tall) before it takes more.
inline float name_keyboard_height(float display_w, float display_h, float u) {
    const float least_card = (tok::kHit + 2.0f * tok::kS2 + tok::kS4 + tok::kS2) * u;
    const float room = display_h - least_card;
    // A window too short for keys of a hit a row and the card (under 272 design points tall): Save
    // is never covered, so the keys give way past their least size, to half of it.
    return std::min(keyboard_height(display_w, u, room), std::max(room, 2.0f * tok::kHit * u));
}

// ---- The name card ----------------------------------------------------------------------------

// What a name card holds, besides its title and field and the Cancel and Save buttons.
struct NameCardSpec {
    int quick = 0;                 // one-tap names offered, osk_tok::kQuickPerRow to a row
    bool own = false;              // a third button: the file's own name, or the default
    float width = osk_tok::kCardW; // design points, at most
    bool top = false;              // stands at the top of its area (else about 42% down)
    float above = 0.0f; // points the one-row card may reach above the area (over the top row)
                        // where the keys leave it no room below
};

struct NameCardPlan {
    Box card;
    bool compact = false; // one row: the title, the field, Cancel and Save
    bool tight = false;   // the roomy spacing was too much: less padding
    bool quick = false;   // the one-tap names show (never in the compact card)
    Box title;            // the title's slot: above the field, or in the compact card left of it
    Box field;
    Box chips; // the block of one-tap names; empty when they are off
    int chip_rows = 0;
    float chip_gap = 0.0f; // between two rows of them
    Box own;               // the buttons, in a row under the field (the compact card has Cancel
    Box cancel;            // beside the field, Save at the right end, and no third button)
    Box save;
};

// The card in `area`, clear of a keyboard that begins at `keys_top` (`area.y1` with none up).
// `title_w` is the title's text width, which the compact card puts left of its field. The card
// shows what it can of its parts above the keyboard, trying in turn: all of them, all of them
// tight, without the one-tap names, and as one row.
inline NameCardPlan plan_name_card(const Box& area, float u, float keys_top,
                                   const NameCardSpec& spec, float title_w) {
    NameCardPlan p;
    const float margin = tok::kS4 * u;
    const float hit = tok::kHit * u;
    const float gap = tok::kS2 * u;
    const float w = std::min(spec.width * u, area.w() - 2.0f * margin);
    const float x = area.cx() - w * 0.5f;
    const bool keys_up = keys_top < area.y1 - 0.5f;
    const float bottom = keys_top - (keys_up ? gap : 0.0f);
    const bool top = spec.top || keys_up;
    const int rows =
        spec.quick > 0 ? (spec.quick + osk_tok::kQuickPerRow - 1) / osk_tok::kQuickPerRow : 0;
    // The spacing: roomy, or tight.
    struct Spacing {
        float pad;
        float title_h;
        float chip_gap;
    };
    const Spacing roomy{tok::kS3 * u, (tok::kTitle * 1.2f + tok::kS2) * u, gap};
    const Spacing tight{tok::kS2 * u, (tok::kTitle * 1.2f + tok::kS1) * u, gap};
    const auto height = [&](const Spacing& sp, bool with_chips) {
        float h = sp.pad * 2.0f + sp.title_h + hit + gap + hit;
        if (with_chips && rows > 0) {
            h += static_cast<float>(rows) * hit + static_cast<float>(rows - 1) * sp.chip_gap + gap;
        }
        return h;
    };
    const auto place = [&](float h) {
        if (top) {
            return area.y0 + margin;
        }
        const float centred = area.y0 + area.h() * 0.42f - h * 0.5f;
        return std::clamp(centred, area.y0 + gap, std::max(area.y0 + gap, bottom - h));
    };
    Spacing sp = roomy;
    bool with_chips = rows > 0;
    float h = height(sp, with_chips);
    float y = place(h);
    if (y + h > bottom && with_chips) {
        sp = tight;
        p.tight = true;
        h = height(sp, true);
        y = place(h);
        if (y + h > bottom) {
            sp = roomy;
            p.tight = false;
            with_chips = false;
            h = height(sp, false);
            y = place(h);
        }
    }
    if (y + h > bottom && !p.tight) {
        sp = tight;
        p.tight = true;
        h = height(sp, with_chips);
        y = place(h);
    }
    p.compact = y + h > bottom;
    p.quick = with_chips && !p.compact;
    float pad = sp.pad;
    if (p.compact) {
        p.tight = false;
        pad = tok::kS2 * u;
        h = pad * 2.0f + hit;
        y = std::max(area.y0 - spec.above + gap, std::min(area.y0 + margin, bottom - h));
    }
    p.card = {x, y, x + w, y + h};
    p.chip_rows = p.quick ? rows : 0;
    p.chip_gap = sp.chip_gap;
    const float save_w = 2.0f * hit;
    float cy = y + pad;
    if (p.compact) {
        const float tx = x + pad + gap;
        p.title = {tx, cy, tx + title_w, cy + hit};
        p.save = {x + w - pad - save_w, cy, x + w - pad, cy + hit};
        p.cancel = {p.save.x0 - gap - hit, cy, p.save.x0 - gap, cy + hit};
        p.field = {p.title.x1 + 2.0f * gap, cy, p.cancel.x0 - gap, cy + hit};
        return p;
    }
    p.title = {x + pad, cy, x + w - pad, cy + sp.title_h};
    cy += sp.title_h;
    p.field = {x + pad, cy, x + w - pad, cy + hit};
    cy += hit + gap;
    if (p.quick) {
        const float chips_h =
            static_cast<float>(rows) * hit + static_cast<float>(rows - 1) * sp.chip_gap;
        p.chips = {x + pad, cy, x + w - pad, cy + chips_h};
        cy += chips_h + gap;
    }
    const int buttons = spec.own ? 3 : 2;
    const float bw =
        (w - 2.0f * pad - static_cast<float>(buttons - 1) * gap) / static_cast<float>(buttons);
    float bx = x + pad;
    if (spec.own) {
        p.own = {bx, cy, bx + bw, cy + hit};
        bx += bw + gap;
    }
    p.cancel = {bx, cy, bx + bw, cy + hit};
    bx += bw + gap;
    p.save = {bx, cy, bx + bw, cy + hit};
    return p;
}

// One-tap name `index` in a card's block of them: `osk_tok::kQuickPerRow` across, each `hit`
// tall, `row_gap` between two rows.
inline Box quick_name_cell(const Box& chips, float u, float row_gap, std::size_t index) {
    const auto per = static_cast<std::size_t>(osk_tok::kQuickPerRow);
    const float gap = tok::kS2 * u;
    const float cw = (chips.w() - gap * static_cast<float>(per - 1)) / static_cast<float>(per);
    const float hit = tok::kHit * u;
    const std::size_t col = index % per;
    const std::size_t row = index / per;
    const float x = chips.x0 + static_cast<float>(col) * (cw + gap);
    const float y = chips.y0 + static_cast<float>(row) * (hit + row_gap);
    return {x, y, x + cw, y + hit};
}

} // namespace etude::app::ui
