// SPDX-License-Identifier: GPL-3.0-or-later
// The top row's menu popover. A row narrower than `kMenuBelowRowPt` design points has one menu
// button where a wide row has the library's button, Learn's and the profile chip; a tap on it
// opens this popover under the row, with one entry for each of those: Library, Learn and Profile
// (the profile in use's avatar, opening the profile switcher). The host draws the button and acts
// on the entry this returns; the popover closes on a pick and on a tap outside it (layout and the
// rules in top_menu_layout.hpp).
#pragma once

#include "bar.hpp"
#include "profile_port.hpp"
#include "top_menu_layout.hpp"

#include <optional>

namespace etude::app::ui {

// Which entries the popover holds: the library's and Learn's screens when the app has them, and the
// profile in use when the household is known.
struct TopMenuEntries {
    bool library = false;
    bool learn = false;
    const Profile* profile = nullptr;

    TopMenuPlan plan() const { return top_menu_plan(library, learn, profile != nullptr); }
    int count() const { return plan().count; }
};

class TopMenu {
  public:
    // The popover under the row, from the button's left edge, down to the keyboard: the entry
    // picked this frame, if any (the popover is shut by then). Draws nothing while shut.
    // `button` is the menu button's hit area, whose own taps are not "outside".
    std::optional<TopMenuEntry> popover(Kit& k, const Frame& f, const TopMenuEntries& entries,
                                        const Box& button);

    bool open() const { return open_; }
    void open_now() {
        open_ = true;
        outside_ = OutsideTap{};
    }
    void close() { open_ = false; }
    void toggle() {
        if (open_) {
            close();
        } else {
            open_now();
        }
    }

  private:
    bool open_ = false;
    float open_p_ = 0.0f;
    OutsideTap outside_;
};

} // namespace etude::app::ui
