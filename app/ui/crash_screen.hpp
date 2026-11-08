// SPDX-License-Identifier: GPL-3.0-or-later
// The crash screen (`etude --crash-screen`): "Etude closed unexpectedly" and the buttons that
// start the app again. It is a whole screen of its own, drawn over a plain background, with the crash
// report's sheet (crash_card.hpp) over it when Send report is tapped. Laid out by
// crash_screen_layout.hpp; the words are the app's (crash_screen.hpp).
#pragma once

#include "crash_card.hpp"
#include "crash_screen_view.hpp"
#include "kit.hpp"

#include <optional>
#include <string>
#include <vector>

namespace etude::app::ui {

struct CrashScreenTap {
    std::optional<CrashButton> button; // tapped this frame
    CrashChoice report = CrashChoice::None; // the sheet's own answer (OpenUrl on a Mac)
};

class CrashScreenUi {
  public:
    // Draws the screen for `view` in a window `display` points across; `report` is what the sheet
    // reads (null: none, and Send report shows nothing).
    CrashScreenTap draw(Kit& k, const CrashScreenView& view, const CrashView* report, ImVec2 display);
    // Send report was tapped: the sheet opens.
    void open_report() { card_.open_report(); }
    bool report_open() const { return card_.report_open(); }

  private:
    CrashCard card_;
    // The paragraph cut into lines for the column's width and the type size.
    std::vector<std::string> lines_;
    float lines_w_ = -1.0f;
    float lines_size_ = -1.0f;
    std::string lines_from_;
};

} // namespace etude::app::ui
