// SPDX-License-Identifier: GPL-3.0-or-later
#include "accompaniment.hpp"

#include "accompaniment_card.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "item_probe.hpp"
#include "settings_catalogue.hpp"

#include <imgui_internal.h> // ClearActiveID

#include <algorithm>
#include <cmath>

namespace etude::app::ui {

namespace {

MorphGlyph glyph_for(const SongView& v) {
    const bool voice = v.accompaniment_voice;
    switch (v.accompaniment) {
    case AccompState::On:
        return voice ? MorphGlyph::Voice : MorphGlyph::Ensemble;
    case AccompState::Partial:
        return voice ? MorphGlyph::VoicePartial : MorphGlyph::EnsemblePartial;
    case AccompState::Off:
        break;
    }
    return voice ? MorphGlyph::VoiceOff : MorphGlyph::EnsembleOff;
}

const char* label_for(AccompState state) {
    switch (state) {
    case AccompState::On:
        return "Accompaniment on";
    case AccompState::Partial:
        return "Some accompaniment on";
    case AccompState::Off:
        break;
    }
    return "Accompaniment off";
}

} // namespace

void AccompanimentMenu::button(Kit& k, SongPort& song, TempoControls& tempo, ImVec2 centre,
                               ImVec2 hit, float drawn, bool live, const char* caption) {
    const SongView& v = song.view();
    anchor_x_ = centre.x + hit.x * 0.5f;
    // Sounding is lit like the song's sound; all muted is grey with the slash.
    const bool tapped = icon_toggle(k, "##accomp", glyph_for(v),
                                    caption != nullptr ? nullptr : label_for(v.accompaniment),
                                    centre, hit, drawn, v.accompaniment != AccompState::Off, live,
                                    caption);
    // A long press, or a right click, opens the menu instead of switching.
    const bool held = live && ImGui::IsItemActive() &&
                      ImGui::GetIO().MouseDownDuration[0] > tok::kLongPress;
    const bool right = live && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (held || right) {
        open_ = true;
        tempo.close();
        if (held) {
            ImGui::ClearActiveID();
        }
        return;
    }
    if (tapped) {
        song.tap_accompaniment(k.now_ns);
        k.say(label_for(song.view().accompaniment));
    }
}

void AccompanimentMenu::chevron(Kit& k, TempoControls& tempo, ImVec2 centre, ImVec2 hit,
                                float drawn, bool live, const char* caption) {
    anchor_x_ = centre.x - hit.x * 0.5f;
    if (icon_toggle(k, "##accomp-menu", open_ ? MorphGlyph::ChevronUp : MorphGlyph::ChevronDown,
                    caption != nullptr ? nullptr : "Accompaniment parts and volume", centre, hit,
                    drawn, open_, live, caption)) {
        open_ = !open_;
        if (open_) {
            tempo.close();
        }
    }
}

void AccompanimentMenu::popover(Kit& k, UiModel& model, SongPort& song, const Frame& f,
                                float below_y) {
    const SongView& v = song.view();
    if (!v.has_song || !v.has_accompaniment) {
        open_ = false;
    }
    open_p_ = approach(open_p_, open_ ? 1.0f : 0.0f, k.dt, tok::kMove);
    if (open_p_ <= 0.0f) {
        column_h_[0] = column_h_[1] = 0.0f;
        return;
    }
    const float t = ease_out(open_p_);
    const std::span<const AccompPart> parts = v.accompaniment_parts;
    // All, the parts and the volume; the card's shape is accompaniment_card.hpp's.
    const AccompCardPlan plan =
        plan_accomp_card({f.display.x, f.keyboard_top, below_y, k.u,
                          static_cast<int>(parts.size()) + 2});
    const float w = plan.w;
    const float pad = plan.pad;
    const float pad_y = plan.pad_y;
    const float inner = plan.col_w;
    const float x = std::clamp(anchor_x_ - w * 0.5f, k.s(tok::kS3), f.display.x - w - k.s(tok::kS3));
    const float y = below_y + (1.0f - t) * -k.s(tok::kS2);
    const float max_h = f.keyboard_top - y - k.s(tok::kS2);
    const float card_h = std::max(column_h_[0], column_h_[1]) + 2.0f * pad_y;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    // The card is its own window: the bar's window would clip it.
    if (begin_layer("##accomp-card", {x, y},
                    {w, std::max(0.0f, std::min(max_h, column_h_[0] > 0.0f ? card_h : max_h))},
                    true)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            {ImGui::GetStyle().ItemSpacing.x, plan.row_gap});
        // A column of the card: a child as tall as its rows were last frame, and as wide as the
        // card's inside; the first frame lets it grow.
        const auto begin_column = [&](const char* name, float at_x, int index) {
            ImGui::SetCursorScreenPos({at_x, y + pad_y});
            const float known = column_h_[index];
            ImGui::BeginChild(name, {std::ceil(inner) + 1.0f, known},
                              known > 0.0f ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
        };
        const auto end_column = [&](int index) {
            const ImGuiWindow* column = ImGui::GetCurrentWindowRead();
            const float used = std::ceil(column->DC.CursorMaxPos.y - column->Pos.y) + 1.0f;
            ImGui::EndChild();
            column_h_[index] = used;
        };
        int column = 0;
        int in_column = 0;
        begin_column("##accomp-body", x + pad, 0);
        // The row after the last one of the first column starts the second.
        const auto next_row = [&] {
            if (plan.columns == 2 && column == 0 && in_column == plan.first_rows) {
                end_column(0);
                column = 1;
                in_column = 0;
                begin_column("##accomp-body2", x + pad + inner + plan.col_gap, 1);
            }
            ++in_column;
        };
        // All: sounding when every part does. A tap sends every part one way.
        bool all = v.accompaniment == AccompState::On;
        next_row();
        if (switch_row(k, icon::kListMusic, "All", &all)) {
            song.tap_accompaniment(k.now_ns);
            k.say(label_for(song.view().accompaniment));
        }
        for (std::size_t i = 0; i < parts.size(); ++i) {
            bool sounds = !parts[i].muted;
            next_row();
            ImGui::PushID(static_cast<int>(i)); // a part named "All" never shares the All row's id
            if (switch_row(k, icon::kNote, parts[i].name.c_str(), &sounds)) {
                song.toggle_accompaniment_part(i, k.now_ns);
            }
            ImGui::PopID();
        }
        // How loud they sound against the piano: the setting of Settings > Practice.
        catalogue_sweep::drawn(Setting::AccompanimentVolume);
        next_row();
        slider_row_int(k, icon::kSoundBoth, "Volume", &model.app.accompaniment_volume, 0,
                       kMaxAccompanimentVolume, Unit::Percent);
        end_column(column);
        if (column == 0) {
            column_h_[1] = 0.0f;
        }
        ImGui::PopStyleVar();
        // A window too short for the card scrolls it with a drag.
        drag_scroll(k);
        dl->ChannelsSetCurrent(0);
        const ImVec2 wp = ImGui::GetWindowPos();
        const float shown_h = std::min(max_h, std::max(column_h_[0], column_h_[1]) + 2.0f * pad_y);
        k.toast_avoid({wp.x, wp.y, w, shown_h});
        overlay_card(k, dl, wp, {wp.x + w, wp.y + shown_h}, k.s(tok::kRadiusCard));
        dl->ChannelsMerge();
        probe::add_rect("##accomp-card-rect", wp, {wp.x + w, wp.y + shown_h});
        if (tapped_outside(outside_, open_, f, wp, {wp.x + w, wp.y + shown_h})) {
            open_ = false;
        }
    }
    ImGui::End();
    if (!open_) {
        block_input("##accomp-card"); // sliding closed: it takes no input
    }
    ImGui::PopStyleVar();
}

} // namespace etude::app::ui
