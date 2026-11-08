// SPDX-License-Identifier: GPL-3.0-or-later
// The section band under the bar, its practice list popover and a section's menu
// (section_band.hpp).
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

std::size_t index_of(const SectionsView& v, SectionId id) {
    for (std::size_t i = 0; i < v.sections.size(); ++i) {
        if (v.sections[i].id == id) {
            return i;
        }
    }
    return v.sections.size();
}

// A wrapped paragraph in `size`, `colour`, `max_w` wide; returns its height.
float paragraph(Kit& k, ImDrawList* dl, ImVec2 pos, float max_w, float size, ImVec4 colour,
                std::string_view text) {
    text = screen_text(text);
    const char* end = text.data() + text.size();
    const ImVec2 ts = k.regular->CalcTextSizeA(size, FLT_MAX, max_w, text.data(), end);
    dl->AddText(k.regular, size, {std::round(pos.x), std::round(pos.y)}, u32(colour), text.data(),
                end, max_w);
    return ts.y;
}

// Two lines of text in a row `h` tall from `x`: `top` in the label size, `bottom` in the caption
// size under it.
void two_lines(Kit& k, ImDrawList* dl, float x, float y, float h, float max_w, std::string_view top,
               bool bold_top, std::string_view bottom) {
    const float s1 = k.s(tok::kLabel);
    const float s2 = k.s(tok::kCaption);
    ImFont* top_font = bold_top ? k.bold : k.regular;
    const FitText t = fit(top_font, s1, top, max_w);
    const FitText b = fit(k.regular, s2, bottom, max_w);
    const StackLine top_line{top_font, s1, col::kText, t.c_str()};
    const StackLine bottom_line{k.regular, s2, col::kText3, b.c_str()};
    label_stack(k, dl, {x, y, x + max_w, y + h}, top_line, &bottom_line);
}

// A list row: the whole row is its hit and its face; `on` draws it selected. Returns true on a tap.
bool list_row(Kit& k, const char* id, ImVec2 pos, float w, float h, bool on) {
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, {std::max(1.0f, w), h});
    const State st = item_state(k, on);
    const Box row{pos.x, pos.y, pos.x + w, pos.y + h};
    const Box face{pos.x, pos.y + k.s(tok::kInset), pos.x + w, pos.y + h - k.s(tok::kInset)};
    draw_state(k, ImGui::GetWindowDrawList(), id, face, row, st);
    return pressed;
}

// The taps on a row of two buttons side by side.
struct Pair {
    bool a = false;
    bool b = false;
};

// A popover card's window at `pos`, `size`: the card drawn behind its body; the toast keeps off it.
bool begin_card(Kit& k, const char* id, ImVec2 pos, ImVec2 size) {
    k.toast_avoid({pos.x, pos.y, size.x, size.y});
    if (!begin_layer(id, pos, size)) {
        return false;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    overlay_card(k, dl, pos, {pos.x + size.x, pos.y + size.y}, k.s(tok::kRadiusCard),
                 col::kSurface);
    return true;
}

// A tap that began outside the card and outside the band, released without movement: the popover
// closes.
bool tapped_away(ImVec2 min, ImVec2 max, const RectF& band) {
    if (!tapped_away_from(min, max)) {
        return false;
    }
    const ImVec2 m = tap_start();
    return !(m.x >= band.x && m.x <= band.right() && m.y >= band.y && m.y <= band.bottom());
}

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

namespace {

// The title and how many items there are.
void plan_title(Kit& k, const SectionsView& v, float inner) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float size = k.s(tok::kLabel);
    text_at(k, dl, k.bold, size, {p.x, p.y}, col::kText, "Practice list");
    FitText count;
    count.format(v.plan.size() == 1 ? "%zu section" : "%zu sections", v.plan.size());
    const ImVec2 cs = text_size(k.regular, k.s(tok::kCaption), count.c_str());
    text_at(k, dl, k.regular, k.s(tok::kCaption), {p.x + inner - cs.x, p.y}, col::kText3,
            count.c_str());
    ImGui::Dummy({inner, text_size(k.bold, size, "P").y + k.s(tok::kS2)});
}

// The items, a row each, a hit tall (the item spacing keeps the dead gap). `focus` is the one
// being edited.
void plan_rows(Kit& k, const SectionsView& v, float inner, std::size_t& focus) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hit = k.s(tok::kHit);
    if (v.plan.empty()) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float ph = paragraph(k, dl, p, inner, k.s(tok::kLabel), col::kText2,
                                   "The list is empty. Hold a section on the band and add it, "
                                   "or add them all below.");
        ImGui::Dummy({inner, ph + k.s(tok::kS2)});
    }
    for (std::size_t i = 0; i < v.plan.size(); ++i) {
        const PlanRow& r = v.plan[i];
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        const bool item_running = v.plan_running && v.plan_item == static_cast<int>(i);
        if (list_row(k, "##plan-row", pos, inner, hit, i == focus)) {
            focus = i;
        }
        FitText title;
        title.format("%zu  %s", i + 1, r.name.c_str());
        const float text_x = pos.x + k.s(tok::kS3);
        two_lines(k, dl, text_x, pos.y, hit, inner - k.s(tok::kS3) - (item_running ? hit : 0.0f),
                  title, item_running, r.detail);
        if (item_running) {
            glyph(k, dl, icon::kPlay, {pos.x + inner - hit * 0.5f, pos.y + hit * 0.5f},
                  k.s(tok::kIconSm), col::kAccent);
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos({pos.x, pos.y + hit});
        ImGui::Dummy({inner, 0.0f});
    }
}

// Start or Stop, then skip back and forward: the card's footer, outside the scrolling list.
void plan_footer(Kit& k, SectionsPort& sp, const SectionsView& v, float inner) {
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float skip_w = hit;
    const float main_w = inner - 2.0f * skip_w - 2.0f * gap;
    const ImVec2 at{pos.x, pos.y + gap};
    if (v.plan_running) {
        if (action_button(k, "##plan-stop", icon::kStop, v.run_active ? "End" : "Stop", at,
                          {main_w, hit}, true, false, true)) {
            sp.plan_stop(k.now_ns);
        }
    } else if (action_button(k, "##plan-start", icon::kPlay, "Start", at, {main_w, hit},
                             !v.plan.empty(), true)) {
        sp.plan_start(k.now_ns);
        k.say(sp.take_message());
    }
    ImGui::BeginDisabled(!v.plan_running);
    if (icon_toggle(k, "##plan-prev", icon::kPrevious, "Previous section",
                    {at.x + main_w + gap + skip_w * 0.5f, at.y + hit * 0.5f}, {skip_w, hit},
                    k.s(tok::kFace), false, true) &&
        v.plan_running) {
        sp.plan_skip(-1, k.now_ns);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!v.plan_running || (v.run_active && !v.run_can_skip));
    if (icon_toggle(k, "##plan-next", icon::kSkipNext, "Next section",
                    {at.x + main_w + 2.0f * gap + skip_w * 1.5f, at.y + hit * 0.5f}, {skip_w, hit},
                    k.s(tok::kFace), false, true) &&
        v.plan_running) {
        sp.plan_skip(1, k.now_ns);
    }
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({inner, hit + gap});
}

// Add every section, or clear the list.
void plan_list_buttons(Kit& k, SectionsPort& sp, const SectionsView& v, float inner) {
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float half = (inner - gap) * 0.5f;
    const ImVec2 at{pos.x, pos.y + gap};
    if (action_button(k, "##plan-all", icon::kListPlus, "Add all", at, {half, hit},
                      !v.sections.empty())) {
        sp.plan_add_all();
    }
    if (action_button(k, "##plan-clear", icon::kTrash, "Clear", {at.x + half + gap, at.y},
                      {half, hit}, !v.plan.empty(), false, true, true, "Tap to clear")) {
        sp.plan_clear();
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({inner, hit + gap});
}

// The item being edited: its passes and speed-up, and its place in the list.
void plan_item_editor(Kit& k, SectionsPort& sp, const SectionsView& v, float inner,
                      std::size_t& focus) {
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const PlanRow& row = v.plan[focus];
    PlanItem item = row.item;
    bool changed = false;
    FitText item_title;
    item_title.format("ITEM %zu \xC2\xB7 %s", focus + 1, row.name.c_str());
    section(k, item_title.c_str());
    changed |= stepper_row(k, icon::kRepeat, "Passes", &item.repeats, 0, kMaxPlanRepeats,
                           Unit::Times);
    changed |= switch_row(k, icon::kCheck, "Clean passes only", &item.clean_passes_only,
                          "A pass with a wrong or missed note does not count. With 0 "
                          "passes the section stays until you skip it.");
    changed |= switch_row(k, icon::kGauge, "Speed-up", &item.speed_up.enabled);
    if (item.speed_up.enabled) {
        changed |= stepper_row(k, nullptr, "Start", &item.speed_up.start_percent, 25,
                               kMaxSpeedPercent, Unit::Percent);
        changed |= stepper_row(k, nullptr, "Step", &item.speed_up.step_percent, 5, 25,
                               Unit::Percent);
        changed |= stepper_row(k, nullptr, "Up to", &item.speed_up.max_percent, 25,
                               kMaxSpeedPercent, Unit::Percent);
    }
    if (changed) {
        // The grid the transport takes: steps of 5, the top not under the start.
        item.speed_up.start_percent = (item.speed_up.start_percent / 5) * 5;
        item.speed_up.max_percent =
            std::max(item.speed_up.start_percent, (item.speed_up.max_percent / 5) * 5);
        sp.plan_set(focus, item);
    }
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float third = (inner - 2.0f * gap) / 3.0f;
    if (action_button(k, "##item-up", icon::kArrowUp, "Up", pos, {third, hit}, focus > 0)) {
        sp.plan_move(focus, focus - 1);
        --focus;
    }
    if (action_button(k, "##item-down", icon::kArrowDown, "Down", {pos.x + third + gap, pos.y},
                      {third, hit}, focus + 1 < v.plan.size())) {
        sp.plan_move(focus, focus + 1);
        ++focus;
    }
    if (action_button(k, "##item-remove", icon::kTrash, "Remove",
                      {pos.x + 2.0f * (third + gap), pos.y}, {third, hit}, true, false, true)) {
        sp.plan_remove(focus);
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({inner, hit + gap});
}

// The hand path: right hand, then left, then both, for the looped section or the song.
void plan_hand_path(Kit& k, SectionsPort& sp, const SectionsView& v, float inner) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float cpad = k.s(tok::kS3);
    const float text_w = inner - 2.0f * cpad;
    const float body_size = k.s(tok::kLabel);
    const float body_h =
        k.regular->CalcTextSizeA(body_size, FLT_MAX, text_w, screen_text(v.hand_hint.c_str())).y;
    const float btn_h = v.hand_can_apply ? hit : 0.0f;
    const Disclosure fold = disclosure_tap(k, "##hand-path-fold", pos, {inner, hit}, false);
    const float card_h = fold.open ? hit + body_h + (btn_h > 0.0f ? gap + btn_h : 0.0f) + cpad : hit;
    // A plain face, not `card`: the list scrolls, and a card draws past the window's clip.
    dl->AddRectFilled(pos, {pos.x + inner, pos.y + card_h}, u32(col::kRaised),
                      k.s(tok::kRadiusControl));
    const float hand = k.s(tok::kIconSm);
    const float pair_w = hand_pair_width(hand);
    hand_pair(k, dl, {pos.x + cpad + pair_w * 0.5f, pos.y + hit * 0.5f}, hand, col::kText2);
    const float label_x = pos.x + cpad + pair_w + k.s(tok::kS2);
    FitText hand_path;
    hand_path.format("HAND PATH \xC2\xB7 %s", v.hand_scope.c_str());
    const float cap = k.s(tok::kCaption);
    const FitText label =
        fit(k.bold, cap, hand_path, disclosure_label_right(k, pos, {inner, hit}) - label_x);
    label_at(k, dl, k.bold, cap, {label_x, pos.y, pos.x + inner, pos.y + hit}, LabelAlign::Left,
             col::kText3, label.c_str());
    disclosure_chevron(k, fold, pos, {inner, hit});
    if (fold.open) {
        const float ty = pos.y + hit;
        paragraph(k, dl, {pos.x + cpad, ty}, text_w, body_size, col::kText, v.hand_hint);
        if (v.hand_can_apply) {
            FitText use;
            use.format("Use %s", v.hand_apply.c_str());
            if (action_button(k, "##hand-apply", icon::kCheck, use.c_str(),
                              {pos.x + cpad, ty + body_h + gap}, {text_w, btn_h}, true, true)) {
                sp.apply_hand_path(k.now_ns);
                k.say(sp.take_message());
            }
        }
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({inner, card_h});
}

} // namespace

void SectionBand::list_popover(Kit& k, SectionsPort& sp, const BandPlace& place, float anchor_x) {
    const SectionsView& v = sp.view();
    const float margin = k.s(tok::kS3);
    const float w = std::min(place.display_w - 2.0f * margin, k.s(kBandListW));
    const float x = std::clamp(anchor_x - w * 0.5f, margin, place.display_w - w - margin);
    const float y = place.below + k.s(tok::kS2);
    const float max_h = place.keyboard_top - y - k.s(tok::kS2);
    const float pad = k.s(tok::kS3);
    // Closed where the card could not keep the footer and one whole row.
    if (max_h < plan_list_min_h(pad, k.s(tok::kS2), k.s(tok::kHit))) {
        list_open_ = false;
        return;
    }
    // The title, the list that scrolls, and the footer with Start: the footer is never inside the
    // scrolling part, so it stays whole whatever the list's length.
    const PlanListLayout lay =
        plan_list_layout({max_h, pad, text_size(k.bold, k.s(tok::kLabel), "P").y, k.s(tok::kS2),
                          k.s(tok::kHit), list_h_});
    if (plan_focus_ >= v.plan.size()) {
        plan_focus_ = v.plan.empty() ? 0 : v.plan.size() - 1;
    }
    if (v.plan_running && v.plan_item >= 0) {
        plan_focus_ = static_cast<std::size_t>(v.plan_item); // the editor follows the list
    }
    if (begin_card(k, "##band-list-card", {x, y}, {w, lay.h})) {
        ImGui::SetCursorScreenPos({x + pad, y + pad});
        ImGui::BeginChild("##band-list-body", {w - 2.0f * pad, lay.h - 2.0f * pad},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        const float inner = ImGui::GetContentRegionAvail().x;
        const float body_top = ImGui::GetCursorScreenPos().y;
        if (lay.title) {
            plan_title(k, v, inner);
        }
        // Placed by the layout's numbers, not by the cursor, which adds the item spacing.
        ImGui::SetCursorScreenPos({x + pad, body_top + lay.title_h});
        ImGui::BeginChild("##band-list-scroll", {inner, std::max(1.0f, lay.scroll_h)},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        const float top = ImGui::GetCursorScreenPos().y;
        plan_rows(k, v, inner, plan_focus_);
        plan_list_buttons(k, sp, v, inner);
        if (!v.plan.empty()) {
            plan_item_editor(k, sp, v, inner, plan_focus_);
        }
        plan_hand_path(k, sp, v, inner);
        list_h_ = ImGui::GetCursorScreenPos().y - top;
        drag_scroll(k);
        ImGui::EndChild();
        ImGui::SetCursorScreenPos({x + pad, body_top + lay.title_h + lay.scroll_h});
        plan_footer(k, sp, v, inner);
        ImGui::EndChild();
    }
    ImGui::End();
    if (opened_frame_ != ImGui::GetFrameCount() &&
        tapped_away({x, y}, {x + w, y + lay.h},
                    {0.0f, place.band.y, place.display_w, place.below - place.band.y})) {
        list_open_ = false;
    }
}

void SectionBand::menu_popover(Kit& k, SectionsPort& sp, const BandPlace& place) {
    const SectionsView& v = sp.view();
    const std::size_t index = index_of(v, menu_section_);
    if (index >= v.sections.size()) {
        menu_open_ = false; // the section went away
        return;
    }
    const SectionRow& r = v.sections[index];
    const float margin = k.s(tok::kS3);
    const float w = std::min(place.display_w - 2.0f * margin, k.s(kBandMenuW));
    const float x = std::clamp(menu_anchor_x_ - w * 0.5f, margin, place.display_w - w - margin);
    const float y = place.below + k.s(tok::kS2);
    const float max_h = place.keyboard_top - y - k.s(tok::kS2);
    const float pad = k.s(tok::kS3);
    const float h = std::min(max_h, menu_h_ > 0.0f ? menu_h_ + 2.0f * pad : max_h);
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    bool done = false;
    if (begin_card(k, "##band-menu-card", {x, y}, {w, std::max(h, hit)})) {
        ImGui::SetCursorScreenPos({x + pad, y + pad});
        ImGui::BeginChild("##band-menu-body", {w - 2.0f * pad, std::max(h - 2.0f * pad, hit)},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float inner = ImGui::GetContentRegionAvail().x;
        const float top = ImGui::GetCursorScreenPos().y;
        // The section: its colour, name and where it is. Delete is the icon at the end of this
        // row, kDestructiveGap from every other action (the rows below start that far down), and
        // takes a second tap; the toast it leaves offers Undo.
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float dot = k.s(tok::kS2);
            dl->AddCircleFilled({p.x + dot * 0.5f, p.y + hit * 0.5f}, dot * 0.5f,
                                u32(band_tint(index)), 12);
            two_lines(k, dl, p.x + dot + k.s(tok::kS2), p.y, hit, inner - dot - k.s(tok::kS2) - hit,
                      r.label, true, r.detail);
            ImGui::SetCursorScreenPos({p.x + inner - hit, p.y});
            const ImGuiID delete_id = ImGui::GetID("##menu-delete");
            const bool delete_armed = armed(k, delete_id);
            if (icon_action(k, "##menu-delete", icon::kTrash,
                            delete_armed ? "Tap again to delete" : "Delete", hit, true, true,
                            delete_armed)) {
                if (delete_armed) {
                    const std::uint32_t before = sp.edit_serial();
                    sp.erase_section(r.id, k.now_ns);
                    k.say_edit(before);
                    k.confirm_id = 0;
                    done = true;
                } else {
                    k.confirm_id = delete_id;
                    k.confirm_until = k.now + 3.0;
                    k.say("Tap again to delete");
                }
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy({inner, hit + std::max(k.s(kDestructiveGap) - gap, 0.0f)});
        }
        const float half = (inner - gap) * 0.5f;
        // A row of two actions, or one action across the card when `id_b` is null.
        const auto row = [&](const char* id_a, const char* icon_a, const char* label_a, bool en_a,
                             const char* id_b, const char* icon_b, const char* label_b, bool en_b) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 at{p.x, p.y + gap};
            Pair out;
            const float a_w = id_b != nullptr ? half : inner;
            out.a = action_button(k, id_a, icon_a, label_a, at, {a_w, hit}, en_a);
            if (id_b != nullptr) {
                out.b = action_button(k, id_b, icon_b, label_b, {at.x + half + gap, at.y},
                                      {half, hit}, en_b);
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy({inner, hit + gap});
            return out;
        };
        const bool has_next = index + 1 < v.sections.size();
        Pair p = row("##menu-rename", icon::kRename, "Rename", true, "##menu-join", icon::kMerge,
                     "Join next", has_next);
        if (p.a) {
            open_section_rename(r.id, r.custom_name ? r.label : "");
            done = true;
        }
        if (p.b) {
            const std::uint32_t before = sp.edit_serial();
            sp.merge_with_next(r.id, k.now_ns);
            k.say_edit(before);
            done = true;
        }
        p = row("##menu-list", icon::kListPlus, "Add to list", true, "##menu-lane", icon::kUnfold,
                "Edit cuts", true);
        if (p.a) {
            sp.plan_add(r.id);
            k.say("Added to the practice list");
            done = true;
        }
        if (p.b) {
            lane_ = true;
            done = true;
        }
        // Undo, with the loop's save beside it when there is a loop to save.
        if (v.loop_can_be_saved) {
            p = row("##menu-undo", icon::kUndo, "Undo", v.can_undo, "##menu-save-loop", icon::kSave,
                    "Save loop", true);
        } else {
            p = row("##menu-undo", icon::kUndo, "Undo", v.can_undo, nullptr, nullptr, nullptr,
                    false);
        }
        if (p.a) {
            sp.undo(k.now_ns);
            k.say(sp.take_message());
            done = true;
        }
        if (p.b) {
            sp.save_loop_as_section(k.now_ns);
            k.say(sp.take_message());
            done = true;
        }
        menu_h_ = ImGui::GetCursorScreenPos().y - top;
        drag_scroll(k);
        ImGui::EndChild();
    }
    ImGui::End();
    if (done || (opened_frame_ != ImGui::GetFrameCount() &&
                 tapped_away({x, y}, {x + w, y + h},
                             {0.0f, place.band.y, place.display_w, place.below - place.band.y}))) {
        menu_open_ = false;
    }
}

} // namespace etude::app::ui
