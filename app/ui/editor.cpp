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

constexpr float kButtonZoom = 1.6f; // what one tap of + or - zooms by
ImVec2 centre_of(const RectF& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }
bool same_colour(const ImVec4& a, const ImVec4& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace

SectionEditor::SectionEditor(SDL_Renderer* renderer) : renderer_(renderer) {}

void SectionEditor::drop_renderer() {
    if (tile_tex_ != nullptr) {
        gfx::destroy_texture(tile_tex_);
        tile_tex_ = nullptr;
    }
    tex_h_ = 0;
    tile_ = RollTile{};
}

SectionEditor::~SectionEditor() {
    if (tile_tex_ != nullptr) {
        gfx::destroy_texture(tile_tex_);
    }
}

void SectionEditor::open(Kit& k) {
    open_ = true;
    reset(k);
}

void SectionEditor::open_lane(Kit& k) {
    lane_ = true;
    reset(k);
}

void SectionEditor::close_lane(Kit& k) {
    if (!lane_) {
        return;
    }
    lane_ = false;
    drag_ = Drag::None;
    gestures_.cancel();
    gestures_.clear_events();
    tracking_ = false;
    pinching_ = false;
    if (k.sections != nullptr && !open_) {
        k.sections->stop_audition(k.now_ns);
        k.sections->set_suggestions(false);
    }
}

void SectionEditor::reset(Kit& k) {
    want_close_ = false;
    drag_ = Drag::None;
    held_ = false;
    grab_.cancel();
    claims_.clear();
    gestures_.cancel();
    gestures_.clear_events();
    tracking_ = false;
    pinching_ = false;
    momentum_ok_ = false;
    mouse_down_ = false;
    external_zoom_ = 1.0;
    if (k.sections != nullptr && k.sections->view().available && k.sections->view().song != nullptr) {
        const SectionsView& v = k.sections->view();
        view_ = whole_song_view(*v.song);
        const double at = k.song != nullptr ? k.song->view().position_s : 0.0;
        cursor_s_ = snap_to_grid(*v.song, at, false);
        cursor_set_ = true;
        const Section* s = v.data.at(cursor_s_);
        selected_ = s != nullptr ? s->id : kNoSection;
    }
}

void SectionEditor::close(Kit& k) {
    open_ = false;
    drag_ = Drag::None;
    held_ = false;
    grab_.cancel();
    claims_.clear();
    gestures_.cancel();
    gestures_.clear_events();
    tracking_ = false;
    pinching_ = false;
    if (k.sections != nullptr) {
        k.sections->stop_audition(k.now_ns);
        k.sections->set_suggestions(false);
    }
}

void SectionEditor::on_event(const SDL_Event& e, ImVec2 display, std::int64_t now_ns,
                             bool other_fingers_down, bool on_overlay) {
    if (!open_ && !lane_) {
        return;
    }
    const double t = static_cast<double>(now_ns) * 1e-9;
    const bool finger_event = e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION ||
                              e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED;
    // A trackpad's fingers are not on the screen: the pointer and the pinch events cover it.
    if (finger_event) {
        if (!is_screen_touch(e.tfinger.touchID)) {
            return;
        }
    }
    const auto id = [&] { return static_cast<std::int64_t>(e.tfinger.fingerID); };
    const float x = e.tfinger.x * display.x;
    const float y = e.tfinger.y * display.y;
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
        last_finger_s_ = clock_s_ = std::max(clock_s_, t);
        // A finger that lands on a control drawn over the editor (a toast's Undo) is that
        // control's from landing to lift: the roll never sees it.
        if (on_overlay) {
            claims_.claim(id());
            break;
        }
        // With no other finger on the screen a touch the recogniser still holds lost its lift:
        // it is dropped and this one starts clean. A mouse press in progress is no finger's lost
        // lift, so it counts as the other pointer (a mouse and a finger pinch, as ever).
        gestures_.down(id(), x, y, clock_s_, other_fingers_down || mouse_down_);
        break;
    case SDL_EVENT_FINGER_MOTION:
        last_finger_s_ = clock_s_ = std::max(clock_s_, t);
        if (!claims_.claimed(id())) {
            gestures_.move(id(), x, y, clock_s_);
        }
        break;
    case SDL_EVENT_FINGER_UP:
        last_finger_s_ = clock_s_ = std::max(clock_s_, t);
        if (claims_.claimed(id())) {
            claims_.release(id());
        } else {
            gestures_.up(id(), x, y, clock_s_);
        }
        break;
    case SDL_EVENT_FINGER_CANCELED:
        last_finger_s_ = clock_s_ = std::max(clock_s_, t);
        claims_.clear();
        gestures_.cancel();
        break;
    case SDL_EVENT_PINCH_UPDATE:
        // A trackpad pinch: the scale is a factor on the last one.
        if (e.pinch.scale > 0.1f && e.pinch.scale < 10.0f) {
            external_zoom_ *= static_cast<double>(e.pinch.scale);
        }
        break;
    default:
        break;
    }
}

bool SectionEditor::beats(const SectionsView& v) const {
    return v.song != nullptr && beat_snap_zoom(*v.song, view_);
}

void SectionEditor::zoom(const SectionsView& v, double factor, double focus_s) {
    if (v.song != nullptr) {
        view_ = zoom_view(*v.song, view_, factor, focus_s);
    }
}

void SectionEditor::place_cursor(const SectionsView& v, double seconds) {
    if (v.song == nullptr) {
        return;
    }
    cursor_s_ = snap_to_grid(*v.song, seconds, beats(v));
    cursor_set_ = true;
    const Section* s = v.data.at(cursor_s_);
    selected_ = s != nullptr ? s->id : kNoSection;
}

void SectionEditor::ensure_tile(Kit& k, const SectionsView& v, const EditorLayout& l) {
    const Song& song = *v.song;
    const int vw = std::max(16, static_cast<int>(std::lround(l.roll.w * k.fb_scale)));
    const int vh = std::max(16, static_cast<int>(std::lround(l.roll.h * k.fb_scale)));
    const bool same_look = same_colour(tile_left_, k.hand_left) && same_colour(tile_right_, k.hand_right);
    if (tile_tex_ != nullptr && tex_h_ == vh && tile_serial_ == v.song_serial && same_look &&
        tile_.h == vh && !tile_stale(tile_, view_, vw)) {
        return;
    }
    const FrameDetailScope timing(FrameDetail::Texture);
    if (tile_tex_ == nullptr || tex_h_ != vh) {
        if (tile_tex_ != nullptr) {
            gfx::destroy_texture(tile_tex_);
        }
        tile_tex_ = gfx::create_texture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_TARGET, kMaxTileWidth, vh);
        tex_h_ = vh;
        if (tile_tex_ != nullptr) {
            gfx::set_texture_blend(tile_tex_, SDL_BLENDMODE_BLEND);
            gfx::set_texture_linear(tile_tex_);
        }
        tile_ = RollTile{};
    }
    if (tile_tex_ == nullptr) {
        return;
    }
    if (tile_serial_ != v.song_serial) {
        pitches_ = song_pitch_range(song);
    }
    tile_serial_ = v.song_serial;
    tile_left_ = k.hand_left;
    tile_right_ = k.hand_right;
    tile_view_w_ = vw;
    tile_ = tile_for_view(song, view_, vw, vh);
    if (!tile_.valid()) {
        return;
    }
    roll_note_rects(song, tile_.t0_s, tile_.t1_s, tile_.w, tile_.h, pitches_, -1.0, rects_);
    SDL_Texture* previous = gfx::target(renderer_);
    gfx::set_target(renderer_, tile_tex_);
    gfx::set_blend(renderer_, SDL_BLENDMODE_NONE);
    gfx::clear(renderer_, 0, 0, 0, 0);
    const float row_h = static_cast<float>(tile_.h) / static_cast<float>(std::max(1, pitches_.rows()));
    const float gap = row_h >= 3.0f ? 1.0f : 0.0f;
    for (const Hand hand : {Hand::Unknown, Hand::Left, Hand::Right}) {
        frects_.clear();
        for (const RollRect& r : rects_) {
            if (r.hand == hand) {
                frects_.push_back({r.x0, r.y0, r.x1 - r.x0, std::max(1.0f, r.y1 - r.y0 - gap)});
            }
        }
        if (frects_.empty()) {
            continue;
        }
        const ImVec4 c = hand_colour(k, hand);
        gfx::set_colour8(renderer_, static_cast<Uint8>(c.x * 255.0f),
                         static_cast<Uint8>(c.y * 255.0f), static_cast<Uint8>(c.z * 255.0f), 235);
        gfx::fill_rects(renderer_, frects_.data(), static_cast<int>(frects_.size()));
    }
    for (const RollRect& r : rects_) {
        const FoldMarker fm = fold_marker(r.x0, r.y0, r.x1, r.y1 - gap, r.fold);
        if (fm.shown) {
            const SDL_FColor white{1.0f, 1.0f, 1.0f, 0.85f};
            const SDL_Vertex tri[3] = {{{fm.tip.x, fm.tip.y}, white, {0.0f, 0.0f}},
                                       {{fm.left.x, fm.left.y}, white, {0.0f, 0.0f}},
                                       {{fm.right.x, fm.right.y}, white, {0.0f, 0.0f}}};
            gfx::geometry(renderer_, nullptr, tri, 3, nullptr, 0);
        }
    }
    gfx::set_target(renderer_, previous);
}

bool SectionEditor::draw(Kit& k, const Frame& f) {
    SectionsPort* sp = k.sections;
    if (!open_ || sp == nullptr) {
        return false;
    }
    const SectionsView& v = sp->view();
    if (!v.available || v.song == nullptr) {
        return true; // the song went away
    }
    want_close_ = false;
    const float margin = k.s(tok::kS3);
    const RectF area{margin, f.top, f.display.x - 2.0f * margin,
                     f.display.y - f.top - margin};
    const float pad = k.s(10.0f);
    const EditorLayout l =
        editor_layout({area.x + pad, area.y + pad, area.w - 2.0f * pad, area.h - 2.0f * pad}, k.u);

    // The scrim, so nothing under the editor takes a touch.
    if (begin_scrim("##editor-scrim", {0.0f, f.row_max.y},
                    {f.display.x, f.display.y - f.row_max.y})) {
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), {f.display.x, f.display.y},
                                                  u32(col::kScrim));
        ImGui::SetCursorPos({0, 0});
        ImGui::InvisibleButton("##scrim", ImGui::GetWindowSize());
    }
    ImGui::End();

    if (begin_layer("##editor", {0.0f, f.row_max.y}, {f.display.x, f.display.y - f.row_max.y})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        sheet_card(k, dl, min_of(area), max_of(area), k.s(tok::kRadiusLg), col::kSurface);
        // A toast keeps off the toolbar and the ruler; the card is where it may stand over the
        // paused, dimmed notes.
        k.toast_avoid(l.toolbar);
        k.toast_avoid(l.ruler);
        k.toast_regions.surface = area;
        bubble_area_ = l.area;
        toolbar(k, v, l);
        ensure_tile(k, v, l);
        roll(k, v, l);
        loupe(k, v, l);
        actions(k, v, l);
        floating_loupe(k, v);
    }
    ImGui::End();
    if (!want_close_) {
        return false;
    }
    return true;
}

void SectionEditor::draw_lane(Kit& k, const RectF& lane, const RectF& bubble) {
    SectionsPort* sp = k.sections;
    if (!lane_ || open_ || sp == nullptr || lane.empty()) {
        return;
    }
    const SectionsView& v = sp->view();
    if (!v.available || v.song == nullptr) {
        return;
    }
    // The tools stand a dead gap under the band above.
    const RectF inner{lane.x + k.s(tok::kS2), lane.y + k.s(tok::kHitGap), lane.w - k.s(tok::kS4),
                      lane.h - k.s(tok::kHitGap) - k.s(tok::kS1)};
    const LaneLayout l = lane_layout(inner, k.u);
    if (begin_layer("##lane", {lane.x, lane.y}, {lane.w, lane.h})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(min_of(lane), max_of(lane), u32(with_alpha(col::kSurface, 0.96f)));
        dl->AddLine({lane.x, lane.bottom() - tok::kHairline * 0.5f},
                    {lane.right(), lane.bottom() - tok::kHairline * 0.5f}, u32(col::kHairline));
        bubble_area_ = bubble;
        ensure_tile(k, v, l.strip);
        roll(k, v, l.strip);
        lane_tools(k, v, l);
        floating_loupe(k, v);
    }
    ImGui::End();
}

// The lane's four tools: split at the cursor, play across it (or, while suggestions show, accept
// them all), undo, and the whole song.
void SectionEditor::lane_tools(Kit& k, const SectionsView& v, const LaneLayout& l) {
    SectionsPort* sp = k.sections;
    const Song& song = *v.song;
    const float drawn = k.s(tok::kFace);
    const auto button = [&](const char* id, const char* icon, const char* label, const RectF& slot,
                            bool on, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        const bool tap =
            icon_toggle(k, id, icon, label, centre_of(slot), {slot.w, slot.h}, drawn, on, true);
        ImGui::EndDisabled();
        return tap && enabled;
    };
    if (button("##lane-split", icon::kScissors, "Split at the cursor", l.split, false,
               cursor_set_)) {
        const std::uint32_t before = sp->edit_serial();
        sp->split_at(cursor_s_, k.now_ns);
        k.say_edit(before);
        const AuditionRange r = audition_across(song, cursor_s_);
        sp->audition(r.start_s, r.end_s, k.now_ns);
    }
    if (v.suggestions_on && !v.cuts.empty()) {
        if (button("##lane-accept", icon::kCheck, "Accept every suggested cut", l.across, false,
                   true)) {
            sp->accept_all_cuts(false, k.now_ns);
            k.say(sp->take_message());
        }
    } else if (button("##lane-across", icon::kAcross,
                      "Play across the cut: two seconds either side", l.across, false,
                      cursor_set_)) {
        const AuditionRange r = audition_across(song, cursor_s_);
        sp->audition(r.start_s, r.end_s, k.now_ns);
    }
    if (button("##lane-undo", icon::kUndo, "Undo", l.undo, false, v.can_undo)) {
        sp->undo(k.now_ns);
        k.say(sp->take_message());
    }
    if (button("##lane-fit", icon::kFitKeys, "Show the whole song", l.fit, false,
               view_.span_s() < v.duration_s - 1e-6)) {
        view_ = whole_song_view(song);
    }
}

void SectionEditor::toolbar(Kit& k, const SectionsView& v, const EditorLayout& l) {
    SectionsPort* sp = k.sections;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ToolbarSlots t = toolbar_slots(l.toolbar, k.u);
    const float drawn = k.s(36.0f);
    const auto button = [&](const char* id, const char* icon, const char* label, const RectF& slot,
                            bool on, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        const bool tap = icon_toggle(k, id, icon, label, centre_of(slot), {slot.w, slot.h}, drawn,
                                     on, true);
        ImGui::EndDisabled();
        return tap && enabled;
    };
    if (button("##ed-close", icon::kClose, "Close the editor", t.close, false, true)) {
        want_close_ = true;
    }
    if (button("##ed-fit", icon::kFitAll, "Show the whole song", t.fit, false, true)) {
        view_ = whole_song_view(*v.song);
    }
    const double focus = cursor_set_ && cursor_s_ >= view_.start_s && cursor_s_ <= view_.end_s
                             ? cursor_s_
                             : (view_.start_s + view_.end_s) * 0.5;
    if (button("##ed-out", icon::kZoomOut, "Zoom out", t.zoom_out, false,
               view_.span_s() < v.duration_s - 1e-6)) {
        zoom(v, 1.0 / kButtonZoom, focus);
    }
    if (button("##ed-in", icon::kZoomIn, "Zoom in", t.zoom_in, false,
               view_.span_s() > min_view_span(*v.song, focus) + 1e-6)) {
        zoom(v, kButtonZoom, focus);
    }
    if (button("##ed-undo", icon::kUndo, "Undo", t.undo, false, v.can_undo)) {
        sp->undo(k.now_ns);
        k.say(sp->take_message());
    }
    if (v.can_undo && v.undo_depth > 0) {
        // How many steps back there are.
        const std::string n = std::to_string(v.undo_depth);
        const float size = k.s(tok::kCaption) * 0.85f;
        const ImVec2 ts = text_size(k.bold, size, n.c_str());
        const ImVec2 c{t.undo.right() - k.s(9.0f), t.undo.y + k.s(9.0f)};
        const float badge_r = std::max(k.s(7.0f), ts.x * 0.5f + k.s(3.0f));
        dl->AddCircleFilled(c, badge_r, u32(col::kAccent), 16);
        label_at(k, dl, k.bold, size, {c.x - badge_r, c.y - badge_r, c.x + badge_r, c.y + badge_r},
                 LabelAlign::Centre, col::kAccentInk, n.c_str());
    }
    if (button("##ed-suggest", icon::kStyle, "Suggested cuts", t.suggest, v.suggestions_on, true)) {
        sp->set_suggestions(!v.suggestions_on);
        k.say(sp->take_message());
    }
    // The title, or the way to accept the suggestions.
    if (!t.title.empty()) {
        const float h = k.s(tok::kHit);
        const float cy = t.title.y + t.title.h * 0.5f;
        if (v.suggestions_on && !v.cuts.empty()) {
            bool trouble = false;
            for (const CutRow& c : v.cuts) {
                trouble = trouble || c.trouble;
            }
            const float gap = k.s(tok::kS2);
            const std::string all = "Accept all " + std::to_string(v.cuts.size());
            const float aw = std::min(t.title.w, k.s(150.0f));
            if (action_button(k, "##ed-accept-all", icon::kCheck, all.c_str(),
                              {t.title.x, cy - h * 0.5f}, {aw, h}, true, true)) {
                sp->accept_all_cuts(false, k.now_ns);
                k.say(sp->take_message());
            }
            const float tw = std::min(t.title.right() - (t.title.x + aw + gap), k.s(150.0f));
            if (trouble && tw > k.s(100.0f) &&
                action_button(k, "##ed-accept-trouble", icon::kFlash, "Trouble spots",
                              {t.title.x + aw + gap, cy - h * 0.5f}, {tw, h}, true)) {
                sp->accept_all_cuts(true, k.now_ns);
                k.say(sp->take_message());
            }
        } else {
            const char* text = v.suggestions_on ? "No cuts to suggest" : "Split the song";
            const float size = k.s(tok::kTitle);
            const FitText shown = fit(k.bold, size, text, t.title.w);
            label_at(k, dl, k.bold, size, {t.title.x, t.title.y, t.title.right(), t.title.bottom()},
                     LabelAlign::Left, col::kText, shown.c_str());
        }
    }
}

// Where the selected section is in the view's list, or its size when none is selected.
std::size_t SectionEditor::selected_index(const SectionsView& v) const {
    std::size_t index = v.sections.size();
    for (std::size_t i = 0; i < v.sections.size(); ++i) {
        if (v.sections[i].id == selected_) {
            index = i;
        }
    }
    return index;
}

// One Play: the selected section once, and with none selected ten seconds from the cursor (cut at
// the song's end), again and again while the band's loop is on. Tapping the notes or the cursor
// line plays from there to the cut.
void SectionEditor::play_pressed(Kit& k, const SectionsView& v, std::size_t index) {
    const bool have = index < v.sections.size();
    play_or_stop(*k.sections, have ? audition_section(v.data.sections[index])
                                   : audition_idle(*v.song, cursor_s_), k.now_ns, !have);
}

void SectionEditor::actions(Kit& k, const SectionsView& v, const EditorLayout& l) {
    SectionsPort* sp = k.sections;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Song& song = *v.song;
    const ActionSlots s = action_slots(l.actions, k.u);
    const float drawn = k.s(36.0f);
    // One caption rule for the row (action_slots): all of them, or none and a long press names each.
    const auto button = [&](const char* id, const char* icon, const char* label, const char* caption,
                            const RectF& slot, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        const bool tap = icon_toggle(k, id, icon, label, centre_of(slot), {slot.w, slot.h}, drawn,
                                     false, true, s.captions ? caption : nullptr);
        ImGui::EndDisabled();
        return tap && enabled;
    };
    const bool cursor_ok = cursor_set_;
    if (button("##ed-split", icon::kScissors, "Split at the cursor", "Split", s.split, cursor_ok)) {
        const std::uint32_t before = sp->edit_serial();
        sp->split_at(cursor_s_, k.now_ns);
        k.say_edit(before);
        // Hear the cut that was just made.
        const AuditionRange r = audition_across(song, cursor_s_);
        sp->audition(r.start_s, r.end_s, k.now_ns);
    }
    const std::size_t index = selected_index(v);
    const bool have = index < v.sections.size();
    if (button("##ed-play", icon::kPlay,
               have ? "Play the selected section"
                    : "Play 10 seconds from the cursor; repeats while the loop is on",
               "Play", s.play, have || cursor_ok || sp->auditioning())) {
        play_pressed(k, v, index);
    }
    if (button("##ed-across", icon::kAcross, "Play across the cut: two seconds either side",
               "Across", s.play_across, cursor_ok)) {
        const AuditionRange r = audition_across(song, cursor_s_);
        sp->audition(r.start_s, r.end_s, k.now_ns);
    }
    // The selected band's own bar.
    if (!s.band_chip.empty()) {
        const RectF& c = s.band_chip;
        const float cy = c.y + c.h * 0.5f;
        const float ch = k.s(34.0f);
        const ImVec4 tint = have ? band_tint(index) : col::kText3;
        dl->AddRectFilled({c.x, cy - ch * 0.5f}, {c.right(), cy + ch * 0.5f},
                          u32(with_alpha(tint, 0.2f)), k.s(tok::kRadiusMd));
        dl->AddCircleFilled({c.x + k.s(14.0f), cy}, k.s(5.0f), u32(tint), 14);
        const float size = k.s(tok::kLabel);
        const std::string text = have ? v.sections[index].label : "No section selected";
        const FitText shown = fit(k.bold, size, text, c.w - k.s(34.0f));
        label_at(k, dl, k.bold, size, {c.x + k.s(26.0f), c.y, c.right(), c.bottom()},
                 LabelAlign::Left, col::kText, shown.c_str());
    }
    if (button("##ed-band-name", icon::kRename, "Rename", "Rename", s.band_rename, have)) {
        open_section_rename(v.sections[index].id,
                            v.sections[index].custom_name ? v.sections[index].label : "");
    }
    if (button("##ed-band-join", icon::kMerge, "Join with the next section", "Join", s.band_merge,
               have && index + 1 < v.sections.size())) {
        const std::uint32_t before = sp->edit_serial();
        sp->merge_with_next(v.sections[index].id, k.now_ns);
        k.say_edit(before);
    }
    if (button("##ed-band-delete", icon::kTrash, "Delete this section", "Delete", s.band_delete,
               have)) {
        const std::uint32_t before = sp->edit_serial();
        sp->erase_section(v.sections[index].id, k.now_ns);
        k.say_edit(before);
    }
}

} // namespace etude::app::ui
