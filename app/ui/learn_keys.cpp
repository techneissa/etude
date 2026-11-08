// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn screen's drawn parts that are not controls (learn_draw.hpp).
#include "learn_draw.hpp"
#include "learn_melodies_art.hpp"

#include "icons.hpp"
#include "key_ring.hpp"
#include "note_name.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace etude::app::ui {

namespace {

// A name's line is this many times its type tall, so the capitals sit in the middle of it.
constexpr float kNameLineEm = 1.6f;
// The most a name is, in pixels: the font's ink is measured true up to about this (a larger letter
// reads a pixel off centre to the audit).
constexpr float kNameMaxPx = 60.0f;

ImVec4 vec(RGBA c) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f};
}

const LookSettings& default_look() {
    static const LookSettings look;
    return look;
}

// The colour of a key of the diagram: unlit as it is, or lit for the card's scale or chord (the
// root strong, the rest lighter) or in the note's own colour (core learn_lit_colour).
ImVec4 key_tint(const DiagramKey& key, const LearnKeyPaint& paint) {
    if (!key.in_scale) {
        return key.black ? learn_col::kBlackOut : learn_col::kWhiteOut;
    }
    const LookSettings& look = paint.look != nullptr ? *paint.look : default_look();
    return vec(learn_lit_colour(look, paint.card, key.note, key.root, key.black));
}

// The key's colours while Listen sounds it: the right hand's, the left's, or both.
struct Sounding {
    bool right = false;
    bool left = false;
    ImVec4 right_colour{};
    ImVec4 left_colour{};
};

Sounding sounding_key(std::size_t index, const LearnLit* lit, const LookSettings& look) {
    Sounding s;
    if (lit == nullptr || index >= kLearnMaxKeys) {
        return s;
    }
    if (lit->right[index] >= 0) {
        s.right = true;
        s.right_colour = vec(learn_sounding_colour(look, lit->right[index], Hand::Right));
    }
    if (lit->left[index] >= 0) {
        s.left = true;
        s.left_colour = vec(learn_sounding_colour(look, lit->left[index], Hand::Left));
    }
    return s;
}

// G major, one octave: the Scales tile.
const ScaleDiagram& scale_preview() {
    static const ScaleDiagram d = scale_diagram(7, ScaleType::Major);
    return d;
}

// The C major chord on the same keys: the Chords tile.
const ScaleDiagram& chord_preview() {
    static const ScaleDiagram d = [] {
        ScaleDiagram c = scale_diagram(0, ScaleType::Major);
        for (DiagramKey& key : c.keys) {
            key.in_scale = key.note == 60 || key.note == 64 || key.note == 67 || key.note == 72;
            key.root = key.note == 60;
        }
        return c;
    }();
    return d;
}

// A fifth on the same keys (C, then G): the Ear tile's two notes to tell apart.
const ScaleDiagram& ear_preview() {
    static const ScaleDiagram d = [] {
        ScaleDiagram c = scale_diagram(0, ScaleType::Major);
        for (DiagramKey& key : c.keys) {
            key.in_scale = key.note == 60 || key.note == 67;
            key.root = key.note == 60;
        }
        return c;
    }();
    return d;
}

void draw_keys_picture(const Kit& k, ImDrawList* dl, const Box& in, const ScaleDiagram& d,
                       const LearnKeyPaint& paint) {
    // The keys keep their natural proportion: as tall as the room, and no wider than that makes the
    // keyboard (centred), never stretched tall to fill a tall box.
    const float whites = static_cast<float>(d.white_keys);
    const float key_h = std::min(in.h(), in.w() / whites * learn_tok::kPreviewKeyAspect);
    const float width = key_h / learn_tok::kPreviewKeyAspect * whites;
    const float left = in.x0 + (in.w() - width) * 0.5f;
    const float top = in.y0 + (in.h() - key_h) * 0.5f;
    learn_draw_keys(k, dl, {left, top, left + width, top + key_h}, d, false, paint);
}

} // namespace

namespace {

// One key's face: its tint, or while Listen sounds it the hand's colour (a split in two for both
// hands, the left hand's half on the left).
void paint_key(ImDrawList* dl, const Box& b, float right_edge, float radius, ImVec4 tint,
               const Sounding& sound, bool black) {
    const ImDrawFlags flags = ImDrawFlags_RoundCornersBottom;
    if (!sound.right && !sound.left) {
        dl->AddRectFilled({b.x0, b.y0}, {right_edge, b.y1}, u32(tint), radius, flags);
        return;
    }
    // The tint under, so a sounding key is still seen to be in the scale (a black key's edge too).
    dl->AddRectFilled({b.x0, b.y0}, {right_edge, b.y1}, u32(tint), radius, flags);
    const float mid = (b.x0 + right_edge) * 0.5f;
    if (sound.left && sound.right) {
        dl->AddRectFilled({b.x0, b.y0}, {mid, b.y1}, u32(sound.left_colour), radius,
                          ImDrawFlags_RoundCornersBottomLeft);
        dl->AddRectFilled({mid, b.y0}, {right_edge, b.y1}, u32(sound.right_colour), radius,
                          ImDrawFlags_RoundCornersBottomRight);
    } else {
        dl->AddRectFilled({b.x0, b.y0}, {right_edge, b.y1},
                          u32(sound.right ? sound.right_colour : sound.left_colour), radius, flags);
    }
    (void)black;
}

// The note names inside the lit keys: bold, about `kKeyNameShare` of a white key across or the
// largest that fits the key with a pad each side, in the ink that reads on the key's colour.
void draw_key_names(const Kit& k, ImDrawList* dl, const Box& keyboard, const ScaleDiagram& d,
                    const std::array<ImVec4, 64>& shown) {
    const int whites = std::max(d.white_keys, 1);
    const float white_w = keyboard.w() / static_cast<float>(whites);
    const float want = std::min(white_w * learn_tok::kKeyNameShare, kNameMaxPx);
    const float pad = k.s(tok::kS1);
    const float foot = k.s(tok::kS2);
    for (std::size_t i = 0; i < d.keys.size() && i < shown.size(); ++i) {
        const DiagramKey& key = d.keys[i];
        if (!key.in_scale || key.name[0] == '\0') {
            continue;
        }
        const Box b = learn_key_box(keyboard, key, whites);
        const float across = note_name_width(k.bold, want, key.name.data());
        const float room = std::max(0.0f, b.w() - 2.0f * pad);
        // Whole pixels, and a slot on whole pixels, so the letters' cap band sits on the pixel grid.
        const float size =
            std::floor(across > room && across > 0.0f ? want * room / across : want);
        const float name_h = std::round(size * kNameLineEm);
        const float bottom = std::round(b.y1 - foot);
        const Box slot{b.x0, bottom - name_h, b.x1, bottom};
        const ImVec4 fill = shown[i];
        const RGBA ink = label_color_for(
            {static_cast<std::uint8_t>(fill.x * 255.0f + 0.5f),
             static_cast<std::uint8_t>(fill.y * 255.0f + 0.5f),
             static_cast<std::uint8_t>(fill.z * 255.0f + 0.5f), 255});
        note_name_at(k, dl, k.bold, size, slot, vec(ink), key.name.data());
    }
}

} // namespace

void learn_draw_keys(const Kit& k, ImDrawList* dl, const Box& keyboard, const ScaleDiagram& d,
                     bool names, const LearnKeyPaint& paint, const LearnLit* lit) {
    const int whites = std::max(d.white_keys, 1);
    const float hair = k.s(tok::kHairline);
    const float radius = k.s(tok::kInset);
    const LookSettings& look = paint.look != nullptr ? *paint.look : default_look();
    // What each key shows, for the names' ink: the sounding hand's colour, else the tint.
    std::array<ImVec4, 64> shown{};
    for (int pass = 0; pass < 2; ++pass) { // the white keys, then the black over them
        for (std::size_t i = 0; i < d.keys.size(); ++i) {
            const DiagramKey& key = d.keys[i];
            if (key.black != (pass == 1)) {
                continue;
            }
            const Box b = learn_key_box(keyboard, key, whites);
            const ImVec4 tint = key_tint(key, paint);
            const Sounding sound = sounding_key(i, lit, look);
            paint_key(dl, b, key.black ? b.x1 : b.x1 - hair, radius, tint, sound, key.black);
            if (key.black) {
                dl->AddRect({b.x0, b.y0}, {b.x1, b.y1}, u32(learn_col::kBlackLine), radius,
                            ImDrawFlags_RoundCornersBottom, hair);
            }
            if (i < shown.size()) {
                shown[i] = sound.right ? sound.right_colour : sound.left ? sound.left_colour : tint;
            }
        }
    }
    if (names) {
        draw_key_names(k, dl, keyboard, d, shown);
    }
}

void learn_draw_badge(const Kit& k, ImDrawList* dl, const Box& box, int finger, ImVec4 colour,
                      float alpha, float pulse) {
    if (finger < 1 || finger > 9) {
        return;
    }
    // While its note sounds the badge swells, and a ring of its colour goes out from it.
    const float swell = 1.0f + learn_tok::kBadgePulse * pulse;
    const float radius = box.w() * 0.5f * swell;
    if (pulse > 0.0f) {
        dl->AddCircle({box.cx(), box.cy()}, radius + k.s(tok::kS1) * pulse,
                      u32(with_alpha(colour, alpha * pulse)), 24, k.s(tok::kFocusRing));
    }
    dl->AddCircleFilled({box.cx(), box.cy()}, radius, u32(with_alpha(colour, alpha)), 24);
    const char number[2] = {static_cast<char>('0' + finger), '\0'};
    label_at(k, dl, k.bold, box.w() * swell * learn_tok::kBadgeLabelShare, box, LabelAlign::Centre,
             with_alpha(col::kAccentInk, alpha), number);
}

void learn_draw_scale_name(const Kit& k, ImDrawList* dl, const Box& slot, int root_pc,
                           ScaleType type, float size_pt, bool centre) {
    char name[8];
    key_ring_key_name(root_pc, type, name, sizeof name);
    const float size = k.s(size_pt);
    const float across = note_name_width(k.bold, size, name);
    const float gap = k.s(tok::kS2);
    FitText word;
    const char* full = scale_type_name(type);
    word.format("%c%s", std::tolower(static_cast<unsigned char>(full[0])), full + 1);
    // The words take what the name leaves, shrunk to fit; centred, the pair is centred across the slot.
    const FitText shown = fit(k.bold, size, word.view(), std::max(0.0f, slot.w() - across - gap));
    const float words_w = text_size(k.bold, size, shown.c_str()).x;
    const float x0 = centre ? slot.cx() - (across + gap + words_w) * 0.5f : slot.x0;
    note_name_at(k, dl, k.bold, size, {x0, slot.y0, x0 + across, slot.y1}, col::kText, name);
    label_at(k, dl, k.bold, size, {x0 + across + gap, slot.y0, slot.x1, slot.y1}, LabelAlign::Left,
             col::kText, shown.c_str());
}

float learn_chip_need_pt(const Kit& k, float unit, int count, const char* (*word)(int)) {
    float widest = 0.0f;
    for (int i = 0; i < count; ++i) {
        widest = std::max(widest, text_size(k.bold, tok::kBody * unit, word(i)).x / unit);
    }
    return widest + 2.0f * tok::kS1 + 1.0f;
}

void learn_draw_preview(Kit& k, ImDrawList* dl, const Box& box, int card, const LookSettings& look) {
    if (!(box.h() > 0.0f)) {
        return; // the tile is too short for a picture: its title row alone
    }
    // The keys are drawn straight on the tile: no frame behind them.
    const Box& in = box;
    switch (card) {
    case 0:
        draw_keys_picture(k, dl, in, scale_preview(), {&look, learn_section_colour(look, LearnSection::Scales)});
        break;
    case 1:
        draw_keys_picture(k, dl, in, chord_preview(), {&look, learn_section_colour(look, LearnSection::Chords)});
        break;
    case 2:
        draw_keys_picture(k, dl, in, ear_preview(), {&look, learn_section_colour(look, LearnSection::Ear)});
        break;
    default:
        melody_draw_picture(k, dl, in, learn_section_colour(look, LearnSection::Melodies));
        break;
    }
}

LearnTabs learn_measure_tabs(const Kit& k, int selected) {
    LearnTabs tabs;
    tabs.selected = selected;
    for (std::size_t i = 0; i < kLearnTabs; ++i) {
        const LearnTabInfo& info = kLearnTabInfo[i];
        tabs.need[i] = chip_width(k, info.icon, info.label) / k.u;
    }
    return tabs;
}

} // namespace etude::app::ui
