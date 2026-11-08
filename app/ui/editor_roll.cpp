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

constexpr double kSnapFlashS = 0.32;  // how long the mark where a snap landed lasts
// The marks drawn on the notes, design points. A boundary's line and its grip (the two ridges are
// the grip's ink), and the mark where a snap landed: a line, bigger on a bar line than on a beat,
// and a ring that opens from its radius by `kSnapRingGrow`.
constexpr float kHandleW = 1.6f;
constexpr float kHandleWGrabbed = 2.4f;
constexpr float kGripHalfW = 4.5f;
constexpr float kGripHalfH = 11.0f;
constexpr float kGripRidgeX = 1.2f;
constexpr float kGripRidgeHalfH = 5.0f;
constexpr float kSnapBarW = 5.0f;
constexpr float kSnapBeatW = 3.0f;
constexpr float kSnapRingBar = 7.0f;
constexpr float kSnapRingBeat = 5.0f;
constexpr float kSnapRingGrow = 12.0f;
constexpr float kSnapRingW = 2.0f;
// A dashed vertical line.
void dashed_v(ImDrawList* dl, float x, float y0, float y1, ImU32 colour, float thickness, float dash,
              float gap) {
    for (float y = y0; y < y1; y += dash + gap) {
        dl->AddLine({x, y}, {x, std::min(y + dash, y1)}, colour, thickness);
    }
}
// A small pill with text, centred on `centre.x` and kept inside [min_x, max_x]: where it goes
// (readout_pill_rect) and the drawing of it.
RectF pill_rect(const Kit& k, ImVec2 centre, float min_x, float max_x, const std::string& text) {
    const ImVec2 ts = text_size(k.bold, k.s(tok::kCaption), text.c_str());
    return readout_pill_rect(centre.x, centre.y, min_x, max_x, ts.x, ts.y, k.s(6.0f), k.s(3.0f));
}
void label_pill(const Kit& k, ImDrawList* dl, ImVec2 centre, float min_x, float max_x,
                const std::string& text, ImVec4 fill, ImVec4 ink) {
    const RectF r = pill_rect(k, centre, min_x, max_x, text);
    dl->AddRectFilled({r.x, r.y}, {r.right(), r.bottom()}, u32(fill), r.h * 0.5f);
    label_at(k, dl, k.bold, k.s(tok::kCaption), {r.x, r.y, r.right(), r.bottom()},
             LabelAlign::Centre, ink, text.c_str());
}

} // namespace

void SectionEditor::roll(Kit& k, const SectionsView& v, const EditorLayout& l) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Song& song = *v.song;
    const float cap = k.s(tok::kCaption);
    const float reach = k.s(kEditorGrabReach);
    const float axis0 = l.axis_x0();
    const float axis1 = l.axis_x1();
    const auto X = [&](double s) { return editor_x(l, view_, s); };
    const auto visible = [&](float x) { return x >= axis0 - 0.5f && x <= axis1 + 0.5f; };

    // ---- The strip's own background and the ruler.
    const RectF strip{l.ruler.x, l.ruler.y, l.ruler.w, l.bands.bottom() - l.ruler.y};
    dl->AddRectFilled(min_of(strip), max_of(strip), u32({0.045f, 0.052f, 0.075f, 1.0f}),
                      k.s(tok::kRadiusMd));
    dl->PushClipRect(min_of(strip), max_of(strip), true);

    // ---- The notes.
    if (tile_tex_ != nullptr && tile_.valid() && view_.start_s >= tile_.t0_s - 1e-6 &&
        view_.end_s <= tile_.t1_s + 1e-6) {
        float u0 = 0.0f;
        float u1 = 1.0f;
        tile_uv(tile_, view_, u0, u1);
        const float share = static_cast<float>(tile_.w) / static_cast<float>(kMaxTileWidth);
        const ImTextureRef tex(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(tile_tex_)));
        dl->AddImage(tex, min_of(l.roll), max_of(l.roll), {u0 * share, 0.0f}, {u1 * share, 1.0f});
    }

    // ---- Bands tint the notes; the grid and the ruler.
    for (std::size_t i = 0; i < v.sections.size(); ++i) {
        const SectionRow& r = v.sections[i];
        const float a = std::max(X(r.start_s), axis0);
        const float b = std::min(X(r.end_s), axis1);
        if (b <= a) {
            continue;
        }
        const bool sel = r.id == selected_;
        const ImVec4 tint = band_tint(i);
        dl->AddRectFilled({a, l.roll.y}, {b, l.roll.bottom()},
                          u32(with_alpha(tint, sel ? 0.20f : 0.10f)));
    }
    // The readout of the cursor or of a dragged handle stands over the ruler where the roll is too
    // short to hold it: the ruler's numbers under it are left out, so no two labels overlap.
    RectF readout;
    {
        const float ry = l.roll.bottom() - k.s(14.0f);
        if (drag_ == Drag::Handle && handle_ < v.markers.size()) {
            readout = pill_rect(k, {X(handle_s_), ry}, axis0, axis1, position_label(song, handle_s_));
        } else if (cursor_set_ && drag_ != Drag::Handle && visible(X(cursor_s_))) {
            readout = pill_rect(k, {X(cursor_s_), ry}, axis0, axis1, position_label(song, cursor_s_));
        }
    }
    const bool beat_grid = beats(v);
    grid_lines(song, view_.start_s, view_.end_s, beat_grid, grid_);
    const double px_per_s = (axis1 - axis0) / std::max(1e-9, view_.span_s());
    float last_label_x = -1e9f;
    for (const GridLine& g : grid_) {
        const float x = X(g.time_s);
        if (!visible(x)) {
            continue;
        }
        dl->AddLine({x, l.roll.y}, {x, l.roll.bottom()},
                    u32({1, 1, 1, g.bar ? 0.10f : 0.045f}), 1.0f);
        if (g.bar && x - last_label_x >= k.s(34.0f)) {
            const std::string n = std::to_string(g.bar_number);
            const ImVec2 ts = text_size(k.regular, cap * 0.9f, n.c_str());
            const RectF box{x + k.s(3.0f), l.ruler.y, ts.x, l.ruler.h};
            if (x + k.s(3.0f) + ts.x < axis1 && !ruler_label_hidden(box, readout, k.s(2.0f))) {
                label_at(k, dl, k.regular, cap * 0.9f,
                         {x + k.s(3.0f), l.ruler.y, x + k.s(3.0f) + ts.x, l.ruler.bottom()},
                         LabelAlign::Left, col::kText3, n.c_str());
                last_label_x = x;
            }
        }
    }
    (void)px_per_s;
    // The practice position, small on the ruler.
    if (k.song != nullptr) {
        const float px = X(k.song->view().position_s);
        if (visible(px)) {
            dl->AddTriangleFilled({px - k.s(3.0f), l.ruler.y}, {px + k.s(3.0f), l.ruler.y},
                                  {px, l.ruler.y + k.s(5.0f)}, u32(with_alpha(col::kText2, 0.8f)));
        }
    }

    // ---- The band row: names and colours.
    name_w_.assign(v.sections.size(), 0.0f);
    for (std::size_t i = 0; i < v.sections.size(); ++i) {
        const SectionRow& r = v.sections[i];
        const float a = std::max(X(r.start_s), axis0);
        const float b = std::min(X(r.end_s), axis1);
        if (b <= a) {
            continue;
        }
        const bool sel = r.id == selected_;
        const ImVec4 tint = band_tint(i);
        const float inset = k.s(tok::kInset);
        const bool pressed = tracking_ && gestures_.touching() && drag_ == Drag::None &&
                             hit_.section == r.id &&
                             (hit_.target == EditorTarget::Band || hit_.target == EditorTarget::BandName);
        // The band's colour is its idle face; selected, hover and pressed come from the kit's one
        // state helper, in the band's colour, inside the face.
        const Box cell{a, l.bands.y, b, l.bands.bottom()};
        const Box face{a + inset, l.bands.y + inset, b - inset, l.bands.bottom() - inset};
        dl->AddRectFilled({face.x0, face.y0}, {face.x1, face.y1}, u32(with_alpha(tint, 0.18f)),
                          face_radius(k, face, tok::kRadiusInner));
        State st;
        st.tint = &tint;
        st.selected = tween(ImGui::GetID(reinterpret_cast<const void*>(i + 1)), sel ? 1.0f : 0.0f,
                            tok::kFast);
        st.pressed = pressed ? 1.0f : 0.0f;
        st.hover = !k.touch && drag_ == Drag::None &&
                   ImGui::IsMouseHoveringRect({cell.x0, cell.y0}, {cell.x1, cell.y1});
        draw_state(k, dl, "##band", face, cell, st, Idle::None, tok::kRadiusInner);
        const float left = band_name_left(l, view_, r.start_s, reach);
        const float room = b - left - k.s(4.0f);
        if (room >= k.s(28.0f)) {
            const float size = k.s(tok::kLabel);
            const FitText shown = fit(sel ? k.bold : k.regular, size, r.label, room - k.s(12.0f));
            const ImVec2 ts = text_size(sel ? k.bold : k.regular, size, shown.c_str());
            const float w = ts.x + k.s(14.0f);
            name_w_[i] = w;
            const float pill_h = k.s(30.0f);
            const float cy = (l.bands.y + l.bands.bottom()) * 0.5f;
            dl->AddRectFilled({left, cy - pill_h * 0.5f}, {left + w, cy + pill_h * 0.5f},
                              u32({0.0f, 0.0f, 0.0f, 0.28f}), pill_h * 0.5f);
            label_at(k, dl, sel ? k.bold : k.regular, size,
                     {left + k.s(7.0f), cy - pill_h * 0.5f, left + w, cy + pill_h * 0.5f},
                     LabelAlign::Left, col::kText, shown.c_str());
        }
    }

    // ---- The suggested cuts, dashed.
    if (v.suggestions_on) {
        for (const CutRow& c : v.cuts) {
            const float x = X(c.time_s);
            if (!visible(x)) {
                continue;
            }
            const ImVec4 ink = c.trouble ? kTroubleCut : col::kAccent;
            dashed_v(dl, x, l.ruler.y, l.bands.bottom(), u32(with_alpha(ink, 0.95f)), k.s(1.8f),
                     k.s(6.0f), k.s(4.0f));
            // A plus badge at the top of the roll: a tap accepts this cut.
            const ImVec2 bc{x, l.roll.y + k.s(20.0f)};
            dl->AddCircleFilled(bc, k.s(10.0f), u32(ink), 20);
            dl->AddLine({bc.x - k.s(4.5f), bc.y}, {bc.x + k.s(4.5f), bc.y}, u32(col::kAccentInk),
                        k.s(1.8f));
            dl->AddLine({bc.x, bc.y - k.s(4.5f)}, {bc.x, bc.y + k.s(4.5f)}, u32(col::kAccentInk),
                        k.s(1.8f));
        }
    }

    // ---- The handles between bands.
    for (std::size_t m = 0; m < v.markers.size(); ++m) {
        const bool grabbed = drag_ == Drag::Handle && handle_ == m;
        const double t = grabbed ? handle_s_ : v.markers[m];
        const float x = X(t);
        if (!visible(x)) {
            continue;
        }
        const ImVec4 ink = grabbed ? col::kAccent : ImVec4{0.96f, 0.97f, 1.0f, 0.85f};
        dl->AddLine({x, l.ruler.y}, {x, l.bands.bottom()}, u32(ink),
                    k.s(grabbed ? kHandleWGrabbed : kHandleW));
        const ImVec2 gc{x, l.bands.y + l.bands.h * 0.5f};
        dl->AddRectFilled({gc.x - k.s(kGripHalfW), gc.y - k.s(kGripHalfH)},
                          {gc.x + k.s(kGripHalfW), gc.y + k.s(kGripHalfH)}, u32(ink),
                          k.s(tok::kInset));
        for (const float side : {-1.0f, 1.0f}) {
            dl->AddLine({gc.x + side * k.s(kGripRidgeX), gc.y - k.s(kGripRidgeHalfH)},
                        {gc.x + side * k.s(kGripRidgeX), gc.y + k.s(kGripRidgeHalfH)},
                        u32(col::kAccentInk), k.s(tok::kHairline));
        }
        if (grabbed) {
            label_pill(k, dl, {x, l.roll.bottom() - k.s(14.0f)}, axis0, axis1, position_label(song, t),
                       col::kAccent, col::kAccentInk);
        }
    }

    // ---- The cursor, and the music being auditioned.
    if (cursor_set_ && drag_ != Drag::Handle) {
        const float x = X(cursor_s_);
        if (visible(x)) {
            dl->AddLine({x, l.ruler.y}, {x, l.roll.bottom()}, u32(col::kAccent), k.s(2.0f));
            dl->AddTriangleFilled({x - k.s(5.0f), l.ruler.y}, {x + k.s(5.0f), l.ruler.y},
                                  {x, l.ruler.y + k.s(8.0f)}, u32(col::kAccent));
            label_pill(k, dl, {x, l.roll.bottom() - k.s(14.0f)}, axis0, axis1,
                       position_label(song, cursor_s_), col::kAccent, col::kAccentInk);
        }
    }
    if (k.sections->auditioning()) {
        const float x = X(k.sections->audition_position_s());
        if (visible(x)) {
            dl->AddLine({x, l.ruler.y}, {x, l.roll.bottom()}, u32({1, 1, 1, 0.9f}), k.s(1.4f));
        }
    }
    touch_feedback(k, l);
    dl->PopClipRect();
    gestures(k, v, l);
}

// What answers a touch at once: a disc under the finger, the fat grab zone of the handle or cursor
// it landed on, the ring that fills toward a long press, and the mark where a snap just landed.
// None of it depends on hover.
void SectionEditor::touch_feedback(Kit& k, const EditorLayout& l) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto X = [&](double s) { return editor_x(l, view_, s); };
    const bool down = tracking_ && gestures_.touching();
    if (down && (hit_.target == EditorTarget::Handle || hit_.target == EditorTarget::Cursor)) {
        const double at = hit_.target == EditorTarget::Handle
                              ? (drag_ == Drag::Handle ? handle_s_ : press_marker_s_)
                              : cursor_s_;
        const float x = X(at);
        const float reach = k.s(kEditorGrabReach);
        // The grab zone answers the finger from its first frame: pressed, then selected while the
        // handle or cursor is dragged.
        const Box zone{x - reach, l.ruler.y, x + reach, l.bands.bottom()};
        State st;
        st.pressed = drag_ == Drag::None ? 1.0f : 0.0f;
        st.selected = drag_ == Drag::None ? 0.0f : 1.0f;
        draw_state(k, dl, "##grab", zone, zone, st, Idle::None, tok::kRadiusInner);
    }
    if (down || gestures_.pinching()) {
        const float r = k.s(24.0f);
        dl->AddCircleFilled(touch_pt_, r, u32({1.0f, 1.0f, 1.0f, 0.10f}), 28);
        dl->AddCircle(touch_pt_, r, u32({1.0f, 1.0f, 1.0f, 0.35f}), 28, k.s(1.5f));
    }
    const float progress = down && drag_ == Drag::None ? gestures_.long_press_progress(clock_s_) : 0.0f;
    if (progress > 0.02f) {
        const float r = k.s(32.0f);
        const float a0 = -1.5707963f;
        dl->PathArcTo(touch_pt_, r, a0, a0 + progress * 6.2831853f, 32);
        dl->PathStroke(u32(col::kAccent), 0, k.s(3.0f));
    }
    const double age = k.now - snap_time_;
    if (age >= 0.0 && age < kSnapFlashS) {
        const float t = static_cast<float>(age / kSnapFlashS);
        const float x = X(snap_at_s_);
        const float a = 1.0f - t;
        dl->AddLine({x, l.ruler.y}, {x, l.roll.bottom()},
                    u32(with_alpha(col::kAccent, 0.9f * a)), k.s(snap_bar_ ? kSnapBarW : kSnapBeatW));
        dl->AddCircle({x, l.ruler.y + l.ruler.h * 0.5f},
                      k.s((snap_bar_ ? kSnapRingBar : kSnapRingBeat) + kSnapRingGrow * t),
                      u32(with_alpha(col::kAccent, a)), 20, k.s(kSnapRingW));
    }
}

// A snap that landed somewhere new gets a short mark: bigger on a bar line than on a beat.
void SectionEditor::note_snap(Kit& k, const SectionsView& v, double snapped) {
    if (std::fabs(snapped - snap_at_s_) < 1e-9) {
        return;
    }
    snap_at_s_ = snapped;
    snap_time_ = k.now;
    snap_bar_ = std::fabs(snap_to_grid(*v.song, snapped, false) - snapped) < 1e-6;
}

} // namespace etude::app::ui
