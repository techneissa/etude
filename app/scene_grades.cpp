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

float Scene::fog_visibility(const RectF& rect) const {
    // The fog at the top of the note area covers what is under it fully at its top edge and not at
    // all at its bottom (draw_notes); a name fades the same way, by its middle.
    // Under a count band (set_count_band) nothing shows, and the fog begins below it.
    const RectF& area = layout_.note_area;
    const float fog = look_.top_fade * area.h;
    const float band = std::clamp(count_band_h_, 0.0f, area.h) * count_band_shown_;
    const float mid = rect.y + 0.5f * rect.h - area.y - band;
    float visible = 1.0f;
    if (!(fog > 0.0f)) {
        visible = mid < 0.0f ? 0.0f : 1.0f;
    } else {
        visible = std::clamp(mid / fog, 0.0f, 1.0f);
    }
    // A name never touches the combo number: it is gone while it overlaps the box and comes in
    // over half its height once clear of it, above or below.
    if (!combo_rect_.empty() && rect.x < combo_rect_.right() && rect.right() > combo_rect_.x) {
        const float clear = std::max(combo_rect_.y - rect.bottom(), rect.y - combo_rect_.bottom());
        visible *= std::clamp(clear / std::max(0.5f * rect.h, 1.0f), 0.0f, 1.0f);
    }
    return visible;
}

float Scene::grade_font_px() const {
    return std::max(kMinLabelFontPx, std::max(layout_.note_label_font_px * 1.15f, 18.0f * unit_));
}

// Flow Mode's grade words, kept in a row just above the hit line or at the top of the note area
// (the look's choice) at the look's opacity. Core's place_grade_words decides where each goes so
// none overlaps another word, a note name inside a block or the chord label; a word with no room
// is left out. layout_grades measures and places them, draw_grades draws what it placed.
void Scene::layout_grades(const FrameInput& in) {
    grades_.count = 0;
    const RectF& area = layout_.note_area;
    if (in.grades.empty() || area.empty() || !settings_.grade_words_shown()) {
        return;
    }
    static const std::string kWords[] = {"Perfect", "Good", "Early", "Late", "Miss", "OK"};
    const float size = grade_font_px();
    const float pad_x = 0.45f * size;
    grades_.pad_y = 0.12f * size;

    std::size_t n = 0;
    float text_h = 0.0f;
    for (const GradePopup& g : in.grades) {
        const BlockColumn& c = cols_[g.key & 127];
        const auto i = static_cast<std::size_t>(g.grade);
        if (!c.visible || i >= std::size(kWords) || n >= kMaxGradeWords) {
            continue;
        }
        const SDL_FPoint extent = text_.prepare(grade_slot_[i], kWords[i], size, true);
        text_.prepare_outline(grade_slot_[i],
                              std::max(1, static_cast<int>(std::lround(size * 0.08f))));
        grades_.words[n] = {c.lane_x + 0.5f * c.lane_w, extent.x + 2.0f * pad_x,
                            std::clamp(g.age, 0.0f, 1.0f)};
        grades_.kind[n] = static_cast<std::uint8_t>(i);
        text_h = std::max(text_h, extent.y);
        ++n;
    }
    if (n == 0) {
        return;
    }
    std::array<RectF, kMaxBlockLabelsPerFrame + 2> avoid{};
    std::size_t avoid_count = 0;
    for (std::size_t j = 0; j < name_in_.size(); ++j) {
        if (name_out_[j].form != NameForm::Hidden && avoid_count < avoid.size()) {
            avoid[avoid_count++] = name_out_[j].rect;
        }
    }
    if (!key_rect_.empty()) {
        avoid[avoid_count++] = key_rect_; // the key label keeps its corner
    }
    if (!combo_rect_.empty()) {
        avoid[avoid_count++] = combo_rect_; // and the combo number its own
    }
    GradeLayoutParams params;
    params.note_area = area;
    params.chord_label = layout_.chord_label;
    params.position = settings_.grade_position;
    params.word_h = text_h + 2.0f * grades_.pad_y;
    place_grade_words(params, std::span<const RectF>(avoid.data(), avoid_count),
                      std::span<const GradeWordIn>(grades_.words.data(), n),
                      std::span<GradeWordOut>(grades_.placed.data(), n));
    grades_.count = n;
}

void Scene::draw_grades() {
    // Each word in its grade's gradient (core grade_palette.hpp): the same colours as the results
    // card. Wait Mode's "OK" (a note played) is Perfect's green.
    static constexpr Grade kGrades[] = {Grade::Perfect, Grade::Good, Grade::Early,
                                        Grade::Late,    Grade::Miss, Grade::Perfect};
    const bool high = settings_.high_contrast;
    const auto colour = [](RGBA c) { return rgba(c.r, c.g, c.b); };
    const float opacity = settings_.grade_alpha();
    // Text with a thin dark outline round the glyphs, no box behind: it stays readable over
    // falling blocks and sparks without covering them.
    for (std::size_t j = 0; j < grades_.count; ++j) {
        if (!grades_.placed[j].visible) {
            continue;
        }
        const float a = opacity * grade_word_alpha(grades_.words[j].age);
        const RectF& r = grades_.placed[j].rect;
        TextSlot& slot = grade_slot_[grades_.kind[j]];
        const GradeGradient g = grade_gradient(kGrades[grades_.kind[j]], high);
        text_.draw_outlined_gradient(slot, r.x + 0.5f * r.w, r.y + grades_.pad_y, Align::Centre,
                                     with_alpha(colour(g.top), a), with_alpha(colour(g.bottom), a),
                                     with_alpha(look_.background_bottom, 0.85f * a));
    }
}

// A soft "?" just above the hit line over the key of the register the microphone could not name,
// fading out and drifting up a little as it goes. Text with a thin dark outline, no box.
void Scene::draw_unsure(const FrameInput& in) {
    const RectF& area = layout_.note_area;
    if (in.unsure.empty() || area.empty()) {
        return;
    }
    const float size = grade_font_px() * 1.5f;
    const SDL_FPoint extent = text_.prepare(unsure_slot_, "?", size, true);
    text_.prepare_outline(unsure_slot_, std::max(1, static_cast<int>(std::lround(size * 0.07f))));
    for (const UnsureMarker& m : in.unsure) {
        const BlockColumn& c = cols_[m.key & 127];
        if (!c.visible || m.alpha <= 0.0f) {
            continue;
        }
        const float a = 0.7f * std::clamp(m.alpha, 0.0f, 1.0f);
        const float rise = (1.0f - m.alpha) * 0.5f * size;
        const float y = area.bottom() - extent.y - 0.35f * size - rise;
        text_.draw_outlined(unsure_slot_, c.lane_x + 0.5f * c.lane_w, y, Align::Centre,
                            with_alpha(rgba(205, 222, 255), a),
                            with_alpha(look_.background_bottom, 0.85f * a));
    }
}

// A soft strip down the window edge at the height of each block whose key is off screen, with a
// small arrow pointing the way to the key, in the block's own colour. Added to `mesh_` before it
// is drawn, so it shares the note clip.
void Scene::draw_edge_indicators() {
    const RectF& area = layout_.note_area;
    const float arrow = 16.0f * unit_;
    const float strip = 30.0f * unit_;
    for (const EdgeMark& m : edge_marks_) {
        const float dir = static_cast<float>(m.side); // -1 points left
        const float edge = m.side < 0 ? area.x : area.right();
        const float h = std::max(m.half_h, 0.5f * arrow);
        const SDL_FColor strong = with_alpha(m.colour, 0.7f * m.colour.a);
        const SDL_FColor clear = with_alpha(m.colour, 0.0f);
        const float inner = edge - dir * strip;
        // Gradient strip: strong at the edge, fading inward.
        const float x0 = std::min(edge, inner);
        const float x1 = std::max(edge, inner);
        const SDL_FColor at_x0 = m.side < 0 ? strong : clear;
        const SDL_FColor at_x1 = m.side < 0 ? clear : strong;
        const float y0 = m.centre - h;
        const float y1 = m.centre + h;
        // An arrow's strip over the key label counts as a note passing behind it.
        if (!key_rect_.empty() && x0 < key_rect_.right() && x1 > key_rect_.x &&
            y0 < key_rect_.bottom() && y1 > key_rect_.y) {
            key_behind_ = true;
        }
        mesh_.triangle({x0, y0}, {x1, y0}, {x0, y1}, at_x0, at_x1, at_x0);
        mesh_.triangle({x1, y0}, {x1, y1}, {x0, y1}, at_x1, at_x1, at_x0);
        // Arrow tip on the very edge, base inside it.
        const SDL_FColor solid = with_alpha(m.colour, 0.95f * m.colour.a);
        const float tip = edge;
        const float base = edge - dir * arrow;
        mesh_.triangle({tip, m.centre}, {base, m.centre - 0.6f * arrow},
                       {base, m.centre + 0.6f * arrow}, solid, solid, solid);
    }
}

} // namespace etude::app
