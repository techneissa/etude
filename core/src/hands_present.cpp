// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hands_present.hpp"

namespace etude {

HandsPresent hands_present(const Song& song, double from_s, double to_s) {
    bool left = false;
    bool right = false;
    bool any = false;
    for (const Note& n : song.notes) {
        if (n.drum || n.accompaniment || n.start_s < from_s || n.start_s >= to_s) {
            continue;
        }
        any = true;
        left = left || n.hand == Hand::Left || n.hand == Hand::Unknown;
        right = right || n.hand == Hand::Right || n.hand == Hand::Unknown;
    }
    if (!any) {
        return {};
    }
    return {left, right};
}

HandFilter clamp_hands(HandFilter chosen, const HandsPresent& present) {
    if (present.left && !present.right) {
        return HandFilter::Left;
    }
    if (present.right && !present.left) {
        return HandFilter::Right;
    }
    return chosen;
}

} // namespace etude
