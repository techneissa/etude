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

// The number's size in design points, and its parts as shares of the digits' height (their cap
// height) or the font size: the fire icon is a little taller than the digits, a gap sits between
// the two, and the box keeps some air round them. The box is sized for the pulse's largest scale,
// so a pulse never leaves it.
constexpr float kComboFontPt = 28.0f;
constexpr float kComboIconTall = 1.15f; // of the digits' height
constexpr float kComboIconAspect = 0.8f; // width over height
constexpr float kComboIconGap = 0.24f;  // of the font size
constexpr float kComboTimesGap = 0.06f; // between the × and the digits, of the font size
// The edge arrows' strip (draw_edge_indicators), in 1080p units: the number keeps out of it.
constexpr float kEdgeStripUnits = 30.0f;
constexpr float kComboAir = 0.14f;      // of the digits' height, on every side
constexpr float kComboOutline = 0.07f;  // of the font size
// A flame, `h` tall and centred on (cx, cy): a round foot, a main tongue leaning right and a small
// one on the left, as one outline filled from a point inside it. `scale` shrinks it about its
// centre and `dy` moves that centre down (the pale flame inside). The colour runs from `top` at the
// tips to `bottom` at the foot.
void add_flame(Mesh& mesh, float cx, float cy, float h, float scale, float dy, SDL_FColor top,
               SDL_FColor bottom) {
    // The outline in shares of `h` from the flame's centre (x right, y down), clockwise from the
    // main tip. Every point is seen from (0, 0.12) in turn, so a fan from there fills it.
    static constexpr SDL_FPoint kOutline[] = {
        {0.10f, -0.50f}, {0.18f, -0.34f}, {0.30f, -0.18f}, {0.38f, 0.00f},  {0.40f, 0.18f},
        {0.34f, 0.34f},  {0.20f, 0.46f},  {0.00f, 0.50f},  {-0.20f, 0.46f}, {-0.34f, 0.34f},
        {-0.40f, 0.18f}, {-0.38f, 0.00f}, {-0.30f, -0.14f}, {-0.20f, -0.32f}, {-0.10f, -0.12f},
        {0.00f, -0.30f}};
    constexpr SDL_FPoint kCentre{0.0f, 0.12f};
    const auto place = [&](SDL_FPoint p) {
        return SDL_FPoint{cx + p.x * h * scale, cy + dy + p.y * h * scale};
    };
    const auto colour_at = [&](SDL_FPoint p) {
        return mix(top, bottom, std::clamp((p.y + 0.5f) / 1.0f, 0.0f, 1.0f));
    };
    constexpr std::size_t kCount = std::size(kOutline);
    for (std::size_t i = 0; i < kCount; ++i) {
        const SDL_FPoint a = kOutline[i];
        const SDL_FPoint b = kOutline[(i + 1) % kCount];
        mesh.triangle(place(kCentre), place(a), place(b), colour_at(kCentre), colour_at(a),
                      colour_at(b));
    }
}

} // namespace

float Scene::ui_unit_px() const {
    if (ui_unit_ > 0.0f) {
        return ui_unit_;
    }
    return ui_scale(width_, height_, px_per_mm_, pixel_density_, ui_grow_);
}

void Scene::prepare_combo_text(float px) {
    if (px == combo_px_) {
        return;
    }
    combo_px_ = px;
    combo_digit_w_ = 0.0f;
    const int outline = std::max(1, static_cast<int>(std::lround(px * kComboOutline)));
    for (int d = 0; d < 10; ++d) {
        const char digit = static_cast<char>('0' + d);
        TextSlot& slot = combo_slot_[static_cast<std::size_t>(d)];
        const SDL_FPoint extent = text_.prepare_exact(slot, std::string_view(&digit, 1), px, true);
        text_.prepare_outline(slot, outline);
        combo_digit_w_ = std::max(combo_digit_w_, extent.x);
    }
    // The × before the digits, in the same face and size.
    const SDL_FPoint times = text_.prepare_exact(combo_times_slot_, "\xC3\x97", px, true);
    text_.prepare_outline(combo_times_slot_, outline);
    combo_times_w_ = times.x;
    combo_metrics_ = text_.vertical_metrics();
    combo_cap_h_ = combo_metrics_.cap_em * px;
}

void Scene::prepare_combo(const FrameInput& in) {
    const double dt =
        combo_has_clock_ ? static_cast<double>(in.now_ns - combo_ns_) / 1e9 : 0.0;
    combo_ns_ = in.now_ns;
    combo_has_clock_ = true;
    combo_.update(dt, in.combo, in.combo_milestone, settings_.reduced_effects, in.combo_epoch);
    combo_rect_ = {};
    const ComboView& v = combo_.view();
    const RectF& area = layout_.note_area;
    const float u = ui_unit_px();
    // Under the count band, and under the section band while it hangs from the bar over the top of
    // the note area (a pinned bar gives it a row of its own, above the area): the number eases
    // down with the band and back up with it, and does not move at all with reduced effects. The
    // ease runs while the number is hidden too, so it appears where the band leaves it.
    const float band = std::clamp(count_band_h_, 0.0f, area.h) * count_band_shown_;
    const float want = std::max(combo_clear_y_ - area.y, 0.0f);
    const float step = settings_.reduced_effects
                           ? 1.0f
                           : 1.0f - std::exp(-static_cast<float>(std::clamp(dt, 0.0, 0.1)) / 0.06f);
    combo_clear_shown_ += (want - combo_clear_shown_) * step;
    if (!settings_.combo_number || !v.shown() || area.empty()) {
        return;
    }
    prepare_combo_text(std::round(std::max(kMinTextPx, kComboFontPt * u)));
    const float icon_h = kComboIconTall * combo_cap_h_;
    combo_icon_w_ = kComboIconAspect * icon_h;
    const float gap = kComboIconGap * combo_px_;
    const float times_gap = kComboTimesGap * combo_px_;
    const float inner_w = combo_times_w_ + times_gap + static_cast<float>(v.columns) * combo_digit_w_ +
                          gap + combo_icon_w_;
    const float inner_h = icon_h;
    const float grow = 1.0f + kComboPulseGrow;
    const float air = kComboAir * combo_cap_h_;
    const float hang = combo_clear_shown_ < 0.5f ? 0.0f : combo_clear_shown_;
    // Clear of the right-edge arrows (a note off screen on that side): their strip is 30 units wide.
    // And under the sheet-music button, which stands in the same top-right corner.
    const float under_button =
        side_button_.empty() ? 0.0f : std::max(side_button_.bottom() - area.y, 0.0f);
    combo_rect_ = place_combo(area, std::max({band, hang, under_button}), u, inner_w * grow + 2.0f * air,
                              inner_h * grow + 2.0f * air, combo_keep_out_, kEdgeStripUnits * unit_);
    if (combo_rect_.empty()) {
        return;
    }
    combo_inner_ = {combo_rect_.x + 0.5f * (combo_rect_.w - inner_w),
                    combo_rect_.y + 0.5f * (combo_rect_.h - inner_h), inner_w, inner_h};
}

void Scene::draw_combo() {
    if (combo_rect_.empty()) {
        return;
    }
    const ComboView& v = combo_.view();
    const GradeGradient gold = grade_gradient(Grade::Combo, settings_.high_contrast);
    const auto colour = [](RGBA c) { return rgba(c.r, c.g, c.b); };
    const SDL_FColor top = colour(gold.top);
    const SDL_FColor bottom = colour(gold.bottom);
    // The pulse lifts the number toward white for a moment as it grows.
    const SDL_FColor fill = mix(mix(top, bottom, 0.3f), kWhite, 0.35f * v.glow);
    const float cx = combo_rect_.x + 0.5f * combo_rect_.w;
    const float cy = combo_rect_.y + 0.5f * combo_rect_.h;
    const float s = v.scale;
    const SDL_Rect clip = pixel_rect(layout_.note_area);
    gfx::set_clip(renderer_, &clip);

    // "×", then the digits, one column each, then the icon.
    const float cap_top = cy - 0.5f * combo_cap_h_;
    const float slot_y = cap_top - combo_metrics_.cap_top_em * combo_px_;
    const float outline_px = static_cast<float>(combo_slot_[0].outline_px) * s;
    const auto draw_slot = [&](TextSlot& slot, float x, float y, float alpha) {
        const SDL_FRect box{cx + (x - cx) * s, cy + (y - cy) * s, slot.width * s, slot.height * s};
        text_.draw_stand_in_outline(slot, box, outline_px,
                                    with_alpha(look_.background_bottom, 0.85f * alpha));
        text_.draw_stand_in(slot, box, with_alpha(fill, alpha));
    };
    draw_slot(combo_times_slot_, combo_inner_.x, slot_y, v.alpha);
    const float digits_x = combo_inner_.x + combo_times_w_ + kComboTimesGap * combo_px_;
    const auto draw_glyph = [&](const ComboGlyph& g, int column) {
        if (g.digit < 0 || g.digit > 9 || g.alpha <= 0.0f) {
            return;
        }
        TextSlot& slot = combo_slot_[static_cast<std::size_t>(g.digit)];
        const float x = digits_x + (static_cast<float>(column) + 0.5f) * combo_digit_w_ -
                        0.5f * slot.width;
        draw_slot(slot, x, slot_y + g.dy * combo_cap_h_, v.alpha * g.alpha);
    };
    for (int c = 0; c < v.columns; ++c) {
        const ComboColumn& col = v.column[static_cast<std::size_t>(c)];
        draw_glyph(col.out, c);
        draw_glyph(col.now, c);
    }

    // The fire icon at the right end: an outer flame in the gold, a pale one inside it.
    const float icon_h = kComboIconTall * combo_cap_h_;
    const float icon_cx = combo_inner_.x + combo_inner_.w - 0.5f * combo_icon_w_;
    const float fx = cx + (icon_cx - cx) * s;
    const float fy = cy;
    const float fh = icon_h * s;
    const float a = v.alpha;
    combo_mesh_.clear();
    add_flame(combo_mesh_, fx, fy, fh, 1.0f, 0.0f, with_alpha(mix(top, kWhite, 0.25f * v.glow), a),
              with_alpha(mix(bottom, kWhite, 0.2f * v.glow), a));
    add_flame(combo_mesh_, fx, fy, fh, 0.5f, 0.2f * fh, with_alpha(rgba(255, 244, 205), a),
              with_alpha(rgba(255, 224, 140), a));
    combo_mesh_.draw(renderer_, SDL_BLENDMODE_BLEND);
    gfx::clear_clip(renderer_);
}

} // namespace etude::app
