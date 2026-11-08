// SPDX-License-Identifier: GPL-3.0-or-later
#include "kit.hpp"

#include "kit_internal.hpp"

#include "clef_icon.hpp"
#include "font_ink.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "pressed_fade.hpp"
#include "row_layout.hpp"
#include "sections_port.hpp"
#include "etude/accidental_text.hpp"
#include "etude/note_style.hpp"
#include "transport.hpp"

#include <imgui_internal.h> // ClearActiveID

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace etude::app::ui {

namespace {

// A slider row's reset button (reset_to.hpp): `has` it, `active` while the setting is off its
// default, and `tapped` when this frame's tap landed on it (the wrapper does the reset, in the
// setting itself).
struct SliderReset {
    bool has = false;
    bool active = false;
    bool tapped = false;
};

// The toast of a reset, "Latency reset to 0ms" (the default written as the row writes its value),
// with an Undo that puts the exact earlier value back in the setting.
template <class T>
void say_reset_to(Kit& k, const char* label, const char* default_text, T* target, T prior) {
    k.say_undoable(std::string(label) + " reset to " + default_text, undo_of(target, prior));
}

// Where a slider row's track runs, from what its label side placed.
struct SliderGeom {
    float tx0 = 0.0f;
    float tx1 = 0.0f;
    float y = 0.0f; // the top of the track's row (under the label when the row is stacked)
    float hit_x1 = 0.0f; // where the track's touch area ends
};

// The icon, the label with its info button and the value text of a slider row from `pos`; the
// track's ends.
SliderGeom slider_labels(Kit& k, ImDrawList* dl, const char* icon, const char* label,
                         const char* value_text, const char* info, bool with_reset,
                         const RowFrame& frame, ImVec2 pos, float w) {
    const float h = k.s(tok::kHit);
    const float lsize = k.s(tok::kBody);
    const float value_w = k.s(52.0f);
    float x = pos.x + k.s(tok::kS3);
    float label_end = x;
    const ImVec2 ls = text_size(k.regular, lsize, label);
    // The track starts where the column's controls start, whatever this row's label is; the label
    // side is as wide as the widest label of the column needs. Sliders in a column stack together.
    const float info_w = info != nullptr ? k.s(kInfoReserve) : 0.0f;
    row_report(frame, x + k.s(tok::kIcon) + k.s(tok::kS3) + ls.x + info_w - pos.x);
    if (with_reset) {
        row_report_reset(frame);
    }
    const bool stacked = frame.plan.stacked;
    const Span box = wide_span(frame.plan, false, k.u);
    const float lh = stacked ? k.s(info != nullptr ? kStackedInfoLabelH : kStackedLabelH) : h;
    const float cy = stacked ? stacked_label_cy(k, pos.y, info != nullptr) : pos.y + lh * 0.5f;
    if (icon != nullptr) {
        glyph(k, dl, icon, {x + k.s(tok::kIcon) * 0.5f, cy}, k.s(tok::kIcon), col::kText2);
    }
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    const RowLabel shown_label = row_label(
        k.regular, lsize, label, label_room(k, stacked ? box.x1 : box.x0, x) - info_w);
    draw_row_label(k, dl, k.regular, lsize, {x, cy - lh * 0.5f, pos.x + w, cy + lh * 0.5f},
                   col::kText, shown_label);
    label_end = x + shown_label.width;
    if (info != nullptr) {
        info_button(k, info, {label_end + k.s(kInfoOffset), cy}, nullptr, nullptr, 0.0f,
                    stacked ? (k.s(tok::kHit) - k.s(tok::kChip)) * 0.5f : 0.0f);
    }
    const float vsize = k.s(tok::kLabel);
    label_at(k, dl, k.regular, vsize, {x, cy - lh * 0.5f, frame.plan.control_right, cy + lh * 0.5f},
             LabelAlign::Right, col::kText2, value_text);
    SliderGeom g;
    // The track starts where a segmented track in the column does; the knob overhangs its ends.
    g.tx0 = box.x0;
    g.y = pos.y;
    if (stacked) {
        g.y += lh;
        g.tx1 = slider_track_end(frame.plan, box.x1, true, k.u);
    } else {
        g.tx1 = box.x1 - value_w;
    }
    g.hit_x1 = track_hit_right(frame.plan, g.tx1, k.u);
    // What the row drew: the track's start, and its end (stacked) or the value's right edge.
    column_probe::note(frame, RowKind::Wide, {g.tx0, stacked ? box.x1 : frame.plan.control_right},
                       stacked);
    return g;
}

// The track of a slider row: its touch, its fill and its knob. True when the value moved.
bool slider_track(Kit& k, ImDrawList* dl, float* t, const SliderGeom& g) {
    const float h = k.s(tok::kHit);
    const float tx0 = g.tx0;
    const float tx1 = g.tx1;
    // The hit area is the whole track row, the knob's radius past each end.
    ImGui::SetCursorScreenPos({tx0 - k.s(kKnobReach), g.y});
    ImGui::InvisibleButton("##track", {g.hit_x1 - tx0 + k.s(kKnobReach), h});
    const bool active = ImGui::IsItemActive();
    k.slider_held = k.slider_held || active;
    // The knob is the slider's state: it grows from the frame a finger lands on it.
    const State st = item_state(k, false);
    {
        const Box hit_row{tx0 - k.s(kKnobReach), g.y, g.hit_x1, g.y + h};
        audit::face("##track", {hit_row.x0, hit_row.y0}, {hit_row.x1, hit_row.y1},
                    {hit_row.x0, hit_row.y0}, {hit_row.x1, hit_row.y1}, dl);
    }
    bool changed = false;
    const ImGuiIO& io = ImGui::GetIO();
    const SliderTrack track{tx0, tx1};
    const auto apply = [&](std::optional<float> v) {
        if (v && *v != *t) {
            *t = *v;
            changed = true;
        }
    };
    if (ImGui::IsItemActivated()) {
        const SliderPointer who =
            io.MouseSource == ImGuiMouseSource_Mouse ? SliderPointer::Mouse : SliderPointer::Finger;
        apply(k.slider.press(who, io.MouseClickedPos[0].x, io.MouseClickedPos[0].y, track, *t,
                             k.s(tok::kHit) * 0.5f));
    }
    if (active) {
        apply(k.slider.move(io.MousePos.x, io.MousePos.y, track, k.s(kTouchSlopPt)));
        // Only a slider that took the touch keeps the sheet from scrolling with it.
        k.slider_active = k.slider.dragging();
    }
    if (ImGui::IsItemDeactivated()) {
        apply(k.slider.release(io.MousePos.x, track, io.MouseDown[0]));
    }
    const float cy = g.y + h * 0.5f;
    const float th = k.s(tok::kTrack);
    dl->AddRectFilled({tx0, cy - th * 0.5f}, {tx1, cy + th * 0.5f}, u32({1, 1, 1, 0.12f}), th);
    const float kx = tx0 + (tx1 - tx0) * *t;
    dl->AddRectFilled({tx0, cy - th * 0.5f}, {kx, cy + th * 0.5f}, u32(col::kAccent), th);
    const bool grown = (active && !k.slider.scrolling()) ||
                       (st.pressed >= 1.0f && audit::forced_state() == audit::Forced::Pressed);
    const float kr = k.s(grown ? tok::kKnobHeld : tok::kKnob) * 0.5f;
    dl->AddCircleFilled({kx, cy + k.s(1.0f)}, kr + k.s(1.0f), u32({0, 0, 0, 0.3f}), 24);
    dl->AddCircleFilled({kx, cy}, kr, u32({0.96f, 0.97f, 1.0f, 1.0f}), 24);
    return changed;
}

bool slider_core(Kit& k, const char* icon, const char* label, float* t, const char* value_text,
                 const char* info, SliderReset* reset = nullptr) {
    float w = 0;
    const ImVec2 start = row_start(&w);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(label);
    const RowFrame frame = row_frame(start.x, w, k.u);
    const bool with_reset = reset != nullptr && reset->has;
    const SliderGeom g =
        slider_labels(k, dl, icon, label, value_text, info, with_reset, frame, start, w);
    const bool changed = slider_track(k, dl, t, g);
    if (with_reset) {
        reset->tapped = reset_button(k, frame, g.y + k.s(tok::kHit) * 0.5f, reset->active, label);
    }
    ImGui::PopID();
    end_row(start, {w, g.y - start.y + k.s(tok::kHit)});
    return changed;
}

} // namespace

bool slider_row(Kit& k, const char* icon, const char* label, float* value, float lo, float hi,
                float shown_scale, Unit unit, int decimals, const char* info,
                std::optional<ResetTo<float>> reset_to) {
    const UnitText text = format_unit(static_cast<double>(*value * shown_scale), unit, decimals);
    float t = (*value - lo) / (hi - lo);
    float* const target = reset_to && reset_to->target != nullptr ? reset_to->target : value;
    SliderReset reset;
    if (reset_to) {
        reset.has = true;
        reset.active = reset_active(*target, reset_to->def, hi - lo);
    }
    const bool dragged = slider_core(k, icon, label, &t, text.c_str(), info, &reset);
    if (const auto prior = reset.tapped ? tap_reset(target, reset_to->def, hi - lo) : std::nullopt) {
        const double shown = static_cast<double>(reset_to->def * shown_scale);
        say_reset_to(k, label, format_unit(shown, unit, decimals).c_str(), target, *prior);
        return false; // the reset wrote the setting itself
    }
    if (dragged) {
        *value = lo + t * (hi - lo);
    }
    return dragged;
}

bool slider_row_int(Kit& k, const char* icon, const char* label, int* value, int lo, int hi,
                    Unit unit, const char* info, std::optional<ResetTo<int>> reset_to) {
    const UnitText text = format_unit(*value, unit);
    float t = static_cast<float>(*value - lo) / static_cast<float>(hi - lo);
    int* const target = reset_to && reset_to->target != nullptr ? reset_to->target : value;
    SliderReset reset;
    if (reset_to) {
        reset.has = true;
        reset.active = reset_active(*target, reset_to->def);
    }
    const bool dragged = slider_core(k, icon, label, &t, text.c_str(), info, &reset);
    if (const auto prior = reset.tapped ? tap_reset(target, reset_to->def) : std::nullopt) {
        say_reset_to(k, label, format_unit(reset_to->def, unit).c_str(), target, *prior);
        return false; // the reset wrote the setting itself
    }
    if (dragged) {
        const int v = lo + static_cast<int>(std::lround(t * static_cast<float>(hi - lo)));
        if (v != *value) {
            *value = v;
            return true;
        }
    }
    return false;
}

bool stepper_row(Kit& k, const char* icon, const char* label, int* value, int lo, int hi,
                 Unit unit, int skip_dir, std::optional<ResetTo<int>> reset_to) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(label);
    float x = pos.x + k.s(tok::kS3);
    if (skip_dir != 0) {
        // The skip arrow with the step inside; its number morphs as the step changes.
        skip_icon(k, dl, ImGui::GetID("##skip-number"),
                  {x + k.s(tok::kIcon) * 0.5f, pos.y + h * 0.5f}, k.s(tok::kIcon), *value,
                  skip_dir > 0, col::kText2);
    } else if (icon != nullptr) {
        glyph(k, dl, icon, {x + k.s(tok::kIcon) * 0.5f, pos.y + h * 0.5f}, k.s(tok::kIcon),
              col::kText2);
    }
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    // The control: the track starts where the column's controls start and fills the room to their
    // end: a minus cell, the value and a plus cell, each a full hit.
    const RowFrame frame = row_frame(pos.x, w, k.u);
    const float lsize = k.s(tok::kBody);
    row_report(frame, x - pos.x + text_size(k.regular, lsize, label).x);
    if (reset_to) {
        row_report_reset(frame);
    }
    const Span track = inline_span(frame.plan, k.u);
    column_probe::note(frame, RowKind::Inline, track, frame.plan.stacked);
    const float cell = k.s(tok::kHit);
    const float x0 = track.x0;
    const float track_w = track.w();
    const float value_w = track_w - 2.0f * cell;
    draw_row_label(k, dl, k.regular, lsize, {x, pos.y, pos.x + w, pos.y + h}, col::kText,
                   row_label(k.regular, lsize, label, label_room(k, x0, x)));
    const float ch = k.s(tok::kFace);
    const ImVec2 ta{x0, pos.y + (h - ch) * 0.5f};
    dl->AddRectFilled(ta, {ta.x + track_w, ta.y + ch}, u32(col::kFill), k.s(tok::kRadiusControl));
    bool changed = false;
    const auto button = [&](const char* id, const char* glyph_text, float bx, int delta) {
        ImGui::SetCursorScreenPos({bx, pos.y});
        const bool enabled = delta < 0 ? *value > lo : *value < hi;
        const bool pressed = ImGui::InvisibleButton(id, {cell, h});
        const State st = item_state(k, false, true, !enabled);
        const ImVec2 c{bx + cell * 0.5f, pos.y + h * 0.5f};
        audit::note(id, {bx, ta.y}, {bx + cell, ta.y + ch});
        const float in = k.s(tok::kInset);
        draw_state(k, dl, id, {bx + in, ta.y + in, bx + cell - in, ta.y + ch - in},
                   {bx, pos.y, bx + cell, pos.y + h}, st, Idle::None, k.s(tok::kRadiusInner));
        glyph(k, dl, glyph_text, c, k.s(tok::kIconSm), state_ink(st, col::kText));
        if (pressed && enabled) {
            *value = std::clamp(*value + delta, lo, hi);
            changed = true;
        }
    };
    button("##minus", icon::kMinus, x0, -1);
    button("##plus", icon::kPlus, x0 + cell + value_w, 1);
    const UnitText text = format_unit(*value, unit, 0, false);
    const float vsize = k.s(tok::kLabel);
    label_at(k, dl, k.bold, vsize, {x0 + cell, ta.y, x0 + cell + value_w, ta.y + ch},
             LabelAlign::Centre, col::kText, text.c_str());
    if (reset_to) {
        int* const target = reset_to->target != nullptr ? reset_to->target : value;
        const bool tapped = reset_button(k, frame, pos.y + h * 0.5f, reset_active(*target, reset_to->def), label);
        if (const auto prior = tapped ? tap_reset(target, reset_to->def) : std::nullopt) {
            say_reset_to(k, label, format_unit(reset_to->def, unit).c_str(), target, *prior);
        }
    }
    ImGui::PopID();
    end_row(pos, {w, h});
    return changed;
}

int stepper_text_row(Kit& k, const char* icon, const char* label, const char* text, bool can_minus,
                     bool can_plus) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(label);
    float x = pos.x + k.s(tok::kS3);
    const Box slot{x, pos.y, x + k.s(tok::kIcon), pos.y + h};
    if (icon != nullptr) {
        glyph(k, dl, icon, {slot.cx(), slot.cy()}, k.s(tok::kIcon), col::kText2);
    }
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    // The control, as stepper_row's: a minus cell, the value and a plus cell, each a full hit.
    const RowFrame frame = row_frame(pos.x, w, k.u);
    const float lsize = k.s(tok::kBody);
    row_report(frame, x - pos.x + text_size(k.regular, lsize, label).x);
    const Span track = inline_span(frame.plan, k.u);
    column_probe::note(frame, RowKind::Inline, track, frame.plan.stacked);
    const float cell = k.s(tok::kHit);
    const float x0 = track.x0;
    const float track_w = track.w();
    const float value_w = track_w - 2.0f * cell;
    draw_row_label(k, dl, k.regular, lsize, {x, pos.y, pos.x + w, pos.y + h}, col::kText,
                   row_label(k.regular, lsize, label, label_room(k, x0, x)));
    const float ch = k.s(tok::kFace);
    const ImVec2 ta{x0, pos.y + (h - ch) / 2.0f};
    dl->AddRectFilled(ta, {ta.x + track_w, ta.y + ch}, u32(col::kFill), k.s(tok::kRadiusControl));
    int delta = 0;
    const auto button = [&](const char* id, const char* glyph_text, float bx, int direction) {
        ImGui::SetCursorScreenPos({bx, pos.y});
        const bool enabled = direction < 0 ? can_minus : can_plus;
        const bool pressed = ImGui::InvisibleButton(id, {cell, h});
        const State st = item_state(k, false, true, !enabled);
        const Box cell_box{bx, pos.y, bx + cell, pos.y + h};
        audit::note(id, {bx, ta.y}, {bx + cell, ta.y + ch});
        const float in = k.s(tok::kInset);
        draw_state(k, dl, id, {bx + in, ta.y + in, bx + cell - in, ta.y + ch - in},
                   {bx, pos.y, bx + cell, pos.y + h}, st, Idle::None, k.s(tok::kRadiusInner));
        glyph(k, dl, glyph_text, {cell_box.cx(), cell_box.cy()}, k.s(tok::kIconSm),
              state_ink(st, col::kText));
        if (pressed && enabled) {
            delta = direction;
        }
    };
    button("##minus", icon::kMinus, x0, -1);
    button("##plus", icon::kPlus, x0 + cell + value_w, 1);
    const float vsize = k.s(tok::kLabel);
    label_at(k, dl, k.bold, vsize, {x0 + cell, ta.y, x0 + cell + value_w, ta.y + ch},
             LabelAlign::Centre, col::kText, text);
    ImGui::PopID();
    end_row(pos, {w, h});
    return delta;
}

void readout_row(Kit& k, const char* icon, const char* text) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = pos.x + k.s(tok::kS3);
    const Box slot{x, pos.y, x + k.s(tok::kIcon), pos.y + h};
    if (icon != nullptr) {
        glyph(k, dl, icon, {slot.cx(), slot.cy()}, k.s(tok::kIcon), col::kText3);
    }
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    const float size = k.s(tok::kLabel);
    const FitText shown = fit(k.regular, size, text, pos.x + w - k.s(tok::kS3) - x);
    label_at(k, dl, k.regular, size, {x, pos.y, pos.x + w, pos.y + h}, LabelAlign::Left,
             col::kText2, shown.c_str());
    end_row(pos, {w, h});
}

} // namespace etude::app::ui
