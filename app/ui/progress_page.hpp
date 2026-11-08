// SPDX-License-Identifier: GPL-3.0-or-later
// The Progress page of the song library (#348): how the profile in use is doing, and how the
// household is. It takes the library screen's place while it is open, from the "Progress" chip in
// the places row or from "Progress and household" in the profile switcher. Three views of one page:
// Progress (the practice calendar, the accuracy and speed graph and the best run of each song, then
// the profile, the time practised against the daily goal, the songs and the household rank, the
// medals and "Practise today"), Household (a card for each profile side by side; a tap on one
// makes it the profile in use) and Medals (the wall of every achievement, medal_wall.hpp). The numbers
// are core's ProgressView, built on the history thread and handed over by the library port; where
// the parts go is progress_layout.hpp.
#pragma once

#include "kit.hpp"
#include "library_port.hpp"
#include "medal_wall.hpp"
#include "progress_ask.hpp"
#include "progress_layout.hpp"

#include "etude/session_state.hpp"

#include <cstdint>

namespace etude::app::ui {

class ProgressPage {
  public:
    // Shows the page (the numbers are asked for on the next draw), on Household or on Progress.
    void open(bool household);
    // The page on the Medals view, with the ladder of an achievement open (-1: none).
    void open_medals(int ladder_of);
    void close() { open_ = false; }
    bool is_open() const { return open_; }
    // Draws the page filling `area`; true when Back was tapped. The daily goal of the profile in use
    // and the wall's view are in `app`.
    bool draw(Kit& k, LibraryPort& port, const Box& area, etude::AppSettings& app);

    // What the page asks of the app, once: the profile's edit sheet, or another profile in use.
    struct Ask {
        enum class Kind : std::uint8_t { None, Edit, Switch };
        Kind kind = Kind::None;
        std::int64_t id = 0;
    };
    Ask take_ask();

  private:
    void draw_medals_card(Kit& k, ImDrawList* dl, const Box& box, const etude::ProfileProgress& me);
    void draw_header(Kit& k, ImDrawList* dl, const ProgressLayout& lay, const char* title,
                     etude::AppSettings& app, bool* back);
    void draw_me(Kit& k, ImDrawList* dl, LibraryPort& port, const ProgressLayout& lay,
                 const etude::ProgressView& view);
    void draw_practise(Kit& k, ImDrawList* dl, LibraryPort& port, const ProgressLayout& lay,
                       const etude::ProgressView& view);
    void draw_house_row(Kit& k, ImDrawList* dl, const Box& row, const Profile& who,
                        const etude::ProfileProgress& p, bool mine, int most_minutes,
                        const ProgressLayout& lay);
    void draw_household(Kit& k, ImDrawList* dl, LibraryPort& port, const ProgressLayout& lay,
                        const etude::ProgressView& view);
    void draw_profile_card(Kit& k, ImDrawList* dl, const ProgressLayout& lay, const Profile& who,
                           const etude::ProgressView& view);

    bool open_ = false;
    ProgressTab tab_ = ProgressTab::Me;
    MedalWall wall_;
    ProgressAsked asked_; // what the last request was for
    Ask ask_;
};

} // namespace etude::app::ui
