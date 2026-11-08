// SPDX-License-Identifier: GPL-3.0-or-later
// What the audit's measuring (icon_audit.cpp) and its report writers (icon_audit_write.cpp) share.
#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace etude::app {

// Sizes in points compare with this much slack: a float sum of design points times the unit.
inline constexpr float kPointSlack = 0.01f;
// Pressable surfaces that have no face by design: a drag area whose feedback is the content it
// moves (the overview's scrub bar and its playhead, the editor's roll and its cursor), not a
// control drawn with states; and a card's backdrop, the touch outside it that closes it (the part
// the card itself covers is the card's) or a settings card's own surface, which takes the taps that
// fall between its controls so nothing under it answers them. They are measured for size only: no pair check takes them.
// Matched at the end of the item's name.
inline constexpr const char* kSurfaces[] = {"##scrub", "##roll-area", "##scrim", "##library-dismiss",
                                     "##learn-dismiss",
                                     "-settings-card", "-keys-surface"};

inline bool is_surface(const std::string& name) {
    for (const char* s : kSurfaces) {
        const std::size_t n = std::strlen(s);
        if (name.size() >= n && name.compare(name.size() - n, n, s) == 0) {
            return true;
        }
    }
    return false;
}


} // namespace etude::app
