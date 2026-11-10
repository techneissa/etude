// SPDX-License-Identifier: GPL-3.0-or-later
// The section band under the bar and its track and tools (section_band.hpp); the popovers are in
// section_band_popovers.cpp.
#include "section_band.hpp"

#include "bar.hpp"
#include "icons.hpp"
#include "plan_list_layout.hpp"
#include "sections_ui.hpp"

#include "etude/editor_layout.hpp"
#include "etude/level_stars.hpp"
#include "etude/roll_view.hpp"
#include "etude/section_overview.hpp"
#include "etude/transport.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace etude::app::ui {

namespace {

constexpr float kPi = 3.14159265f;

// The bars' difficulty as a strip along the foot of the track, one colour per bar on the stars'
// ramp (green easy to red hard), solid so it reads on every section colour and clear of the
// section names above it. Bars narrower than a pixel share a column and show the hardest of them,
// so a long song draws no more rectangles than the track has pixels. Allocates nothing.
void difficulty_strip(const Kit& k, ImDrawList* dl, const SongView& v, const OverviewTrack& tr,
                      float y0, float y1) {
    const std::size_t n = v.bar_starts_s.size();
    float run_x0 = 0.0f;
    float run_x1 = 0.0f;
    float run_stars = 0.0f;
    const auto flush = [&]() {
        if (run_x1 > run_x0) {
            const RGBA ramp = level_star_colour(static_cast<double>(run_stars), k.high_contrast);
            const ImU32 c =
                u32({static_cast<float>(ramp.r) / 255.0f, static_cast<float>(ramp.g) / 255.0f,
                     static_cast<float>(ramp.b) / 255.0f, 1.0f});
            dl->AddRectFilled({run_x0, y0}, {run_x1, y1}, c);
        }
    };
    for (std::size_t i = 0; i < n; ++i) {
        const float x0 = std::round(tr.x_at(v.bar_starts_s[i]));
        const float x1 = std::round(tr.x_at(i + 1 < n ? v.bar_starts_s[i + 1] : v.duration_s));
        if (x1 <= x0) {
            continue;
        }
        const float stars = v.bar_difficulty[i];
        if (x0 < run_x1) {
            run_x1 = std::max(run_x1, x1);
            run_stars = std::max(run_stars, stars);
            continue;
        }
        flush();
        run_x0 = x0;
        run_x1 = x1;
        run_stars = stars;
    }
    flush();
}

ImVec2 min_of(const RectF& r) { return {r.x, r.y}; }
ImVec2 max_of(const RectF& r) { return {r.right(), r.bottom()}; }
ImVec2 centre_of(const RectF& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

} // namespace

const FitText& SectionBand::clock(double position_s, double duration_s) {
    const auto whole = [](double s) {
        return static_cast<int>(std::max(0.0, std::isnan(s) ? 0.0 : s));
    };
    const int p = whole(position_s);
    const int d = whole(duration_s);
    if (p != clock_position_ || d != clock_duration_) {
        clock_position_ = p;
        clock_duration_ = d;
        clock_.format("%d:%02d / %d:%02d", p / 60, p % 60, d / 60, d % 60);
    }
    return clock_;
}

void SectionBand::draw(Kit& k, SongPort& song, const BandPlace& place) {
    const SongView& v = song.view();
    if (!v.has_song || place.band.empty()) {
        touch_.cancel();
        close_popovers();
        lane_ = false;
        return;
    }
    tint_ = place.difficulty;
    SectionsPort* sp = k.sections != nullptr && k.sections->view().available ? k.sections : nullptr;
    if (sp == nullptr) {
        lane_ = false;
        close_popovers();
    }
    const RectF& b = place.band;
    const float cap = k.s(tok::kCaption);
    const FitText& time = clock(v.position_s, v.duration_s);
    const bool run = sp != nullptr && sp->view().run_active;
    const BandLayout l = band_layout(b, text_size(k.regular, cap, time.c_str()).x, k.u, run, lane_);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, place.alpha);
    if (begin_layer("##band", min_of(b), {b.w, b.h})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(min_of(b), max_of(b), u32(with_alpha(col::kSurface, 0.96f)));
        dl->AddLine({b.x, b.bottom() - tok::kHairline * 0.5f},
                    {b.right(), b.bottom() - tok::kHairline * 0.5f}, u32(col::kHairline));
        // Every face stays on the band.
        const FaceBand faces(k, b.y, b.bottom());
        track(k, song, sp, l, place.live);
        if (!l.time.empty()) {
            label_at(k, dl, k.regular, cap, {l.time.x, b.y, l.time.right(), b.bottom()},
                     LabelAlign::Left, col::kText2, time.c_str());
        }
        tools(k, song, sp, l, place.live);
    }
    ImGui::End();
    if (sp != nullptr) {
        if (list_open_) {
            list_popover(k, *sp, place, list_anchor_x_);
        }
        if (menu_open_) {
            menu_popover(k, *sp, place);
        }
    }
    ImGui::PopStyleVar();
}

void SectionBand::track(Kit& k, SongPort& song, SectionsPort* sp, const BandLayout& l, bool live) {
    const SongView& v = song.view();
    const SectionsView* sv = sp != nullptr ? &sp->view() : nullptr;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RectF& t = l.track;
    if (t.w <= 0.0f || v.duration_s <= 0.0) {
        return;
    }
    const float cy = l.band.y + l.band.h * 0.5f;
    const float seg_h = std::min(k.s(kBandSegmentPt), l.band.h - 2.0f * k.s(tok::kS1));
    const float y0 = cy - seg_h * 0.5f;
    const float y1 = cy + seg_h * 0.5f;
    const OverviewTrack tr{t.x, t.right(), v.duration_s};
    const auto at = [&](double s) { return tr.x_at(s); };

    // The touch: the whole track, the band's height.
    ImGui::SetCursorScreenPos(min_of(t));
    ImGui::InvisibleButton("##track", {t.w, t.h});
    const State track_state = item_state(k, false, live);
    const bool active = ImGui::IsItemActive() && live;
    const bool hovered = ImGui::IsItemHovered() && !k.touch;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float grab = k.s(kEditorGrabReach);
    if (live && ImGui::IsItemActivated()) {
        const OverviewHit hit =
            sv != nullptr ? overview_hit(sv->data, tr, mouse.x, grab) : OverviewHit{};
        touch_.press(hit, mouse.x, mouse.y, k.now);
        if (hit.kind == OverviewHit::Kind::Marker) {
            cut_s_ = sv->data.sections[hit.marker].end_s;
        }
        list_open_ = false;
        menu_open_ = false;
    }
    if (active && tap_cancelled()) {
        touch_.cancel(); // a second finger landed: the band does nothing with this touch
    }
    if (active) {
        k.slider_active = true;
        switch (touch_.hold(mouse.x, mouse.y, k.now, k.s(kTouchSlopPt))) {
        case BandTouch::Hold::Scrub: {
            const double to = tr.time_at(mouse.x);
            if (std::fabs(to - v.position_s) > 1e-3) {
                song.seek(to, k.now_ns);
            }
            break;
        }
        case BandTouch::Hold::Menu:
            menu_section_ = touch_.hit.section;
            menu_anchor_x_ = touch_.press_x;
            menu_open_ = true;
            opened_frame_ = ImGui::GetFrameCount();
            break;
        case BandTouch::Hold::Lane:
            lane_ = sp != nullptr;
            if (sp == nullptr) {
                k.say("Open a song from the library to cut it into sections");
            }
            break;
        case BandTouch::Hold::MoveCut: {
            const SongSections& data = sv->data;
            const std::size_t m = touch_.hit.marker;
            cut_s_ = marker_drag_time(*sv->song, data.sections[m].start_s,
                                      data.sections[m + 1].end_s, tr.time_at(mouse.x), true);
            break;
        }
        case BandTouch::Hold::None:
            break;
        }
    }
    if (live && ImGui::IsItemDeactivated()) {
        const Section* under = sv != nullptr ? sv->data.at(tr.time_at(touch_.press_x)) : nullptr;
        switch (touch_.lift(under != nullptr ? under->id : kNoSection)) {
        case BandTouch::Lift::Loop:
            sp->select_section(touch_.section, k.now_ns);
            break;
        case BandTouch::Lift::Seek:
            song.seek(tr.time_at(touch_.seek_x), k.now_ns);
            break;
        case BandTouch::Lift::MoveCut:
            sp->move_marker(touch_.hit.marker, cut_s_, k.now_ns);
            k.say(sp->take_message());
            break;
        case BandTouch::Lift::None:
            break;
        }
    }

    // The sections, or the whole song as one plain band when it has none.
    bool loop_is_section = false;
    const bool pending = active && touch_.mode == BandTouch::Mode::Pending;
    if (sv == nullptr || sv->sections.empty()) {
        const Box face{t.x, y0, t.right(), y1};
        dl->AddRectFilled({face.x0, face.y0}, {face.x1, face.y1}, u32(col::kFill),
                          k.s(tok::kRadiusInner));
        State st = track_state;
        st.pressed = pending ? 1.0f : 0.0f;
        draw_state(k, dl, "##track-song", face, {t.x, t.y, t.right(), t.bottom()}, st, Idle::None,
                   k.s(tok::kRadiusInner));
        const FitText title = fit(k.regular, k.s(tok::kCaption), v.title, t.w - k.s(tok::kS4));
        label_at(k, dl, k.regular, k.s(tok::kCaption),
                 {t.x + k.s(tok::kS2), face.y0, t.right(), face.y1}, LabelAlign::Left, col::kText3,
                 title.c_str());
    } else {
        for (std::size_t i = 0; i < sv->sections.size(); ++i) {
            const SectionRow& r = sv->sections[i];
            const bool looping = r.id == sv->looping;
            loop_is_section = loop_is_section || looping;
            const float a = std::round(at(r.start_s)) + k.s(tok::kHairline);
            const float e = std::round(at(r.end_s)) - k.s(tok::kHairline);
            if (e <= a) {
                continue;
            }
            const Box face{a, y0, e, y1};
            dl->AddRectFilled({a, y0}, {e, y1}, u32(with_alpha(band_tint(i), 0.3f)),
                              k.s(tok::kRadiusInner));
            // Its state: the section looping is the selected one; the one under a finger answers at
            // once, before the tap is decided; a mouse over it (never a finger) shows hover.
            State st = track_state;
            st.selected = looping ? 1.0f : 0.0f;
            const bool under =
                touch_.hit.kind == OverviewHit::Kind::Segment && touch_.hit.section == r.id;
            st.pressed = pending && under ? 1.0f : 0.0f;
            st.hover = hovered && mouse.x >= a && mouse.x < e && !active;
            draw_state(k, dl, "##track-section", face, {t.x, t.y, t.right(), t.bottom()}, st,
                       Idle::None, k.s(tok::kRadiusInner));
            const float size = k.s(tok::kCaption);
            const FitText label = fit(k.bold, size, r.label, e - a - k.s(tok::kS3));
            if (!label.empty() && e - a > k.s(tok::kS6)) {
                label_at(
                    k, dl, k.bold, size, {a + k.s(tok::kS2), face.y0, e, face.y1}, LabelAlign::Left,
                    state_ink(st, with_alpha(col::kText, looping ? 1.0f : 0.9f)), label.c_str());
            }
        }
    }
    if (tint_ && !v.bar_difficulty.empty() && v.bar_difficulty.size() == v.bar_starts_s.size()) {
        difficulty_strip(k, dl, v, tr, y1 - k.s(tok::kS1), y1);
    }
    // Bar lines every four bars along the bottom of the track, faint, so it reads as music.
    for (std::size_t bar = 4; bar < v.bar_starts_s.size(); bar += 4) {
        const float bx = std::round(at(v.bar_starts_s[bar]));
        dl->AddLine({bx, y1 - k.s(tok::kS1)}, {bx, y1},
                    u32({1, 1, 1, bar % 16 == 0 ? 0.22f : 0.1f}), 1.0f);
    }
    // A loop set at the keys (not a section's): tinted, outlined.
    const bool loop_on = v.loop_enabled && v.loop_end_s > v.loop_start_s;
    if (loop_on) {
        seen_loop_start_ = v.loop_start_s;
        seen_loop_end_ = v.loop_end_s;
    }
    const float loop_strength =
        tween(ImGui::GetID("##loop-fade"), loop_on && !loop_is_section ? 1.0f : 0.0f, tok::kSheet);
    if (loop_strength > 0.01f && seen_loop_end_ > seen_loop_start_) {
        const float a = at(seen_loop_start_);
        const float e = at(seen_loop_end_);
        dl->AddRectFilled({a, y0}, {e, y1}, u32(with_alpha(col::kAccent, 0.16f * loop_strength)),
                          k.s(tok::kRadiusInner));
        dl->AddRect({a, y0}, {e, y1}, u32(with_alpha(col::kAccent, 0.7f * loop_strength)),
                    k.s(tok::kRadiusInner), 0, k.s(tok::kHairline));
    }
    // The cuts between sections: a line with a grip on top; the one being dragged follows the
    // finger and names its bar above the band.
    if (sv != nullptr) {
        for (std::size_t m = 0; m < sv->markers.size(); ++m) {
            const bool grabbed = active && touch_.mode == BandTouch::Mode::Marker &&
                                 touch_.hit.marker == m && touch_.moved;
            const double ts = grabbed ? cut_s_ : sv->markers[m];
            const float mx = std::round(at(ts));
            const ImVec4 ink = grabbed ? col::kAccent : with_alpha(col::kText, 0.8f);
            dl->AddLine({mx, y0}, {mx, y1}, u32(ink), k.s(grabbed ? kBandPlayheadPt : kBandCutPt));
            const float g = k.s(kBandGripPt) * 0.5f;
            dl->AddTriangleFilled({mx - g, y0 - g * 0.5f}, {mx + g, y0 - g * 0.5f},
                                  {mx, y0 + g * 0.5f}, u32(ink));
            if (grabbed) {
                if (cut_label_s_ != ts) {
                    cut_label_s_ = ts;
                    cut_label_ = position_label(*sv->song, ts);
                }
                const float size = k.s(tok::kCaption);
                const ImVec2 lsz = text_size(k.bold, size, cut_label_.c_str());
                const float pad = k.s(tok::kS2);
                const ImVec2 pa{mx - lsz.x * 0.5f - pad, l.band.y - lsz.y - k.s(tok::kS3)};
                ImDrawList* fg = ImGui::GetForegroundDrawList();
                overlay_card(k, fg, pa, {pa.x + lsz.x + 2.0f * pad, pa.y + lsz.y + k.s(tok::kS1)},
                             k.s(tok::kRadiusInner), col::kRaised);
                fg->AddText(k.bold, size, {pa.x + pad, pa.y + k.s(tok::kS1) * 0.5f},
                            u32(col::kText), cut_label_.c_str());
            }
        }
    }
    // The playhead: a line across the track with a knob on top.
    const float px = std::round(at(v.position_s));
    dl->AddLine({px, y0 - k.s(tok::kS1)}, {px, y1 + k.s(tok::kS1)}, u32(col::kText),
                k.s(kBandPlayheadPt));
    dl->AddCircleFilled({px, y0 - k.s(tok::kS1)}, k.s(tok::kS1), u32(col::kText), 12);
    // Holding still toward a long press: a ring fills round the finger.
    const float progress = active ? touch_.long_press_progress(k.now) : 0.0f;
    if (progress > 0.2f) {
        const ImVec2 c{touch_.press_x, cy};
        const float r = seg_h * 0.5f + k.s(tok::kS1);
        dl->PathArcTo(c, r, -0.5f * kPi, -0.5f * kPi + 2.0f * kPi * progress, 32);
        dl->PathStroke(u32(col::kAccent), 0, k.s(tok::kFocusRing));
    }
}

namespace {

// The practice list's length, a badge in the corner of its button's face.
void list_badge(Kit& k, std::size_t count, const RectF& slot, float drawn) {
    char n[8];
    std::snprintf(n, sizeof n, "%zu", std::min<std::size_t>(count, 99));
    const float d = k.s(kBandBadgePt);
    const ImVec2 c{slot.x + slot.w * 0.5f + drawn * 0.5f - d * 0.5f,
                   slot.y + slot.h * 0.5f - drawn * 0.5f + d * 0.5f};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, d * 0.5f, u32(col::kAccent), 16);
    const float size = k.s(tok::kMark);
    label_at(k, dl, k.bold, size, {c.x - d * 0.5f, c.y - d * 0.5f, c.x + d * 0.5f, c.y + d * 0.5f},
             LabelAlign::Centre, col::kAccentInk, n);
}

} // namespace

void SectionBand::tools(Kit& k, SongPort& song, SectionsPort* sp, const BandLayout& l, bool live) {
    const SongView& v = song.view();
    const SectionsView* sv = sp != nullptr ? &sp->view() : nullptr;
    const float drawn = std::min(k.s(kBandFace), l.band.h - 2.0f * k.s(tok::kS1));
    const auto button = [&](const char* id, const char* icon, const char* label, const RectF& slot,
                            bool on, bool enabled) {
        if (slot.empty()) {
            return false; // a narrow band gives this tool up (band_layout)
        }
        ImGui::BeginDisabled(!enabled);
        const bool tap =
            icon_toggle(k, id, icon, label, centre_of(slot), {slot.w, slot.h}, drawn, on, live);
        ImGui::EndDisabled();
        return tap && enabled;
    };
    const bool loop_on = v.loop_enabled && v.loop_end_s > v.loop_start_s;
    if (button("##band-loop", icon::kRepeat, "Loop: the section at the playhead, or end the loop",
               l.loop, loop_on, true)) {
        const Section* at = sv != nullptr ? sv->data.at(v.position_s) : nullptr;
        switch (loop_toggle_tap(loop_on, at != nullptr ? at->id : kNoSection)) {
        case LoopTap::EndLoop:
            song.clear_loop(k.now_ns);
            k.say("Loop off");
            break;
        case LoopTap::LoopSection:
            sp->select_section(at->id, k.now_ns);
            k.say(sp->take_message());
            break;
        case LoopTap::NoSection:
            k.say("Cut the song first: the scissors put a cut at the playhead");
            break;
        }
    }
    const bool can = sp != nullptr;
    if (button("##band-cut", icon::kScissors, "Cut here: a cut at the playhead", l.tools.cut, false,
               can)) {
        const std::uint32_t before = sp->edit_serial();
        sp->add_marker_at_playhead(k.now_ns);
        k.say_edit(before);
    }
    // A trouble-bar run: skip the bar being practised, or end the whole run, from here: never over
    // the notes. Skip is the check-in's Skip.
    if (can && sv->run_active) {
        if (button("##band-run-skip", icon::kSkipNext, "Skip this bar and go on", l.tools.skip,
                   false, sv->run_can_skip)) {
            sp->plan_skip(1, k.now_ns);
            k.say(sp->take_message());
        }
        if (button("##band-run-end", icon::kStop, "End the run", l.tools.end, false, true)) {
            sp->run_command(RunCommand::End, k.now_ns);
            k.say(sp->take_message());
        }
    }
    const bool running = can && sv->plan_running;
    list_anchor_x_ = l.tools.list.x + l.tools.list.w * 0.5f; // the list hangs under its button
    if (button("##band-list", icon::kListMusic, "Practice list", l.tools.list,
               list_open_ || running, can)) {
        list_open_ = !list_open_;
        menu_open_ = false;
        opened_frame_ = ImGui::GetFrameCount();
    }
    if (can && !sv->plan.empty()) {
        list_badge(k, sv->plan.size(), l.tools.list, drawn);
    }
    const bool suggesting = can && sv->suggestions_on;
    if (button("##band-suggest", icon::kStyle, "Suggested cuts, on the lane", l.tools.suggest,
               suggesting, can)) {
        sp->set_suggestions(!suggesting);
        k.say(sp->take_message());
        if (!suggesting) {
            lane_ = true; // they show on the lane's roll
        }
    }
    if (button("##band-lane", lane_ ? icon::kFold : icon::kUnfold,
               lane_ ? "Close the lane" : "Edit the cuts on the lane", l.tools.lane, lane_, can)) {
        lane_ = !lane_;
    }
    if (button("##band-editor", icon::kFitAll, "Open the section editor", l.tools.editor, false,
               can)) {
        k.open_editor = true;
        close_popovers();
    }
}

} // namespace etude::app::ui
