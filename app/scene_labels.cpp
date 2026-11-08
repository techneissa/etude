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

// What tells one key label from another, to cross-fade when it changes: the key as played and the
// way the tonic is named.
std::uint64_t key_label_id(const KeyChange& key, int transpose, bool solfege, bool flats) {
    std::uint64_t id = 1;
    id = id << 4 | key.tonic;
    id = id << 4 | static_cast<std::uint64_t>(key.mode);
    id = id << 6 | static_cast<std::uint64_t>(key.fifths + 16);
    id = id << 8 | static_cast<std::uint64_t>(transpose + 64);
    id = id << 1 | (solfege ? 1u : 0u);
    id = id << 1 | (flats ? 1u : 0u);
    return id;
}

} // namespace

// The chord label: a thin line under each hand's chord notes with the chord's name centred above
// it, in the strip above the note area. The name and its line fade out and in on a change; both
// fade out while the section band hangs over the strip (the combo number is kept under the same
// band, so its clearance is the band's foot). The octave offer's card is the one thing that
// stands at the strip's height: names step aside from it, and their lines are cut round it. The
// combo number and the key label stand in the note area below the strip and the block names are
// drawn there too, so none reaches a name; the combo and the key label are still in the list of
// what to keep clear of, so a layout that moved them up could not cover one.
void Scene::draw_chord_labels(const FrameInput& in) {
    ChordLabelFrame f;
    const bool shown = chord_label_shown_ && layout_.chord_label_visible && in.chords != nullptr;
    if (shown) {
        f.strip = layout_.chord_label;
        f.font_px = layout_.chord_label_font_px;
        f.ui_unit = ui_unit_px();
        f.visible = !(combo_clear_y_ > layout_.chord_label.y + 1.0f);
        f.reduced_effects = settings_.reduced_effects;
        f.now_ns = in.now_ns;
        f.chords = in.chords;
        f.serial = in.chords_serial;
        f.time_s = in.chords_time_s;
        f.transposition = in.chords_transposition;
        f.columns = &cols_;
        f.colours = chord_colours_;
    }
    const std::array<RectF, 4> avoid{combo_rect_, combo_keep_out_, key_rect_, side_button_};
    f.avoid = avoid;
    chord_view_.draw(renderer_, text_, mesh_, f);
}

// Lays the label's pieces out left to right at `font_px`: the tonic, its accidental as a drawn
// glyph, the mode, then the key-signature hint (a count and its glyph, smaller).
void Scene::measure_key_layer(KeyLayer& l, float font_px) {
    const FrameDetailScope timing(FrameDetail::Text);
    const KeyLabelParts& p = l.parts;
    l.font_px = font_px;
    const SDL_FPoint tonic = text_.prepare(l.tonic, p.tonic, font_px);
    const SDL_FPoint mode = text_.prepare(l.mode, p.mode, font_px);
    l.height = std::max(tonic.y, mode.y);
    const float gap = std::round(0.32f * font_px);
    float x = 0.0f;
    l.tonic_x = x;
    x += tonic.x;
    l.glyph_h = 0.0f;
    if (p.accidental != AccidentalKind::None) {
        l.glyph_h = accidental_glyph_height_px(name_metrics_.cap_em * font_px);
        l.glyph_x = x + std::round(0.05f * font_px);
        x = l.glyph_x + accidental_glyph_width_px(p.accidental, l.glyph_h);
    }
    x += gap;
    l.mode_x = x;
    x += mode.x;
    l.sig_h = 0.0f;
    if (p.signature_count > 0) {
        const float hint_px = std::max(9.0f, std::round(0.78f * font_px));
        char count[16];
        std::snprintf(count, sizeof count, "%d", p.signature_count);
        const SDL_FPoint hint = text_.prepare(l.count, count, hint_px);
        x += 2.0f * gap;
        l.count_x = x;
        x += hint.x;
        l.sig_h = accidental_glyph_height_px(name_metrics_.cap_em * hint_px);
        l.sig_x = x + std::round(0.05f * hint_px);
        x = l.sig_x + accidental_glyph_width_px(p.signature_kind, l.sig_h);
        l.count_y = l.height - hint.y; // its foot level with the words'
        l.hint_cap_top = std::round(name_metrics_.cap_top_em * hint_px);
    }
    l.width = x;
}

void Scene::prepare_key_label(const FrameInput& in) {
    key_rect_ = {};
    const bool behind = key_behind_;
    key_behind_ = false;
    const KeyChange* key = in.key_label.key;
    const bool shown = settings_.show_key && key != nullptr && !layout_.note_area.empty();
    const bool solfege = settings_.name_mode == NameMode::Solfege;
    const bool flats = settings_.names_spelling == NameSpelling::Flats;
    const std::uint64_t id =
        key != nullptr ? key_label_id(*key, in.key_label.transpose, solfege, flats) : 0;
    const KeyLabelFade::Update change =
        key_fade_.update(in.now_ns, shown, id, behind, settings_.reduced_effects);
    if (change.new_label && key != nullptr) {
        KeyLayer& l = key_layers_[static_cast<std::size_t>(change.slot)];
        l.parts = key_label_parts(*key, solfege, in.key_label.transpose, flats);
        l.font_px = 0.0f; // measured below
    }
    if (!key_fade_.visible() || layout_.note_area.empty()) {
        return;
    }
    const float font = key_label_font_px(unit_);
    float width = 0.0f;
    float height = 0.0f;
    for (int slot = 0; slot < 2; ++slot) {
        if (key_fade_.layer(slot).presence <= 0.0f) {
            continue;
        }
        KeyLayer& l = key_layers_[static_cast<std::size_t>(slot)];
        if (l.font_px != font) {
            measure_key_layer(l, font);
        }
        width = std::max(width, l.width);
        height = std::max(height, l.height);
    }
    const KeyLabelPlacement placed = place_key_label(
        layout_.note_area, width, height, std::round(kKeyLabelMarginEm * height),
        std::span<const RectF>(&side_button_, 1));
    if (placed.visible) {
        key_rect_ = placed.rect;
    }
}

void Scene::draw_key_label() {
    if (key_rect_.empty()) {
        return;
    }
    const SDL_Rect clip = pixel_rect(layout_.note_area);
    gfx::set_clip(renderer_, &clip);
    key_glyphs_.clear();
    const SDL_FColor ink = settings_.high_contrast ? kWhite : rgba(236, 240, 248);
    for (int slot = 0; slot < 2; ++slot) {
        const float alpha = key_fade_.layer(slot).presence * key_fade_.opacity();
        if (alpha <= 0.0f) {
            continue;
        }
        KeyLayer& l = key_layers_[static_cast<std::size_t>(slot)];
        const SDL_FColor colour = with_alpha(ink, alpha);
        const float x = key_rect_.x;
        const float y = key_rect_.y + std::round(0.5f * (key_rect_.h - l.height));
        text_.draw(l.tonic, x + l.tonic_x, y, Align::Left, colour);
        text_.draw(l.mode, x + l.mode_x, y, Align::Left, colour);
        if (l.glyph_h > 0.0f) {
            add_glyph(key_glyphs_, accidental_shape(l.parts.accidental, l.glyph_h), x + l.glyph_x,
                      y + std::round(name_metrics_.cap_top_em * l.font_px), 0.0f, colour);
        }
        if (l.parts.signature_count > 0) {
            text_.draw(l.count, x + l.count_x, y + l.count_y, Align::Left,
                       colour);
            add_glyph(key_glyphs_, accidental_shape(l.parts.signature_kind, l.sig_h),
                      x + l.sig_x, y + l.count_y + l.hint_cap_top, 0.0f, colour);
        }
    }
    key_glyphs_.draw(renderer_, SDL_BLENDMODE_BLEND);
    gfx::clear_clip(renderer_);
}

void Scene::draw_prompt(const FrameInput& in) {
    if (in.no_keyboard && !layout_.note_area.empty()) {
        // Centred in the note area, shrunk to fit it.
        const RectF& notes = layout_.note_area;
        const float free_w = notes.w;
        const SDL_FPoint extent = text_.prepare_fit(
            prompt_slot_, "Connect a MIDI keyboard, or click the keys or type A S D F",
            std::max(kMinTextPx, 44.0f * unit_), free_w * 0.9f, kMinTextPx);
        text_.draw(prompt_slot_, notes.x + 0.5f * free_w, notes.y + (notes.h - extent.y) * 0.5f,
                   Align::Centre, look_.prompt_text);
    }
}

} // namespace etude::app
