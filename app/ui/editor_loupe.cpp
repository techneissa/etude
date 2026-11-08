// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor.hpp"

#include "editor_internal.hpp"

#include "../frame_log.hpp"
#include "../gfx.hpp"
#include "../touch_device.hpp"
#include "icons.hpp"
#include "sections_ui.hpp"

#include "etude/section_editing.hpp"
#include "etude/section_overview.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace etude::app::ui {

namespace {

constexpr float kFingerLift = 64.0f;  // design points between a dragging finger and the magnifier
constexpr float kBubbleW = 250.0f;
constexpr float kBubbleH = 120.0f;

} // namespace

// While a handle or the cursor is dragged, the magnifier floats above the finger, where the finger
// does not hide it.
void SectionEditor::floating_loupe(Kit& k, const SectionsView& v) {
    if (!tracking_ || (drag_ != Drag::Handle && drag_ != Drag::Cursor)) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RectF bubble = loupe_bubble_rect(touch_pt_.x, touch_pt_.y, bubble_area_, k.s(kBubbleW),
                                           k.s(kBubbleH), k.s(kFingerLift), k.s(6.0f));
    const float radius = k.s(tok::kRadiusMd);
    dl->AddRectFilled({bubble.x + k.s(2.0f), bubble.y + k.s(3.0f)},
                      {bubble.right() + k.s(2.0f), bubble.bottom() + k.s(3.0f)},
                      u32({0.0f, 0.0f, 0.0f, 0.35f}), radius);
    dl->AddRectFilled(min_of(bubble), max_of(bubble), u32({0.045f, 0.052f, 0.075f, 1.0f}), radius);
    draw_loupe(k, dl, v, bubble, drag_ == Drag::Handle ? handle_s_ : cursor_s_);
    dl->AddRect(min_of(bubble), max_of(bubble), u32(col::kAccent), radius, 0, k.s(1.6f));
}

EditorHit SectionEditor::hit_at(Kit& k, const SectionsView& v, const EditorLayout& l, float x,
                                float y) const {
    return editor_hit(l, view_, v.data, cursor_s_, cursor_set_, x, y, k.s(kEditorGrabReach),
                      name_w_.data(), name_w_.size(), k.s(kTouchSlopPt));
}

void SectionEditor::loupe(Kit& k, const SectionsView& v, const EditorLayout& l) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = k.s(tok::kRadiusMd);
    dl->AddRectFilled(min_of(l.loupe), max_of(l.loupe), u32({0.045f, 0.052f, 0.075f, 1.0f}), radius);
    const bool on_handle = drag_ == Drag::Handle;
    if (!on_handle && !cursor_set_) {
        const char* hint = "Tap the roll to put the cut cursor where you want to split";
        const float size = k.s(tok::kLabel);
        label_at(k, dl, k.regular, size, {l.loupe.x, l.loupe.y, l.loupe.right(), l.loupe.bottom()},
                 LabelAlign::Centre, col::kText3, hint);
        return;
    }
    draw_loupe(k, dl, v, l.loupe, on_handle ? handle_s_ : cursor_s_);
}

// The notes around `focus`, magnified, split by the cut: before it at full strength, after it
// softer, a note across it outlined. The labels have a header band of their own and the notes are
// clipped below it, so nothing is ever drawn through the text.
void SectionEditor::draw_loupe(Kit& k, ImDrawList* dl, const SectionsView& v, const RectF& rect,
                               double focus) {
    const Song& song = *v.song;
    const double half = std::clamp(min_view_span(song, focus) * 0.5, 1.0, 4.0);
    const float cap = k.s(tok::kCaption);
    const LoupeRegions g = loupe_regions(rect, k.s(6.0f), cap + k.s(8.0f));
    const ImVec2 a{g.notes.x, g.notes.y};
    const ImVec2 b{g.notes.right(), g.notes.bottom()};
    const float w = g.notes.w;
    const float h = g.notes.h;
    const PitchRange pitches = window_pitch_range(song, focus - half, focus + half, 18);
    roll_note_rects(song, focus - half, focus + half, std::max(1, static_cast<int>(w)),
                    std::max(1, static_cast<int>(h)), pitches, focus, rects_);
    dl->PushClipRect(a, b, true);
    const float mid = g.mid_x;
    dl->AddRectFilled({mid, a.y}, {b.x, b.y}, u32({1, 1, 1, 0.035f}));
    for (const RollRect& r : rects_) {
        ImVec4 c = hand_colour(k, r.hand);
        const ImVec2 p0{a.x + r.x0, a.y + r.y0};
        const ImVec2 p1{a.x + r.x1, a.y + std::max(r.y1 - 0.8f, r.y0 + 1.0f)};
        if (r.side < 0) {
            dl->AddRectFilled(p0, p1, u32(with_alpha(c, 0.95f)), 1.5f);
        } else if (r.side > 0) {
            dl->AddRectFilled(p0, p1, u32(with_alpha(c, 0.5f)), 1.5f);
        } else {
            dl->AddRectFilled(p0, p1, u32(with_alpha(c, 0.8f)), 1.5f);
            dl->AddRect({p0.x, p0.y - 1.0f}, {p1.x, p1.y + 1.0f}, u32(kTroubleCut), 1.5f, 0, 1.5f);
        }
        const FoldMarker m = fold_marker(p0.x, p0.y, p1.x, p1.y, r.fold);
        if (m.shown) {
            dl->AddTriangleFilled(m.tip, m.left, m.right, u32({1.0f, 1.0f, 1.0f, 0.85f}));
        }
    }
    dl->AddLine({mid, a.y}, {mid, b.y}, u32(col::kAccent), k.s(2.0f));
    dl->PopClipRect();
    // The header: the two labels either side of the scissors, each only if it fits.
    const float cy = g.header.y + g.header.h * 0.5f;
    const float icon_half = k.s(tok::kIconSm) * 0.5f + k.s(4.0f);
    const Box header{g.header.x, g.header.y, g.header.right(), g.header.bottom()};
    const ImVec2 bs = text_size(k.regular, cap, "before the cut");
    if (g.header.x + k.s(6.0f) + bs.x <= mid - icon_half) {
        label_at(k, dl, k.regular, cap, {header.x0 + k.s(6.0f), header.y0, mid, header.y1},
                 LabelAlign::Left, col::kText3, "before the cut");
    }
    const ImVec2 as = text_size(k.regular, cap, "after");
    if (g.header.right() - k.s(6.0f) - as.x >= mid + icon_half) {
        label_at(k, dl, k.regular, cap, {mid, header.y0, header.x1 - k.s(6.0f), header.y1},
                 LabelAlign::Right, col::kText3, "after");
    }
    glyph(k, dl, icon::kScissors, {mid, cy}, k.s(tok::kIconSm), col::kAccent);
}

} // namespace etude::app::ui
