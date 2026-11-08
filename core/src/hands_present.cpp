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

HandToggles hand_toggles_for(HandFilter chosen, const HandsPresent& present) {
    const HandFilter shown = clamp_hands(chosen, present);
    HandToggles t;
    t.left_on = shown != HandFilter::Right;
    t.right_on = shown != HandFilter::Left;
    // A hand with no notes is unavailable. The hand that has them can be tapped, but with it the
    // only hand there is, the tap is refused.
    t.left_enabled = present.left;
    t.right_enabled = present.right;
    t.both_offered = present.both();
    t.other_hand_sound_offered = present.both();
    return t;
}

HandFilter toggle_hand(HandFilter chosen, const HandsPresent& present, bool left_toggle) {
    if (!present.both()) {
        return clamp_hands(chosen, present);
    }
    const bool left = chosen != HandFilter::Right;
    const bool right = chosen != HandFilter::Left;
    const bool next_left = left_toggle ? !left : left;
    const bool next_right = left_toggle ? right : !right;
    if (!next_left && !next_right) {
        return chosen;
    }
    return next_left && next_right ? HandFilter::Both
                                   : (next_left ? HandFilter::Left : HandFilter::Right);
}

} // namespace etude
