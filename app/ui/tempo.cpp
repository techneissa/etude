// SPDX-License-Identifier: GPL-3.0-or-later
#include "tempo.hpp"

#include "item_probe.hpp"

#include "bar_metrics.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "settings_catalogue.hpp"
#include "settings_defaults.hpp"
#include "tempo_card.hpp"
#include "tempo_entry.hpp"
#include "transpose_text.hpp"

#include "etude/tempo_control.hpp"

#include <imgui_internal.h> // ClearActiveID

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace etude::app::ui {

namespace {

constexpr float kNoteIcon = bar_metrics::kChipNote; // the beat's note in the chip
constexpr float kNumberSize = 15.0f;                // the tempo number in the chip
constexpr float kRepeatDelayS = 0.4f;
constexpr float kRepeatEveryS = 0.12f;
// The beat flash under the tempo chip (design points): the reduced-effects bar's half width and the
// fading band's height, each longer on the accented beat.
constexpr float kFlashHalfW = 44.0f;
constexpr float kFlashHalfWAccent = 70.0f;
constexpr float kFlashBand = 20.0f;
constexpr float kFlashBandAccent = 34.0f;

int tempo_shown(const SongView& v) { return display_bpm(v.tempo_bpm); }

// The beat's mark: a quarter note, a dotted one where the beat is a dotted quarter (6/8).
const char* beat_icon(const SongView& v) { return v.beat_dotted ? icon::kNoteDotted : icon::kNote; }
float note_slot(const Kit& k, const SongView& v) {
    return k.s(v.beat_dotted ? kNoteIcon + bar_metrics::kChipDot : kNoteIcon);
}

// A rounded pill with centred text (and an optional drawn icon before it), the popover's presets.
// Returns true on a tap.
bool pill(Kit& k, const char* id, const char* icon, const char* text, ImVec2 pos, ImVec2 size,
          bool on) {
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, size);
    const State st = item_state(k, on);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = k.s(tok::kFace);
    const ImVec2 a{pos.x, pos.y + (size.y - h) * 0.5f};
    const ImVec2 b{pos.x + size.x, a.y + h};
    draw_state(k, dl, id, {a.x, a.y, b.x, b.y}, {pos.x, pos.y, pos.x + size.x, pos.y + size.y}, st,
               Idle::Fill);
    const float size_px = k.s(tok::kLabel);
    const ImVec2 ts = text_size(on ? k.bold : k.regular, size_px, text);
    const float icon_w = icon != nullptr ? k.s(tok::kIconSm) + k.s(tok::kS1) : 0.0f;
    float x = pos.x + (size.x - ts.x - icon_w) * 0.5f;
    const float cy = (a.y + b.y) * 0.5f;
    const ImVec4 ink = state_ink(st, col::kText);
    if (icon != nullptr) {
        glyph(k, dl, icon, {x + k.s(tok::kIconSm) * 0.5f, cy}, k.s(tok::kIconSm), ink);
        x += icon_w;
    }
    label_at(k, dl, on ? k.bold : k.regular, size_px, {x, a.y, x + ts.x, b.y}, LabelAlign::Centre,
             ink, text);
    return tapped(pressed);
}

// A minus or plus cell of the speed row: a held press repeats after a pause.
bool step_button(Kit& k, const char* id, const char* glyph_text, ImVec2 pos, float cell,
                 float row_h, bool enabled) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, {cell, row_h});
    ImGui::EndDisabled();
    const bool held = ImGui::IsItemActive() && enabled;
    const State st = item_state(k, false, true, !enabled);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = k.s(tok::kFace);
    const ImVec2 a{pos.x + (cell - d) * 0.5f, pos.y + (row_h - d) * 0.5f};
    draw_state(k, dl, id, {a.x, a.y, a.x + d, a.y + d}, {pos.x, pos.y, pos.x + cell, pos.y + row_h},
               st, Idle::Fill);
    audit::note(id, a, {a.x + d, a.y + d});
    glyph(k, dl, glyph_text, {a.x + d * 0.5f, a.y + d * 0.5f}, k.s(tok::kIconSm),
          state_ink(st, col::kText));
    if (!enabled) {
        return false;
    }
    if (tapped(pressed)) {
        return true;
    }
    if (held) {
        // Held past the long-press time the step repeats, so a far change is one press.
        const float down = ImGui::GetIO().MouseDownDuration[0];
        const float prev = ImGui::GetIO().DeltaTime;
        if (down > kRepeatDelayS && std::fmod(down - kRepeatDelayS, kRepeatEveryS) < prev) {
            return true;
        }
    }
    return false;
}

// The rows under the speed controls that a narrow bar leaves to the popover: the metronome and the
// Flow/Wait choice, then the count-in.
void tempo_extras(Kit& k, UiModel& model, SongPort& song, bool metronome_row, bool mode_row) {
    const SongView& v = song.view();
    if (metronome_row) {
        catalogue_sweep::drawn(Setting::MetronomeOn);
        if (switch_row(k, icon::kMetronome, "Metronome", &model.app.metronome)) {
            k.say(model.app.metronome ? "Metronome on" : "Metronome off");
        }
    }
    if (mode_row) {
        constexpr Seg kModes[] = {{nullptr, "Flow"}, {nullptr, "Wait"}};
        int mode = v.mode == PlayMode::Wait ? 1 : 0;
        if (segmented(k, "mode", mode == 1 ? icon::kSeconds : icon::kTrail, "Mode", kModes, &mode)) {
            song.set_mode(mode == 1 ? PlayMode::Wait : PlayMode::Flow, k.now_ns);
            k.say(mode == 1 ? "Wait: the song waits for each chord"
                            : "Flow: the song plays on and grades each note");
        }
    }
    constexpr Seg kBars[] = {{nullptr, "Off"}, {nullptr, "1 bar"}, {nullptr, "2 bars"}};
    int bars = std::clamp(v.count_in_bars, 0, 2);
    catalogue_sweep::drawn(Setting::CountIn);
    if (segmented(k, "count-in", icon::kCountdown, "Count-in", kBars, &bars)) {
        song.set_count_in_bars(bars, k.now_ns);
    }
}

} // namespace

float TempoControls::chip_width(const Kit& k, ChipLook look, bool dotted) {
    return k.s(bar_metrics::chip_width(look == ChipLook::Full, dotted));
}

void TempoControls::chip(Kit& k, SongPort& song, ImVec2 pos, ImVec2 hit, float drawn_h,
                         ChipLook look, bool live) {
    const SongView& v = song.view();
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton("##tempo", hit);
    audit::note("##tempo", pos, {pos.x + hit.x, pos.y + hit.y}, InkKind::Target);
    const bool held = ImGui::IsItemActive() && live;
    const State st = item_state(k, open_, live);
    // A long press puts the speed back to 100%, once, and the release is no tap.
    if (held && ImGui::GetIO().MouseDownDuration[0] > tok::kLongPress) {
        if (v.speed_percent != 100) {
            song.set_speed(100, k.now_ns);
        }
        k.say("Speed 100%");
        ImGui::ClearActiveID();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const UnitText percent_text = format_unit(v.speed_percent, Unit::Percent);
    const char* const percent = percent_text.c_str();
    // Captioned: the control's name goes under the face, in the accent while the speed is not
    // 100%. Like every face, it is laid out inside its cell `kFaceInset` clear of the sides and
    // fitted in height inside the card it sits on, with its caption under it (face_layout.hpp). A
    // text chip: the pill shape.
    const bool captioned = look == ChipLook::Captioned;
    const char* const caption = bar_metrics::caption_of(bar_metrics::Ctl::Tempo);
    const ImVec2 centre{pos.x + hit.x * 0.5f, pos.y + hit.y * 0.5f};
    const FacePlan plan = plan_face(k, centre, hit, hit.x, drawn_h, captioned ? caption : nullptr);
    const ImVec2 a{plan.lay.face.x0, plan.lay.face.y0};
    const ImVec2 b{plan.lay.face.x1, plan.lay.face.y1};
    const float face_h = b.y - a.y;
    draw_state(k, dl, "##tempo", plan.lay.face, face_area(k, centre, hit), st, Idle::Fill,
               face_h * 0.5f);
    const float cy = (a.y + b.y) * 0.5f;
    const float pad = k.s(bar_metrics::kChipPad);
    // The content is centred in the face: the note, the number, and the badge when there is one.
    const bool badge = look == ChipLook::Full && v.speed_percent != 100;
    char number[16];
    std::snprintf(number, sizeof number, "%d", tempo_shown(v));
    const float badge_size = k.s(tok::kCaption);
    const ImVec2 bs = text_size(k.bold, badge_size, percent);
    const float badge_w = badge ? bs.x + k.s(10.0f) : 0.0f;
    const float number_size =
        k.s(kNumberSize) * bar_metrics::chip_number_scale(static_cast<int>(std::strlen(number)));
    const ImVec2 ns = text_size(k.bold, number_size, number);
    const float slot = note_slot(k, v);
    const float content_w = slot + k.s(bar_metrics::kChipGap) + ns.x + (badge ? k.s(5.0f) + badge_w : 0.0f);
    float x = a.x + (b.x - a.x - content_w) * 0.5f;
    x = std::max(x, a.x + pad * 0.5f);
    const ImVec4 ink = state_ink(st, col::kText);
    glyph(k, dl, beat_icon(v), {x + k.s(kNoteIcon) * 0.5f, cy}, k.s(kNoteIcon), ink);
    x += slot + k.s(bar_metrics::kChipGap);
    // The number sits in a slot of three digits, so the chip does not shift.
    label_at(k, dl, k.bold, number_size, {x, a.y, x + ns.x, b.y}, LabelAlign::Centre, ink, number);
    x += ns.x + k.s(5.0f);
    if (badge) {
        const float bh = bs.y + k.s(4.0f);
        dl->AddRectFilled({x, cy - bh * 0.5f}, {x + badge_w, cy + bh * 0.5f},
                          u32(with_alpha(col::kAccent, 0.22f)), bh * 0.5f);
        label_at(k, dl, k.bold, badge_size, {x, cy - bh * 0.5f, x + badge_w, cy + bh * 0.5f},
                 LabelAlign::Centre, col::kAccent, percent);
    }
    if (plan.lay.caption) {
        label_at(k, dl, k.regular, plan.caption_size, plan.lay.caption_box, LabelAlign::Centre,
                 v.speed_percent != 100 ? col::kAccent : col::kText2, caption);
        audit::ink({plan.lay.caption_box.x0, plan.lay.caption_box.y0},
                   {plan.lay.caption_box.x1, plan.lay.caption_box.y1});
    }
    if (live && tapped(pressed)) {
        open_ = !open_;
        bpm_mode_ = false;
        bpm_focus_ = false;
        bpm_keys_ = false;
    }
    anchor_x_ = pos.x + hit.x * 0.5f;
}

bool TempoControls::wants_open(Page* page) {
    if (!want_settings_) {
        return false;
    }
    want_settings_ = false;
    *page = Page::Practice;
    return true;
}

void TempoControls::popover(Kit& k, UiModel& model, SongPort& song, const Frame& f, float below_y,
                            bool metronome_row) {
    const SongView& v = song.view();
    // The Flow/Wait chip is not on the narrowest bar: its choice is a row here, in step with it.
    const bool mode_row = !bar_has_mode(f.form);
    if (!v.has_song) {
        open_ = false;
    }
    transpose_.popover(k, song, f, below_y, anchor_x_);
    open_p_ = approach(open_p_, open_ ? 1.0f : 0.0f, k.dt, tok::kMove);
    if (!open_ || !bpm_mode_) {
        bpm_focus_ = false;
    }
    // The field takes only tempos the song's speed range allows: no more digits than the largest
    // has, and no digit that makes a number past it.
    const TempoRange limits =
        tempo_range(v.main_bpm * kMinSpeedPercent / 100.0, v.main_bpm * kMaxSpeedPercent / 100.0);
    osk::FieldRules bpm_rules;
    bpm_rules.kind = osk::FieldKind::Number;
    bpm_rules.max_bytes = static_cast<std::size_t>(tempo_digits(limits));
    bpm_rules.max_value = limits.high;
    // Whatever happens below, the field's state is told when the function ends.
    struct FieldSync {
        bool& focus;
        ~FieldSync() { osk::set_field_open(osk::Owner::Tempo, focus); }
    } field_sync{bpm_focus_};
    // The number pad of the typed tempo: the card keeps above it (the card is laid out to the
    // pad's top as it would be to the piano's).
    const float pad_full = keyboard_height(f.display.x, k.u, 1e9f, 2);
    const float pad_up = bpm_pad_.step(k, bpm_focus_ && bpm_keys_);
    const float pad_shown = pad_full * pad_up;
    bpm_pad_box_ =
        pad_shown > 0.0f ? Box{0.0f, f.display.y - pad_shown, f.display.x, f.display.y} : Box{};
    const auto draw_pad = [&] {
        if (pad_shown <= 0.0f) {
            return;
        }
        if (begin_layer("##bpm-keys", {bpm_pad_box_.x0, bpm_pad_box_.y0},
                        {bpm_pad_box_.w(), bpm_pad_box_.h()})) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            if (bpm_pad_.draw(k, bpm_pad_box_, pad_full, bpm_text_, bpm_rules, true)) {
                bpm_apply_ = true;
            }
        }
        ImGui::End();
    };
    if (open_p_ <= 0.0f) {
        column_h_[0] = column_h_[1] = 0.0f;
        draw_pad();
        return;
    }
    const float t = ease_out(open_p_);
    // One column where it fits under the bar; else two (the speed, then the metronome and the
    // count-in), tempo_card.hpp.
    const float card_floor =
        bpm_focus_ && bpm_keys_ ? std::min(f.keyboard_top, f.display.y - pad_full) : f.keyboard_top;
    // With the number pad up the card keeps to the readout and the field being typed in.
    const bool entry = bpm_mode_ && bpm_focus_ && bpm_keys_;
    TempoCardInput card_in{f.display.x,
                           card_floor,
                           below_y,
                           k.u,
                           3 + (bpm_mode_ ? 1 : 0),
                           tempo_right_rows(metronome_row, mode_row)};
    if (entry) {
        card_in.left_rows = 2;
        card_in.right_rows = 0;
        card_in.single = true;
    }
    const TempoCardPlan plan = plan_tempo_card(card_in);
    const float w = plan.w;
    const float x =
        std::clamp(anchor_x_ - w * 0.5f, k.s(tok::kS3), f.display.x - w - k.s(tok::kS3));
    const float y = plan.y + (1.0f - t) * -k.s(tok::kS2);
    const float max_h = card_floor - y - k.s(tok::kS2);
    const float pad = plan.pad;
    const float pad_y = plan.pad_y;
    const float inner = plan.col_w;
    const float row_h = k.s(tok::kHit);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    // The card is its own window: the bar's window would clip it.
    if (begin_layer("##tempo-card", {x, y},
                    {w, std::max(0.0f, std::min(max_h, card_h_ > 0.0f ? card_h_ : k.s(260.0f)))},
                    true)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        // The rows' gap is the plan's: none in the densest card, where the hit areas touch.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, plan.row_gap});
        // A column of the card: a child as tall as its rows were last frame, and as wide as its
        // rows, each plus a pixel (ImGui puts a window on a whole pixel and sizes one that grows to
        // its rows from its own cursor, which cuts a row the kit placed between two pixels by a
        // fraction of one); the first frame lets it grow.
        const auto begin_column = [&](const char* name, ImVec2 at, int index) {
            ImGui::SetCursorScreenPos(at);
            const float known = column_h_[index];
            ImGui::BeginChild(name, {std::ceil(inner) + 1.0f, known},
                              known > 0.0f ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
        };
        // Ends it, remembering where its last row ended (from the child's own top, which ImGui
        // rounds down to a whole pixel); returns that height.
        const auto end_column = [&](int index) {
            const ImGuiWindow* column = ImGui::GetCurrentWindowRead();
            const float used = std::ceil(column->DC.CursorMaxPos.y - column->Pos.y) + 1.0f;
            ImGui::EndChild();
            column_h_[index] = used;
            return used;
        };
        // The columns follow the window's scroll, which the card takes when it is shorter than its
        // rows.
        const float scrolled = ImGui::GetScrollY();
        const ImVec2 first_at{x + pad, y + pad_y - scrolled};
        begin_column("##tempo-body", first_at, 0);

        // The readout: the tempo you hear and the speed, the one being set in the accent.
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float cy = p.y + row_h * 0.5f;
            const ImVec4 tempo_ink = bpm_mode_ ? col::kAccent : col::kText;
            const ImVec4 speed_ink = bpm_mode_ ? col::kText2 : (v.speed_percent != 100 ? col::kAccent : col::kText);
            glyph(k, dl, beat_icon(v), {p.x + k.s(tok::kIconLg) * 0.5f, cy}, k.s(tok::kIconLg),
                  tempo_ink);
            char text[24];
            std::snprintf(text, sizeof text, "%d", tempo_shown(v));
            const float big = k.s(tok::kDisplay);
            label_at(k, dl, k.bold, big,
                     {p.x + k.s(tok::kIconLg) + k.s(tok::kS2), p.y, p.x + inner, p.y + row_h},
                     LabelAlign::Left, tempo_ink, text);
            const UnitText speed = format_unit(v.speed_percent, Unit::Percent);
            const float sz = k.s(tok::kTitle);
            label_at(k, dl, k.bold, sz, {p.x, p.y, p.x + inner, p.y + row_h}, LabelAlign::Right,
                     speed_ink, speed.c_str());
            // The song's transposition between the two: its value, a tap opens its card in this
            // one's place. It takes the room the two numbers leave, never over them.
            {
                const float left_end = p.x + k.s(tok::kIconLg) + k.s(tok::kS2) +
                                       text_size(k.bold, big, text).x + k.s(tok::kS2);
                const float right_start =
                    p.x + inner - text_size(k.bold, sz, speed.c_str()).x - k.s(tok::kS2);
                char shift[text::kShort];
                text::shift(shift, sizeof shift, v.transpose);
                ChipOpts opts;
                opts.icon = icon::kHeight;
                opts.on = v.transpose != 0;
                // The value beside the icon where the numbers leave the room, else the icon alone.
                const bool words = ui::chip_width(k, opts.icon, shift) <= right_start - left_end;
                const char* const value = words ? shift : "";
                const float cw = ui::chip_width(k, opts.icon, value);
                // Centred in the row, moved off the numbers where they come close.
                const float centred = p.x + (inner - cw) * 0.5f;
                const float cx = std::max(left_end, std::min(centred, right_start - cw));
                if (ui::chip(k, "##transpose-open", value, {cx, p.y}, opts)) {
                    open_ = false;
                    transpose_.open();
                }
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy({inner, row_h});
        }

        // The speed: minus, a large slider, plus.
        if (!entry) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float cell = row_h;
            const float gap = k.s(tok::kHitGap); // the slider's hit area keeps a dead gap from the buttons
            if (step_button(k, "##slower", icon::kMinus, p, cell, row_h,
                            v.speed_percent > kMinSpeedPercent)) {
                song.set_speed(step_speed(v.speed_percent, -1), k.now_ns);
            }
            if (step_button(k, "##faster", icon::kPlus, {p.x + inner - cell, p.y}, cell, row_h,
                            v.speed_percent < kMaxSpeedPercent)) {
                song.set_speed(step_speed(v.speed_percent, 1), k.now_ns);
            }
            const float x0 = p.x + cell + gap + k.s(14.0f);
            const float x1 = p.x + inner - cell - gap - k.s(14.0f);
            ImGui::SetCursorScreenPos({x0 - k.s(14.0f), p.y});
            ImGui::InvisibleButton("##speed", {x1 - x0 + k.s(28.0f), row_h});
            const bool active = ImGui::IsItemActive();
            // The knob is the slider's state; it stays in the slider's row.
            const State st = item_state(k, false);
            audit::face("##speed", {x0 - k.s(14.0f), p.y}, {x1 + k.s(14.0f), p.y + row_h},
                        {x0 - k.s(14.0f), p.y}, {x1 + k.s(14.0f), p.y + row_h}, dl);
            const ImGuiIO& io = ImGui::GetIO();
            const SliderTrack track{x0, x1};
            const float span = static_cast<float>(kMaxSpeedPercent - kMinSpeedPercent);
            const float shown_t = static_cast<float>(v.speed_percent - kMinSpeedPercent) / span;
            const auto apply = [&](std::optional<float> value) {
                if (value) {
                    const int percent = snap_speed_percent(
                        kMinSpeedPercent + static_cast<int>(std::lround(*value * span)));
                    if (percent != v.speed_percent) {
                        song.set_speed(percent, k.now_ns);
                    }
                }
            };
            if (ImGui::IsItemActivated()) {
                const SliderPointer who = io.MouseSource == ImGuiMouseSource_Mouse
                                              ? SliderPointer::Mouse
                                              : SliderPointer::Finger;
                apply(k.slider.press(who, io.MouseClickedPos[0].x, io.MouseClickedPos[0].y, track,
                                     shown_t, k.s(tok::kHit) * 0.5f));
            }
            if (active) {
                apply(k.slider.move(io.MousePos.x, io.MousePos.y, track, k.s(kTouchSlopPt)));
                k.slider_active = k.slider.dragging();
            }
            if (ImGui::IsItemDeactivated()) {
                apply(k.slider.release(io.MousePos.x, track, io.MouseDown[0]));
            }
            const float cy = p.y + row_h * 0.5f;
            const float th = k.s(tok::kTrack);
            dl->AddRectFilled({x0, cy - th * 0.5f}, {x1, cy + th * 0.5f}, u32({1, 1, 1, 0.12f}),
                              th);
            const float kx = x0 + (x1 - x0) * shown_t;
            dl->AddRectFilled({x0, cy - th * 0.5f}, {kx, cy + th * 0.5f}, u32(col::kAccent), th);
            // A tick where the song's own speed is.
            const float tx = x0 + (x1 - x0) * static_cast<float>(100 - kMinSpeedPercent) / span;
            dl->AddLine({tx, cy - k.s(9.0f)}, {tx, cy - k.s(5.0f)}, u32(col::kText3), 1.5f);
            const bool grown = (active && !k.slider.scrolling()) ||
                               (st.pressed >= 1.0f &&
                                audit::forced_state() == audit::Forced::Pressed);
            const float kr = k.s(grown ? tok::kKnobHeld : tok::kKnob) * 0.5f;
            dl->AddCircleFilled({kx, cy + k.s(1.0f)}, kr + k.s(1.0f), u32({0, 0, 0, 0.3f}), 24);
            dl->AddCircleFilled({kx, cy}, kr, u32({0.96f, 0.97f, 1.0f, 1.0f}), 24);
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy({inner, row_h});
        }

        // Presets, and "set (note) =" which turns the readout and the field to the tempo.
        if (!entry) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float gap = k.s(tok::kS2);
            const float cw = (inner - 3.0f * gap) / 4.0f;
            const int presets[3] = {50, 75, 100};
            for (int i = 0; i < 3; ++i) {
                const UnitText label_text = format_unit(presets[i], Unit::Percent);
                const char* const label = label_text.c_str();
                char id[16];
                std::snprintf(id, sizeof id, "##p%d", presets[i]);
                if (pill(k, id, nullptr, label, {p.x + static_cast<float>(i) * (cw + gap), p.y},
                         {cw, row_h}, v.speed_percent == presets[i])) {
                    song.set_speed(presets[i], k.now_ns);
                    char said[32];
                    std::snprintf(said, sizeof said, "Speed %s", label);
                    k.say(said);
                }
            }
            const float set_x = p.x + 3.0f * (cw + gap);
            audit::note("##set", {set_x, p.y}, {set_x + cw, p.y + row_h}, InkKind::Target);
            if (pill(k, "##set", beat_icon(v), "=", {set_x, p.y}, {cw, row_h}, bpm_mode_)) {
                bpm_mode_ = !bpm_mode_;
                bpm_text_.clear();
                bpm_focus_ = false;
                bpm_keys_ = false;
                bpm_reveal_ = bpm_mode_ ? 6 : 0;
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy({inner, row_h});
        }

        // The typed tempo: what the song plays at is the main tempo times the speed it gives.
        if (bpm_mode_) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float cy = p.y + row_h * 0.5f;
            glyph(k, dl, beat_icon(v), {p.x + k.s(14.0f), cy}, k.s(tok::kIcon), col::kText2);
            label_at(k, dl, k.regular, k.s(tok::kBody),
                     {p.x + k.s(30.0f), p.y, p.x + k.s(46.0f), p.y + row_h}, LabelAlign::Left,
                     col::kText2, "=");
            const float fw = k.s(96.0f);
            const float fx = p.x + k.s(46.0f);
            // The field: a hit tall, its face a little less. A tap on it takes the computer
            // keyboard's typing; a finger's raises the number pad too.
            ImGui::SetCursorScreenPos({fx, p.y});
            const bool field_tapped = ImGui::InvisibleButton("##bpm", {fw, row_h});
            const bool field_hover = ImGui::IsItemHovered();
            const Box area{fx, p.y, fx + fw, p.y + row_h};
            const Box face{fx, p.y + (row_h - k.s(tok::kFace)) * 0.5f, fx + fw,
                           p.y + (row_h + k.s(tok::kFace)) * 0.5f};
            State fs = item_state(k, false);
            fs.focus = fs.focus || bpm_focus_;
            draw_state(k, dl, "##bpm", face, area, fs, Idle::Fill);
            audit::note("##bpm", {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
            if (field_tapped) {
                if (!bpm_focus_) {
                    bpm_text_.clear();
                }
                bpm_focus_ = true;
                bpm_keys_ = osk::by_touch();
                bpm_pad_.reset();
            } else if (bpm_focus_ && ImGui::IsMouseClicked(0) && !field_hover &&
                       !(bpm_pad_box_.h() > 0.0f && ImGui::GetMousePos().y >= bpm_pad_box_.y0)) {
                bpm_focus_ = false; // a press elsewhere leaves the field
                bpm_keys_ = false;
                bpm_text_.clear();
            }
            char current[8];
            std::snprintf(current, sizeof current, "%d", tempo_shown(song.view()));
            // Until a digit is typed the tempo shown is the field's hint (the typed digits replace
            // it); a field not being typed in shows it as its value.
            const bool show_hint = bpm_focus_ && bpm_text_.empty();
            const std::string shown = bpm_focus_ || !bpm_text_.empty() ? bpm_text_ : current;
            osk::field_text(k, dl, {fx + k.s(tok::kS3), face.y0, fx + fw - k.s(tok::kS3), face.y1},
                            shown, show_hint ? current : nullptr, bpm_focus_);
            if (bpm_focus_) {
                osk::want_text();
                const osk::Typed typed = osk::feed_keyboard(bpm_text_, bpm_rules);
                if (typed.any) {
                    bpm_keys_ = false; // a computer keyboard is in use
                }
                if (typed.done) {
                    bpm_apply_ = true;
                }
                if (typed.cancel) {
                    bpm_focus_ = false;
                    bpm_keys_ = false;
                    bpm_text_.clear();
                }
            }
            if (bpm_apply_) {
                bpm_apply_ = false;
                if (check_tempo_entry(bpm_text_, limits) == TempoEntry::Ok) {
                    const int bpm = std::atoi(bpm_text_.c_str());
                    song.set_target_bpm(static_cast<double>(bpm), k.now_ns);
                    char said[32];
                    std::snprintf(said, sizeof said, "Speed %s",
                                  format_unit(song.view().speed_percent, Unit::Percent).c_str());
                    k.say(said);
                    bpm_text_.clear();
                    bpm_focus_ = false;
                    bpm_keys_ = false;
                } else {
                    // Nothing typed, or a number the song's speed does not allow (the field takes
                    // none, but "0" and a short number can still be below): it stays open and the
                    // range is said again.
                    bpm_hint_until_ = k.now + 2.5;
                }
            }
            const bool say_again = k.now < bpm_hint_until_;
            char hint[40];
            std::snprintf(hint, sizeof hint, say_again ? "Type %d to %d" : "%d to %d", limits.low,
                          limits.high);
            label_at(k, dl, k.regular, k.s(tok::kCaption), {p.x, p.y, p.x + inner, p.y + row_h},
                     LabelAlign::Right, say_again ? col::kAccent : col::kText3, hint);
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy({inner, row_h});
        }

        // The metronome where the bar has no toggle of its own, then the count-in: under the speed
        // controls, or in the second column of the two-column card.
        const auto extras = [&] { tempo_extras(k, model, song, metronome_row, mode_row); };
        float body_h = 0.0f;
        if (entry) {
            body_h = end_column(0);
        } else if (plan.two_columns) {
            body_h = end_column(0);
            const ImVec2 second_at{x + pad + inner + k.s(tok::kS4), y + pad_y - scrolled};
            begin_column("##tempo-extras", second_at, 1);
            extras();
            body_h = std::max(body_h, end_column(1));
        } else {
            extras();
            body_h = end_column(0);
        }
        ImGui::PopStyleVar();
        // A window too short for the card scrolls it with a drag; the field just opened is
        // brought into view, once the card knows how tall it is.
        drag_scroll(k);
        if (bpm_reveal_ > 0) {
            if (ImGui::GetScrollMaxY() > 0.0f) {
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
                bpm_reveal_ = 0;
            } else {
                --bpm_reveal_; // the card learns its height a frame or two after the row appears
            }
        }
        card_h_ = body_h + 2.0f * pad_y;
        dl->ChannelsSetCurrent(0);
        const ImVec2 wp = ImGui::GetWindowPos();
        const float shown_h = std::min(max_h, card_h_);
        // A toast keeps off the card.
        k.toast_avoid({wp.x, wp.y, w, shown_h});
        overlay_card(k, dl, wp, {wp.x + w, wp.y + shown_h}, k.s(tok::kRadiusCard));
        dl->ChannelsMerge();
        // The rectangle the outside-tap test uses, for the settings check to pick a tap clear of it.
        probe::add_rect("##tempo-card-rect", wp, {wp.x + w, wp.y + shown_h});
        // The number pad belongs to the card: a tap on it does not close it.
        const ImVec2 outside_max{bpm_pad_box_.h() > 0.0f ? bpm_pad_box_.x1 : wp.x + w,
                                 bpm_pad_box_.h() > 0.0f ? bpm_pad_box_.y1 : wp.y + shown_h};
        const ImVec2 outside_min{bpm_pad_box_.h() > 0.0f ? std::min(wp.x, bpm_pad_box_.x0) : wp.x,
                                 wp.y};
        if (tapped_outside(outside_, open_, f, outside_min, outside_max)) {
            open_ = false;
        }
    }
    ImGui::End();
    if (!open_) {
        block_input("##tempo-card"); // sliding closed: it takes no input
    }
    ImGui::PopStyleVar();
    draw_pad();
}

bool metronome_button(Kit& k, UiModel& model, ImVec2 centre, ImVec2 hit, float drawn, bool live,
                      const char* caption, bool* long_press) {
    catalogue_sweep::drawn(Setting::MetronomeOn);
    const bool toggled = icon_toggle(k, "##metronome", icon::kMetronome, "Metronome", centre, hit,
                                     drawn, model.app.metronome, live, caption);
    // A long press opens the metronome's settings instead of switching it.
    if (live && ImGui::IsItemActive() && ImGui::GetIO().MouseDownDuration[0] > tok::kLongPress) {
        if (long_press != nullptr) {
            *long_press = true;
        }
        ImGui::ClearActiveID();
    }
    if (toggled) {
        model.app.metronome = !model.app.metronome;
        k.say(model.app.metronome ? "Metronome on" : "Metronome off");
    }
    return toggled;
}

void metronome_rows(Kit& k, UiModel& model) {
    AppSettings& app = model.app;
    catalogue_sweep::drawn(Setting::MetronomeVolume);
    slider_row_int(k, icon::kSoundBoth, "Volume", &app.metronome_volume, kMinClickVolume,
                   kMaxClickVolume, Unit::Percent, nullptr, app_defaults().metronome_volume);
    constexpr Seg kVoices[] = {{nullptr, "Wood block"}, {nullptr, "Click"}, {nullptr, "Beep"}};
    int voice = static_cast<int>(app.metronome_sound);
    catalogue_sweep::drawn(Setting::MetronomeSound);
    if (segmented(k, "metro-sound", icon::kWave, "Sound", kVoices, &voice)) {
        app.metronome_sound = static_cast<ClickVoice>(voice);
    }
    catalogue_sweep::drawn(Setting::MetronomeAccent);
    switch_row(k, icon::kArrowUp, "Accent beat one", &app.metronome_accent,
               "Beat one of each bar clicks louder and higher, so you hear where the bar starts.");
    // Divide splits the beat in 2, or in 3 eighths in 6/8, 9/8 and 12/8, where Triplets has no
    // meaning and is not offered (a saved Triplets shows as Divide there, and plays as it).
    const bool compound = k.song != nullptr && k.song->view().beat_dotted;
    constexpr Seg kSubs[] = {{nullptr, "Off"}, {nullptr, "Divide"}, {nullptr, "Triplets"}};
    int sub = static_cast<int>(app.metronome_subdivision);
    if (compound && app.metronome_subdivision == ClickSubdivision::Triplets) {
        sub = static_cast<int>(ClickSubdivision::Divide);
    }
    catalogue_sweep::drawn(Setting::MetronomeSubdivision);
    if (segmented(k, "metro-sub", icon::kRepeat, "Subdivide",
                  std::span<const Seg>(kSubs, compound ? 2 : 3), &sub,
                  "Extra, quieter clicks between the beats: Divide splits each beat in two "
                  "(in three eighths in 6/8, 9/8 and 12/8), Triplets in three.")) {
        app.metronome_subdivision = static_cast<ClickSubdivision>(sub);
    }
    catalogue_sweep::drawn(Setting::MetronomePulse);
    switch_row(k, icon::kFlash, "Beat pulse", &app.metronome_pulse,
               "A pulse on the hit line with each beat, for practising without sound.");
}

void count_in_indicator(Kit& k, const SongView& v, const TopIndicator& t, float font) {
    if (v.count_in_beat <= 0 || !t.visible()) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const double beats = static_cast<double>(v.count_in_beats) * v.count_in_progress;
    const auto phase = static_cast<float>(beats - std::floor(beats + 1e-9));
    // A new beat grows in a little; with reduced effects only the light changes.
    const float appear = k.reduced_motion ? 1.0f : ease_out(std::min(1.0f, phase / 0.2f));
    // Beat one is the accent: it is where the bar starts.
    const ImVec4 lit = v.count_in_beat == 1 ? col::kAccent : col::kText;
    const float cy = t.rect.y + t.rect.h * 0.5f;
    if (t.dot > 0.0f) {
        const int dots = std::max(v.count_in_bar_beats, v.count_in_beat);
        for (int i = 0; i < dots; ++i) {
            const float cx = t.rect.x + t.dot * 0.5f + static_cast<float>(i) * (t.dot + t.dot_gap);
            const bool on = i + 1 == v.count_in_beat;
            const float r = t.dot * 0.5f * (on ? 1.0f + 0.3f * (1.0f - appear) : 1.0f);
            dl->AddCircleFilled({cx, cy}, r, u32(on ? lit : ImVec4{1, 1, 1, 0.22f}), 16);
        }
    }
    char text[16];
    std::snprintf(text, sizeof text, "%d", v.count_in_beat);
    label_at(k, dl, k.bold, font, {t.number.x, t.rect.y, t.number.right(), t.rect.bottom()},
             LabelAlign::Centre, with_alpha(lit, 0.6f + 0.4f * appear), text);
}

void beat_pulse_overlay(Kit& k, const SongView& v, ImVec2 min, ImVec2 max) {
    if (!v.beat_active) {
        return;
    }
    // Reduced effects: a short static tick on the hit line, no fade.
    const float life = k.reduced_motion ? 0.12f : 0.24f;
    const auto age = static_cast<float>(v.beat_age_s);
    if (age < 0.0f || age >= life) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec4 tint = v.beat_accent ? col::kAccent : col::kText;
    if (k.reduced_motion) {
        const float half = k.s(v.beat_accent ? kFlashHalfWAccent : kFlashHalfW);
        const float mid = (min.x + max.x) * 0.5f;
        dl->AddRectFilled({mid - half, max.y - k.s(5.0f)}, {mid + half, max.y},
                          u32(with_alpha(tint, 0.9f)), k.s(2.0f));
        return;
    }
    const float fade = 1.0f - age / life;
    const float strength = fade * fade * (v.beat_accent ? 0.85f : 0.5f);
    const float band = k.s(v.beat_accent ? kFlashBandAccent : kFlashBand);
    const ImU32 clear = u32(with_alpha(tint, 0.0f));
    const ImU32 lit = u32(with_alpha(tint, strength));
    dl->AddRectFilledMultiColor({min.x, max.y - band}, {max.x, max.y}, clear, clear, lit, lit);
    dl->AddRectFilled({min.x, max.y - k.s(2.0f)}, {max.x, max.y}, u32(with_alpha(tint, strength)));
}

void count_in_band(Kit& k, const SongView& v, const CountBand& band, float shown, float scale) {
    const bool counting_in = v.count_in_beat > 0;
    const bool countdown = !counting_in && v.countdown_left_s > 0.0;
    if (!band.visible() || !(shown > 0.0f) || (!counting_in && !countdown)) {
        return;
    }
    // Under every window of the UI (the bar, the band under it, popovers), over the notes.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    int number = 0;
    float phase = 1.0f; // 0 as a beat or a second begins, 1 as it ends
    if (counting_in) {
        number = v.count_in_beat;
        const double beats = static_cast<double>(v.count_in_beats) * v.count_in_progress;
        phase = static_cast<float>(beats - std::floor(beats + 1e-9));
    } else {
        const double left = v.countdown_left_s;
        number = static_cast<int>(std::ceil(left));
        phase = static_cast<float>(1.0 - (left - std::floor(left - 1e-9)));
    }
    // The pulse: each new beat comes in a little larger and brighter and settles; with reduced
    // effects only its light changes.
    const float settle = ease_out(std::min(1.0f, phase / tok::kSheet));
    const float grow = k.reduced_motion ? 0.0f : tok::kCountPulseGrow * (1.0f - settle);
    // Beat one is the accent: it is where the bar starts.
    const ImVec4 lit = counting_in && number == 1 ? col::kAccent : col::kText;
    const float font = band.font_px * scale * (1.0f + grow);
    char text[16];
    std::snprintf(text, sizeof text, "%d", number);
    // The number's slot: the band's width, a line of the (grown) font tall round its centre.
    const ImVec2 c{band.number_cx * scale, band.number_cy * scale};
    const Box slot{band.band.x * scale, c.y - font * 0.5f, band.band.right() * scale,
                   c.y + font * 0.5f};
    label_at(k, dl, k.bold, font, slot, LabelAlign::Centre,
             with_alpha(lit, shown * (0.75f + 0.25f * settle)), text);
    if (!counting_in) {
        return;
    }
    const int dots = std::max(v.count_in_bar_beats, v.count_in_beat);
    const float d = band.dot_px * scale;
    const float gap = band.dot_gap_px * scale;
    const float row = static_cast<float>(dots) * d + static_cast<float>(std::max(0, dots - 1)) * gap;
    const float y = band.dots_cy * scale;
    for (int i = 0; i < dots; ++i) {
        const float x = c.x - row * 0.5f + d * 0.5f + static_cast<float>(i) * (d + gap);
        const bool on = i + 1 == v.count_in_beat;
        const float r = d * 0.5f * (on ? 1.0f + grow : 1.0f);
        dl->AddCircleFilled({x, y}, r,
                            u32(on ? with_alpha(lit, shown) : with_alpha(col::kText, 0.25f * shown)),
                            16);
    }
}

} // namespace etude::app::ui
