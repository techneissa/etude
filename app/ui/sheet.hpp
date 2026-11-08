// SPDX-License-Identifier: GPL-3.0-or-later
// The settings: a centred sheet over a dimmed screen, the pages down the left (icons with names,
// or icons with captions when narrow) and the page on the right, in two columns when wide. The
// sheet has no close button of its own: the gear (an X while the sheet is open) and a tap on the
// dimmed screen close it.
#pragma once

#include "bar.hpp"
#include "kit.hpp"
#include "pages.hpp"
#include "sheet_layout.hpp"

#include <array>

namespace etude::app::ui {

class SettingsSheet {
  public:
    // Shows `p`; a sub-page opens under its rail page.
    void open_at(Page p);
    // Shows the rail page last chosen (Notes at first).
    void reopen() { open_at(last_); }
    Page page() const { return nav_.page; }
    // Draws the sheet `open` (0..1, eased) of the way in. Returns true when it asks to close: the
    // close button or a tap on the dimmed screen.
    bool draw(Kit& k, UiModel& model, const Frame& f, float open);
    // The sheet and its scrim take no pointer input from the next frame on. Called each frame
    // from the one the close starts in until the sheet has slid away, so a tap on what it covers
    // works at once.
    void stop_input();
    // Measures how tall the rail pages are at this window size and scale, so the sheet opens as
    // tall as the tallest one needs (fit_sheet_height): one page a frame, drawn where nothing shows
    // it and nothing touches it, until every page is measured; nothing after that until the size
    // or the scale changes. Called every frame, `shown` or not; the page a shown sheet draws is
    // measured as it draws.
    void measure(Kit& k, UiModel& model, const Frame& f, bool shown);

  private:
    // Page changes fade and lift the new page; a sub-page pushes in from the right.
    struct Nav {
        Page page = Page::Notes;
        float t = 1.0f;
        float dir = 0.0f; // +1 push, -1 pop, 0 a switch between rail pages

        void go(Page p, float d) {
            if (p == page) {
                return;
            }
            page = p;
            t = 0.0f;
            dir = d;
        }
        void update(float dt) { t = approach(t, 1.0f, dt, tok::kMove); }
        float offset(float distance) const { return (1.0f - ease_out(t)) * distance * dir; }
        float alpha() const { return 0.25f + 0.75f * ease_out(t); }
    };

    Nav nav_;
    void end_stale_arm(Kit& k);
    void note_open_height(const PageArea& cap_area, float u);
    Page last_ = Page::Notes;
    int arm_page_ = -1;    // the page a two-tap row was last drawn on, and the frame (confirm_arm.hpp)
    int arm_frame_ = -10;
    bool reveal_rail_row_ = true; // scroll the rail to the chosen page on the next draw
    float rail_to_ = 0.0f;        // where the scrolling rail is heading: a row edge
    bool rail_easing_ = false;    // it is on its way there
    bool raise_ = true;           // bring the scrim and the sheet to the front on the next draw
    // Each rail page's body content height (sheet_layout.hpp).
    PageHeights<kRail.size()> heights_;
};

} // namespace etude::app::ui
