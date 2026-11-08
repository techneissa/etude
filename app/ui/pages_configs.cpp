// SPDX-License-Identifier: GPL-3.0-or-later
// The Configs settings page: three saved configs as cards with a live preview each (a tap applies
// one) and, under each, three icon buttons (Rename, Save current here, Clear); a row of the other
// profiles to copy a config from; and Reset all to default settings as a quiet row at the bottom.
// Every word takes the lines its measured width needs: nothing here is cut with an ellipsis.
#include "pages.hpp"

#include "configs_layout.hpp"
#include "configs_logic.hpp"
#include "confirm_arm.hpp"
#include "crash_layout.hpp" // wrap_report
#include "icon_audit.hpp"
#include "icons.hpp"
#include "item_probe.hpp"
#include "osk.hpp"
#include "pages_configs_confirm.hpp"
#include "preview.hpp"
#include "profile_ui.hpp"
#include "row_layout.hpp"
#include "tap.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

// The slot whose name is being edited, -1 for none.
int g_rename_slot = -1;
osk::NameDialog g_rename_dialog{osk::Owner::LookName, osk::kLookNameRules};

void open_rename(UiModel& model, int slot) {
    g_rename_slot = slot;
    g_rename_dialog.open(model.user_presets.slot_name(slot), osk::by_touch(), true);
}

void rename_dialog(Kit& k, UiModel& model) {
    if (g_rename_slot < 0) {
        return;
    }
    osk::NameCard card;
    card.title = "Name this config";
    card.quick = kQuickNames;
    card.fill = col::kSurfaceHi;
    // An empty name saves as the slot's default one.
    const osk::NameResult result = g_rename_dialog.draw(k, "##rename", card);
    if (result == osk::NameResult::Save) {
        if (config_rename(model, g_rename_slot, g_rename_dialog.text())) {
            k.say("Renamed to " + std::string(model.user_presets.slot_name(g_rename_slot)));
        }
    }
    if (result == osk::NameResult::Save || result == osk::NameResult::Cancel) {
        g_rename_slot = -1;
    }
}

// `words` broken at spaces into the lines `room` points hold at the real font (never by a
// constant).
std::vector<std::string> words_in(Kit& k, const char* words, float size, float room) {
    return wrap_report(words, room, [&](const std::string& line) {
        return text_size(k.regular, size, line.c_str()).x;
    });
}

// Reset all to default settings: a quiet row in the warning colour with the trash icon; the first
// tap arms it (the words change to say so), and a second tap within 3 s puts the look back to its
// defaults. The saved configs and the player's own choices stay (`reset_look`). The words take
// as many lines as their width needs in the row.
void reset_all_row(Kit& k, UiModel& model) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImGuiID reset_id = ImGui::GetID("##reset-all");
    const bool reset_armed = armed(k, reset_id);
    const char* words = reset_armed ? "Tap again to reset" : "Reset all to default settings";
    const float size = k.s(tok::kLabel);
    const std::vector<std::string> lines =
        words_in(k, words, size, configs_reset_words_room(w, k.u));
    const float h = configs_reset_row_h(static_cast<int>(lines.size()), k.u);
    const Box row{pos.x, pos.y, pos.x + w, pos.y + h};
    const bool pressed = press_area(k, "##reset-all", row);
    const State st = item_state(k, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float face_h = std::max(k.s(tok::kFace), h - k.s(tok::kHit) + k.s(tok::kFace));
    const Box face{row.x0, row.cy() - face_h * 0.5f, row.x1, row.cy() + face_h * 0.5f};
    dl->AddRectFilled({face.x0, face.y0}, {face.x1, face.y1},
                      u32(with_alpha(col::kWarning, reset_armed ? tok::kSelectedFill : 0.08f)),
                      face_radius(k, face));
    draw_state(k, dl, "##reset-all", face, row, st);
    audit::note("##reset-all", {row.x0, row.y0}, {row.x1, row.y1}, InkKind::Target);
    const ImVec4 ink = state_ink(st, col::kWarning);
    const float icon_w = k.s(tok::kIconSm) + k.s(tok::kS2);
    float block_w = 0.0f;
    for (const std::string& line : lines) {
        block_w = std::max(block_w, text_size(k.regular, size, line.c_str()).x);
    }
    const float x0 = row.x0 + (w - block_w - icon_w) * 0.5f;
    if (!audit::icons_hidden()) {
        glyph(k, dl, icon::kClear, {x0 + k.s(tok::kIconSm) * 0.5f, row.cy()}, k.s(tok::kIconSm),
              ink);
    }
    const float line_h = configs_words_line_h(k.u);
    const float top = row.cy() - line_h * static_cast<float>(lines.size()) * 0.5f;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float y = top + static_cast<float>(i) * line_h;
        label_at(k, dl, k.regular, size, {x0 + icon_w, y, x0 + icon_w + block_w, y + line_h},
                 LabelAlign::Centre, ink, lines[i].c_str());
    }
    if (tapped(pressed)) {
        if (reset_all_tap(model.look, reset_armed) == TapStep::Fire) {
            k.say("Settings reset to defaults");
            k.confirm_id = 0;
        } else {
            k.confirm_id = reset_id;
            k.confirm_until = k.now + 3.0;
            k.say("Tap again to reset");
        }
    }
}

// What the page says goes to the kit's toast.
class KitToaster : public ConfigToaster {
  public:
    explicit KitToaster(Kit& k) : k_(k) {}
    void say(const std::string& text) override { k_.say(text); }
    void say_undoable(const std::string& text, std::function<void()> undo) override {
        k_.say_undoable(text, std::move(undo));
    }

  private:
    Kit& k_;
};

// The place an empty card's preview would be: a hairline frame with the card's words in it, on as
// many lines as their width needs in the frame.
void empty_preview(Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, const ConfigsCards& c, State st) {
    dl->AddRectFilled(a, b, u32(with_alpha(col::kText3, 0.10f)), k.s(tok::kRadiusMd));
    dl->AddRect(a, b, u32(col::kHairline), k.s(tok::kRadiusMd), 0, 1.0f);
    const float size = k.s(tok::kBody);
    const std::vector<std::string> lines =
        words_in(k, kConfigEmptyWords, size, configs_empty_words_room(c, k.u));
    const float line_h = configs_words_line_h(k.u);
    const float top = (a.y + b.y) * 0.5f - line_h * static_cast<float>(lines.size()) * 0.5f;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float y = top + static_cast<float>(i) * line_h;
        label_at(k, dl, k.regular, size, {a.x, y, b.x, y + line_h}, LabelAlign::Centre,
                 state_ink(st, col::kText2), lines[i].c_str());
    }
}

// The three icon buttons under card `slot`, each a hit square, a dead gap apart: Rename, Save
// current here and Clear. Rename and Clear are greyed on an empty card.
void config_buttons(Kit& k, UiModel& model, int slot, const ConfigsCards& c, ImVec2 pos) {
    struct Button {
        ConfigAction action;
        const char* id;
        const char* icon;
        const char* label;
    };
    static constexpr Button kButtons[] = {
        {ConfigAction::Rename, "rename", icon::kRename, "Rename"},
        {ConfigAction::SaveHere, "save", icon::kSave, "Save current here"},
        {ConfigAction::Clear, "clear", icon::kClear, "Clear"},
    };
    const bool used = model.user_presets.is_used(slot);
    for (int i = 0; i < kConfigButtons; ++i) {
        const Button& b = kButtons[i];
        char id[32];
        std::snprintf(id, sizeof id, "##config-%d-%s", slot + 1, b.id);
        ImGui::SetCursorScreenPos({pos.x + c.button_x(i), pos.y + c.btns_y});
        const bool enabled = config_action_enabled(b.action, used);
        if (icon_action(k, id, b.icon, b.label, c.btn, enabled, b.action == ConfigAction::Clear)) {
            KitToaster toast(k);
            if (config_button_act(model, b.action, slot, toast)) {
                open_rename(model, slot);
            }
        }
    }
}

// A card's name in `font` broken at spaces into the lines `c` gives it room for, by the width it
// measures at the real font (a word wider than a line is cut at a character, never an ellipsis).
std::vector<std::string> name_lines(Kit& k, ImFont* font, const std::string& name,
                                    const ConfigsCards& c) {
    return wrap_report(name, configs_name_room(c), [&](const std::string& line) {
        return text_size(font, k.s(tok::kBody), line.c_str()).x;
    });
}

// The name each card shows and the font it is in: the slot's own, bold while it is the one in use.
std::string card_name(const UiModel& model, int slot) {
    return model.user_presets.is_used(slot) ? std::string(model.user_presets.slot_name(slot))
                                            : config_default_name(slot);
}

// One card at `pos`: the tap area, its preview (drawn from the slot's own config, never the look in
// use) over its name, and the buttons under it.
void config_card(Kit& k, UiModel& model, int slot, const ConfigsCards& c, ImVec2 pos) {
    char id[16];
    std::snprintf(id, sizeof id, "##config-%d", slot + 1);
    const bool used = model.user_presets.is_used(slot);
    const bool on = config_is_current(model, slot);
    const Box box{pos.x, pos.y, pos.x + c.tap_w, pos.y + c.tap_h};
    const bool pressed = press_area(k, id, box);
    const State st = item_state(k, on);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_state(k, dl, id, box, box, st, Idle::Fill);
    audit::note(id, {box.x0, box.y0}, {box.x1, box.y1}, InkKind::Target);
    const ImVec2 pa{pos.x + c.pad, pos.y + c.pad};
    const ImVec2 pb{pa.x + c.preview_w, pa.y + c.preview_h};
    std::string name_text;
    if (used) {
        const std::optional<LookSettings> own = config_card_look(model.user_presets, slot);
        LivePreview* preview = k.config_previews[static_cast<std::size_t>(slot)];
        if (own && preview != nullptr) {
            ImGui::SetCursorScreenPos(pa);
            preview->draw(k, *own, c.preview_w, c.preview_h, PreviewLoop::Song);
        }
        name_text = card_name(model, slot);
    } else {
        empty_preview(k, dl, pa, pb, c, st);
        name_text = card_name(model, slot);
    }
    // The name on the lines its width needs under the preview, the first where one line would sit.
    ImFont* font = on ? k.bold : k.regular;
    const std::vector<std::string> lines = name_lines(k, font, name_text, c);
    const float line_h = configs_words_line_h(k.u);
    const float first = pb.y + (configs_tok::kNameH * k.u - line_h) * 0.5f;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float y = first + static_cast<float>(i) * line_h;
        label_at(k, dl, font, k.s(tok::kBody),
                 {pos.x + c.pad, y, pos.x + c.tap_w - c.pad, y + line_h}, LabelAlign::Centre,
                 used ? (on ? col::kAccent : col::kText) : col::kText3, lines[i].c_str());
    }
    if (tapped(pressed)) {
        KitToaster toast(k);
        config_tap(model, slot, toast);
    }
    config_buttons(k, model, slot, c, pos);
}

void cards_row(Kit& k, UiModel& model) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    // The names decide how tall the name line is: the most lines any of the three needs.
    const ConfigsCards one = configs_cards(avail, k.u);
    int most = 1;
    for (int slot = 0; slot < kUserPresetSlots; ++slot) {
        ImFont* font = config_is_current(model, slot) ? k.bold : k.regular;
        const auto lines =
            static_cast<int>(name_lines(k, font, card_name(model, slot), one).size());
        most = std::max(most, lines);
    }
    const ConfigsCards c = configs_cards(avail, k.u, most);
    for (int slot = 0; slot < kUserPresetSlots; ++slot) {
        config_card(k, model, slot, c, {origin.x + c.x(slot), origin.y + c.y(slot)});
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({avail, c.total_h()});
}

// One pill of the copy row: the other profile's avatar and name; a tap copies their config.
bool copy_pill(Kit& k, int index, const Profile& p, float w, ImVec2 pos) {
    char id[16];
    std::snprintf(id, sizeof id, "##copy-%d", index + 1);
    const Box hit{pos.x, pos.y, pos.x + w, pos.y + k.s(tok::kHit)};
    const bool pressed = press_area(k, id, hit);
    const State st = item_state(k, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float inset = (k.s(tok::kHit) - k.s(tok::kFace)) * 0.5f;
    const Box face{hit.x0, hit.y0 + inset, hit.x1, hit.y1 - inset};
    draw_state(k, dl, id, face, hit, st, Idle::Fill, face.h() * 0.5f);
    audit::note(id, {hit.x0, hit.y0}, {hit.x1, hit.y1}, InkKind::Target);
    const float d = k.s(configs_tok::kAvatar);
    profile_avatar(k, dl, {face.x0 + k.s(tok::kS2) + d * 0.5f, face.cy()}, d, p);
    const float tx = face.x0 + k.s(tok::kS2) + d + k.s(tok::kS2);
    const FitText shown =
        fit(k.regular, k.s(tok::kBody), p.name, face.x1 - k.s(tok::kS3) - tx + 1.0f);
    label_at(k, dl, k.regular, k.s(tok::kBody), {tx, face.y0, face.x1 - k.s(tok::kS3), face.y1},
             LabelAlign::Left, state_ink(st, col::kText), shown.c_str());
    return tapped(pressed);
}

// The question the pills raise (configs_logic.hpp): a tap on a pill only asks.
CopyAsk g_copy_ask;
bool g_ask_last = false; // --panel configs-copy: ask for the last profile when there is one

// "Copy from another profile": a pill for each of the others, or a line saying there is none.
void copy_row(Kit& k) {
    section(k, "Copy from another profile");
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    if (config_other_count(k.profiles) == 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        label_at(k, dl, k.regular, k.s(tok::kBody),
                 {origin.x + k.s(tok::kS3), origin.y, origin.x + avail, origin.y + k.s(tok::kHit)},
                 LabelAlign::Left, col::kText2, kConfigNoOtherProfile);
        ImGui::Dummy({avail, k.s(tok::kHit)});
        return;
    }
    std::array<const Profile*, kMaxProfiles> others{};
    std::array<float, kMaxProfiles> widths{};
    std::array<PillSpot, kMaxProfiles> spots{};
    std::size_t n = 0;
    for (const Profile& p : k.profiles->set->all()) {
        if (p.id == k.profiles->active || n >= others.size()) {
            continue;
        }
        others[n] = &p;
        // The name's whole width: a pill never cuts it; pills that do not fit a line go on the
        // next.
        const float text_w = text_size(k.regular, k.s(tok::kBody), p.name.c_str()).x;
        widths[n] = configs_pill_width(text_w + 1.0f, k.u);
        ++n;
    }
    const int rows = configs_place_pills({widths.data(), n}, avail, k.u, spots);
    for (std::size_t i = 0; i < n; ++i) {
        if (copy_pill(k, static_cast<int>(i), *others[i], widths[i],
                      {origin.x + spots[i].x, origin.y + spots[i].y})) {
            g_copy_ask.ask(others[i]->id);
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({avail, configs_pills_height(rows, k.u)});
}

} // namespace

void configs_page_ask_last() { g_ask_last = true; }

// What the copy question covers is not the audit's: it takes the touches there.
void configs_page_cover_audit() {
    ImVec2 covered_min;
    ImVec2 covered_max;
    if (copy_confirm_covers(&covered_min, &covered_max)) {
        audit::set_overlay(covered_min, covered_max);
    }
}

void page_configs(Kit& k, UiModel& model, int columns) {
    (void)columns;
    if (g_ask_last && k.profiles != nullptr && k.profiles->set != nullptr) {
        for (const Profile& p : k.profiles->set->all()) {
            if (p.id != k.profiles->active) {
                g_copy_ask.ask(p.id);
                g_ask_last = false; // the last of the others is the one it ends on
            }
        }
    }
    configs_page_cover_audit();
    catalogue_sweep::drawn(Setting::Configs);
    cards_row(k, model);
    copy_row(k);
    ImGui::Dummy({1.0f, k.s(tok::kS2)});
    catalogue_sweep::drawn(Setting::ResetAll);
    reset_all_row(k, model);
    rename_dialog(k, model);
    audit::clear_overlay(); // the question is audited itself
    if (const std::optional<std::int64_t> chosen = draw_copy_confirm(k, g_copy_ask)) {
        if (k.profiles != nullptr) {
            KitToaster toast(k);
            config_copy_confirmed(model, *k.profiles, *chosen, toast);
        }
    }
}

} // namespace etude::app::ui
