// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/icon_colour.hpp"

#include <string>

namespace etude {

bool icon_colour_unlocked(IconColour colour, int best_title) {
    return colour == kDefaultIconColour || best_title >= icon_colour_info(colour).title;
}

bool icon_colour_available(IconColour colour, IconColour chosen, int best_title) {
    return colour == chosen || icon_colour_unlocked(colour, best_title);
}

std::string icon_name(IconColour colour) {
    return "etude-" + std::string(icon_colour_info(colour).key);
}

} // namespace etude
