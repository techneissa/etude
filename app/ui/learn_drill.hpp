// SPDX-License-Identifier: GPL-3.0-or-later
// The drill a Learn card's settings ask for (#373): what Listen plays and Practice opens in the
// player. Plain data from the profile's settings, so a test holds the mapping.
#pragma once

#include "etude/drill.hpp"
#include "etude/session_state.hpp"

namespace etude::app::ui {

inline HandsChoice learn_hands_choice(LearnHands hands) {
    switch (hands) {
    case LearnHands::Right:
        return HandsChoice::Right;
    case LearnHands::Left:
        return HandsChoice::Left;
    case LearnHands::Both:
        break;
    }
    return HandsChoice::Together;
}

inline Direction learn_direction_of(LearnDirection direction) {
    switch (direction) {
    case LearnDirection::Down:
        return Direction::Down;
    case LearnDirection::UpDown:
        return Direction::UpDown;
    case LearnDirection::Up:
        break;
    }
    return Direction::Up;
}

// The scale card plays the scale, or its arpeggio where the scale has a triad (`scale_triad`); the
// chord card plays the chord's arpeggio in the inversion shown.
inline bool learn_scale_arpeggio(const AppSettings& app) {
    return app.learn_arpeggio && scale_triad(app.learn_scale).has_value();
}

inline DrillSpec learn_drill_spec(const AppSettings& app, bool chord_card) {
    DrillSpec s;
    s.octaves = app.learn_octaves;
    s.direction = learn_direction_of(app.learn_direction);
    s.hands = learn_hands_choice(app.learn_hands);
    s.bpm = drill_tempo(app.learn_tempo);
    if (chord_card) {
        s.kind = DrillKind::Arpeggio;
        s.root_pc = app.learn_chord_root;
        s.chord = app.learn_chord;
        s.inversion = app.learn_inversion;
    } else {
        s.kind = learn_scale_arpeggio(app) ? DrillKind::Arpeggio : DrillKind::Scale;
        s.root_pc = app.learn_root;
        s.scale = app.learn_scale;
    }
    return s;
}

} // namespace etude::app::ui
