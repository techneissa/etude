// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene.hpp"

#include "scene_internal.hpp"

#include "frame_log.hpp"
#include "gfx.hpp"
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

// How far a stand-in's stretch may differ between its width and its height before the letters look
// wrong (a share of the height's stretch).
constexpr float kStandInSkew = 0.2f;

} // namespace

// Queues the name of `note` for the block. Whether it fits, and where, is decided once the
// frame's blocks are all known (draw_note_text). The blocks come oldest first; the buffer keeps the
// ones nearest the keys, and core names the nearest of those.
void Scene::add_block_name(int note, const BlockColumn& column, const RectF& block,
                           SDL_FColor colour, float alpha) {
    if (!layout_.block_labels_visible) {
        return;
    }
    const int i = note_index(note);
    const NameWidths& width = name_widths_[static_cast<std::size_t>(i)];
    if (width.letter.empty() || name_stale_[static_cast<std::size_t>(i)]) {
        return;
    }
    BlockNameIn in{block, i, is_black_key(i), name_accidental_[static_cast<std::size_t>(i)],
                   width};
    in.lane_x = column.lane_x; // a black key's name spilled under Always stays in its lane
    in.lane_w = column.lane_w;
    colour.a *= alpha;
    const std::size_t slot =
        name_candidate_slot(name_in_, kMaxNameCandidates, in, layout_.note_area);
    if (slot == name_in_.size()) {
        name_in_.push_back(in);
        name_colour_.push_back(colour);
    } else if (slot < name_in_.size()) {
        name_in_[slot] = in;
        name_colour_[slot] = colour;
    }
}

BlockNameParams Scene::name_params() const {
    BlockNameParams params;
    params.clip = layout_.note_area;
    params.max_font_px = layout_.note_label_font_px;
    params.min_font_px = min_name_px_;
    params.min_small_font_px = min_small_name_px_;
    params.line_em = name_metrics_.line_em;
    params.cap_em = name_metrics_.cap_em;
    params.cap_top_em = name_metrics_.cap_top_em;
    params.black_names = settings_.black_key_names;
    params.max_names = static_cast<std::size_t>(kMaxBlockLabelsPerFrame);
    params.keep_clear = key_rect_;
    return params;
}

Scene::PieceSlot Scene::piece_slot(const std::string& text, float px, float outline_px, float width,
                                   float max_height, float x, float y) {
    const int size = std::max(1, static_cast<int>(px));
    const int outline = static_cast<int>(outline_px);
    auto it = name_cache_.find({text, size, false});
    const bool made = it != name_cache_.end() &&
                      Text::is_prepared(it->second, text, static_cast<float>(size), outline);
    if (!made && raster_budget_spent()) {
        // Another size of the same text, if it fits the planned box without a visible distortion.
        TextSlot* other = nearest_slot(text, size);
        if (other == nullptr) {
            return {};
        }
        const float h = stand_in_height(other->size, other->width, other->height,
                                        static_cast<float>(size), width, max_height, kStandInSkew);
        if (!(h > 0.0f)) {
            return {};
        }
        other->used = frame_;
        return {other, true, {x, y, width, h}};
    }
    if (it == name_cache_.end()) {
        it = name_cache_
                 .emplace(std::piecewise_construct, std::forward_as_tuple(text, size, false),
                          std::forward_as_tuple())
                 .first;
    }
    TextSlot& slot = it->second;
    slot.used = frame_;
    if (!made) {
        const std::int64_t start = frame_clock_ns();
        text_.prepare_exact(slot, text, static_cast<float>(size));
        text_.prepare_outline(slot, outline);
        raster_budget().spent_ns += frame_clock_ns() - start;
    }
    return {&slot, false, {}};
}

TextSlot* Scene::nearest_slot(const std::string& text, int px) {
    TextSlot* best = nullptr;
    int best_gap = 0;
    for (auto it = name_cache_.lower_bound({text, 0, false});
         it != name_cache_.end() && std::get<0>(it->first) == text; ++it) {
        TextSlot& slot = it->second;
        if (slot.epoch != gfx::epoch() || slot.texture == nullptr || std::get<2>(it->first)) {
            continue;
        }
        const int gap = std::abs(std::get<1>(it->first) - px);
        if (best == nullptr || gap < best_gap) {
            best = &slot;
            best_gap = gap;
        }
    }
    return best;
}

void Scene::add_glyph(Mesh& mesh, const AccidentalShape& glyph, float x, float y, float grow,
                      SDL_FColor colour) {
    for (std::size_t q = 0; q < glyph.count; ++q) {
        const GlyphQuad& g = glyph.quads[q];
        const float cx = 0.25f * (g.x[0] + g.x[1] + g.x[2] + g.x[3]);
        const float cy = 0.25f * (g.y[0] + g.y[1] + g.y[2] + g.y[3]);
        SDL_FPoint p[4];
        for (std::size_t i = 0; i < 4; ++i) {
            p[i] = {x + g.x[i] + (g.x[i] > cx ? grow : -grow),
                    y + g.y[i] + (g.y[i] > cy ? grow : -grow)};
        }
        mesh.triangle(p[0], p[1], p[2], colour, colour, colour);
        mesh.triangle(p[0], p[2], p[3], colour, colour, colour);
    }
}

// The names core placed: each is its pieces side by side, the letter, the accidental as a drawn
// glyph whose top is level with the letter's capitals, and the octave number, all inside the
// block, at the sizes core planned. The text pieces are cached textures; the glyph is a few
// pixel-snapped quads. Everything is outlined in the opposite shade so the name reads on any
// block (and a little past one, when the look asks for that): the glyphs' outline first, then the
// text, then the glyphs, so no outline covers a piece.
void Scene::draw_block_names() {
    const std::size_t n = name_in_.size();
    // First the textures of every name, so a name that has to wait (the frame's budget for making
    // them is spent and no stand-in fits) leaves out all its parts, accidental included.
    struct Piece {
        const std::string* text;
        float x;
    };
    for (std::size_t i = 0; i < n; ++i) {
        name_ready_[i] = false;
        const BlockNamePlacement& p = name_out_[i];
        if (p.form == NameForm::Hidden) {
            continue;
        }
        const NameParts& parts = name_parts_[static_cast<std::size_t>(name_in_[i].note)];
        const float left = p.rect.x + p.outline_px;
        const float top = p.rect.y + p.outline_px;
        const Piece pieces[2] = {
            {&parts.letter, left},
            {p.form == NameForm::Full ? &parts.octave : nullptr, left + p.letter_w + p.glyph_w},
        };
        const float widths[2] = {p.letter_w, p.octave_w};
        const float room = p.rect.h - 2.0f * p.outline_px;
        bool complete = true;
        for (std::size_t k = 0; k < 2; ++k) {
            name_slots_[i][k] = {};
            if (pieces[k].text != nullptr && !pieces[k].text->empty()) {
                name_slots_[i][k] = piece_slot(*pieces[k].text, p.font_px, p.outline_px, widths[k],
                                               room, pieces[k].x, top);
                complete = complete && name_slots_[i][k].slot != nullptr;
            }
        }
        name_ready_[i] = complete;
    }
    name_halo_.clear();
    name_glyph_.clear();
    for (std::size_t i = 0; i < n; ++i) {
        const BlockNamePlacement& p = name_out_[i];
        if (!name_ready_[i] || p.accidental == AccidentalKind::None) {
            continue;
        }
        const SDL_FColor fill =
            with_alpha(name_colour_[i], name_colour_[i].a * fog_visibility(p.rect));
        const bool dark_text = fill.r + fill.g + fill.b < 1.5f;
        const AccidentalShape glyph = accidental_shape(p.accidental, p.glyph_h);
        const float x = p.rect.x + p.outline_px + p.letter_w;
        const float y = p.rect.y + p.outline_px + p.glyph_dy;
        add_glyph(name_halo_, glyph, x, y, p.outline_px,
                  with_alpha(dark_text ? kWhite : kBlack, 0.8f * fill.a));
        add_glyph(name_glyph_, glyph, x, y, 0.0f, fill);
    }
    name_halo_.draw(renderer_, SDL_BLENDMODE_BLEND);
    for (std::size_t i = 0; i < n; ++i) {
        if (!name_ready_[i]) {
            continue;
        }
        const BlockNamePlacement& p = name_out_[i];
        const SDL_FColor fill =
            with_alpha(name_colour_[i], name_colour_[i].a * fog_visibility(p.rect));
        const bool dark_text = fill.r + fill.g + fill.b < 1.5f;
        const SDL_FColor outline = with_alpha(dark_text ? kWhite : kBlack, 0.8f * fill.a);
        const float left = p.rect.x + p.outline_px;
        const float top = p.rect.y + p.outline_px;
        const float xs[2] = {left, left + p.letter_w + p.glyph_w};
        for (std::size_t k = 0; k < 2; ++k) {
            const PieceSlot& slot = name_slots_[i][k];
            if (slot.slot == nullptr) {
                continue;
            }
            if (slot.stand_in) {
                text_.draw_stand_in_outline(*slot.slot, slot.box, p.outline_px, outline);
            } else {
                text_.draw_outline(*slot.slot, xs[k], top, Align::Left, outline);
            }
        }
        for (std::size_t k = 0; k < 2; ++k) {
            const PieceSlot& slot = name_slots_[i][k];
            if (slot.slot == nullptr) {
                continue;
            }
            if (slot.stand_in) {
                text_.draw_stand_in(*slot.slot, slot.box, fill);
            } else {
                text_.draw(*slot.slot, xs[k], top, Align::Left, fill);
            }
        }
    }
    name_glyph_.draw(renderer_, SDL_BLENDMODE_BLEND);
}

} // namespace etude::app
