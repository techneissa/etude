// SPDX-License-Identifier: GPL-3.0-or-later
#include "osk.hpp"

#include "fit_text.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "tap.hpp"

#include <imgui_internal.h> // the IME request

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace etude::app::ui::osk {

namespace {
int g_modal_frame = -10;
// Backspace: once as the finger lifts; held past the long-press time it repeats, and the lift after
// a repeat is no tap (the tap rule). `repeat_at` is when the next repeat is due (0: none yet). Call
// right after the key's item is made.
void backspace_key(Kit& k, bool tapped_now, double& repeat_at, std::string& text) {
    if (tapped_now) {
        erase_last(text);
    } else if (held_still() && ImGui::IsItemActive()) {
        if (repeat_at <= 0.0) {
            repeat_at = k.now;
        }
        if (k.now >= repeat_at) {
            erase_last(text);
            repeat_at = k.now + tok::kFast * 0.6;
        }
    } else if (!ImGui::IsItemActive()) {
        repeat_at = 0.0;
    }
}

} // namespace

bool modal_open() { return g_modal_frame + 1 >= ImGui::GetFrameCount(); }
void note_modal() { g_modal_frame = ImGui::GetFrameCount(); }

bool by_touch() { return ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen; }

void want_text() {
    GImGui->PlatformImeData.WantTextInput = true;
    GImGui->PlatformImeData.ViewportId = ImGui::GetMainViewport()->ID;
}

Typed feed_keyboard(std::string& text, const FieldRules& rules) {
    Typed t;
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        const ImWchar c = io.InputQueueCharacters[i];
        if (c < 32) {
            continue;
        }
        char utf8[5] = {};
        ImTextCharToUtf8(utf8, c);
        type_text(text, utf8, rules);
        t.any = true;
    }
    io.InputQueueCharacters.resize(0);
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
        erase_last(text);
        t.any = true;
    }
    t.done = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
             ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    t.cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    return t;
}

// ---- The keys ---------------------------------------------------------------------------------

namespace {

// What a key shows on its face: its letter, word or glyph, placed by its ink and sized with the key.
void draw_key_face(Kit& k, ImDrawList* dl, const KeySpec& key, const char* id, const Box& face,
                   const ImVec4& ink, const KeyInk& ink_size, Layer layer) {
    const ImVec2 c{face.cx(), face.cy()};
    const ImVec2 lo{face.x0, face.y0};
    const ImVec2 hi{face.x1, face.y1};
    switch (key.kind) {
    case KeyKind::Letter: {
        audit::note(id, lo, hi, InkKind::Target);
        char shown[4] = {};
        std::snprintf(shown, sizeof shown, "%s", key.text);
        if (layer == Layer::Letters) {
            shown[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shown[0])));
        }
        label_at(k, dl, k.regular, ink_size.letter, face, LabelAlign::Centre, ink, shown);
        break;
    }
    case KeyKind::Backspace:
        audit::note(id, lo, hi);
        glyph(k, dl, icon::kBackspace, c, ink_size.icon, ink);
        break;
    case KeyKind::Layer:
        audit::note(id, lo, hi, InkKind::Target);
        label_at(k, dl, k.bold, ink_size.word, face, LabelAlign::Centre, ink,
                 layer == Layer::Symbols ? "ABC" : "123");
        break;
    case KeyKind::Clear:
        audit::note(id, lo, hi, InkKind::Target);
        label_at(k, dl, k.regular, ink_size.word, face, LabelAlign::Centre, ink, "Clear");
        break;
    case KeyKind::Space: {
        audit::note(id, lo, hi, InkKind::Target);
        const float bar = std::min(face.w() * 0.22f, ink_size.letter * 2.4f);
        dl->AddRectFilled({c.x - bar, c.y + k.s(tok::kS1)},
                          {c.x + bar, c.y + k.s(tok::kS1) + k.s(tok::kHairline) * 2.0f},
                          u32(with_alpha(ink, 0.4f)), k.s(tok::kHairline));
        break;
    }
    case KeyKind::Done:
        audit::note(id, lo, hi);
        glyph(k, dl, icon::kEnter, c, ink_size.icon, ink);
        break;
    }
}

} // namespace

float Keyboard::step(const Kit& k, bool want) {
    p_ = approach(p_, want ? 1.0f : 0.0f, k.dt, tok::kSheet);
    return ease_out(p_);
}

bool Keyboard::draw(Kit& k, const Box& box, float full, std::string& text, const FieldRules& rules,
                    bool numbers) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Opaque: what lies under it (the piano's keys, the page) must not show through the keys.
    ImVec4 ground = col::kSurface;
    ground.w = 1.0f;
    dl->AddRectFilled({box.x0, box.y0}, {box.x1, box.y1}, u32(ground));
    dl->AddLine({box.x0, box.y0}, {box.x1, box.y0}, u32(col::kHairline), k.s(tok::kHairline));
    k.toast_avoid({box.x0, box.y0, box.w(), box.h()});
    // The keys sit at their full size and slide in with the box.
    const Box keys{box.x0, box.y0, box.x1, box.y0 + full};
    const Layer layer = numbers ? Layer::Numbers : layer_;
    const std::span<const KeyRow> rows = keyboard_rows(layer);
    const float row_pitch =
        (keys.h() - 2.0f * k.s(osk_tok::kKeyPad)) / static_cast<float>(rows.size());
    const KeyInk ink_size = key_ink(row_pitch, k.u);
    bool done_pressed = false;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        for (std::size_t i = 0; i < rows[r].count; ++i) {
            const KeySpec& key = rows[r].keys[i];
            const Box cell = keyboard_key(keys, k.u, layer, r, i);
            char id[48]; // "##key-" and two 20-digit numbers
            std::snprintf(id, sizeof id, "##key-%zu-%zu", r, i);
            // The key's face is its hit area: the cell less half the dead gap on each side.
            const Box face = keyboard_key_hit(cell, k.s(osk_tok::kKeyGap));
            ImGui::SetCursorScreenPos({face.x0, face.y0});
            const bool pressed = ImGui::InvisibleButton(id, {face.w(), face.h()});
            // A letter types when the finger lifts on the key it landed on (the tap rule), like
            // every control; backspace also repeats while it is held.
            const bool down = tapped(pressed);
            // A letter or the space the field would not take (it is full) is disabled.
            const bool types = key.kind == KeyKind::Letter || key.kind == KeyKind::Space;
            const bool refused = types && take(text, key.kind == KeyKind::Space ? " " : key.text,
                                               rules) != Take::Yes;
            const State st = item_state(k, false, true, refused);
            const bool done = key.kind == KeyKind::Done;
            draw_state(k, dl, id, face, face, st, done ? Idle::Accent : Idle::Fill);
            const ImVec4 ink = state_ink(st, done ? col::kAccentInk : col::kText, done);
            draw_key_face(k, dl, key, id, face, ink, ink_size, layer);
            switch (key.kind) {
            case KeyKind::Letter:
                if (down && !refused) {
                    type_text(text, key.text, rules);
                }
                break;
            case KeyKind::Backspace:
                backspace_key(k, down, backspace_at_, text);
                break;
            case KeyKind::Layer:
                if (tapped(pressed)) {
                    layer_ = layer_ == Layer::Symbols ? Layer::Letters : Layer::Symbols;
                }
                break;
            case KeyKind::Clear:
                if (tapped(pressed)) {
                    text.clear();
                }
                break;
            case KeyKind::Space:
                if (down && !refused) {
                    type_text(text, " ", rules);
                }
                break;
            case KeyKind::Done:
                done_pressed = tapped(pressed);
                break;
            }
        }
    }
    return done_pressed;
}

// ---- The field ---------------------------------------------------------------------------------

void field_text(Kit& k, ImDrawList* dl, const Box& slot, const std::string& text, const char* hint,
                bool caret, float size_pt) {
    const float size = k.s(size_pt > 0.0f ? size_pt : tok::kBody);
    float caret_x = slot.x0;
    if (!text.empty()) {
        const ImVec2 ts = text_size(k.regular, size, text.c_str());
        const float shift = std::max(0.0f, ts.x - slot.w());
        // A hairline of room on the left: a letter that starts on the edge (an A) is not cut by it.
        dl->PushClipRect({slot.x0 - k.s(tok::kHairline), slot.y0}, {slot.x1, slot.y1}, true);
        label_at(k, dl, k.regular, size, {slot.x0 - shift, slot.y0, slot.x1, slot.y1},
                 LabelAlign::Left, col::kText, text.c_str());
        dl->PopClipRect();
        caret_x = slot.x0 + std::min(ts.x, slot.w());
    } else if (hint != nullptr) {
        const FitText cut = fit(k.regular, size, hint, slot.w());
        label_at(k, dl, k.regular, size, slot, LabelAlign::Left, col::kText3, cut.c_str());
    }
    if (caret) {
        const float x = caret_x + k.s(tok::kHairline);
        const float half = size * 0.55f;
        dl->AddLine({x, slot.cy() - half}, {x, slot.cy() + half}, u32(col::kAccent),
                    k.s(tok::kFocusRing));
    }
}

// ---- The name card -----------------------------------------------------------------------------

NameDrawn draw_name_card(Kit& k, const Box& area, float keys_top, std::string& text,
                         const NameCard& c) {
    NameDrawn out;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float title_w = text_size(k.bold, k.s(tok::kLabel), c.title).x;
    NameCardSpec spec;
    spec.quick = static_cast<int>(c.quick.size());
    spec.own = c.own != nullptr;
    spec.width = c.width;
    spec.top = c.top;
    spec.above = c.above;
    const NameCardPlan p = plan_name_card(area, k.u, keys_top, spec, title_w);
    out.card = p.card;
    sheet_card(k, dl, {p.card.x0, p.card.y0}, {p.card.x1, p.card.y1}, k.s(tok::kRadiusSheet),
               c.fill);
    k.toast_avoid({p.card.x0, p.card.y0, p.card.w(), p.card.h()});
    if (p.compact) {
        label_at(k, dl, k.bold, k.s(tok::kLabel), p.title, LabelAlign::Left, col::kText, c.title);
    } else {
        text_at(k, dl, k.bold, k.s(tok::kTitle), {p.title.x0, p.title.y0}, col::kText, c.title);
    }
    // The field: a tap on it with a finger raises the keyboard.
    {
        ImGui::SetCursorScreenPos({p.field.x0, p.field.y0});
        out.field_tapped = ImGui::InvisibleButton("##name-field", {p.field.w(), p.field.h()});
        State st = item_state(k, false);
        st.focus = st.focus || c.focused;
        draw_state(k, dl, "##name-field", p.field, p.field, st, Idle::Fill);
        audit::note("##name-field", {p.field.x0, p.field.y0}, {p.field.x1, p.field.y1},
                    InkKind::Target);
        field_text(k, dl,
                   {p.field.x0 + k.s(tok::kS3), p.field.y0, p.field.x1 - k.s(tok::kS3), p.field.y1},
                   text, nullptr, c.focused);
    }
    for (int i = 0; p.quick && i < spec.quick; ++i) {
        const Box cell = quick_name_cell(p.chips, k.u, p.chip_gap, static_cast<std::size_t>(i));
        char id[24];
        std::snprintf(id, sizeof id, "##quick-%d", i);
        ChipOpts o;
        o.width = cell.w();
        if (chip(k, id, c.quick[static_cast<std::size_t>(i)], {cell.x0, cell.y0}, o)) {
            text.assign(c.quick[static_cast<std::size_t>(i)]);
        }
    }
    if (p.compact) {
        // Cancel is a cross beside the field, Save the card's right end.
        const ImVec2 cc{p.cancel.cx(), p.cancel.cy()};
        ImGui::SetCursorScreenPos({p.cancel.x0, p.cancel.y0});
        if (ImGui::InvisibleButton("##name-cancel", {p.cancel.w(), p.cancel.h()})) {
            out.result = NameResult::Cancel;
        }
        const State st = item_state(k, false);
        const float half = k.s(tok::kFace) * 0.5f;
        const Box face{cc.x - half, cc.y - half, cc.x + half, cc.y + half};
        draw_state(k, dl, "##name-cancel", face, p.cancel, st, Idle::Fill, half);
        audit::note("##name-cancel", {face.x0, face.y0}, {face.x1, face.y1});
        glyph(k, dl, icon::kClose, cc, k.s(tok::kIconSm), state_ink(st, col::kText));
    } else {
        if (c.own != nullptr &&
            action_button(k, "##name-own", icon::kReset, c.own, {p.own.x0, p.own.y0},
                          {p.own.w(), p.own.h()}, c.own_enabled)) {
            out.result = NameResult::Own;
        }
        if (action_button(k, "##name-cancel", icon::kClose, "Cancel", {p.cancel.x0, p.cancel.y0},
                          {p.cancel.w(), p.cancel.h()}, true)) {
            out.result = NameResult::Cancel;
        }
    }
    if (action_button(k, "##name-save", icon::kCheck, "Save", {p.save.x0, p.save.y0},
                      {p.save.w(), p.save.h()}, c.can_save, true)) {
        out.result = NameResult::Save;
    }
    return out;
}

// ---- The dialog --------------------------------------------------------------------------------

void NameDialog::open(std::string_view text, bool by_touch_now, bool has_quick) {
    requested_ = true;
    closing_ = false;
    set_field_open(owner_, true); // before the next event: the dialog keeps typing and touches
    text_ = cut_utf8(text, rules_.max_bytes);
    // A finger raises the keyboard at once; with one-tap names to pick from, the field waits for
    // a tap, so the names stay in view until the player chooses to type.
    focused_ = !(by_touch_now && has_quick);
    keys_ = by_touch_now && focused_;
    keyboard_.reset();
}

NameResult NameDialog::draw(Kit& k, const char* popup_id, const NameCard& card_in) {
    if (requested_) {
        ImGui::OpenPopup(popup_id);
        requested_ = false;
        open_ = true;
    }
    if (!open_) {
        closing_ = false;
        return NameResult::None;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize(display);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::BeginPopupModal(popup_id, nullptr, kFlags)) {
        open_ = false;
        closing_ = false;
        set_field_open(owner_, false);
        return NameResult::Cancel;
    }
    if (closing_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        open_ = false;
        closing_ = false;
        set_field_open(owner_, false);
        return NameResult::None;
    }
    g_modal_frame = ImGui::GetFrameCount();
    const bool was_quiet = audit::quiet();
    audit::set_quiet(false);
    // Under the top row, whose status text is drawn over every window.
    const Box area{0.0f, k.top_row_bottom, display.x, display.y};
    NameCard card = card_in;
    card.above = k.top_row_bottom;
    want_text();
    const Typed typed = feed_keyboard(text_, rules_);
    if (typed.any) {
        // A computer keyboard is in use: the on-screen one steps aside.
        focused_ = true;
        keys_ = false;
    }
    card.focused = focused_;
    // The keyboard leaves room for the card's one-row form at least, which may reach over the top
    // row where the window is short.
    const float full = name_keyboard_height(display.x, display.y, k.u);
    const float up = keyboard_.step(k, keys_);
    const float shown = full * up;
    const float keys_top = keys_ ? area.y1 - full : area.y1 - shown;
    NameDrawn drawn = draw_name_card(k, area, keys_top, text_, card);
    if (drawn.field_tapped) {
        focused_ = true;
        keys_ = by_touch();
        keyboard_.reset();
    }
    bool kb_done = false;
    if (shown > 0.0f) {
        kb_done =
            keyboard_.draw(k, {area.x0, area.y1 - shown, area.x1, area.y1}, full, text_, rules_);
    }
    NameResult r = drawn.result;
    if (r == NameResult::None && (typed.done || kb_done) && card.can_save) {
        r = NameResult::Save;
    }
    if (r == NameResult::None && typed.cancel) {
        r = NameResult::Cancel;
    }
    if (r == NameResult::Save || r == NameResult::Cancel) {
        ImGui::CloseCurrentPopup();
        open_ = false;
        set_field_open(owner_, false);
    }
    audit::set_quiet(was_quiet);
    ImGui::EndPopup();
    return r;
}

} // namespace etude::app::ui::osk
