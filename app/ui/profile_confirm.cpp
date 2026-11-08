// SPDX-License-Identifier: GPL-3.0-or-later
// Deleting a profile from its edit sheet: the Delete button beside the colours, and the confirm that
// takes the sheet's place (profile_layout.hpp's plan_profile_confirm).
#include "profile_ui.hpp"

#include "crash_layout.hpp" // wrap_report
#include "icon_audit.hpp"
#include "icons.hpp"
#include "tap.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app::ui {

namespace pt = profile_tok;

// Delete, at the left end of the colours' row. A profile that cannot be deleted shows it dimmed and
// says why: a tap on it puts the reason in a toast, and the sheet's title row carries it too.
void ProfileUi::draw_delete(Kit& k, ImDrawList* dl, const ProfileInfo& info,
                            const ProfileSheetPlan& plan) {
    const ProfileResult can = info.set != nullptr ? info.set->check_delete(edit_id_)
                                                  : ProfileResult::NotReady;
    const bool allowed = can == ProfileResult::Ok && info.ready;
    const bool tapped_it = action_button(k, "##profile-delete", icon::kTrash, "Delete",
                                         {plan.del.x0, plan.del.y0}, {plan.del.w(), plan.del.h()},
                                         allowed, false, true);
    if (!allowed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        ImGui::IsMouseClicked(0) && *profile_delete_reason(can) != '\0') {
        k.say_free(profile_delete_reason(can));
    }
    if (plan.form == SheetForm::Full && *profile_delete_reason(can) != '\0') {
        const Box slot{plan.title.x0 + plan.title.w() * 0.5f, plan.title.y0, plan.title.x1, plan.title.y1};
        label_at(k, dl, k.regular, k.s(tok::kCaption), slot, LabelAlign::Right, col::kText3,
                 profile_delete_reason(can));
    }
    if (tapped_it) {
        start_confirm();
    }
}

namespace {

// `text` broken at spaces into at most `max_lines` lines no wider than `room`; what does not fit the
// last line is cut with an ellipsis.
std::vector<std::string> wrap_words(Kit& k, std::string_view text, float size, float room, int max_lines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find(' ', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string word(text.substr(at, end - at));
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && static_cast<int>(lines.size()) + 1 < max_lines &&
            text_size(k.regular, size, trial.c_str()).x > room) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        at = end + 1;
    }
    if (!line.empty()) {
        lines.push_back(fit(k.regular, size, line, room).c_str());
    }
    return lines;
}

// "Delete <name>?" in the heading's width: one line, or more for a long name (it wraps, it is never
// cut). Over the keyboard (`tight`) there is no room for a second line: a name that does not fit
// one line gives way to "Delete this profile?", and the field's hint still names it.
std::vector<std::string> heading_lines(const Kit& k, const Profile& who, float title_pt, float room,
                                       bool tight) {
    const auto wrap = [&](const std::string& text) {
        return wrap_report(text, room, [&](const std::string& line) {
            return text_size(k.bold, k.s(title_pt), line.c_str()).x;
        });
    };
    std::vector<std::string> lines = wrap("Delete " + who.name + "?");
    return tight && lines.size() > 1 ? wrap("Delete this profile?") : lines;
}

} // namespace

// What is lost, across the card at over body size, and (Roomy) the heading with the avatar.
void ProfileUi::draw_confirm_words(Kit& k, ImDrawList* dl, const Profile& who,
                                   const ProfileConfirmPlan& plan) {
    if (plan.form == ConfirmForm::Roomy) {
        profile_avatar(k, dl, {plan.avatar.cx(), plan.avatar.cy()}, plan.avatar.w(), who);
    }
    // The heading, at the plan's size, on one line or two.
    const std::vector<std::string> heading = heading_lines(k, who, plan.title_pt, plan.title.w(), plan.form == ConfirmForm::Tight);
    const float title_lh = k.s(plan.title_pt) * pt::kConfirmLineRatio;
    for (std::size_t i = 0; i < heading.size(); ++i) {
        const float y = plan.title.y0 + static_cast<float>(i) * title_lh;
        const Box slot = heading.size() == 1 ? plan.title : Box{plan.title.x0, y, plan.title.x1, y + title_lh};
        label_at(k, dl, k.bold, k.s(plan.title_pt), slot, LabelAlign::Left, col::kText, heading[i].c_str());
    }
    const float size = k.s(profile_confirm_text(plan.step));
    // What is lost over the lines but the last (the whole sentence when there is room for two, a
    // shorter one when there is one), then, on a line of its own, bold and red, the warning.
    const bool roomy = plan.form == ConfirmForm::Roomy;
    const std::string said = roomy ? who.name + "'s runs, scores, streaks and badges will be deleted from this device."
                                   : "Deletes " + who.name + "'s runs, scores, streaks and badges.";
    const int body_lines = plan.lines - 1; // none when the room is short: the heading says it
    const std::vector<std::string> lines =
        body_lines > 0 ? wrap_words(k, said, size, plan.line[0].w(), body_lines) : std::vector<std::string>{};
    // One line only: centred between the slots, so no blank line hangs under it.
    const float drop = static_cast<float>(body_lines - static_cast<int>(lines.size())) * plan.line[0].h() * 0.5f;
    for (std::size_t i = 0; i < lines.size() && static_cast<int>(i) < body_lines; ++i) {
        const Box& slot = plan.line[i];
        label_at(k, dl, k.regular, size, {slot.x0, slot.y0 + drop, slot.x1, slot.y1 + drop},
                 LabelAlign::Left, col::kText2, lines[i].c_str());
    }
    // The warning is on the trailing side, at the card's right edge.
    label_at(k, dl, k.bold, size, plan.line[static_cast<std::size_t>(body_lines)], LabelAlign::Right,
             col::kWarning, "This can't be undone.");
}

// The field the name is typed in: its hint says what to type; a tap on it raises the keyboard under a
// finger, as the sheet's name field does.
void ProfileUi::draw_confirm_field(Kit& k, ImDrawList* dl, const Profile& who,
                                   const ProfileConfirmPlan& plan) {
    const bool pressed = press_area(k, "##profile-delete-name", plan.field);
    State st = item_state(k, false);
    st.focus = st.focus || focused_;
    draw_state(k, dl, "##profile-delete-name", plan.field, plan.field, st, Idle::Fill);
    audit::note("##profile-delete-name", {plan.field.x0, plan.field.y0}, {plan.field.x1, plan.field.y1},
                InkKind::Target);
    const float inner = k.s(tok::kS3);
    const std::string hint = "Type " + who.name + " to confirm";
    osk::field_text(k, dl, {plan.field.x0 + inner, plan.field.y0, plan.field.x1 - inner, plan.field.y1},
                    confirm_text_, hint.c_str(), focused_, pt::kNameText);
    if (tapped(pressed)) {
        focused_ = true;
        keys_ = osk::by_touch();
        keyboard_.reset();
    }
}

// "Delete <name>?", what is lost, the field to type the name in, Cancel at the left and Delete at the
// right. Delete is off until the field holds the name, exactly. The keyboard is the sheet's.
void ProfileUi::draw_confirm(Kit& k, const ProfileInfo& info, const Box& display, float top_y,
                             float anchor_x) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Profile* who = info.set != nullptr ? info.set->find(edit_id_) : nullptr;
    if (who == nullptr || info.set->check_delete(edit_id_) != ProfileResult::Ok) {
        confirming_ = false; // it was deleted or became the only one under us
        return;
    }
    osk::want_text();
    const osk::FieldRules rules{osk::FieldKind::Text, kMaxProfileNameBytes, 0};
    osk::Typed typed;
    if (focused_) {
        typed = osk::feed_keyboard(confirm_text_, rules);
    }
    if (typed.any) {
        keys_ = false; // a computer keyboard is in use: the on-screen one steps aside
    }
    const float full = profile_keyboard_height(display.x1, display.y1, k.u);
    const float shown = full * keyboard_.step(k, keys_);
    const float keys_top = keys_ ? display.y1 - full : display.y1 - shown;
    // The tight card's warning stands at the right of the heading's row: it is as wide as its words.
    const float warning_w =
        text_size(k.bold, k.s(profile_confirm_text(tok::wide_step(display.w()))), "This can't be undone.").x + 1.0f;
    ProfileConfirmPlan plan = plan_profile_confirm(anchor_x, top_y, display, k.u, keys_top, 1, warning_w);
    // A long name's heading takes more lines: the card is planned again with them.
    const auto lines = static_cast<int>(
        heading_lines(k, *who, plan.title_pt, plan.title.w(), plan.form == ConfirmForm::Tight).size());
    if (lines > 1 && plan.form == ConfirmForm::Roomy) {
        plan = plan_profile_confirm(anchor_x, top_y, display, k.u, keys_top, lines, warning_w);
    }
    covers_top_ = plan.card.y0 < top_y - 0.5f;
    const Box c = plan.card;
    sheet_card(k, dl, {c.x0, c.y0}, {c.x1, c.y1}, k.s(tok::kRadiusSheet), col::kSurface);
    k.toast_avoid({c.x0, c.y0, c.w(), c.h()});
    draw_confirm_words(k, dl, *who, plan);
    draw_confirm_field(k, dl, *who, plan);
    last_match_ = confirms_profile_name(confirm_text_, who->name);
    const float face_h = plan.step > 0 ? tok::kFace * tok::popup_grow(plan.step) : 0.0f;
    const float label_pt =
        plan.step > 0 ? tok::popup_type(tok::kLabel, plan.step, tok::PopupText::Button) : 0.0f;
    const bool cancel = action_button(k, "##profile-delete-cancel", icon::kClose, "Cancel",
                                      {plan.cancel.x0, plan.cancel.y0},
                                      {plan.cancel.w(), plan.cancel.h()}, true, false, false, false,
                                      nullptr, face_h, label_pt);
    bool remove = action_button(k, "##profile-delete-confirm", icon::kTrash, "Delete",
                                {plan.remove.x0, plan.remove.y0}, {plan.remove.w(), plan.remove.h()},
                                info.ready && last_match_, false, true, false, nullptr, face_h,
                                label_pt);
    if (shown > 0.0f) {
        const bool done = keyboard_.draw(k, {0.0f, display.y1 - shown, display.x1, display.y1}, full,
                                         confirm_text_, rules);
        remove = remove || (done && info.ready && confirms_profile_name(confirm_text_, who->name));
    }
    remove = remove || (typed.done && info.ready && confirms_profile_name(confirm_text_, who->name));
    if (cancel || typed.cancel) {
        confirming_ = false;
        focused_ = true; // the sheet's name field takes the typing again
        keys_ = false;
    } else if (remove) {
        choice_ = {ProfileAction::Delete, edit_id_, {}, 0, -1};
        confirming_ = false;
        end_sheet(false);
    }
}

void ProfileUi::start_confirm() {
    confirming_ = true;
    confirm_text_.clear();
    last_match_ = false;
    focused_ = true;
    keys_ = osk::by_touch();
    keyboard_.reset();
}

// The developer's way to a shot: the sheet (or its confirm) for the household's last or first
// profile, once the profiles are in.
void ProfileUi::run_demo(const ProfileInfo& info) {
    const Demo demo = std::exchange(demo_, Demo::None);
    const std::span<const Profile> all = info.set->all();
    if (all.empty()) {
        return;
    }
    const Profile& who = demo == Demo::EditFirst ? all.front() : all.back();
    start_sheet(&who, info.goal_minutes);
    keys_ = false;
    if (demo == Demo::DeleteLast || demo == Demo::DeleteKeys || demo == Demo::DeleteTyped) {
        start_confirm();
        keys_ = demo == Demo::DeleteKeys;
        confirm_text_ = demo == Demo::DeleteKeys ? "Dan" : demo == Demo::DeleteTyped ? who.name : "";
    }
}

} // namespace etude::app::ui
