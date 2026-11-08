// SPDX-License-Identifier: GPL-3.0-or-later
// The profile switcher: a chip with the current profile's avatar and name in the top row, the
// "Who's playing" panel under it (a tile for each profile with its streak, an Add tile, Edit), and
// the sheet that adds or edits a profile: name, one of eight colours, the daily goal, Cancel and
// Save, with the app's on-screen keyboard under it across the whole window. Where each part goes
// is profile_layout.hpp; what the app is told is profile_port.hpp.
#pragma once

#include "kit.hpp"
#include "osk.hpp"
#include "profile_layout.hpp"
#include "profile_port.hpp"

#include <cstdint>
#include <string>

namespace etude::app::ui {

// A profile's avatar: its colour as a disc with its letter, `d` across, centred on `centre`. The
// one the chip, the panel, the sheet, the Progress page and the summary page's board share; and a
// profile's colour as a UI colour.
void profile_avatar(const Kit& k, ImDrawList* dl, ImVec2 centre, float d, const Profile& p);
ImVec4 profile_rgb(std::uint32_t colour);

class ProfileUi {
  public:
    // The chip's hit width for the profile in use (the top row reserves it before the status text).
    float chip_hit_width(const Kit& k, const ProfileInfo& info) const;
    // The chip in `hit` (the row's own height at least a hit): a tap opens or closes the panel.
    void draw_chip(Kit& k, const ProfileInfo& info, const Box& hit);
    // The panel and the sheet, over everything else: `top_y` is the top row's bottom, `anchor_x` the
    // chip's left edge. Draws nothing while both are closed.
    void draw(Kit& k, const ProfileInfo& info, const Box& display, float top_y, float anchor_x);

    // The top row's menu entry: opens the "Who's playing" panel, as a tap on the chip does.
    void open_switcher() {
        panel_ = true;
        editing_ = false;
        outside_ = OutsideTap{};
    }

    // The Progress page's pencil: the sheet for profile `id` (the goal is the profile in use's).
    void open_edit(const ProfileInfo& info, std::int64_t id) {
        if (const Profile* p = info.set != nullptr ? info.set->find(id) : nullptr) {
            start_sheet(p, info.goal_minutes);
        }
    }
    // The Progress page's Household view: another profile in use.
    void choose(const ProfileChoice& choice) { choice_ = choice; }

    // What was chosen since the last call (action None for nothing), and cleared.
    ProfileChoice take_choice();
    // Escape or the gear: closes the sheet, else the panel. False when none was open.
    bool close_top();
    bool panel_open() const { return panel_ || panel_p_ > 0.0f; }
    // The delete's confirm is up and its card reaches over the top row (a small window with the
    // keyboard up): the host leaves the top row out, so nothing shows through or can be tapped.
    bool covers_top_row() const { return confirming_ && covers_top_; }
    bool sheet_open() const { return sheet_open_ || sheet_requested_; }
    // Developer (--panel profiles ...): opens the panel (with Edit on), or the sheet for a new
    // profile (with the keyboard up and "Dani" typed).
    void open_panel(bool editing) {
        panel_ = true;
        editing_ = editing;
    }
    // Developer (--panel profile-edit-sheet|profile-delete|profile-edit-first): the edit sheet of the
    // household's last profile (Dani) or of the first (Anna, who cannot be deleted), or the
    // confirm of the last. Resolved on the first frame that has the profiles.
    // The confirm comes with an empty field, with the keyboard up and "Dan" typed, or with the whole
    // name typed.
    enum class Demo : std::uint8_t { None, EditLast, DeleteLast, DeleteKeys, DeleteTyped, EditFirst };
    void open_demo_sheet(Demo demo) { demo_ = demo; }
    void open_new_profile_sheet(bool keyboard) {
        start_sheet(nullptr, 15);
        keys_ = keyboard;
        if (keyboard) {
            name_ = "Eli";
        }
    }

  private:
    void draw_panel(Kit& k, const ProfileInfo& info, const Box& display, float top_y, float anchor_x);
    void draw_tile(Kit& k, const ProfileInfo& info, const ProfilePanelPlan& plan, std::size_t i,
                   const Box& tile);
    void draw_progress_row(Kit& k, const ProfileInfo& info, const Box& row);
    void draw_sheet(Kit& k, const ProfileInfo& info, const Box& display, float top_y, float anchor_x);
    bool begin_sheet(const ProfileInfo& info, const Box& display);
    void end_sheet(bool end_popup);
    void draw_sheet_head(Kit& k, ImDrawList* dl, const ProfileSheetPlan& plan, bool* cancel);
    void draw_name_field(Kit& k, ImDrawList* dl, const ProfileInfo& info, const ProfileSheetPlan& plan);
    void draw_swatches(Kit& k, ImDrawList* dl, const ProfileSheetPlan& plan);
    void draw_goal(Kit& k, ImDrawList* dl, const ProfileSheetPlan& plan);
    // The name field lets go of the focus (another control was tapped, or the sheet closed) and
    // the on-screen keyboard goes with it.
    void blur() {
        focused_ = false;
        keys_ = false;
    }
    void start_sheet(const Profile* edit, int goal_minutes);
    void draw_delete(Kit& k, ImDrawList* dl, const ProfileInfo& info, const ProfileSheetPlan& plan);
    void draw_confirm(Kit& k, const ProfileInfo& info, const Box& display, float top_y, float anchor_x);
    void draw_confirm_words(Kit& k, ImDrawList* dl, const Profile& who, const ProfileConfirmPlan& plan);
    void draw_confirm_field(Kit& k, ImDrawList* dl, const Profile& who, const ProfileConfirmPlan& plan);
    void start_confirm();
    void run_demo(const ProfileInfo& info);
    void save_sheet(const ProfileInfo& info, bool goal_shown);

    bool panel_ = false;
    float panel_p_ = 0.0f;
    bool editing_ = false;
    OutsideTap outside_;
    Box chip_box_;
    ProfileChoice choice_;
    // The sheet.
    bool sheet_requested_ = false;
    bool sheet_open_ = false;
    bool sheet_closing_ = false;
    bool adding_ = true;
    std::int64_t edit_id_ = 0;
    std::string name_;
    std::uint32_t colour_ = 0;
    int goal_ = 15;
    bool goal_shown_ = true;
    bool focused_ = true;
    bool keys_ = false;
    bool confirming_ = false; // the delete's confirm is in the sheet's place
    bool covers_top_ = false; // and its card reaches over the top row
    std::string confirm_text_; // what was typed in it
    bool last_match_ = false;  // ... and it was the name when last drawn
    Demo demo_ = Demo::None;
    osk::Keyboard keyboard_;
};

} // namespace etude::app::ui
