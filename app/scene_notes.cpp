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

// A falling note struck by the held key is lit at least this much (a note's flash, 0..1), more than
// the 0.45 a graded note keeps, so a held note stands out from one already hit and let go.
constexpr float kStruckLit = 0.75f;
// Most edge arrows drawn in one frame.
constexpr std::size_t kMaxEdgeMarks = 64;
float smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

// The beat lines for this frame's song blocks, thinned to stay apart on screen.
void Scene::prepare_beat_lines(const FrameInput& in) {
    const RectF& area = layout_.note_area;
    beat_count_ = 0;
    if (in.song != nullptr && settings_.beat_lines != BeatLines::Off) {
        BeatLineWindow w;
        w.mode = settings_.beat_lines;
        w.now_s = in.song_now_s;
        w.ahead_s = static_cast<double>(settings_.visible_seconds);
        w.area_px = area.h;
        w.min_gap_px = kBeatLineMinGapPt * unit_;
        beat_count_ = beat_line_heights(*in.song, w, beat_heights_).count;
        // A name sits at a block's foot: keep clear of the planner's box at the largest size.
        const bool names = settings_.name_mode != NameMode::Off && settings_.names_on_blocks;
        beat_name_clear_px_ =
            names ? name_clearance_px(name_params(), layout_.note_label_font_px) : 0.0f;
    }
}

void Scene::draw_notes(const FrameInput& in) {
    mesh_.clear();
    glow_.clear();
    outline_.clear();
    painter_.begin(recipe_, unit_);
    black_painter_.begin(black_recipe_, unit_);
    name_in_.clear();
    name_colour_.clear();
    const RectF& area = layout_.note_area;
    if (area.empty()) {
        return;
    }
    // The hit line is where the notes meet the key bed: the bottom of the note area.
    const float base_y = area.bottom();
    const float min_height = look_.min_block_height * unit_;
    const float gap = look_.block_gap * unit_;
    // Clip to the note area: blocks never draw over the chord label or the key bed.
    const SDL_Rect clip = pixel_rect(area);
    edge_marks_.clear();
    prepare_beat_lines(in);
    draw_song_bars(in, base_y, min_height, gap);
    draw_replay_marks(in, base_y, min_height);
    // No rising blocks at all while they are hidden: nothing to lay out, and no edge arrows.
    for (const TrailBar& bar : in.trail_alpha > 0.0f ? in.bars : std::span<const TrailBar>{}) {
        const BlockColumn& c = cols_[bar.note];
        if (!c.visible) {
            // Off the left or right edge of the view (or off the keyboard): an arrow at that edge
            // instead of the block vanishing. The out-of-range count reports the ones off the
            // keyboard too.
            const int side = in.view.side_of(bar.note);
            if (side != 0 && edge_marks_.size() < kMaxEdgeMarks) {
                float top = base_y - static_cast<float>(bar.top) * area.h;
                float bottom = bar.held ? base_y : base_y - static_cast<float>(bar.bottom) * area.h;
                top = std::min(top, bottom - min_height);
                float alpha = 1.0f;
                if (!bar.held && settings_.trail_fade) {
                    const float fade = (static_cast<float>(bar.bottom) - look_.fade_start) /
                                       std::max(1.0f - look_.fade_start, 1e-3f);
                    alpha = 1.0f - smoothstep(fade);
                }
                if (alpha > 0.0f) {
                    const NotePaint p =
                        paint(bar.note, bar.velocity, bar.held, flash_at(in.now_ns, bar.start_ns));
                    edge_marks_.push_back({side, 0.5f * (top + bottom), 0.5f * (bottom - top),
                                           with_alpha(p.base, p.base.a * alpha)});
                }
            }
            continue;
        }
        const float radius = block_radius(c.w);
        float top = base_y - static_cast<float>(bar.top) * area.h;
        float bottom = base_y - static_cast<float>(bar.bottom) * area.h;
        if (bar.held) {
            bottom = base_y + radius; // Cut flat by the clip, so it meets the bed's edge cleanly.
        } else if (bottom - top > min_height + gap) {
            bottom -= gap;
        }
        top = std::min(top, bottom - min_height);

        float alpha = 1.0f;
        if (!bar.held && settings_.trail_fade) {
            const float fade = (static_cast<float>(bar.bottom) - look_.fade_start) /
                               std::max(1.0f - look_.fade_start, 1e-3f);
            alpha = 1.0f - smoothstep(fade);
        }
        alpha *= in.trail_alpha;
        if (alpha <= 0.0f) {
            continue;
        }
        const NotePaint p =
            paint(bar.note, bar.velocity, bar.held, flash_at(in.now_ns, bar.start_ns));
        add_block(bar.note, c, top, bottom, radius, p, alpha, true,
                  label_colour_for(bar.note, bar.velocity));
    }
    // Blocks reaching the top dissolve into the background instead of ending at a hard edge.
    const float fog = look_.top_fade * area.h;
    const SDL_FColor bottom_colour = look_.background_bottom;
    const SDL_FColor sky =
        settings_.effective_background() == BackgroundStyle::Plain
            ? bottom_colour
            : mix(look_.background_top, bottom_colour, area.y / std::max(height_, 1.0f));
    // A count-in or the resume countdown: a solid band of the same colour above the fade, `shown`
    // of the way in, so no block shows under the count (count_band.hpp).
    const float band = std::clamp(count_band_h_, 0.0f, area.h) * count_band_shown_;
    if (band > 0.0f) {
        mesh_.rect({area.x, area.y, area.w, band}, with_alpha(sky, count_band_shown_),
                   with_alpha(sky, count_band_shown_));
    }
    mesh_.rect({area.x, area.y + band, area.w, fog}, sky, with_alpha(sky, 0.0f));
    draw_edge_indicators();
    gfx::set_clip(renderer_, &clip);
    glow_.draw(renderer_, SDL_BLENDMODE_ADD, textures_.glow());
    // White keys' blocks, then black keys' over them.
    painter_.draw(renderer_, textures_);
    outline_.draw(renderer_, SDL_BLENDMODE_BLEND);
    black_painter_.draw(renderer_, textures_);
    mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
    gfx::clear_clip(renderer_);
}

// The text over the falling blocks, drawn after them and the keys so nothing covers it: the block
// names queued by draw_notes, then the grade words.
void Scene::draw_note_text(const FrameInput& in) {
    if (layout_.note_area.empty()) {
        return;
    }
    const SDL_Rect clip = pixel_rect(layout_.note_area);
    gfx::set_clip(renderer_, &clip);
    // The names first; the grade words dodge them.
    const std::size_t n = name_in_.size();
    plan_block_names(name_params(), name_in_, std::span<BlockNamePlacement>(name_out_.data(), n));
    layout_grades(in);
    draw_block_names();
    draw_finger_marks();
    draw_grades();
    draw_unsure(in);
    gfx::clear_clip(renderer_);
}

// The song's notes falling to the hit line, in their hand's colour like the keys. A note of a hand
// you are not practising is faint; a graded note brightens when hit and greys out when missed. A
// note moved by octaves to fit the keyboard carries a small arrow at its top, pointing the way
// it moved.
void Scene::draw_song_bars(const FrameInput& in, float base_y, float min_height, float gap) {
    fingers_.clear();
    const RectF& area = layout_.note_area;
    const SDL_FColor grey = rgba(120, 124, 136);
    for (const SongBar& bar : in.song_bars) {
        const BlockColumn& c = cols_[bar.key & 127];
        float top = base_y - bar.top * area.h;
        const float bottom = base_y - bar.bottom * area.h;
        if (bottom - top > min_height + gap) {
            top += gap; // a repeated note shows as two blocks
        }
        top = std::min(top, bottom - min_height);
        float alpha = bar.practised ? 1.0f : 0.32f;
        const bool graded = bar.grade >= 0;
        const bool missed = graded && static_cast<NoteGrade>(bar.grade) == NoteGrade::Missed;
        float flash = graded && !missed ? 0.45f : 0.0f;
        // Struck by the held key: lit in its hand's colour as it was hit, and held lit.
        const bool lit = bar.struck && !missed;
        if (lit) {
            flash =
                std::max({flash, kStruckLit, flash_at(in.now_ns, in.last_press_ns[bar.key & 127])});
        }
        NotePaint p = paint(bar.key, bar.velocity, lit, flash, bar.hand);
        if (missed) {
            p.base = with_alpha(mix(p.base, grey, 0.7f), p.base.a);
            p.lower = with_alpha(mix(p.lower, grey, 0.7f), p.lower.a);
            p.glow = 0.0f;
            alpha *= 0.6f;
        }
        if (!c.visible) {
            const int side = in.view.side_of(bar.key);
            if (side != 0 && edge_marks_.size() < kMaxEdgeMarks && bottom > area.y &&
                top < base_y) {
                edge_marks_.push_back({side, 0.5f * (top + bottom), 0.5f * (bottom - top),
                                       with_alpha(p.base, p.base.a * alpha)});
            }
            continue;
        }
        const float x = c.x;
        const float w = c.w;
        const float radius = block_radius(w);
        add_block(bar.key & 127, c, top, bottom, radius, p, alpha, bar.practised,
                  bar.hand == kFreePlayHand
                      ? label_colour_for(bar.key, bar.velocity)
                      : to_fcolor(colours_.label(bar.key, bar.velocity, bar.hand)));
        if (beat_count_ > 0) {
            const float o = c.black ? c.outline_px : 0.0f;
            add_beat_lines({x + o, top + o, w - 2.0f * o, bottom - top - 2.0f * o}, c.black, p.base,
                           alpha);
        }
        if (bar.finger > 0 && bar.practised && !missed && fingers_.size() < fingers_.capacity()) {
            // The block's top, or the note area's top where it runs past it.
            const float at = std::max(top, area.y);
            // The disc must fit the block: its width, and what is left of its height.
            fingers_.push_back({x + 0.5f * w, at, std::min(w, bottom - at),
                                static_cast<std::uint8_t>(bar.finger)});
        }
        if (bar.fold != 0) {
            const float a = std::min(0.4f * w, 10.0f * unit_);
            const float cx = x + 0.5f * w;
            const float ty = top + 0.6f * a + 2.0f * unit_;
            const float dir = bar.fold > 0 ? -1.0f : 1.0f; // raised: points up
            const SDL_FColor arrow = with_alpha(kWhite, 0.85f * alpha);
            mesh_.triangle({cx, ty + dir * 0.5f * a}, {cx - 0.5f * a, ty - dir * 0.5f * a},
                           {cx + 0.5f * a, ty - dir * 0.5f * a}, arrow, arrow, arrow);
        }
    }
}

void Scene::add_beat_lines(const SDL_FRect& block, bool black, SDL_FColor colour, float alpha) {
    const RectF& area = layout_.note_area;
    const float base_y = area.bottom();
    const float thick =
        std::max(1.0f, std::round((black ? black_recipe_ : recipe_).beat_line_px * unit_));
    // Lines strictly inside the block, above its name: heights between these two. The name
    // stands on the block's visible foot, the hit line for a block that runs past it.
    const float foot = std::min(block.y + block.h, base_y);
    const float lowest_y = foot - std::max(beat_name_clear_px_, thick);
    const float highest_y = block.y + thick;
    if (lowest_y <= highest_y) {
        return;
    }
    const float from = (base_y - lowest_y) / area.h;
    const float to = (base_y - highest_y) / area.h;
    const auto first =
        std::lower_bound(beat_heights_.begin(),
                         beat_heights_.begin() + static_cast<std::ptrdiff_t>(beat_count_), from);
    StylePainter& painter = black ? black_painter_ : painter_;
    for (auto it = first;
         it != beat_heights_.begin() + static_cast<std::ptrdiff_t>(beat_count_) && *it < to; ++it) {
        const float y = std::round(base_y - *it * area.h - 0.5f * thick);
        painter.add_beat_line({block.x, y, block.w, thick}, colour, alpha);
    }
}

float Scene::block_radius(float w) const {
    const float k = recipe_.corner_scale;
    return std::min({look_.block_radius * k * w, look_.block_radius_max * k * unit_, 0.5f * w});
}

void Scene::add_block(int note, const BlockColumn& c, float top, float bottom, float radius,
                      const NotePaint& p, float alpha, bool glow, SDL_FColor name_colour) {
    const float glow_size = recipe_.glow_width_share * white_width_ * p.glow_reach;
    if (glow && p.glow > 0.0f && glow_size > 0.0f) {
        glow_.nine_slice({c.x - glow_size, top - glow_size, c.w + 2.0f * glow_size,
                          bottom - top + 2.0f * glow_size},
                         glow_size + radius,
                         with_alpha(saturated(p.base, recipe_.saturation), p.glow * alpha));
    }
    const SDL_FRect rect{c.x, top, c.w, bottom - top};
    if (!key_rect_.empty() && alpha > 0.0f && c.x < key_rect_.right() &&
        c.x + c.w > key_rect_.x && top < key_rect_.bottom() && bottom > key_rect_.y) {
        key_behind_ = true; // a block passes behind the key label
    }
    if (!c.black) {
        painter_.add(rect, radius, p.base, p.lower, alpha);
    } else if (c.outline_px > 0.0f) {
        // Over the white keys' blocks: a dark edge, then the body inside it.
        const float o = c.outline_px;
        const SDL_FColor line =
            with_alpha(look_.black_block_outline, look_.black_block_outline.a * alpha);
        outline_.rounded_rect(rect, radius, line, line, 0.0f);
        black_painter_.add({rect.x + o, rect.y + o, rect.w - 2.0f * o, rect.h - 2.0f * o},
                           std::max(0.0f, radius - o), p.base, p.lower, alpha);
    } else {
        black_painter_.add(rect, radius, p.base, p.lower, alpha);
    }
    // The name sits across the block at its bottom edge, or not at all (see draw_note_text).
    // The frame draws at most kMaxBlockLabelsPerFrame names, the ones nearest the keys.
    add_block_name(note, c, {c.x, top, c.w, bottom - top}, name_colour, alpha);
}

} // namespace etude::app
