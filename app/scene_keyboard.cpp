// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene.hpp"

#include "scene_internal.hpp"

#include "frame_log.hpp"
#include "etude/beat_lines.hpp"
#include "etude/grade_palette.hpp"
#include "etude/grade_words.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

namespace etude::app {

using namespace scene_detail;

namespace {

// A wrong press flashes its key red (kWrongRed), with this much glow at full strength.
constexpr float kWrongGlow = 0.7f;

} // namespace

// The glow of a lit key on the bed under the note's lane, where it may bleed up onto the notes' end
// but never onto the key faces.
void Scene::add_key_glow(int note, const NotePaint& p, float glow_size) {
    glow_size *= p.glow_reach;
    const RectF& bed = layout_.key_bed;
    const BlockColumn& c = cols_[static_cast<std::size_t>(note)];
    if (p.glow > 0.0f && glow_size > 0.0f) {
        const float h = 0.6f * bed.h;
        glow_.nine_slice({c.lane_x - glow_size, bed.y + 0.2f * bed.h - glow_size,
                          c.lane_w + 2.0f * glow_size, h + 2.0f * glow_size},
                         glow_size + 0.3f * bed.h,
                         with_alpha(saturated(p.base, recipe_.saturation), p.glow));
    }
}

// A lit key's overlay: a fresh press washes its face toward white (Key flash), and the key tips
// down: a darker band along its top, as tall as the bed's shadow and then some, which fades while
// the key flashes.
void Scene::add_key_tip(float x, float width, float top, float bottom, float tip_h,
                        const NotePaint& p) {
    if (p.wash > 0.0f) {
        overlay_.rect({x, top, width, bottom - top}, with_alpha(kWhite, 0.85f * p.wash),
                      with_alpha(kWhite, 0.6f * p.wash));
    }
    const SDL_FColor dark = with_alpha(scaled(p.base, look_.pressed_band_darken),
                                       look_.pressed_band_alpha * (1.0f - p.wash));
    overlay_.rect({x, top, width, tip_h}, dark, with_alpha(dark, 0.0f));
}

void Scene::draw_keyboard(const FrameInput& in) {
    const RectF& area = layout_.key_area;
    const RectF& bed = layout_.key_bed;
    if (area.empty()) {
        return;
    }
    // `glow_` still holds the blocks' glow from draw_notes; the keys' glow joins it, and the lot
    // is drawn once more over the bed at the end.
    const float top = area.y;
    const float bottom = area.bottom() - 1.5f * unit_;
    const float gap = std::max(1.0f, 1.6f * unit_);
    const float white_radius = 0.12f * white_width_;
    const float black_length = area.h * kBlackKeyLengthShare;
    const float glow_size = 0.7f * white_width_ * budget_.glow_width;
    const float tip_h = std::min(1.6f * layout_.key_shadow_h, area.h);
    // A see-through style shows the dark background through a lit key, as through its block,
    // tinted with the note's colour (core key_underlay_color) so the key still reads as lit.
    const bool see_through = style_is_see_through(recipe_);

    // A lit key is painted like its note's block, by the same painter and recipe: the same
    // colour, style, flash and glow.
    struct Lit {
        bool on = false;
        int velocity = 0;
        NotePaint paint;
    };
    const auto lit_paint = [&](int note) {
        const std::size_t i = static_cast<std::size_t>(note);
        const float flash = flash_at(in.now_ns, in.last_press_ns[i]);
        const bool held = in.keys.is_down(note);
        Lit lit;
        lit.on = held || flash > 0.0f;
        lit.velocity =
            held ? in.keys.press_velocity(note).value_or(in.last_velocity[i]) : in.last_velocity[i];
        lit.paint = paint(note, lit.velocity, held, flash);
        if (in.replay_keys != nullptr && (*in.replay_keys)[i].on && !(*in.replay_keys)[i].wrong) {
            // A replay's pressed key: lit like a held one, in the hand of its note.
            const ReplayLitKey& r = (*in.replay_keys)[i];
            lit.on = true;
            lit.velocity = r.velocity;
            lit.paint = paint(note, r.velocity, true, 0.0f, r.hand);
        }
        if (in.wrong_flash != nullptr && (*in.wrong_flash)[i] > 0.0f) {
            // A wrong press: the key goes red for a moment, at the hit line only.
            const float w = (*in.wrong_flash)[i];
            lit.on = true;
            lit.paint.base = mix(lit.paint.base, with_alpha(kWrongRed, lit.paint.base.a), w);
            lit.paint.lower = mix(lit.paint.lower, with_alpha(kWrongRed, lit.paint.lower.a), w);
            lit.paint.glow = std::max(lit.paint.glow, kWrongGlow * w);
        }
        if (!lit.on && in.hint_hands != nullptr && (*in.hint_hands)[i] != 0) {
            // Wait Mode: a key to press pulses in its hand's colour.
            const Hand hand = static_cast<Hand>((*in.hint_hands)[i] - 1);
            const double t = static_cast<double>(in.now_ns - epoch_ns_) * 1e-9;
            const float pulse =
                0.5f + 0.5f * static_cast<float>(std::sin(t * 2.0 * 3.14159265 * 1.2));
            lit.on = true;
            lit.velocity = 80;
            lit.paint = paint(note, lit.velocity, false, 0.15f + 0.25f * pulse, hand);
            lit.paint.glow = std::max(lit.paint.glow, 0.45f + 0.45f * pulse);
        }
        return lit;
    };
    const auto underlay_for = [&](const NotePaint& p) {
        return with_alpha(mix(look_.background_bottom, p.base, kKeyUnderlayTint), 1.0f);
    };
    const auto add_glow = [&](int note, const NotePaint& p) { add_key_glow(note, p, glow_size); };
    // Keys draw only inside the key area, so nothing crosses the bed.
    const SDL_Rect key_clip = pixel_rect(area);
    gfx::set_clip(renderer_, &key_clip);

    // White keys first so black keys draw on top. Every key starts at the top of the key area (the
    // rounded corners sit above it and are clipped away), so the keys begin where the bed ends.
    // Each pass: unlit faces, then the lit faces in the note style, then the bands over them.
    mesh_.clear();
    overlay_.clear();
    painter_.begin(recipe_, unit_);
    for (int note = range_.lowest; note <= range_.highest; ++note) {
        const KeyPx& k = keys_[static_cast<std::size_t>(note)];
        if (k.black || !k.in_range) {
            continue;
        }
        const Lit lit = lit_paint(note);
        const SDL_FRect face{k.x + gap * 0.5f, top - white_radius, k.width - gap,
                             bottom - top + white_radius};
        if (!lit.on) {
            add_unlit_white(note, face, white_radius);
            continue;
        }
        painter_.add(face, white_radius, lit.paint.base, lit.paint.lower, 1.0f,
                     see_through ? underlay_for(lit.paint) : SDL_FColor{0, 0, 0, 0});
        add_key_tip(face.x, face.w, top, bottom, tip_h, lit.paint);
        add_glow(note, lit.paint);
    }
    mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
    painter_.draw(renderer_, textures_);
    overlay_.draw(renderer_, SDL_BLENDMODE_BLEND);

    // A lit black key is painted like its block, in the style's fuller version.
    mesh_.clear();
    overlay_.clear();
    black_painter_.begin(black_recipe_, unit_);
    for (int note = range_.lowest; note <= range_.highest; ++note) {
        const KeyPx& k = keys_[static_cast<std::size_t>(note)];
        if (!k.black || !k.in_range) {
            continue;
        }
        const Lit lit = lit_paint(note);
        const float radius = 0.16f * k.width;
        const SDL_FRect face{k.x, top - radius, k.width, black_length + radius};
        // A lighter lip near the front edge gives the key some depth.
        const float lip = 0.07f * black_length;
        const SDL_FRect lip_rect{k.x + 0.12f * k.width, top + black_length - 1.6f * lip,
                                 k.width * 0.76f, lip};
        if (!lit.on) {
            add_unlit_black(note, face, radius, lip_rect, 0.5f * lip);
            continue;
        }
        black_painter_.add(face, radius, lit.paint.base, lit.paint.lower, 1.0f,
                           see_through ? underlay_for(lit.paint) : SDL_FColor{0, 0, 0, 0});
        const SDL_FColor lip_colour = with_alpha(mix(lit.paint.base, kWhite, 0.25f), 0.6f);
        overlay_.rounded_rect(lip_rect, 0.5f * lip, with_alpha(lip_colour, 0.0f), lip_colour, 0.0f);
        add_key_tip(k.x, k.width, top, bottom, tip_h, lit.paint);
        add_glow(note, lit.paint);
    }
    // The bed's shadow on the top of every key, so they look like they slide under it.
    const SDL_FColor shadow = with_alpha(kBlack, look_.shadow_alpha);
    overlay_.rect({0.0f, top, width_, layout_.key_shadow_h}, shadow, with_alpha(shadow, 0.0f));
    mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
    black_painter_.draw(renderer_, textures_);
    overlay_.draw(renderer_, SDL_BLENDMODE_BLEND);

    // Names on the keys, near the front edge of each. A lit key takes the label colour that reads
    // on its note colour, an unlit one a fixed colour for its key colour. A C key shows its marker
    // (name and octave, middle C bold and strong, the others dim) instead of its plain name.
    if (layout_.key_labels_visible || c_marker_px_ > 0.0f) {
        mesh_.clear();
        for (int note = range_.lowest; note <= range_.highest; ++note) {
            const std::size_t i = static_cast<std::size_t>(note);
            const KeyPx& k = keys_[i];
            if (!k.in_range || names_root_key(note)) {
                continue; // the overlay names a root key itself (draw_scale_marks)
            }
            const float cx = k.x + 0.5f * k.width;
            const float label_bottom = k.black ? top + 0.88f * black_length : bottom - 4.0f * unit_;
            if (c_marker_px_ > 0.0f && !c_names_[i].empty()) {
                if (c_stale_[i]) {
                    continue; // the marker the settings now ask for is not ready: none, not the old
                }
                const bool middle = is_middle_c(note);
                const Lit lit = lit_paint(note);
                SDL_FColor colour = middle ? look_.c_marker_middle : look_.c_marker_other;
                if (lit.on) {
                    colour = key_label_colour_for(note, lit.velocity);
                    colour.a = middle ? 1.0f : look_.c_marker_lit_alpha;
                }
                const SDL_FPoint extent =
                    draw_name(c_names_[i], c_marker_px_, cx, label_bottom, colour, middle);
                if (middle && extent.x > 0.0f) {
                    // An accent underline, so middle C is found at a glance.
                    const float h = std::max(1.5f, look_.c_underline_height * c_marker_px_);
                    const float w = look_.c_underline_width * extent.x;
                    const SDL_FColor accent = lit.on ? colour : look_.accent;
                    mesh_.rounded_rect(
                        {cx - 0.5f * w, label_bottom - 0.05f * c_marker_px_ - h, w, h}, 0.5f * h,
                        accent, accent, 0.0f);
                }
                continue;
            }
            const float size = k.black ? black_name_px_ : white_name_px_;
            if (!layout_.key_labels_visible || size <= 0.0f || names_[i].empty() ||
                name_stale_[i] || c_stale_[i]) {
                continue;
            }
            const Lit lit = lit_paint(note);
            SDL_FColor colour = k.black ? look_.key_label_black : look_.key_label_white;
            if (lit.on) {
                colour = key_label_colour_for(note, lit.velocity);
            }
            draw_key_name(note, size, cx, label_bottom, colour);
        }
        mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
    }
    draw_scale_marks(top, bottom, black_length);
    // The computer-keyboard letters as small keycaps, white keys at their middle and black keys on
    // their face, so they never meet the note names near the front edge.
    if (in.typing_labels != nullptr) {
        const float cap_px = std::min(0.46f * white_width_, 24.0f * unit_);
        if (cap_px >= kMinLabelFontPx) {
            std::vector<TypingCap>& caps = typing_caps_;
            caps.clear();
            caps.reserve(128);
            for (int note = range_.lowest; note <= range_.highest; ++note) {
                const std::size_t i = static_cast<std::size_t>(note);
                const KeyPx& k = keys_[i];
                if (!k.in_range || (*in.typing_labels)[i].empty()) {
                    continue;
                }
                const float cy =
                    k.black ? top + 0.40f * black_length : top + 0.60f * (bottom - top);
                caps.push_back({note, k.x + 0.5f * k.width, cy, k.black});
            }
            const float side = 1.55f * cap_px;
            mesh_.clear();
            for (const TypingCap& c : caps) {
                const SDL_FColor fill =
                    c.black ? rgba(214, 220, 232, 0.92f) : rgba(36, 40, 54, 0.88f);
                mesh_.rounded_rect({c.cx - 0.5f * side, c.cy - 0.5f * side, side, side},
                                   0.22f * side, fill, fill, 0.8f * unit_);
            }
            mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
            for (const TypingCap& c : caps) {
                const std::string text((*in.typing_labels)[static_cast<std::size_t>(c.note)]);
                const SDL_FColor ink = c.black ? rgba(20, 24, 34) : rgba(240, 244, 255);
                // draw_name sets the text's lower edge at `bottom`: centre the glyphs on the cap.
                draw_name(text, cap_px, c.cx, c.cy + 0.5f * cap_px, ink, true);
            }
        }
    }
    gfx::clear_clip(renderer_);

    // The key bed: the strip the notes land on. It ends exactly where the keys begin, with a thin
    // felt line on its lower edge and the accent hit line on its top edge.
    mesh_.clear();
    mesh_.rect({bed.x, bed.y, bed.w, bed.h}, look_.bed_top, look_.bed_bottom);
    mesh_.rect({0.0f, bed.bottom() - layout_.felt_line_h, width_, layout_.felt_line_h},
               look_.felt_line, look_.felt_line);
    const SDL_FColor line = with_alpha(look_.accent, 0.5f);
    mesh_.rect({0.0f, bed.y, width_, std::min(layout_.felt_line_h, bed.h)}, line, line);
    mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);

    // Glow of the blocks and keys that reaches onto the bed, clipped so it stops at the keys.
    const SDL_Rect bed_clip = pixel_rect(bed);
    gfx::set_clip(renderer_, &bed_clip);
    glow_.draw(renderer_, SDL_BLENDMODE_ADD, textures_.glow());
    gfx::clear_clip(renderer_);
}

void Scene::draw_hit_effects(const FrameInput& in) {
    const HitEffect effect =
        affordable_hit_effect(settings_.effective_hit_effect(), budget_.particles);
    const RectF& area = layout_.note_area;
    // One white key's length above the hit line, the most any effect may cover.
    const float reach = layout_.key_area.h;
    for (int note = range_.lowest; note <= range_.highest; ++note) {
        const auto i = static_cast<std::size_t>(note);
        const std::int64_t pressed = in.last_press_ns[i];
        if (pressed == seen_press_[i]) {
            continue;
        }
        // Only fresh presses: a press older than a few frames (a hitch, the first frame) is past.
        constexpr std::int64_t kFreshNs = 100'000'000;
        const std::uint8_t look = in.press_look != nullptr ? (*in.press_look)[i] : kPressFine;
        if (look == kPressPending && in.now_ns - pressed <= kFreshNs) {
            continue; // the grading has not judged it yet: decide in a later frame
        }
        seen_press_[i] = pressed;
        const BlockColumn& c = cols_[i];
        const bool wrong = look == kPressWrong;
        if (effect == HitEffect::None || !c.visible || in.now_ns - pressed > kFreshNs ||
            area.empty() || wrong) {
            continue;
        }
        hits_.spawn(effect, {c.lane_x + 0.5f * c.lane_w, area.bottom(), c.lane_w, reach, unit_},
                    note, in.last_velocity[i]);
    }
    hits_.update(in.now_ns);
    if (area.empty()) {
        return;
    }
    const std::size_t n = hits_.shapes(hit_shapes_);
    if (n == 0) {
        return;
    }
    fx_.clear();
    hit_lines_.clear();
    for (std::size_t k = 0; k < n; ++k) {
        const HitShape& shape = hit_shapes_[k];
        add_hit_shape(shape, saturated(fill_for(shape.note, shape.velocity), 1.2f), unit_, fx_,
                      hit_lines_);
    }
    // Above the hit line only: never over the bed, the keys or the top row.
    const SDL_Rect clip = pixel_rect(area);
    gfx::set_clip(renderer_, &clip);
    fx_.draw(renderer_, SDL_BLENDMODE_ADD, textures_.glow());
    hit_lines_.draw(renderer_, SDL_BLENDMODE_ADD);
    gfx::clear_clip(renderer_);
}

SDL_Rect Scene::effects_clip() const {
    const RectF& notes = layout_.note_area;
    const float bottom = layout_.key_bed.empty() ? notes.bottom() : layout_.key_bed.bottom();
    return pixel_rect({notes.x, notes.y, notes.w, std::max(0.0f, bottom - notes.y)});
}

} // namespace etude::app
