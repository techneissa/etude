// SPDX-License-Identifier: GPL-3.0-or-later
#include "icon_service.hpp"

#include "desktop_entry.hpp"
#include "icon_assets.hpp"
#include "window_icon.hpp"

#include <string>

namespace etude::app {

bool IconService::sync(SDL_Window* window, etude::IconColour colour) {
    if (quiet_ || window == nullptr || applied_ == colour) {
        return false;
    }
    applied_ = colour;
    set_window_icon(window, colour); // the Dock on a Mac, the window's icon elsewhere
#if defined(__linux__)
    // A failure is told once and the app goes on: the window icon above is already set.
    const DesktopResult result =
        apply_launcher_colour(desktop_folders_from_env(), colour, icon_png(colour, 256));
    if (result == DesktopResult::Failed) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "icon: could not switch the launcher entry to %s",
                    std::string(etude::icon_colour_info(colour).key).c_str());
    }
#endif
    return true;
}

} // namespace etude::app
