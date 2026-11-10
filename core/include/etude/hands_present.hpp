// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/note_window.hpp"
#include "etude/song.hpp"

namespace etude {

// Which hands have playable notes, for the whole song or for a stretch of it. A song with notes
// for one hand only (a melody, a left-hand exercise) offers only that hand: the other hand's
// toggle is unavailable, the present hand cannot be turned off, "both" is not a choice, and the
// "song plays the other hand" sound is hidden. Wait Mode and scoring read the present hand only.
//
// A playable note is a non-drum, non-accompaniment note. Notes with no hand (a song whose hands
// were not split) are played in "both", so they count for both hands; a song with no playable
// note at all offers both hands too, so nothing is locked for no reason.
struct HandsPresent {
    bool left = true;
    bool right = true;

    bool both() const { return left && right; }
    friend bool operator==(const HandsPresent&, const HandsPresent&) = default;
};

// The hands with playable notes starting in [from_s, to_s). The defaults cover the whole song.
HandsPresent hands_present(const Song& song, double from_s = 0.0, double to_s = 1e18);

// `chosen` limited to the hands present: when only one hand has notes, that hand.
HandFilter clamp_hands(HandFilter chosen, const HandsPresent& present);

// What the hand toggles and the song sound button show and allow, for the hands chosen and the
// hands present.
struct HandToggles {
    bool left_on = true; // the left hand is practised
    bool right_on = true;
    bool left_enabled = true; // the hand has notes; false: grey, a tap only shows a hint
    bool right_enabled = true;
    bool both_offered = true;
    bool other_hand_sound_offered = true;
    friend bool operator==(const HandToggles&, const HandToggles&) = default;
};

} // namespace etude
