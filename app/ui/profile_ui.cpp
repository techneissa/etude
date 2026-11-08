// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_ui.hpp"

#include "bar.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "medal_draw.hpp"
#include "medal_emblem.hpp"
#include "tap.hpp"

#include "etude/medals.hpp"
#include "etude/progress.hpp"
#include "etude/session_state.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront

#include <algorithm>
#include <cstdio>

namespace etude::app::ui {

namespace {

namespace pt = profile_tok;

constexpr const char* kChevronDown = "\xEE\x81\xAD"; // chevron-down
constexpr const char* kChevronUp = "\xEE\x81\xB0";   // chevron-up

ImVec4 rgb(std::uint32_t c) {
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f, 1.0f};
}

// An avatar: the profile's colour as a disc, its letter on it. A circle is the kit's round thing for
// avatars, swatches and badges.
void avatar(const Kit& k, ImDrawList* dl, ImVec2 centre, float d, const Profile& p) {
    dl->AddCircleFilled(centre, d * 0.5f, u32(rgb(p.colour)));
    if (colour_needs_outline(p.colour)) { // black on the dark background: a light edge shows it
        dl->AddCircle(centre, d * 0.5f - k.s(tok::kFocusRing) * 0.5f, u32(rgb(profile_visible_colour(p.colour))), 0,
                      k.s(tok::kFocusRing));
    }
    const Box slot{centre.x - d * 0.5f, centre.y - d * 0.5f, centre.x + d * 0.5f, centre.y + d * 0.5f};
    const std::string initial = profile_initial(p.name);
    const ImVec4 ink = avatar_ink_dark(p.colour) ? col::kAccentInk : ImVec4{1, 1, 1, 1};
    label_at(k, dl, k.bold, d * 0.46f, slot, LabelAlign::Centre, ink, initial.c_str());
}

const Profile* active_profile(const ProfileInfo& info) {
    return info.set != nullptr ? info.set->find(info.active) : nullptr;
}

int streak_of(const ProfileInfo& info, std::size_t i) {
    return i < info.streaks.size() ? info.streaks[i] : 0;
}

// The player's overall title (1 .. 8, "Soloist" is 4) from the medals; 0 until the numbers are in.
int freezes_of(const ProfileInfo& info, std::size_t i) { return i < info.freezes.size() ? info.freezes[i] : 0; }

int title_of(const ProfileInfo& info, std::size_t i) { return i < info.titles.size() ? info.titles[i] : 0; }

const char* sheet_title(bool adding) { return adding ? "New profile" : "Edit profile"; }

// A new profile starts on the goal every profile starts with (core AppSettings).
const int kNewProfileGoalMinutes = AppSettings{}.daily_goal_minutes;

} // namespace

void profile_avatar(const Kit& k, ImDrawList* dl, ImVec2 centre, float d, const Profile& p) {
    avatar(k, dl, centre, d, p);
}

ImVec4 profile_rgb(std::uint32_t colour) { return rgb(profile_visible_colour(colour)); }

// ---- The chip ----------------------------------------------------------------------------------

float ProfileUi::chip_hit_width(const Kit& k, const ProfileInfo& info) const {
    const Profile* p = active_profile(info);
    if (p == nullptr) {
        return 0.0f;
    }
    return profile_chip_width(text_size(k.bold, k.s(tok::kLabel + 1.0f), p->name.c_str()).x, k.u);
}

void ProfileUi::draw_chip(Kit& k, const ProfileInfo& info, const Box& hit) {
    const Profile* p = active_profile(info);
    if (p == nullptr) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float face_h = k.s(tok::kFace);
    const float u = k.u;
    // The face is the hit area less a margin on the right, so two chips never touch.
    const Box face{hit.x0, hit.cy() - face_h * 0.5f, hit.x1 - k.s(tok::kS1), hit.cy() + face_h * 0.5f};
    ImGui::SetCursorScreenPos({hit.x0, hit.y0});
    const bool pressed = ImGui::InvisibleButton("##profile-chip", {hit.w(), hit.h()});
    const State st = item_state(k, panel_);
    draw_state(k, dl, "##profile-chip", face, hit, st, Idle::Fill, face.h() * 0.5f);
    audit::chip("##profile-chip", {face.x0, face.y0}, {face.x1, face.y1}, true, false);
    audit::note("##profile-chip", {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
    const float d = k.s(pt::kChipAvatar);
    avatar(k, dl, {face.x0 + (tok::kS2 + pt::kChipAvatar * 0.5f) * u, face.cy()}, d, *p);
    const float text_x = face.x0 + (tok::kS2 + pt::kChipAvatar + tok::kS2) * u;
    const float chevron = (tok::kS2 + tok::kIconSm) * u;
    const Box slot{text_x, face.y0, face.x1 - chevron, face.y1};
    const FitText name = fit(k.bold, k.s(tok::kLabel + 1.0f), p->name, slot.w());
    label_at(k, dl, k.bold, k.s(tok::kLabel + 1.0f), slot, LabelAlign::Left,
             state_ink(st, col::kText), name.c_str());
    glyph(k, dl, panel_ ? kChevronUp : kChevronDown,
          {face.x1 - (tok::kS2 + tok::kIconSm * 0.5f) * u, face.cy()}, k.s(tok::kIconSm),
          state_ink(st, col::kText2));
    chip_box_ = hit;
    if (tapped(pressed)) {
        panel_ = !panel_;
        editing_ = false;
        if (panel_) {
            outside_ = OutsideTap{};
        }
    }
}

// ---- The panel ---------------------------------------------------------------------------------

void ProfileUi::draw(Kit& k, const ProfileInfo& info, const Box& display, float top_y, float anchor_x) {
    if (info.set == nullptr) {
        panel_ = false;
        sheet_open_ = false;
        sheet_requested_ = false;
    }
    if (demo_ != Demo::None && info.set != nullptr && info.ready) {
        run_demo(info);
    }
    draw_panel(k, info, display, top_y, anchor_x);
    draw_sheet(k, info, display, top_y, anchor_x);
}

void ProfileUi::draw_panel(Kit& k, const ProfileInfo& info, const Box& display, float top_y,
                           float anchor_x) {
    panel_p_ = approach(panel_p_, panel_ && !sheet_open() ? 1.0f : 0.0f, k.dt, tok::kMove);
    if (panel_p_ <= 0.0f || info.set == nullptr) {
        return;
    }
    const float t = ease_out(panel_p_);
    const ProfilePanelPlan plan =
        plan_profile_panel(anchor_x, top_y, display, k.u, info.set->size(), info.progress);
    const Box pb = plan.panel;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    if (begin_layer("##profile-panel", {pb.x0, pb.y0 - (1.0f - t) * k.s(tok::kS2)},
                    {pb.w(), pb.h()})) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        overlay_card(k, dl, {pb.x0, pb.y0}, {pb.x1, pb.y1}, k.s(tok::kRadiusCard), col::kSurface);
        k.toast_avoid({pb.x0, pb.y0, pb.w(), pb.h()});
        label_at(k, dl, k.bold, k.s(tok::kCaption), plan.title, LabelAlign::Left, col::kText3,
                 "WHO'S PLAYING");
        ChipOpts edit;
        edit.icon = icon::kRename;
        edit.on = editing_;
        edit.live = info.ready;
        const float ew = chip_width(k, edit.icon, editing_ ? "Done" : "Edit");
        if (chip(k, "##profiles-edit", editing_ ? "Done" : "Edit",
                 {plan.edit.x1 - ew, plan.edit.y0}, edit)) {
            editing_ = !editing_;
        }
        const Box g = plan.grid;
        if (plan.scrolls) {
            // More people than the window holds: the tiles scroll under the title (a drag, or the wheel).
            ImGui::SetCursorScreenPos({g.x0, g.y0});
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
            ImGui::BeginChild("##profile-grid", {g.w(), g.h()}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
            drag_scroll(k);
            ImGui::Dummy({g.w(), plan.content_h});
        }
        const float dy = plan.scrolls ? -ImGui::GetScrollY() : 0.0f;
        for (std::size_t i = 0; i < plan.tiles; ++i) {
            const Box tb = plan.tile[i];
            draw_tile(k, info, plan, i, {tb.x0, tb.y0 + dy, tb.x1, tb.y1 + dy});
        }
        if (plan.scrolls) {
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        if (info.progress) {
            draw_progress_row(k, info, plan.progress);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    const Box away = {std::min(pb.x0, chip_box_.x0), std::min(pb.y0, chip_box_.y0),
                      std::max(pb.x1, chip_box_.x1), std::max(pb.y1, chip_box_.y1)};
    if (panel_ && outside_.away(true, {away.x0, away.y0}, {away.x1, away.y1})) {
        panel_ = false;
        editing_ = false;
    }
}

// "Progress and household": the library opens on the Progress page.
void ProfileUi::draw_progress_row(Kit& k, const ProfileInfo& info, const Box& row) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool pressed = press_area(k, "##profile-progress", row);
    const State st = item_state(k, false, true, !info.ready);
    draw_state(k, dl, "##profile-progress", row, row, st, Idle::Fill);
    audit::note("##profile-progress", {row.x0, row.y0}, {row.x1, row.y1}, InkKind::Target);
    const ImVec4 ink = state_ink(st, col::kText);
    const float pad = k.s(tok::kS3);
    const float icon = k.s(tok::kIcon);
    glyph(k, dl, icon::kGauge, {row.x0 + pad + icon * 0.5f, row.cy()}, icon, state_ink(st, col::kText2));
    label_at(k, dl, k.regular, k.s(tok::kBody),
             {row.x0 + pad + icon + k.s(tok::kS2), row.y0, row.x1 - pad - icon, row.y1},
             LabelAlign::Left, ink, "Progress and household");
    glyph(k, dl, icon::kChevron, {row.x1 - pad - icon * 0.5f, row.cy()}, icon, state_ink(st, col::kText2));
    if (tapped(pressed) && info.ready) {
        choice_ = {ProfileAction::Progress, 0, {}, 0, -1};
        panel_ = false;
        editing_ = false;
    }
}

namespace {

// A flat tile's person: the avatar, the name with the title's medal, the streak and its freezes under it.
void draw_flat_profile(Kit& k, ImDrawList* dl, const ProfileInfo& info, std::size_t i, const Profile& who, ImVec2 c,
                       float d, const Box& text, ImVec4 ink) {
    avatar(k, dl, c, d, who);
    const int days = streak_of(info, i);
    char line[32];
    if (days > 0) {
        std::snprintf(line, sizeof line, "%d-day streak", days);
    } else {
        std::snprintf(line, sizeof line, "No streak yet");
    }
    // The name and the title on the top line ("Anna \xC2\xB7 Soloist"), the streak under it.
    const float line_h = k.s(tok::kBody) * 1.5f;
    const float cap_h = k.s(tok::kCaption) * 1.5f;
    const float y = text.cy() - (line_h + cap_h) * 0.5f;
    name_title_medal(k, dl, {text.x0, y, text.x1, y + line_h}, k.s(tok::kBody), who.name.c_str(), title_of(info, i), ink);
    const float freeze_w = days > 0 ? draw_freezes(k, dl, {0.0f, 0.0f}, k.s(tok::kCaption), 0, true) + k.s(tok::kS2) : 0.0f;
    const FitText streak = fit(k.regular, k.s(tok::kCaption), line, text.w() - freeze_w);
    label_at(k, dl, k.regular, k.s(tok::kCaption), {text.x0, y + line_h, text.x1, y + line_h + cap_h},
             LabelAlign::Left, col::kText2, streak.c_str());
    if (days > 0) {
        draw_freezes(k, dl, {text.x0 + text_size(k.regular, k.s(tok::kCaption), streak.c_str()).x + k.s(tok::kS2), y + line_h + cap_h * 0.5f},
                     k.s(tok::kCaption), freezes_of(info, i));
    }
}

// A big tile's person: the avatar with the title's medal at its corner, the name, the title, then the streak
// (a flame and the days) and the freezes banked.
void draw_big_profile(Kit& k, ImDrawList* dl, const ProfileInfo& info, std::size_t i, const Profile& who,
                      const Box& tile, ImVec2 c, float d, const Box& name_slot, ImVec4 ink) {
    avatar(k, dl, c, d, who);
    const FitText name = fit(k.bold, k.s(tok::kBody), who.name, name_slot.w());
    label_at(k, dl, k.bold, k.s(tok::kBody), name_slot, LabelAlign::Centre, ink, name.c_str());
    // The title under the name, then the streak: a flame and the days.
    const Box title_slot{name_slot.x0, name_slot.y1, name_slot.x1, name_slot.y1 + k.s(pt::kTileTitleH)};
    if (const int tier = title_of(info, i); tier > 0) {
        // The title's medal at the avatar's lower right, and its words under the name.
        draw_title_medal_small(k, dl, tier, {c.x + d * 0.36f, c.y + d * 0.36f}, k.s(pt::kTileMedal));
        const FitText cut = fit(k.regular, k.s(tok::kCaption), etude::tier_title(tier), title_slot.w());
        label_at(k, dl, k.regular, k.s(tok::kCaption), title_slot, LabelAlign::Centre, col::kText2,
                 cut.c_str());
    }

    char days[16];
    std::snprintf(days, sizeof days, "%d", streak_of(info, i));
    const float tw = text_size(k.regular, k.s(tok::kCaption), days).x;
    const bool banked = streak_of(info, i) > 0;
    const float freeze_w = banked ? k.s(tok::kS2) + draw_freezes(k, dl, {0.0f, 0.0f}, k.s(tok::kCaption), 0, true) : 0.0f;
    const float gw = k.s(tok::kIconSm) + k.s(tok::kS1) + tw + freeze_w; // flame, days, snowflake and its count
    const float gx = tile.cx() - gw * 0.5f;
    const Box line{gx + k.s(tok::kIconSm) + k.s(tok::kS1), title_slot.y1 + k.s(tok::kS1),
                   gx + gw - freeze_w, title_slot.y1 + k.s(tok::kS1) + k.s(pt::kTileStreakH)};
    glyph(k, dl, icon::kFlame, {gx + k.s(tok::kIconSm) * 0.5f, line.cy()}, k.s(tok::kIconSm),
          streak_of(info, i) > 0 ? ImVec4{1.0f, 0.60f, 0.24f, 1.0f} : col::kText3);
    label_at(k, dl, k.regular, k.s(tok::kCaption), line, LabelAlign::Left, col::kText2, days);
    if (streak_of(info, i) > 0) { // the freezes banked, after the days
        draw_freezes(k, dl, {gx + gw - freeze_w + k.s(tok::kS2), line.cy()}, k.s(tok::kCaption), freezes_of(info, i));
    }
}

} // namespace

void ProfileUi::draw_tile(Kit& k, const ProfileInfo& info, const ProfilePanelPlan& plan,
                          std::size_t i, const Box& tile) {
    const bool is_add = plan.add && i + 1 == plan.tiles;
    const Profile* p = is_add ? nullptr : &info.set->all()[i];
    const bool active = p != nullptr && p->id == info.active;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char id[32];
    if (is_add) {
        std::snprintf(id, sizeof id, "##profile-add");
    } else {
        std::snprintf(id, sizeof id, "##profile-tile-%lld", static_cast<long long>(p->id));
    }
    const bool pressed = press_area(k, id, tile);
    const State st = item_state(k, active, true, !info.ready);
    draw_state(k, dl, id, tile, tile, st, Idle::Fill);
    audit::note(id, {tile.x0, tile.y0}, {tile.x1, tile.y1}, InkKind::Target);
    const ImVec4 ink = state_ink(st, col::kText);
    const float d = plan.avatar;
    const float pad = k.s(tok::kS3);
    if (plan.flat) {
        const ImVec2 c{tile.x0 + pad + d * 0.5f, tile.cy()};
        const Box text{tile.x0 + pad + d + k.s(tok::kS2), tile.y0, tile.x1 - pad, tile.y1};
        if (is_add) {
            dl->AddCircle(c, d * 0.5f, u32(with_alpha(col::kText2, 0.55f)), 0, k.s(tok::kFocusRing));
            glyph(k, dl, icon::kPlus, c, k.s(tok::kIcon), state_ink(st, col::kText2));
            label_at(k, dl, k.regular, k.s(tok::kBody), text, LabelAlign::Left, ink, "Add");
        } else {
            draw_flat_profile(k, dl, info, i, *p, c, d, text, ink);
        }
    } else {
        const ImVec2 c{tile.cx(), tile.y0 + pad + d * 0.5f};
        const Box name_slot{tile.x0 + k.s(tok::kS1), c.y + d * 0.5f + k.s(tok::kS2), tile.x1 - k.s(tok::kS1),
                            c.y + d * 0.5f + k.s(tok::kS2) + k.s(pt::kTileNameH)};
        if (is_add) {
            dl->AddCircle(c, d * 0.5f, u32(with_alpha(col::kText2, 0.55f)), 0, k.s(tok::kFocusRing));
            glyph(k, dl, icon::kPlus, c, k.s(tok::kIconLg), state_ink(st, col::kText2));
            label_at(k, dl, k.regular, k.s(tok::kBody), name_slot, LabelAlign::Centre, ink, "Add");
        } else {
            draw_big_profile(k, dl, info, i, *p, tile, c, d, name_slot, ink);
        }
    }
    if (editing_ && p != nullptr) {
        glyph(k, dl, icon::kRename, {tile.x1 - k.s(tok::kS3) - k.s(tok::kIconSm) * 0.5f, tile.y0 + k.s(tok::kS3) + k.s(tok::kIconSm) * 0.5f},
              k.s(tok::kIconSm), col::kText2);
    }
    if (!tapped(pressed) || !info.ready) {
        return;
    }
    if (is_add) {
        start_sheet(nullptr, kNewProfileGoalMinutes);
    } else if (editing_) {
        start_sheet(p, info.goal_minutes);
    } else {
        if (!active) {
            choice_ = {ProfileAction::Switch, p->id, {}, 0, -1};
        }
        panel_ = false;
    }
}

// ---- The sheet ---------------------------------------------------------------------------------

void ProfileUi::start_sheet(const Profile* edit, int goal_minutes) {
    sheet_requested_ = true;
    sheet_closing_ = false;
    confirming_ = false;
    adding_ = edit == nullptr;
    edit_id_ = edit != nullptr ? edit->id : 0;
    name_ = edit != nullptr ? edit->name : std::string{};
    colour_ = edit != nullptr ? edit->colour : 0;
    goal_ = goal_minutes;
    focused_ = true;
    keys_ = osk::by_touch() && edit == nullptr; // an edit starts with the keyboard down: a tap on the name raises it
    keyboard_.reset();
    osk::set_field_open(osk::Owner::ProfileName, true);
    panel_ = false;
    editing_ = false;
}

void ProfileUi::save_sheet(const ProfileInfo& info, bool goal_shown) {
    const bool is_active = !adding_ && edit_id_ == info.active;
    choice_.action = adding_ ? ProfileAction::Add : ProfileAction::Edit;
    choice_.id = edit_id_;
    choice_.name = name_;
    choice_.colour = colour_;
    choice_.goal_minutes = goal_shown && (adding_ || is_active) ? goal_ : -1;
}

// The modal that holds the sheet: opened on the frame after it was asked for, closed on Save, Cancel
// and Escape. False when it is not drawn this frame (it ends itself, and the caller does not).
bool ProfileUi::begin_sheet(const ProfileInfo& info, const Box& display) {
    if (sheet_requested_) {
        if (colour_ == 0 && adding_ && info.set != nullptr) {
            colour_ = info.set->unused_colour();
        }
        ImGui::OpenPopup("##profile-sheet");
        sheet_requested_ = false;
        sheet_open_ = true;
        goal_shown_ = adding_ || edit_id_ == info.active;
        goal_ = adding_ ? kNewProfileGoalMinutes : info.goal_minutes;
    }
    if (!sheet_open_) {
        return false;
    }
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize({display.x1, display.y1});
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::BeginPopupModal("##profile-sheet", nullptr, kFlags)) {
        sheet_open_ = false;
        osk::set_field_open(osk::Owner::ProfileName, false);
        return false;
    }
    if (sheet_closing_) {
        sheet_closing_ = false;
        end_sheet(false);
        ImGui::EndPopup();
        return false;
    }
    return true;
}

void ProfileUi::end_sheet(bool end_popup) {
    ImGui::CloseCurrentPopup();
    sheet_open_ = false;
    osk::set_field_open(osk::Owner::ProfileName, false);
    blur();
    if (end_popup) {
        ImGui::EndPopup();
    }
}

// The title row (Full only) and the avatar, which follows what is typed and picked.
void ProfileUi::draw_sheet_head(Kit& k, ImDrawList* dl, const ProfileSheetPlan& plan, bool* cancel) {
    if (plan.form == SheetForm::Full) {
        ImGui::SetCursorScreenPos({plan.back.x0, plan.back.y0});
        if (icon_action(k, "##profile-back", icon::kBack, "Back", plan.back.w(), true)) {
            *cancel = true;
        }
        label_at(k, dl, k.bold, k.s(tok::kTitle), plan.title, LabelAlign::Left, col::kText,
                 sheet_title(adding_));
    }
    Profile preview;
    preview.name = name_;
    preview.colour = colour_;
    avatar(k, dl, {plan.avatar.cx(), plan.avatar.cy()}, plan.avatar.w(), preview);
}

// The name field: a tap raises the keyboard under a finger; a name another profile has says so.
void ProfileUi::draw_name_field(Kit& k, ImDrawList* dl, const ProfileInfo& info,
                                const ProfileSheetPlan& plan) {
    const bool pressed = press_area(k, "##profile-name", plan.field);
    State st = item_state(k, false);
    st.focus = st.focus || focused_;
    draw_state(k, dl, "##profile-name", plan.field, plan.field, st, Idle::Fill);
    audit::note("##profile-name", {plan.field.x0, plan.field.y0}, {plan.field.x1, plan.field.y1},
                InkKind::Target);
    const bool taken = info.set != nullptr && !name_.empty() &&
                       info.set->check_name(name_, edit_id_) == ProfileResult::NameTaken;
    const float inner = k.s(tok::kS3);
    const float note_w =
        taken ? text_size(k.regular, k.s(tok::kCaption), "Name in use").x + inner : 0.0f;
    osk::field_text(k, dl, {plan.field.x0 + inner, plan.field.y0, plan.field.x1 - inner - note_w, plan.field.y1},
                    name_, "Name", focused_, pt::kNameText);
    if (taken) {
        label_at(k, dl, k.regular, k.s(tok::kCaption),
                 {plan.field.x1 - note_w - inner * 0.5f, plan.field.y0, plan.field.x1 - inner, plan.field.y1},
                 LabelAlign::Right, col::kWarning, "Name in use");
    }
    if (tapped(pressed)) {
        focused_ = true;
        keys_ = osk::by_touch();
        keyboard_.reset();
    }
}

// The colours, each a disc in a hit cell (the cells share the card's width); the chosen one has a ring
// and a check, in dark ink on a light colour and light on a dark one. White and black are discs too,
// the black one with a light edge.
void ProfileUi::draw_swatches(Kit& k, ImDrawList* dl, const ProfileSheetPlan& plan) {
    if (!plan.swatches) {
        return;
    }
    for (std::size_t i = 0; i < plan.swatch.size(); ++i) {
        const Box cell = plan.swatch[i];
        char id[24];
        std::snprintf(id, sizeof id, "##profile-colour-%zu", i);
        const bool pressed = press_area(k, id, cell);
        const State st = item_state(k, false);
        const std::uint32_t value = kProfileColours[i];
        const ImVec2 centre{cell.cx(), cell.cy()};
        const float r = k.s(pt::kSwatch) * 0.5f;
        const bool chosen = colour_ == value;
        if (chosen) {
            dl->AddCircle(centre, r + k.s(tok::kS1), u32(col::kAccent), 0, k.s(tok::kFocusRing));
        }
        dl->AddCircleFilled(centre, r, u32(rgb(value)));
        if (colour_needs_outline(value)) {
            dl->AddCircle(centre, r - k.s(tok::kFocusRing) * 0.5f, u32(rgb(profile_visible_colour(value))), 0,
                          k.s(tok::kFocusRing));
        }
        // The state is drawn inside the disc: a round face.
        draw_state(k, dl, id, {centre.x - r, centre.y - r, centre.x + r, centre.y + r}, cell, st,
                   Idle::None, r);
        audit::note(id, {centre.x - r, centre.y - r}, {centre.x + r, centre.y + r}, InkKind::Target);
        if (chosen) {
            glyph(k, dl, icon::kCheck, centre, k.s(tok::kIconSm),
                  avatar_ink_dark(value) ? col::kAccentInk : ImVec4{1, 1, 1, 1});
        }
        if (tapped(pressed)) {
            colour_ = value;
            blur();
        }
    }
}

// The daily goal: a stepper by five minutes; 0 is off.
void ProfileUi::draw_goal(Kit& k, ImDrawList* dl, const ProfileSheetPlan& plan) {
    label_at(k, dl, k.regular, k.s(tok::kBody), plan.goal_label, LabelAlign::Left, col::kText2,
             "Daily goal");
    ImGui::SetCursorScreenPos({plan.goal_minus.x0, plan.goal_minus.y0});
    if (icon_action(k, "##profile-goal-minus", icon::kMinus, "Less", plan.goal_minus.w(),
                    goal_ > pt::kGoalMin)) {
        goal_ = std::max(pt::kGoalMin, goal_ - pt::kGoalStepMin);
        blur();
    }
    char value[24];
    if (goal_ > 0) {
        std::snprintf(value, sizeof value, "%d min", goal_);
    } else {
        std::snprintf(value, sizeof value, "Off");
    }
    label_at(k, dl, k.bold, k.s(tok::kLabel), plan.goal_value, LabelAlign::Centre, col::kText, value);
    ImGui::SetCursorScreenPos({plan.goal_plus.x0, plan.goal_plus.y0});
    if (icon_action(k, "##profile-goal-plus", icon::kPlus, "More", plan.goal_plus.w(),
                    goal_ < pt::kGoalMax)) {
        goal_ = std::min(pt::kGoalMax, goal_ + pt::kGoalStepMin);
        blur();
    }
}

void ProfileUi::draw_sheet(Kit& k, const ProfileInfo& info, const Box& display, float top_y,
                           float anchor_x) {
    if (!begin_sheet(info, display)) {
        return;
    }
    osk::note_modal();
    const bool was_quiet = audit::quiet();
    audit::set_quiet(false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (confirming_) {
        draw_confirm(k, info, display, top_y, anchor_x);
        audit::set_quiet(was_quiet);
        ImGui::EndPopup();
        return;
    }
    // The keyboard first: the sheet is laid out clear of it.
    osk::want_text();
    const osk::FieldRules rules{osk::FieldKind::Text, kMaxProfileNameBytes, 0};
    // The computer keyboard types into the name only while the field has the focus (a tap on it
    // brings the focus back).
    osk::Typed typed;
    if (focused_) {
        typed = osk::feed_keyboard(name_, rules);
    }
    if (typed.any) {
        keys_ = false; // a computer keyboard is in use: the on-screen one steps aside
    }
    const float full = profile_keyboard_height(display.x1, display.y1, k.u);
    const float shown = full * keyboard_.step(k, keys_);
    const float keys_top = keys_ ? display.y1 - full : display.y1 - shown;
    const ProfileSheetPlan plan =
        plan_profile_sheet(anchor_x, top_y, display, k.u, keys_top, goal_shown_, !adding_);
    const Box c = plan.card;
    sheet_card(k, dl, {c.x0, c.y0}, {c.x1, c.y1}, k.s(tok::kRadiusSheet), col::kSurface);
    k.toast_avoid({c.x0, c.y0, c.w(), c.h()});
    bool cancel = false;
    draw_sheet_head(k, dl, plan, &cancel);
    draw_name_field(k, dl, info, plan);
    draw_swatches(k, dl, plan);
    if (plan.goal) {
        draw_goal(k, dl, plan);
    }
    if (plan.del.w() > 0.0f) {
        draw_delete(k, dl, info, plan);
    }
    const bool can_save = info.ready && info.set != nullptr &&
                          info.set->check_name(name_, edit_id_) == ProfileResult::Ok;
    cancel = action_button(k, "##profile-cancel", icon::kClose, "Cancel", {plan.cancel.x0, plan.cancel.y0},
                           {plan.cancel.w(), plan.cancel.h()}, true) || cancel;
    bool save = action_button(k, "##profile-save", icon::kCheck, "Save", {plan.save.x0, plan.save.y0},
                              {plan.save.w(), plan.save.h()}, can_save, true);
    if (shown > 0.0f) {
        const bool done = keyboard_.draw(k, {0.0f, display.y1 - shown, display.x1, display.y1}, full,
                                         name_, rules);
        save = save || (done && can_save);
    }
    save = save || (typed.done && can_save);
    if (save) {
        save_sheet(info, plan.goal);
    }
    if (save || cancel || typed.cancel) {
        end_sheet(false);
    }
    audit::set_quiet(was_quiet);
    ImGui::EndPopup();
}

// ---- What the app reads ------------------------------------------------------------------------

ProfileChoice ProfileUi::take_choice() {
    ProfileChoice out = std::move(choice_);
    choice_ = {};
    return out;
}

bool ProfileUi::close_top() {
    if (confirming_) {
        confirming_ = false; // back to the sheet
        return true;
    }
    if (sheet_open()) {
        sheet_closing_ = sheet_open_;
        blur();
        if (sheet_requested_) {
            sheet_requested_ = false;
            osk::set_field_open(osk::Owner::ProfileName, false);
        }
        return true;
    }
    if (panel_) {
        panel_ = false;
        editing_ = false;
        return true;
    }
    return false;
}

} // namespace etude::app::ui
