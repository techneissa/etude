// SPDX-License-Identifier: GPL-3.0-or-later
// What the About page's icon-colour row needs from the app: the best title among the device's
// profiles, which decides the colours it offers, and the medals that profile holds (shown beside the
// colours). The page only asks; the colour chosen is the `icon_colour` setting, which the window
// applies to the Dock icon or the launcher entry.
#pragma once

namespace etude::app::ui {

class IconPort {
  public:
    virtual ~IconPort() = default;

    // The best overall title (1 Beginner .. 8 Virtuoso) any profile on this device holds, or 0 while
    // it is not known yet. Asking for it, in a frame, is what makes the app work it out.
    virtual int best_title() = 0;
    // The most medals any profile on this device holds (the title above follows from it); only
    // meaningful once `best_title` is not 0.
    virtual int best_points() = 0;
    // The title medal was tapped: the window closes Settings and opens the Medals view.
    virtual void open_medals() = 0;
};

} // namespace etude::app::ui
