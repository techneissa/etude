// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/editor_layout.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

EditorLayout editor_layout(const RectF& area, float u) {
    EditorLayout l;
    l.area = area;
    const float gap = kEditorGap * u;
    const float toolbar_h = kEditorToolbar * u;
    const float ruler_h = kEditorRuler * u;
    const float bands_h = kEditorBands * u;
    const float actions_h = kEditorActions * u;
    float loupe_h = kEditorLoupe * u;
    const float min_roll = kEditorMinRoll * u;

    // toolbar, gap, ruler + roll + bands (one block), gap, loupe, gap, actions
    const float fixed = toolbar_h + ruler_h + bands_h + actions_h + 3.0f * gap;
    float roll_h = area.h - fixed - loupe_h;
    if (roll_h < min_roll) {
        loupe_h = std::max(kEditorLoupeMin * u, loupe_h - (min_roll - roll_h));
        roll_h = area.h - fixed - loupe_h;
    }
    if (roll_h < min_roll) {
        l.fits = false;
        roll_h = std::max(0.0f, roll_h);
    }
    float y = area.y;
    l.toolbar = {area.x, y, area.w, toolbar_h};
    y += toolbar_h + gap;
    l.ruler = {area.x, y, area.w, ruler_h};
    y += ruler_h;
    l.roll = {area.x, y, area.w, roll_h};
    y += roll_h;
    l.bands = {area.x, y, area.w, bands_h};
    y += bands_h + gap;
    l.loupe = {area.x, y, area.w, loupe_h};
    y += loupe_h + gap;
    l.actions = {area.x, y, area.w, actions_h};
    return l;
}

LaneLayout lane_layout(const RectF& area, float u) {
    LaneLayout l;
    const float hit = kEditorHit * u;
    const float gap = kLaneGap * u;
    // Two columns of two tools; a lane shorter than two rows puts all four in one row (only a lane
    // under its least height, which the screen layout never opens).
    const bool two_rows = area.h >= 2.0f * hit + gap;
    const float row_h = two_rows ? std::max(hit, (area.h - gap) * 0.5f) : std::max(hit, area.h);
    const float y0 = area.y;
    const float y1 = two_rows ? area.y + area.h - row_h : area.y;
    // The tools are a hit and a dead gap apart.
    const float pitch = hit + gap;
    l.split = {area.x, y0, hit, row_h};
    l.across = {area.x + pitch, y0, hit, row_h};
    l.undo = two_rows ? RectF{area.x, y1, hit, row_h} : RectF{area.x + 2.0f * pitch, y0, hit, row_h};
    l.fit =
        two_rows ? RectF{area.x + pitch, y1, hit, row_h} : RectF{area.x + 3.0f * pitch, y0, hit, row_h};
    const float tools_w = two_rows ? pitch + hit : 3.0f * pitch + hit;
    const float x = area.x + tools_w + gap;
    const RectF strip{x, area.y, std::max(0.0f, area.right() - x), area.h};
    EditorLayout& e = l.strip;
    e.area = strip;
    const float ruler_h = kLaneRuler * u;
    const float bands_h = kEditorBands * u;
    const float roll_h = std::max(0.0f, strip.h - ruler_h - bands_h);
    e.fits = roll_h > 0.0f;
    e.ruler = {strip.x, strip.y, strip.w, ruler_h};
    e.roll = {strip.x, strip.y + ruler_h, strip.w, roll_h};
    e.bands = {strip.x, strip.y + ruler_h + roll_h, strip.w, bands_h};
    e.toolbar = {strip.x, strip.y, strip.w, 0.0f};
    e.loupe = {strip.x, strip.bottom(), strip.w, 0.0f};
    e.actions = {strip.x, strip.bottom(), strip.w, 0.0f};
    return l;
}

ToolbarSlots toolbar_slots(const RectF& bar, float u) {
    ToolbarSlots s;
    const float hit = kEditorHit * u;
    const float gap = kMinHitGapPt * u;
    // From the right: close (the top right corner, where the settings sheet's close is too),
    // suggest, undo, zoom in, zoom out, fit; from the left: the title in what is left.
    float x = bar.right();
    const auto take_right = [&](RectF& r) {
        x -= hit;
        r = {x, bar.y, hit, bar.h};
        x -= gap;
    };
    take_right(s.close);
    x -= kEditorCloseGap * u; // a tool tapped in a hurry is never the close
    take_right(s.suggest);
    take_right(s.undo);
    take_right(s.zoom_in);
    take_right(s.zoom_out);
    take_right(s.fit);
    const float tx = bar.x + 4.0f * u;
    const float tw = x - tx;
    s.title = tw >= 60.0f * u ? RectF{tx, bar.y, tw, bar.h} : RectF{};
    return s;
}

ActionSlots action_slots(const RectF& bar, float u) {
    ActionSlots s;
    const float hit = kEditorHit * u;
    const float gap = kMinHitGapPt * u;
    const float min_chip = 40.0f * u;
    // Six captioned buttons, the gaps (one more before the chip), the room Delete keeps from the
    // button beside it (its gap is one of the gaps counted) and the least chip.
    const float wide = hit + kEditorCaptionExtra * u;
    const float delete_room = std::max(0.0f, kDestructiveGap * u - gap);
    s.captions = 6.0f * wide + 8.0f * gap + delete_room + min_chip <= bar.w;
    const float w = s.captions ? wide : hit;
    float x = bar.x;
    const auto take_left = [&](RectF& r) {
        r = {x, bar.y, w, bar.h};
        x += w + gap;
    };
    take_left(s.split);
    take_left(s.play);
    take_left(s.play_across);
    // The band's own bar from the right: delete, merge, rename, then its chip in what is left.
    float right = bar.right();
    const auto take_right = [&](RectF& r) {
        right -= w;
        r = {right, bar.y, w, bar.h};
        right -= gap;
    };
    take_right(s.band_delete);
    right -= delete_room;
    take_right(s.band_merge);
    take_right(s.band_rename);
    const float chip_x = x + gap * 2.0f;
    s.band_chip = right - chip_x >= min_chip ? RectF{chip_x, bar.y, right - chip_x, bar.h} : RectF{};
    return s;
}

float editor_x(const EditorLayout& layout, const RollView& view, double seconds) {
    const double span = view.span_s();
    if (!(span > 0.0)) {
        return layout.axis_x0();
    }
    const double t = (seconds - view.start_s) / span;
    return layout.axis_x0() + static_cast<float>(t) * (layout.axis_x1() - layout.axis_x0());
}

double editor_time(const EditorLayout& layout, const RollView& view, float x) {
    const float w = layout.axis_x1() - layout.axis_x0();
    if (!(w > 0.0f)) {
        return view.start_s;
    }
    return view.start_s + static_cast<double>((x - layout.axis_x0()) / w) * view.span_s();
}

float band_name_left(const EditorLayout& layout, const RollView& view, double band_start_s,
                     float reach) {
    return std::max(editor_x(layout, view, band_start_s), layout.axis_x0()) + reach;
}

EditorHit editor_hit(const EditorLayout& layout, const RollView& view, const SongSections& sections,
                     double cursor_s, bool cursor_visible, float x, float y, float reach,
                     const float* name_w, std::size_t name_count, float handle_margin) {
    EditorHit hit;
    const bool in_roll = y >= layout.ruler.y && y < layout.roll.bottom();
    const bool in_bands = y >= layout.bands.y && y < layout.bands.bottom();
    if (x < layout.area.x || x >= layout.area.right() || !(in_roll || in_bands)) {
        return hit;
    }
    // Handles and the cursor: the nearest whose grab zone holds the touch (a handle on a tie).
    float best = reach;
    float best_x = 0.0f;
    for (std::size_t i = 0; i + 1 < sections.sections.size(); ++i) {
        const float hx = editor_x(layout, view, sections.sections[i].end_s);
        const float d = std::fabs(hx - x);
        if (d <= best) {
            best = d;
            best_x = hx;
            hit.target = EditorTarget::Handle;
            hit.handle = i;
        }
    }
    if (cursor_visible && in_roll) {
        const float cx = editor_x(layout, view, cursor_s);
        const float d = std::fabs(cx - x);
        // A handle within `handle_margin` of the cursor line keeps the touch whoever is nearer: a
        // hold on a boundary the playhead stands beside must take the handle, not cut at the cursor.
        const bool handle_wins = hit.target == EditorTarget::Handle &&
                                 (d >= best || std::fabs(cx - best_x) <= handle_margin);
        if (d <= reach && !handle_wins) {
            hit.target = EditorTarget::Cursor;
            return hit;
        }
    }
    if (hit.target == EditorTarget::Handle) {
        return hit;
    }
    const double t = editor_time(layout, view, x);
    const Section* band = sections.at(std::clamp(t, 0.0, sections.sections.empty()
                                                              ? 0.0
                                                              : sections.sections.back().end_s));
    if (band != nullptr) {
        hit.section = band->id;
    }
    if (in_bands) {
        if (band != nullptr) {
            const std::size_t i = static_cast<std::size_t>(band - sections.sections.data());
            const float left = band_name_left(layout, view, band->start_s, reach);
            const float w = name_w != nullptr && i < name_count ? name_w[i] : 0.0f;
            hit.target = w > 0.0f && x >= left && x < left + w ? EditorTarget::BandName
                                                                : EditorTarget::Band;
        } else {
            hit.target = EditorTarget::None;
        }
        return hit;
    }
    hit.target = EditorTarget::Roll;
    return hit;
}

RectF loupe_bubble_rect(float touch_x, float touch_y, const RectF& area, float width, float height,
                        float lift, float margin) {
    RectF r;
    r.w = width;
    r.h = height;
    r.x = std::clamp(touch_x - width * 0.5f, area.x + margin,
                     std::max(area.x + margin, area.right() - width - margin));
    const float above = touch_y - lift - height;
    const float below = touch_y + lift;
    if (above >= area.y + margin) {
        r.y = above;
    } else if (below + height <= area.bottom() - margin) {
        r.y = below;
    } else {
        r.y = area.y + margin;
    }
    return r;
}

LoupeRegions loupe_regions(const RectF& rect, float inset, float header_h) {
    LoupeRegions r;
    const float x = rect.x + inset;
    const float y = rect.y + inset;
    const float w = std::max(0.0f, rect.w - 2.0f * inset);
    const float h = std::max(0.0f, rect.h - 2.0f * inset);
    const float head = std::clamp(header_h, 0.0f, h * 0.4f);
    r.header = {x, y, w, head};
    r.notes = {x, y + head, w, h - head};
    r.mid_x = x + w * 0.5f;
    return r;
}

RectF readout_pill_rect(float centre_x, float centre_y, float min_x, float max_x, float text_w,
                        float text_h, float pad_x, float pad_y) {
    const float w = text_w + 2.0f * pad_x;
    const float x = std::clamp(centre_x - w * 0.5f, min_x, std::max(min_x, max_x - w));
    return {x, centre_y - text_h * 0.5f - pad_y, w, text_h + 2.0f * pad_y};
}

bool ruler_label_hidden(const RectF& label, const RectF& pill, float margin) {
    if (pill.empty()) {
        return false;
    }
    return label.x < pill.right() + margin && label.right() > pill.x - margin &&
           label.y < pill.bottom() + margin && label.bottom() > pill.y - margin;
}

} // namespace etude
