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

void SectionEditor::gestures(Kit& k, const SectionsView& v, const EditorLayout& l) {
    const ImGuiIO& io = ImGui::GetIO();
    const RectF area{l.ruler.x, l.ruler.y, l.ruler.w, l.bands.bottom() - l.ruler.y};
    ImGui::SetCursorScreenPos(min_of(area));
    ImGui::InvisibleButton("##roll-area", {area.w, area.h});
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = io.MousePos;

    // The recogniser's defaults are in design points; the screen's scale makes them ImGui points.
    const GestureRecognizer::Config base;
    GestureRecognizer::Config cfg = base;
    cfg.drag_threshold = base.drag_threshold * k.u;
    cfg.double_tap_distance = base.double_tap_distance * k.u;
    cfg.fling_min_speed = base.fling_min_speed * k.u;
    cfg.fling_stop_speed = base.fling_stop_speed * k.u;
    cfg.drag_after_long_press = true; // a hold on a handle takes it; moving then drags it
    gestures_.set_config(cfg);

    // The mouse, as one more finger. A touch also arrives as a synthesised mouse, which is
    // ignored: the finger events carry it.
    clock_s_ = std::max(clock_s_, k.now);
    const bool fingers = io.MouseSource == ImGuiMouseSource_TouchScreen || clock_s_ - last_finger_s_ < kIgnoreMouseAfterFingerS;
    if (!fingers) {
        if (ImGui::IsItemActivated()) {
            gestures_.down(0, mouse.x, mouse.y, clock_s_);
            mouse_down_ = true;
        }
        if (mouse_down_ && active && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
            gestures_.move(0, mouse.x, mouse.y, clock_s_);
        }
        if (mouse_down_ && ImGui::IsItemDeactivated()) {
            gestures_.up(0, mouse.x, mouse.y, clock_s_);
            mouse_down_ = false;
        }
        // A right click is a long press.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            gestures_.long_press_now(mouse.x, mouse.y, clock_s_);
        }
        // The wheel zooms around the pointer; sideways it pans.
        if (hovered && io.MouseWheel != 0.0f) {
            zoom(v, std::pow(1.2, static_cast<double>(io.MouseWheel)), editor_time(l, view_, mouse.x));
        }
        if (hovered && io.MouseWheelH != 0.0f) {
            view_ = pan_view(*v.song, view_, -io.MouseWheelH * view_.span_s() * 0.1);
        }
        if (hovered && external_zoom_ != 1.0) {
            zoom(v, external_zoom_, editor_time(l, view_, mouse.x));
        }
    } else if (mouse_down_) {
        mouse_down_ = false;
    }
    external_zoom_ = 1.0;

    gestures_.update(clock_s_);
    evs_.assign(gestures_.events().begin(), gestures_.events().end());
    gestures_.clear_events();
    for (const GestureRecognizer::Event& e : evs_) {
        on_gesture(k, v, l, e);
    }
    if (gestures_.touching() || gestures_.pinching()) {
        k.slider_active = true; // a drag here is not a page scroll
    }
}

// A tap that is not on a suggested cut's badge. A second tap while the music plays stops it, on
// the roll, the cursor line, a section's name or a handle (not on a band, which only selects); the
// first plays on to the cut of the section the point is in (audition_tap), or a short stretch
// across a handle.
void SectionEditor::on_tap(Kit& k, const SectionsView& v, const EditorLayout& l) {
    SectionsPort* sp = k.sections;
    const Song& song = *v.song;
    const std::int64_t now = k.now_ns;
    const auto play = [&](const AuditionRange& r) { play_or_stop(*sp, r, now); };
    switch (hit_.target) {
    case EditorTarget::Roll:
    case EditorTarget::None:
        place_cursor(v, editor_time(l, view_, press_.x));
        play(audition_tap(song, v.data, cursor_s_));
        break;
    case EditorTarget::BandName:
        // The name plays its section from its start to its cut; renaming is a long press, or
        // the bar's Rename.
        selected_ = hit_.section;
        if (const Section* s = v.data.find(hit_.section)) {
            play(audition_section(*s));
        }
        break;
    case EditorTarget::Band:
        selected_ = hit_.section;
        break;
    case EditorTarget::Handle:
        // Hear across it.
        play(audition_across(song, v.markers[hit_.handle]));
        break;
    case EditorTarget::Cursor:
        play(audition_tap(song, v.data, cursor_s_));
        break;
    }
}

void SectionEditor::on_gesture(Kit& k, const SectionsView& v, const EditorLayout& l,
                               const GestureRecognizer::Event& e) {
    using Kind = GestureRecognizer::Event::Kind;
    SectionsPort* sp = k.sections;
    const Song& song = *v.song;
    const std::int64_t now = k.now_ns;
    const RectF area{l.ruler.x, l.ruler.y, l.ruler.w, l.bands.bottom() - l.ruler.y};
    const auto inside = [&](float x, float y) {
        return x >= area.x && x <= area.right() && y >= area.y && y <= area.bottom();
    };
    const auto pan_by = [&](double dx_px) {
        view_ = pan_view(song, view_, -dx_px / std::max(1.0f, l.roll.w) * view_.span_s());
    };
    switch (e.kind) {
    case Kind::Press: {
        momentum_ok_ = false;
        drag_ = Drag::None;
        held_ = false;
        grab_.cancel();
        moved_ = false;
        cut_badge_ = false;
        touch_pt_ = press_ = {e.x, e.y};
        press_view_ = view_;
        tracking_ = inside(e.x, e.y);
        if (!tracking_) {
            break;
        }
        hit_ = hit_at(k, v, l, e.x, e.y);
        press_marker_s_ = hit_.target == EditorTarget::Handle && hit_.handle < v.markers.size()
                              ? v.markers[hit_.handle]
                              : 0.0;
        if (v.suggestions_on && hit_.target != EditorTarget::Handle && e.y >= l.roll.y &&
            e.y <= l.roll.y + k.s(44.0f)) {
            float best = k.s(kEditorGrabReach);
            for (std::size_t i = 0; i < v.cuts.size(); ++i) {
                const float d = std::fabs(editor_x(l, view_, v.cuts[i].time_s) - e.x);
                if (d <= best) {
                    best = d;
                    cut_badge_ = true;
                    cut_hit_ = i;
                }
            }
        }
        break;
    }
    case Kind::Tap: {
        if (!tracking_) {
            break;
        }
        if (cut_badge_ && cut_hit_ < v.cuts.size()) {
            sp->accept_cut(cut_hit_, now);
            k.say(sp->take_message());
            break;
        }
        on_tap(k, v, l);
        break;
    }
    case Kind::DoubleTap: {
        if (!tracking_) {
            break;
        }
        // The first tap of the pair started the music, so the pair stops it and does not zoom.
        if (tap_play(sp->auditioning(), true) == TapPlay::Stop) {
            sp->stop_audition(now);
            break;
        }
        if (hit_.target == EditorTarget::Band || hit_.target == EditorTarget::BandName) {
            const Section* s = v.data.find(hit_.section);
            if (s != nullptr) {
                selected_ = s->id;
                view_ = view_around(song, s->start_s, s->end_s, 0.06);
            }
        } else if (hit_.target == EditorTarget::Roll || hit_.target == EditorTarget::None) {
            zoom(v, 2.0, editor_time(l, view_, e.x));
        }
        break;
    }
    case Kind::LongPress: {
        if (!inside(e.x, e.y)) {
            break;
        }
        const EditorHit hit = hit_at(k, v, l, e.x, e.y);
        switch (hit.target) {
        case EditorTarget::Handle:
            // A hold takes the handle, and the finger then moves it (HeldMove) or lets go (the
            // handle stays). It never joins the two sections: Join is in the section's menu and in
            // the bar below, where a slip is not one hold away.
            if (tracking_ && hit.handle < v.markers.size()) {
                drag_ = Drag::Handle;
                held_ = true;
                handle_ = hit.handle;
                orig_s_ = handle_s_ = v.markers[handle_];
                grab_.grab(handle_, orig_s_, editor_time(l, view_, press_.x));
                touch_pt_ = press_;
            }
            break;
        case EditorTarget::BandName:
        case EditorTarget::Band:
            for (const SectionRow& r : v.sections) {
                if (r.id == hit.section) {
                    selected_ = r.id;
                    open_section_rename(r.id, r.custom_name ? r.label : "");
                }
            }
            break;
        case EditorTarget::Cursor:
        case EditorTarget::Roll:
        case EditorTarget::None: {
            place_cursor(v, editor_time(l, view_, e.x));
            note_snap(k, v, cursor_s_);
            const std::uint32_t before = sp->edit_serial();
            sp->split_at(cursor_s_, now);
            k.say_edit(before);
            const AuditionRange r = audition_across(song, cursor_s_);
            sp->audition(r.start_s, r.end_s, now);
            break;
        }
        }
        break;
    }
    case Kind::DragStart: {
        if (!tracking_) {
            break;
        }
        moved_ = true;
        grab_s_ = editor_time(l, view_, e.x);
        if (hit_.target == EditorTarget::Handle && hit_.handle < v.markers.size()) {
            drag_ = Drag::Handle;
            handle_ = hit_.handle;
            orig_s_ = handle_s_ = v.markers[handle_];
        } else if (hit_.target == EditorTarget::Cursor) {
            drag_ = Drag::Cursor;
            orig_s_ = cursor_s_;
        } else {
            drag_ = Drag::Pan;
        }
        break;
    }
    case Kind::DragMove: {
        if (!tracking_) {
            break;
        }
        touch_pt_ = {press_.x + e.total_dx, press_.y + e.total_dy};
        const double raw = orig_s_ + (editor_time(l, view_, touch_pt_.x) - grab_s_);
        if (drag_ == Drag::Handle && handle_ + 1 < v.data.sections.size()) {
            const double snapped = snap_to_grid(song, raw, beats(v));
            handle_s_ = marker_drag_time(song, v.data.sections[handle_].start_s,
                                         v.data.sections[handle_ + 1].end_s, snapped, false);
            note_snap(k, v, handle_s_);
        } else if (drag_ == Drag::Cursor) {
            place_cursor(v, raw);
            note_snap(k, v, cursor_s_);
        } else if (drag_ == Drag::Pan) {
            view_ = pan_view(song, press_view_,
                             -static_cast<double>(e.total_dx) / std::max(1.0f, l.roll.w) *
                                 press_view_.span_s());
        }
        break;
    }
    case Kind::DragEnd: {
        if (!tracking_) {
            break;
        }
        if (drag_ == Drag::Handle) {
            if (sp->move_marker(handle_, handle_s_, now)) {
                const AuditionRange r = audition_from(song, handle_s_);
                sp->audition(r.start_s, r.end_s, now);
            } else {
                k.say(sp->take_message());
            }
        } else if (drag_ == Drag::Cursor) {
            const AuditionRange r = audition_from(song, cursor_s_);
            sp->audition(r.start_s, r.end_s, now);
        } else if (drag_ == Drag::Pan) {
            momentum_ok_ = true; // the view coasts on
        }
        drag_ = Drag::None;
        tracking_ = false;
        if (!momentum_ok_) {
            gestures_.cancel(); // a handle or the cursor never flings
        }
        break;
    }
    case Kind::Cancel:
        // A second finger came, or the touch was taken away: nothing is committed.
        drag_ = Drag::None;
        held_ = false;
        grab_.cancel();
        tracking_ = false;
        break;
    case Kind::PinchStart:
        pinching_ = inside(e.x, e.y);
        pinch_x_ = e.x;
        touch_pt_ = {e.x, e.y};
        break;
    case Kind::PinchMove:
        if (pinching_) {
            zoom(v, e.factor, editor_time(l, view_, pinch_x_));
            pinch_x_ += e.dx;
            pan_by(e.dx);
            touch_pt_ = {pinch_x_, touch_pt_.y + e.dy};
        }
        break;
    case Kind::PinchEnd:
        momentum_ok_ = pinching_;
        pinching_ = false;
        break;
    case Kind::Fling:
        if (momentum_ok_) {
            pan_by(e.dx);
        }
        break;
    case Kind::ZoomFling:
        if (momentum_ok_) {
            zoom(v, e.factor, editor_time(l, view_, e.x));
        }
        break;
    case Kind::FlingEnd:
        momentum_ok_ = false;
        break;
    case Kind::HeldMove:
    case Kind::HeldEnd:
    case Kind::HeldRelease: {
        // The handle a hold took (core HandleGrab): it follows the finger, snapped and kept
        // between its neighbours; the lift commits a move, or lets it go as it was.
        if (!tracking_ || !held_ || drag_ != Drag::Handle) {
            break;
        }
        const auto place = [&](double raw) {
            if (handle_ + 1 >= v.data.sections.size()) {
                return raw;
            }
            return marker_drag_time(song, v.data.sections[handle_].start_s,
                                    v.data.sections[handle_ + 1].end_s,
                                    snap_to_grid(song, raw, beats(v)), false);
        };
        const HandleGrabStep step = grab_.on_event(e, editor_time(l, view_, e.x), place);
        if (step.kind == HandleGrabStep::Kind::Moved) {
            moved_ = true;
            touch_pt_ = {press_.x + e.total_dx, press_.y + e.total_dy};
            handle_s_ = step.time_s;
            note_snap(k, v, handle_s_);
            break;
        }
        if (step.kind == HandleGrabStep::Kind::Commit) {
            if (sp->move_marker(handle_, step.time_s, now)) {
                const AuditionRange r = audition_from(song, step.time_s);
                sp->audition(r.start_s, r.end_s, now);
            } else {
                k.say(sp->take_message());
            }
        } else if (step.kind == HandleGrabStep::Kind::Released && handle_ < v.markers.size()) {
            // Let go of the handle without moving it: nothing changes, and it is heard as a tap.
            const AuditionRange r = audition_across(song, v.markers[handle_]);
            sp->audition(r.start_s, r.end_s, now);
        }
        drag_ = Drag::None;
        held_ = false;
        tracking_ = false;
        break;
    }
    }
}

} // namespace etude::app::ui
