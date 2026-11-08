// SPDX-License-Identifier: GPL-3.0-or-later
#include "medal_emblem.hpp"

#include "medal_atlas.hpp"
#include "music_glyph.hpp"

#include "etude/medal_art.hpp"
#include "etude/medals.hpp"

#include <algorithm>
#include <cmath>

namespace etude::app::ui {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kEmblemShare = 0.64f;      // the emblem's box, over the disc's diameter
constexpr float kEmblemShareSmall = 0.70f; // ... on a small medal, where it has to be read

// The emblem box is 100 units square, y down; `xf` puts a unit point on the screen.
struct Xf {
    ImVec2 c;   // the box's centre on the screen
    float s;    // pixels per unit
    ImVec2 at(float x, float y) const { return {c.x + (x - 50.0f) * s, c.y + (y - 50.0f) * s}; }
    float len(float units) const { return units * s; }
    // The same box scaled by `k` about the unit point (50, 50) and moved by (dx, dy) units.
    Xf sub(float k, float dx, float dy) const { return {at(50.0f + dx, 50.0f + dy), s * k}; }
};

// What a painter draws with: the silhouette colour, the details cut into it (0: none, the emboss's
// edge passes draw the silhouette alone) and whether the emblem is small (no ribbon, 28 pt): the
// painters leave out what would only smear.
struct Paint {
    ImDrawList* dl;
    ImU32 ink;
    ImU32 detail;
    ImU32 light; // the lines drawn across a dark detail
    bool small;
};

void rect(const Paint& p, const Xf& xf, float x0, float y0, float x1, float y1, ImU32 col, float round = 0.0f) {
    p.dl->AddRectFilled(xf.at(x0, y0), xf.at(x1, y1), col, xf.len(round));
}

void poly(const Paint& p, const Xf& xf, std::initializer_list<ImVec2> units, ImU32 col) {
    ImVec2 pts[16];
    int n = 0;
    for (const ImVec2& u : units) {
        pts[n++] = xf.at(u.x, u.y);
    }
    p.dl->AddConvexPolyFilled(pts, n, col);
}

void line(const Paint& p, const Xf& xf, float x0, float y0, float x1, float y1, ImU32 col, float width) {
    p.dl->AddLine(xf.at(x0, y0), xf.at(x1, y1), col, std::max(1.0f, xf.len(width)));
}

// `n` white keys across (x0 .. x1, y0 .. y1) with dividers, and the black keys of the octave when
// `blacks` (after keys 1, 2, 4, 5 and 6 of seven; the pattern of two and three).
void keys(const Paint& p, const Xf& xf, float x0, float y0, float x1, float y1, int n, bool blacks) {
    rect(p, xf, x0, y0, x1, y1, p.ink, 2.0f);
    if (p.detail == 0) {
        return;
    }
    const float w = (x1 - x0) / static_cast<float>(n);
    for (int i = 1; i < n; ++i) {
        line(p, xf, x0 + w * static_cast<float>(i), y0 + 2.0f, x0 + w * static_cast<float>(i), y1 - 2.0f, p.detail, 2.2f);
    }
    if (blacks) {
        for (const int after : {1, 2, 4, 5, 6}) {
            if (after < n) {
                const float x = x0 + w * static_cast<float>(after);
                rect(p, xf, x - w * 0.32f, y0, x + w * 0.32f, y0 + (y1 - y0) * 0.6f, p.detail, 1.0f);
            }
        }
    }
}

// A grand piano seen from the side, its lid down.
void grand(const Paint& p, const Xf& xf) {
    poly(p, xf, {{20, 36}, {64, 27}, {84, 30}, {93, 41}, {89, 52}, {20, 52}}, p.ink); // the body, its curve
    rect(p, xf, 4, 50, 36, 58, p.ink, 1.5f);                                           // the keyboard shelf
    rect(p, xf, 10, 56, 16, 86, p.ink);                                                // the legs
    rect(p, xf, 78, 50, 84, 86, p.ink);
    rect(p, xf, 44, 56, 48, 76, p.ink);                                                // the pedal lyre
    rect(p, xf, 38, 76, 54, 80, p.ink, 1.0f);
    if (p.detail != 0) {
        line(p, xf, 24, 40, 82, 35, p.detail, 2.2f); // the lid's edge
        rect(p, xf, 6, 52, 34, 56, p.detail);        // the keys
    }
}

// A grand piano with its lid up and music on the stand.
void grand_open(const Paint& p, const Xf& xf) {
    const Xf low = xf.sub(1.0f, 0.0f, 10.0f);
    grand(p, low);
    poly(p, xf, {{26, 44}, {60, 10}, {92, 22}, {88, 44}}, p.ink); // the lid, raised
    line(p, xf, 54, 18, 48, 46, p.ink, 3.0f);                      // its prop
    rect(p, xf, 14, 22, 32, 40, p.ink, 1.5f);                      // the music on the stand
    if (p.detail != 0) {
        for (const float y : {27.0f, 31.0f, 35.0f}) {
            line(p, xf, 17, y, 29, y, p.detail, 1.6f);
        }
        line(p, xf, 34, 40, 86, 28, p.detail, 2.0f); // the lid's hinge side
    }
}

// An upright piano from the front: the body, the key shelf and legs; the keys cut into the body.
void upright(const Paint& p, const Xf& xf) {
    rect(p, xf, 12, 10, 88, 64, p.ink, 3.0f);
    rect(p, xf, 6, 62, 94, 70, p.ink, 1.5f);
    rect(p, xf, 14, 70, 24, 92, p.ink);
    rect(p, xf, 76, 70, 86, 92, p.ink);
    rect(p, xf, 46, 70, 54, 84, p.ink);
    if (p.detail != 0) {
        line(p, xf, 14, 36, 86, 36, p.detail, 2.4f); // the fall-board
        rect(p, xf, 18, 44, 82, 60, p.detail, 1.0f); // the keys
        if (!p.small) {
            for (int i = 1; i < 9; ++i) {
                const float x = 18.0f + 64.0f * static_cast<float>(i) / 9.0f;
                line(p, xf, x, 44, x, 60, p.light, 1.4f);
            }
        } else {
            for (const float x : {34.0f, 50.0f, 66.0f}) {
                line(p, xf, x, 44, x, 60, p.light, 2.0f);
            }
        }
    }
}

// Two branches of leaves round a ring, open at the top.
void wreath(const Paint& p, const Xf& xf) {
    const ImVec2 mid = xf.at(50.0f, 52.0f);
    const float radius = xf.len(40.0f);
    const int leaves = p.small ? 5 : 7;
    for (const float side : {-1.0f, 1.0f}) {
        for (int i = 0; i < leaves; ++i) {
            const float a = (kPi * 0.5f + 0.14f) + static_cast<float>(i) * ((kPi - 0.55f) / static_cast<float>(leaves - 1));
            const float ax = side * std::cos(a);
            const float ay = std::sin(a);
            const ImVec2 c{mid.x + ax * radius, mid.y + ay * radius};
            // A leaf: a pointed lozenge along the ring, going up it and leaning a little outward.
            const float len = xf.len(p.small ? 14.0f : 12.0f);
            const float wid = xf.len(p.small ? 6.0f : 5.0f);
            float ux = -side * std::sin(a) + ax * 0.35f; // the ring's direction as the angle rises
            float uy = std::cos(a) + ay * 0.35f;
            const float ul = std::sqrt(ux * ux + uy * uy);
            ux /= ul;
            uy /= ul;
            const ImVec2 tip{c.x + ux * len * 0.5f, c.y + uy * len * 0.5f};
            const ImVec2 base{c.x - ux * len * 0.5f, c.y - uy * len * 0.5f};
            const ImVec2 left{c.x - uy * wid * 0.5f, c.y + ux * wid * 0.5f};
            const ImVec2 right{c.x + uy * wid * 0.5f, c.y - ux * wid * 0.5f};
            const ImVec2 pts[4] = {base, left, tip, right};
            p.dl->AddConvexPolyFilled(pts, 4, p.ink);
        }
    }
    // The stem the leaves sit on.
    p.dl->PathArcTo(mid, radius * 0.93f, kPi * 0.5f + 0.05f, kPi * 1.5f - 0.35f);
    p.dl->PathStroke(p.ink, 0, std::max(1.0f, xf.len(1.6f)));
    p.dl->PathArcTo(mid, radius * 0.93f, -kPi * 0.5f + 0.35f, kPi * 0.5f - 0.05f);
    p.dl->PathStroke(p.ink, 0, std::max(1.0f, xf.len(1.6f)));
}

// A music-font glyph centred on `centre` with its ink `ink_h` tall.
void music_glyph(const Kit& k, const Paint& p, ImVec2 centre, ImWchar cp, float ink_h) {
    draw_music_glyph(k, p.dl, centre, cp, ink_h, p.ink);
}

// The emblem of `title` in the box `xf`.
void emblem(const Kit& k, const Paint& p, const Xf& xf, int title) {
    switch (title) {
    case 1: // Beginner: a single quarter note
        music_glyph(k, p, xf.at(50, 50), 0x2669, xf.len(78.0f));
        break;
    case 2: // Student: two beamed eighth notes
        music_glyph(k, p, xf.at(50, 50), 0x266B, xf.len(78.0f));
        break;
    case 3: // Performer: an octave of piano keys
        keys(p, xf, 3, 24, 97, 76, 7, !p.small);
        break;
    case 4: // Soloist: a treble clef over a few keys
        music_glyph(k, p, xf.at(50, 36), 0x1D11E, xf.len(68.0f));
        keys(p, xf, 18, 72, 82, 94, p.small ? 3 : 4, false);
        break;
    case 5: // Pianist: an upright piano
        upright(p, xf);
        break;
    case 6: // Concert Pianist: a grand piano
        grand(p, xf);
        break;
    case 7: // Maestro: a grand with its lid up and music on the stand
        grand_open(p, xf);
        break;
    default: // Virtuoso: a grand inside a laurel wreath
        wreath(p, xf);
        grand(p, xf.sub(0.5f, 0.0f, 4.0f));
        break;
    }
}

// The emblem's colours on each tier's face, chosen for contrast on that material (they are not the
// badge icons' `medal_look`): the fill, the light edge (up and left), the dark edge (down and right)
// and the colour its details are cut in, the opposite of the fill.
struct EmblemInk {
    std::uint32_t fill;
    std::uint32_t light;
    std::uint32_t dark;
    std::uint32_t cut;
};

EmblemInk emblem_ink(int tier) {
    switch (tier) {
    case 1: // wood: dark brown on the pale grain
        return {0x2E190A, 0xE6BC84, 0x120904, 0xE6BC84};
    case 2: // iron: light steel on the dark forging
        return {0xD2D9E6, 0xFFFFFF, 0x000000, 0x15171C};
    case 3: // bronze: dark brown on the warm face
        return {0x3E1D08, 0xF7C896, 0x1E0D03, 0xF1B77C};
    case 4: // silver: slate on the bright face
        return {0x363C4A, 0xFFFFFF, 0x13161D, 0xEEF1F7};
    case 5: // gold: umber on the gold
        return {0x5E3504, 0xFFF3B8, 0x2F1B01, 0xFFE7A0};
    case 6: // platinum: deep blue on the pale blue
        return {0x24396E, 0xFFFFFF, 0x101B3A, 0xE4ECFF};
    case 7: // diamond: deep teal on the ice
        return {0x0B4468, 0xFFFFFF, 0x04202F, 0xDDF5FF};
    default: // obsidian: gold on the violet glass
        return {0xF2CC5A, 0xFFF6C8, 0x120A26, 0x1A0F33};
    }
}

ImU32 rgba(std::uint32_t c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32({static_cast<float>((c >> 16) & 0xFF) / 255.0f,
                                           static_cast<float>((c >> 8) & 0xFF) / 255.0f,
                                           static_cast<float>(c & 0xFF) / 255.0f, alpha});
}

} // namespace

void draw_title_medal(const Kit& k, ImDrawList* dl, int title, ImVec2 centre, float d, bool large) {
    const int tier = std::clamp(title, 1, etude::kTierCount);
    const etude::Material material = etude::tier_material(tier);
    if (k.medals != nullptr) {
        k.medals->draw(dl, material, large, centre, d);
    }
    const EmblemInk ink = emblem_ink(tier);
    const bool small = d < 44.0f;
    const float box = d * (small ? kEmblemShareSmall : kEmblemShare);
    const float off = std::max(1.0f, d * 0.5f * 0.69f * 0.07f);
    const auto at = [&](float dx, float dy) { return Xf{{centre.x + dx, centre.y + dy}, box / 100.0f}; };
    // The two edges, silhouette only; then the fill with its details cut in.
    const Paint dark{dl, rgba(ink.dark, 1.0f), 0, 0, small};
    emblem(k, dark, at(off, off), tier);
    const Paint light{dl, rgba(ink.light, 1.0f), 0, 0, small};
    emblem(k, light, at(-off * 0.7f, -off * 0.7f), tier);
    const Paint fill{dl, rgba(ink.fill, 1.0f), rgba(ink.cut, 0.95f), rgba(ink.fill, 1.0f), small};
    emblem(k, fill, at(0.0f, 0.0f), tier);
}

} // namespace etude::app::ui
