// SPDX-License-Identifier: GPL-3.0-or-later
#include "kit.hpp"

#include "kit_internal.hpp"

#include "clef_icon.hpp"
#include "font_ink.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "info_toggle.hpp"
#include "pressed_fade.hpp"
#include "row_layout.hpp"
#include "sections_port.hpp"
#include "etude/accidental_text.hpp"
#include "etude/note_style.hpp"
#include "transport.hpp"
#include "undo_chip.hpp"

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

// `fit_with_name` for the toast, remembered for the last text, size and width so a toast that
// stays the same is not measured again every frame.
FitText fit_name_cached(ImFont* font, float size, const char* text, TextSpan name, float max_w) {
    static FitText last_text;
    static float last_size = -1.0f;
    static float last_w = -1.0f;
    static TextSpan last_name;
    static FitText last_out;
    if (last_text == std::string_view(text) && last_size == size && last_w == max_w &&
        last_name.begin == name.begin && last_name.size == name.size) {
        return last_out;
    }
    last_text.assign(text);
    last_size = size;
    last_w = max_w;
    last_name = name;
    // The name's span counts bytes of the text as written: a note named in the text becomes longer
    // (a flat sign is three bytes), so the text is converted in three pieces to keep the span.
    static std::string before;
    static std::string inside;
    static std::string after;
    const std::string_view raw(text);
    if (has_accidental_spelling(raw)) {
        const std::size_t begin = std::min(name.begin, raw.size());
        const std::size_t end = std::min(begin + name.size, raw.size());
        accidental_display_text_into(raw.substr(0, begin), before);
        accidental_display_text_into(raw.substr(begin, end - begin), inside);
        accidental_display_text_into(raw.substr(end), after);
        before += inside;
        const std::size_t shown_begin = before.size() - inside.size();
        before += after;
        last_out = fit_with_name(before, shown_begin, inside.size(), max_w,
                                 [&](const char* line) { return text_size(font, size, line).x; });
        return last_out;
    }
    last_out = fit_with_name(text, name.begin, name.size, max_w,
                             [&](const char* line) { return text_size(font, size, line).x; });
    return last_out;
}
// The Undo chip was tapped: the toast goes, and the section edit or the host's own change is taken
// back.
void run_undo(Kit& k, bool own) {
    k.toast.state.dismiss();
    if (!own) {
        k.sections->undo(k.now_ns);
        k.say_free(k.sections->take_message());
    } else if (auto run = std::move(k.toast.undo)) {
        k.toast.undo = nullptr;
        run();
    }
}

// What the Undo toast is drawn from: the words' size, its pad, the chip's width, the air before it and
// beside it, and the popup rule's button.
struct UndoToast {
    float size;
    float pad_x;
    float chip_w;
    float chip_gap;
    float chip_edge;
    float button_pt;
    float face_h;
    int step;
    bool own; // the host's own change is undone, not a section edit
};

// The toast of an edit with its Undo chip, in `place`; the alpha style is the caller's.
void draw_undo_toast(Kit& k, const Toast& t, const ToastPlacement& place, const char* text,
                     ImVec4 ink, const UndoToast& u) {
    const float size = u.size;
    const float pad_x = u.pad_x;
    const float chip_w = u.chip_w;
    const float chip_gap = u.chip_gap;
    const float chip_edge = u.chip_edge;
    const float button_pt = u.button_pt;
    const int step = u.step;
    const RectF& box = place.box;
    // The pill is drawn by a window that takes no input, and only the chip, a window of its
    // own on top of it, takes the touch: a finger on the words, or one that lands beside the
    // chip, reaches whatever is under the pill.
    const float chip_x = box.right() - chip_edge - chip_w;
    ImGui::SetNextWindowPos({box.x, box.y});
    ImGui::SetNextWindowSize({box.w, box.h});
    constexpr ImGuiWindowFlags kPlain =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##toast-undo-pill", nullptr, kPlain | ImGuiWindowFlags_NoInputs)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        overlay_card(k, wdl, {box.x, box.y}, {box.right(), box.bottom()}, box.h * 0.5f,
                     col::kRaised, ImDrawFlags_RoundCornersAll, 0.8f);
        const float room = chip_x - chip_gap - (box.x + pad_x);
        // The whole text when the pill is whole; cut only when placement had to shorten it.
        const Box slot{box.x + pad_x, box.y, chip_x - chip_gap, box.bottom()};
        if (place.form == ToastForm::Shortened) {
            const FitText cut = t.state.name().size > 0
                                    ? fit_name_cached(k.regular, size, text, t.state.name(), room)
                                    : fit(k.regular, size, text, room);
            label_at(k, wdl, k.regular, size, slot, LabelAlign::Left, ink, cut.c_str());
        } else {
            label_at(k, wdl, k.regular, size, slot, LabelAlign::Left, ink, text);
        }
    }
    ImGui::End();
    ImGui::SetNextWindowPos({chip_x, box.y});
    ImGui::SetNextWindowSize({chip_w, box.h});
    if (ImGui::Begin("##toast-undo-chip-window", nullptr, kPlain)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        k.toast_chip = RectF{chip_x, box.y, chip_w, box.h};
        ChipOpts opts;
        opts.icon = icon::kUndo;
        opts.hit_h = box.h;
        if (step > 0) {
            opts.label_pt = button_pt;
            opts.face_h = u.face_h;
            opts.width =
                undo_chip_asked_width(step, chip_w); // the larger words fit: "Undo" is never cut
        }
        if (chip(k, "##toast-undo-chip", "Undo", {chip_x, box.y}, opts)) {
            run_undo(k, u.own);
        }
    }
    ImGui::End();
}

// The pill for the toast's text, drawn over everything. It is never a window, so it takes no touch,
// except in two cases: a warning that finds no room for its words shows as an icon a finger wide,
// and a long press on it shows the full text; and the toast of an edit carries an Undo chip, a
// window the size of the pill. Where it goes, and in what form, is core's `place_toast`.
void draw_toast(Kit& k, ImDrawList* dl, ImVec2 display) {
    const float alpha = k.toast.state.alpha(k.now, k.reduced_motion);
    k.toast_needs_room = false;
    k.toast_chip = RectF{};
    if (alpha <= 0.0f) {
        return;
    }
    // Undo takes back the latest edit only: once anything else was edited or undone, or the song
    // changed, the offer is gone with its toast.
    const bool own_undo = (k.toast.state.action_token() & kUndoOwnBit) != 0;
    if (k.toast.state.action() == ToastAction::Undo && !own_undo &&
        (k.sections == nullptr || k.toast.state.resolve_action(k.sections->edit_serial()))) {
        k.toast.state.dismiss();
        return;
    }
    const Toast& t = k.toast;
    const bool warning = t.state.warning();
    const char* text = t.state.text();
    // The popup rule (tok::popup_type): the words step up on a wide window, the Undo chip with them.
    const int step = tok::wide_step(display.x);
    const float grow = tok::popup_grow(step);
    const bool song = t.state.song();
    const float text_pt = tok::popup_type(tok::kLabel, step, tok::PopupText::Text);
    const float size = k.s(song ? tok::popup_type(tok::kLabel, step, tok::PopupText::Detail) : text_pt);
    const float button_pt = tok::popup_type(tok::kLabel, step, tok::PopupText::Button);
    const float pad_x = k.s(tok::kS4);
    const float pad_y = k.s(tok::kS2) + k.s(tok::kS1) * 0.5f;
    // The song-opened form: the name bold and a little larger, the detail under it (the composer)
    // in the quieter colour; the pill grows to the longer line.
    const char* detail = t.state.detail();
    const bool has_detail = song && detail[0] != '\0';
    ImFont* title_font = song ? k.bold : k.regular;
    const float title_size =
        song ? k.s(tok::popup_type(tok::kBody, step, tok::PopupText::Text)) : size;
    const ImVec2 ts = text_size(title_font, title_size, text);
    const ImVec2 ds = has_detail ? text_size(k.regular, size, detail) : ImVec2{0.0f, 0.0f};
    // The song form's icons: a treble clef before the title and, when a composer is shown, a quill
    // before it; both sit in one column so the two lines' text starts together.
    const bool has_composer = has_detail && t.state.composer();
    const float clef_h = title_size * tok::kToastClefEm;
    const float quill_size = size * tok::kToastQuillEm;
    const float clef_w = clef_h * clef_aspect();
    const float icon_col = song ? std::max(clef_w, has_composer ? quill_size : 0.0f) : 0.0f;
    const float line_gap = has_detail ? k.s(tok::kToastLineGap) : 0.0f;
    const float icon_gap = song ? k.s(tok::kS2) : 0.0f;
    const float text_dx = icon_col + icon_gap;
    // The Undo chip of an edit's toast: a pill a finger tall, the words, then the chip ending a
    // hairline of air from the pill's end.
    const bool undo = t.state.action() == ToastAction::Undo && !song;
    const float chip_w = undo ? chip_width_at(k, icon::kUndo, "Undo", step > 0 ? button_pt : 0.0f) : 0.0f;
    const float chip_gap = undo ? k.s(tok::kS2) : 0.0f;
    const float chip_edge = undo ? (k.s(tok::kHit) - k.s(tok::kFace)) * 0.5f * grow : 0.0f;
    const float end_room = undo ? chip_gap + chip_w + chip_edge : pad_x; // right of the words
    const float w = std::max(ts.x, ds.x) + text_dx + pad_x + end_room;
    const float h = std::max(ts.y + ds.y + line_gap + 2.0f * pad_y, undo ? k.s(tok::kHit) * grow : 0.0f);
    RectF anchor;
    if (t.anchored) {
        anchor = RectF{t.anchor_min.x, t.anchor_min.y, t.anchor_max.x - t.anchor_min.x,
                       t.anchor_max.y - t.anchor_min.y};
    }
    const ToastPlacement place =
        place_toast(w, h, kToastMinTextEm * size + pad_x + end_room, k.s(tok::kHit), warning, anchor,
                    display.x, display.y, k.toast_regions, k.s(tok::kS2), undo);
    // A warning without its words (the icon, or nowhere at all) asks the host for room.
    k.toast_needs_room = toast_lacks_words(warning, place.form);
    if (place.spot == ToastSpot::None) {
        return; // nowhere to put it that covers nothing: the message is dropped
    }
    const RectF& box = place.box;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    const ImVec4 ink = warning ? col::kWarning : col::kText;
    if (undo) {
        draw_undo_toast(k, t, place, text, ink,
                        {size, pad_x, chip_w, chip_gap, chip_edge, button_pt, k.s(tok::kFace) * grow, step, own_undo});
        ImGui::PopStyleVar();
        return;
    }
    overlay_card(k, dl, {box.x, box.y}, {box.right(), box.bottom()}, box.h * 0.5f, col::kRaised,
                 ImDrawFlags_RoundCornersAll, 0.8f);
    if (place.form == ToastForm::Icon) {
        glyph(k, dl, icon::kWarning, {box.x + box.w * 0.5f, box.y + box.h * 0.5f},
              k.s(tok::kIcon), ink);
        // The one toast that takes a touch: the box is the hit area, a long press shows the text.
        ImGui::SetNextWindowPos({box.x, box.y});
        ImGui::SetNextWindowSize({box.w, box.h});
        if (ImGui::Begin("##toast-icon", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            ImGui::SetCursorScreenPos({box.x, box.y});
            ImGui::InvisibleButton("##toast-icon-hit", {box.w, box.h});
            long_press_tip(k, text);
        }
        ImGui::End();
    } else if (song) {
        // Each line is cut to the pill with an ellipsis, only when it is longer than the room.
        // The pill is as wide as its longest line, so the room is that line's width up to a rounding:
        // half a pixel of slack keeps the longest line whole.
        const float room = box.w - 2.0f * pad_x - text_dx + 0.5f;
        const FitText top = fit(title_font, title_size, text, room);
        const FitText low = has_detail ? fit(k.regular, size, detail, room) : FitText{};
        const StackLine first{title_font, title_size, ink, top.c_str()};
        const StackLine second{k.regular, size, warning ? ink : col::kText2, low.c_str()};
        StackBands bands;
        label_stack(k, dl, {box.x + pad_x + text_dx, box.y, box.right() - pad_x, box.bottom()},
                    first, has_detail ? &second : nullptr, &bands, line_gap);
        // Each icon takes its line's colour and centres on its line's capital band.
        const float col_x = box.x + pad_x + icon_col * 0.5f;
        draw_clef(k, dl, {col_x, bands.top}, clef_h, first.colour);
        if (has_composer) {
            glyph(k, dl, icon::kQuill, {col_x, bands.bottom}, quill_size, second.colour);
        }
    } else if (place.form == ToastForm::Shortened) {
        // A song's title in it is what is shortened, by the rule it follows wherever it shows.
        const TextSpan name = t.state.name();
        const FitText cut =
            name.size > 0 ? fit_name_cached(k.regular, size, text, name, box.w - 2.0f * pad_x)
                          : fit(k.regular, size, text, box.w - 2.0f * pad_x);
        label_at(k, dl, k.regular, size, {box.x, box.y, box.right(), box.bottom()},
                 LabelAlign::Centre, ink, cut.c_str());
    } else {
        label_at(k, dl, k.regular, size, {box.x, box.y, box.right(), box.bottom()},
                 LabelAlign::Centre, ink, text);
    }
    ImGui::PopStyleVar();
}

} // namespace

void Kit::say(std::string_view text, bool warning) {
    if (text.empty()) {
        return;
    }
    toast.state.show(text, now, warning);
    toast.anchored = false;
    // The control just drawn, when there is one on screen to point at.
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetCurrentWindowRead() != nullptr) {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (b.x > a.x && b.y > a.y && a.x >= 0.0f && a.y >= 0.0f && b.x <= display.x &&
            b.y <= display.y) {
            toast.anchored = true;
            toast.anchor_min = a;
            toast.anchor_max = b;
        }
    }
}

void Kit::say_edit(std::uint32_t before) {
    if (sections == nullptr) {
        return;
    }
    const std::string message = sections->take_message();
    say(message);
    // The edit just made is the latest one: Undo takes it back while that stays true.
    if (!message.empty()) {
        toast.state.offer_undo(before, sections->edit_serial());
    }
}

void Kit::say_undoable(std::string_view text, std::function<void()> undo) {
    say(text);
    if (text.empty() || !toast.state.active()) {
        return;
    }
    toast.undo = std::move(undo);
    toast.state.set_action(ToastAction::Undo, kUndoOwnBit | ++toast.undo_serial);
}

void Kit::say_free(std::string_view text, bool warning) {
    if (!text.empty()) {
        toast.state.show(text, now, warning);
        toast.anchored = false;
    }
}

void Kit::say_later(std::string_view text, bool warning, TextSpan name, SongLines lines) {
    if (!text.empty()) {
        toast.state.queue(text, warning, name, lines);
        toast.anchored = false;
    }
}

void Kit::toast_avoid(const RectF& r) {
    const bool kept = toast_regions.add_avoid(r);
    assert(kept && "ToastRegions::kMaxToastAvoid is too small");
    (void)kept;
}

void Kit::start_pending_toast() { toast.state.begin_frame(now); }

void finish_frame(Kit& k) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    draw_toast(k, dl, display);
    const int step = tok::wide_step(display.x);
    if (!k.tip.empty()) {
        const float size = k.s(tok::popup_type(tok::kLabel, step, tok::PopupText::Text));
        const float pad_x = k.s(tok::kS3);
        // A long text (a toast's, from its icon) is cut to the window rather than run off it.
        const auto tip = fit(k.regular, size, k.tip, display.x - 2.0f * pad_x - 2.0f * k.s(tok::kS2));
        const ImVec2 ts = text_size(k.regular, size, tip.c_str());
        const float bh = ts.y + k.s(tok::kS2) * 2.0f;
        const float cx = (k.tip_min.x + k.tip_max.x) * 0.5f;
        // Above the control unless that leaves the screen; a finger covers the control itself.
        float y = k.tip_min.y - bh - k.s(tok::kS2);
        if (y < k.s(tok::kS1)) {
            y = k.tip_max.y + k.s(tok::kS2);
        }
        float x = std::clamp(cx - ts.x * 0.5f - pad_x, k.s(tok::kS2),
                             display.x - ts.x - 2.0f * pad_x - k.s(tok::kS2));
        overlay_card(k, dl, {x, y}, {x + ts.x + 2.0f * pad_x, y + bh}, bh * 0.5f, col::kRaised,
                     ImDrawFlags_RoundCornersAll, 0.8f);
        label_at(k, dl, k.regular, size, {x, y, x + ts.x + 2.0f * pad_x, y + bh},
                 LabelAlign::Centre, col::kText, tip.c_str());
        k.tip.clear();
    }
    if (k.info_id != 0) {
        if (ImGui::IsMouseClicked(0)) {
            k.info_id = info_after_press(k.info_id, k.info_seen);
        }
        if (k.info_id != 0) {
            // Inside the Settings sheet the bubble keeps its size; elsewhere it takes the popup rule.
            const int bubble_step = tok::info_bubble_step(step, k.info_in_settings);
            const float text_pt = tok::info_bubble_text(bubble_step);
            const float size = k.s(text_pt);
            const float wrap_pt = tok::info_bubble_wrap(bubble_step, display.x / k.u);
            const float wrap = std::min(k.s(wrap_pt), display.x - k.s(32.0f));
            const char* info = screen_text(k.info_text.c_str());
            const ImVec2 ts = k.regular->CalcTextSizeA(size, FLT_MAX, wrap, info);
            const float pad = k.s(tok::kS3);
            const float bw = ts.x + 2.0f * pad;
            const float bh = ts.y + 2.0f * pad;
            const float x = std::clamp(k.info_anchor.x - bw * 0.5f, k.s(tok::kS2),
                                       display.x - bw - k.s(tok::kS2));
            float y = k.info_anchor.y + k.s(tok::kS1);
            if (y + bh > display.y - k.s(tok::kS2)) {
                y = k.info_anchor.y - k.s(30.0f) - bh;
            }
            overlay_card(k, dl, {x, y}, {x + bw, y + bh}, k.s(tok::kRadiusCard), col::kRaised);
            dl->AddText(k.regular, size, {x + pad, y + pad}, u32(col::kText), info, nullptr, wrap);
        }
    }
    k.info_seen = false;
    k.slider_active = false;
}

} // namespace etude::app::ui
