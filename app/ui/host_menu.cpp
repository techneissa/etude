// SPDX-License-Identifier: GPL-3.0-or-later
// A narrow top row's menu in the Ui (app/ui.hpp): its one button in the library's place and the
// popover that holds Library, Learn and Profile (top_menu.hpp).
#include "host_impl.hpp"

#include "icons.hpp"

namespace etude::app {

// Where the menu button goes, and what its popover holds: set from the layout, which decides which
// rows are narrow (`ScreenLayout::menu`). A button with nothing behind it is not drawn.
void Ui::Impl::place_menu_button(DrawFrame& d) {
    const ScreenLayout& lay = d.lay;
    const ProfileInfo* who = d.info.profiles;
    d.menu_entries.library = library_port != nullptr;
    d.menu_entries.learn = learn_port != nullptr;
    d.menu_entries.profile =
        who != nullptr && who->set != nullptr ? who->set->find(who->active) : nullptr;
    d.menu_button = !lay.menu.empty() && d.menu_entries.count() > 0;
    if (d.menu_button) {
        // The hit area is the row's share of 12.2 mm, as the wide row's buttons', so a dead gap is
        // left to what is under the row.
        const ImVec2 min = to_points(lay.menu.x, lay.menu.y, d.scale);
        const ImVec2 max = to_points(lay.menu.right(), lay.menu.bottom(), d.scale);
        const float w = (max.x - min.x) * kTopRowHitShare;
        const float h = (max.y - min.y) * kTopRowHitShare;
        const ImVec2 c{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
        d.menu_box = {c.x - w * 0.5f, c.y - h * 0.5f, c.x + w * 0.5f, c.y + h * 0.5f};
    }
}

// A narrow row's one menu button, in the library's place: a tap opens the popover with Library,
// Learn and Profile. While one of those screens is up the button is its close cross, as the wide
// row's buttons are theirs.
void Ui::Impl::draw_menu_button(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    if (!d.menu_button) {
        im.top_menu.close();
        return;
    }
    const ScreenLayout& lay = d.lay;
    const ImVec2 min = to_points(lay.menu.x, lay.menu.y, d.scale);
    const ImVec2 max = to_points(lay.menu.right(), lay.menu.bottom(), d.scale);
    const float hit = max.y - min.y;
    begin_layer("##menu-button-layer", min, {max.x - min.x, hit});
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    const bool screen_up = d.library_up;
    const bool up = screen_up || im.top_menu.open();
    const ImVec2 c{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    if (icon_toggle(k, "##menu-button", up ? icon::kClose : icon::kGrid, "Menu", c,
                    {d.menu_box.w(), d.menu_box.h()}, k.s(tok::kFace), false, true)) {
        if (screen_up) {
            im.library.close();
            im.learn.close();
        } else {
            im.top_menu.toggle();
            if (im.top_menu.open()) {
                // One popover from the top row at a time.
                im.open = false;
                im.bar->close_popovers();
                im.band.close_popovers();
                im.scale_pill.close();
                if (!im.profile.sheet_open()) {
                    im.profile.close_top();
                }
            }
        }
    }
    ImGui::End();
    // The popover closes under anything that covers the screen, and on a pick opens that place.
    if (im.open || im.editor->is_open()) {
        im.top_menu.close();
    }
    if (const auto pick = im.top_menu.popover(k, d.f, d.menu_entries, d.menu_box)) {
        switch (*pick) {
        case TopMenuEntry::Library:
            im.show_library(k);
            break;
        case TopMenuEntry::Learn:
            im.show_learn(k);
            break;
        case TopMenuEntry::Profile:
            im.profile.open_switcher();
            break;
        }
    }
}

} // namespace etude::app
