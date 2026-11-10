// SPDX-License-Identifier: GPL-3.0-or-later
// The free-play scale overlay's part of the keyboard (core scale_overlay.hpp): the dimmed faces of
// the keys outside the scale, the root keys' bar and name, and the fingers. Everything is
// read from the mask the caller worked out once; a frame draws from cached text and allocates
// nothing.
#include "scene.hpp"

#include "scene_internal.hpp"
#include "scene_scale_layout.hpp"

#include "etude/learn_colours.hpp"
#include "etude/screen_layout.hpp"

#include <algorithm>
#include <cstdio>

namespace etude::app {

using namespace scene_detail;

namespace {

// A white key outside the scale goes this far toward a cool grey, a black one toward the dark.
const SDL_FColor kDimWhite = rgba(132, 138, 158);
constexpr float kDimWhiteShare = 0.62f;
const SDL_FColor kDimBlack = rgba(86, 92, 114);
constexpr float kDimBlackShare = 0.7f;
// The root's name and the fingers, in key widths (clamped to what a key can hold).
constexpr float kRootNameShare = 0.62f;
constexpr float kFingerShare = 0.62f;

} // namespace

SDL_FColor Scene::dimmed_key(SDL_FColor face, bool black) const {
    return black ? mix(face, with_alpha(kDimBlack, face.a), kDimBlackShare)
                 : mix(face, with_alpha(kDimWhite, face.a), kDimWhiteShare);
}

void Scene::add_unlit_white(Mesh& out, bool dim, const SDL_FRect& face, float radius) {
    out.rounded_rect(
        face, radius, dim ? dimmed_key(look_.key_white_top, false) : look_.key_white_top,
        dim ? dimmed_key(look_.key_white_bottom, false) : look_.key_white_bottom, 0.8f * unit_);
}

void Scene::add_unlit_black(Mesh& out, bool dim, const SDL_FRect& face, float radius,
                            const SDL_FRect& lip, float lip_radius) {
    out.rounded_rect(
        face, radius, dim ? dimmed_key(look_.key_black_top, true) : look_.key_black_top,
        dim ? dimmed_key(look_.key_black_bottom, true) : look_.key_black_bottom, 0.8f * unit_);
    out.rounded_rect(lip, lip_radius, with_alpha(look_.key_black_lip, 0.0f),
                     dim ? dimmed_key(look_.key_black_lip, true) : look_.key_black_lip, 0.0f);
}

void Scene::draw_scale_marks(float top, float bottom, float black_length) {
    if (scale_ == nullptr || !scale_->on) {
        return;
    }
    const float root_px = std::min(kRootNameShare * white_width_, 26.0f * unit_);
    const float want = std::min(kFingerShare * white_width_, 24.0f * unit_);
    const AccidentalKind sign = scale_->root_accidental > 0   ? AccidentalKind::Sharp
                                : scale_->root_accidental < 0 ? AccidentalKind::Flat
                                                              : AccidentalKind::None;
    const char letter[2] = {scale_->root_letter, '\0'};
    const auto marks_of = [&](int note, const KeyPx& k) {
        ScaleMarkKey m;
        m.black = k.black;
        m.x = k.x;
        m.width = k.width;
        m.top = top;
        m.bottom = bottom;
        m.black_length = black_length;
        m.unit = unit_;
        m.label_px = k.black ? black_name_px_ : std::max(white_name_px_, c_marker_px_);
        m.root = scale_->is_root(note);
        m.has_marker = has_c_marker(note);
        m.root_px = root_px;
        m.finger_px = scale_->fingers_shown && scale_->finger[static_cast<std::size_t>(note)] != 0
                          ? std::max(want, m.label_px)
                          : 0.0f;
        return scale_marks_layout(m);
    };
    // Bars first: the mesh is drawn before any text, so a name stays over its bar.
    mesh_.clear();
    for (int note = range_.lowest; note <= range_.highest; ++note) {
        const KeyPx& k = keys_[static_cast<std::size_t>(note)];
        if (!k.in_range || !scale_->is_root(note)) {
            continue;
        }
        const float bar = std::max(2.0f * unit_, scale_marks_tok::kBarHeight * k.width);
        // The Scales card's colour (the note's own under Colour by: Note), not the accent blue the
        // right hand's badges share: one meaning per colour (core learn_colours.hpp).
        const SDL_FColor tint = to_fcolor(learn_lit_colour(settings_, learn_section_colour(settings_, LearnSection::Scales), note, true, k.black));
        mesh_.rounded_rect({k.x + 0.15f * k.width, top + 0.5f * unit_, 0.7f * k.width, bar},
                           0.5f * bar, tint, tint, 0.0f);
    }
    mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
    mesh_.clear();
    for (int note = range_.lowest; note <= range_.highest; ++note) {
        const std::size_t i = static_cast<std::size_t>(note);
        const KeyPx& k = keys_[i];
        if (!k.in_range) {
            continue;
        }
        const ScaleMarkBoxes marks = marks_of(note, k);
        if (marks.name_shown) {
            const SDL_FColor ink = k.black ? look_.key_label_black : look_.c_marker_middle;
            TextSlot* slot = name_slot(letter, root_px, true);
            if (slot != nullptr) {
                const float glyph_h = accidental_glyph_height_px(name_metrics_.cap_em * root_px);
                const float glyph_w =
                    sign == AccidentalKind::None ? 0.0f : accidental_glyph_width_px(sign, glyph_h);
                const float left = std::round(marks.name.cx() - 0.5f * (slot->width + glyph_w));
                const float y = marks.name.y1 - slot->height;
                text_.draw(*slot, left, y, Align::Left, ink);
                if (sign != AccidentalKind::None) {
                    add_glyph(mesh_, accidental_shape(sign, glyph_h), left + slot->width,
                              y + std::round(name_metrics_.cap_top_em * root_px), 0.0f, ink);
                }
            }
        }
        if (marks.finger_shown) {
            const char digit[2] = {static_cast<char>('0' + scale_->finger[i]), '\0'};
            // The text's lower edge sits a little under the digit's baseline.
            draw_name(digit, marks.finger_px, marks.finger.cx(),
                      marks.finger.cy() + 0.56f * marks.finger_px,
                      k.black ? kWhite : rgba(18, 82, 150), true);
        }
    }
    mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
}

} // namespace etude::app
