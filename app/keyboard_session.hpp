// SPDX-License-Identifier: GPL-3.0-or-later
// The player's keyboard as the frame loop knows it: the keys held, the layout, the matched
// profile and the label, and how a MIDI event or a port change moves them. Inline, since the frame
// loop calls them every frame.
#pragma once

#include "midi_input.hpp"

#include "etude/key_state.hpp"
#include "etude/keyboard.hpp"
#include "etude/session_state.hpp"
#include "etude/shift_offer.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace etude::app {

// Everything the frame loop knows about the player's keyboard.
struct Session {
    KeyState keys;
    KeyboardLayout layout{etude::kRange88};
    const etude::KeyboardProfile* profile = nullptr;
    int open_inputs = 0;
    // Key presses outside the layout's range, a hint that the keyboard's octave shift is on.
    std::uint64_t out_of_range = 0;
    std::string label = "No keyboard";
    // The saved keyboard choice (Profile or Manual) is in force instead of the detected keyboard.
    bool overridden = false;
    etude::KeyRange override_range{0, 0};
    // The keyboard's notes arrive this many semitones off what its profile says (its octave button
    // is on), as the player accepted it for this session: the layout is the range moved by it.
    // Gone when the keyboard is plugged again or another is chosen, and never saved.
    int sent_shift = 0;
};

// The range the keyboard's keys are labelled with, before any shift of what it sends.
inline etude::KeyRange base_range(const Session& s) {
    if (s.overridden) {
        return s.override_range;
    }
    return s.profile != nullptr ? s.profile->range : etude::kRange88;
}

// The layout the keyboard sends: the base range moved by the shift in force. The held keys and the
// count of presses outside are of the old reading.
inline void rebuild_layout(Session& s) {
    const std::optional<etude::KeyRange> moved =
        etude::shifted_range(base_range(s), s.sent_shift);
    if (!moved) {
        s.sent_shift = 0;
    }
    s.layout = KeyboardLayout(moved ? *moved : base_range(s));
    s.out_of_range = 0;
}

inline std::string keyboard_label(const Session& s) {
    if (s.open_inputs == 0) {
        return "No keyboard";
    }
    if (s.profile == nullptr) {
        return "Unknown keyboard, 88 keys";
    }
    return std::string(s.profile->name) + ", " + std::to_string(s.profile->range.key_count()) +
           " keys";
}

// Applies port changes: releases every key when an input went away and rebuilds the layout when
// the matched keyboard changed. Returns the release (AllReleased, or None) and sets `label_changed`
// when the keyboard label changed.
inline KeyTransition apply_port_changes(Session& s, const MidiInput& midi,
                                        etude::app::PortChanges changes, bool& label_changed) {
    KeyTransition released;
    if (changes.closed > 0) {
        released = s.keys.clear();
    }
    label_changed = false;
    const auto* profile = midi.profile();
    const int open = midi.open_count();
    if (profile == s.profile && open == s.open_inputs) {
        return released;
    }
    if (profile != s.profile) {
        s.layout = KeyboardLayout(profile != nullptr ? profile->range : etude::kRange88);
    }
    s.profile = profile;
    s.open_inputs = open;
    if (s.sent_shift != 0) { // a keyboard plugged in again sends what its profile says
        s.sent_shift = 0;
        rebuild_layout(s);
    }
    s.label = keyboard_label(s);
    label_changed = true;
    return released;
}

inline KeyTransition apply_event(Session& s, const MidiEvent& ev) {
    const KeyTransition t = s.keys.apply(ev);
    if (t.type == KeyTransitionType::Pressed && !s.layout.range().contains(t.note)) {
        ++s.out_of_range;
    }
    return t;
}

// The saved keyboard choice, from Settings > View. Auto (false) leaves the detected keyboard in
// charge; Profile and Manual give the range and label to use instead.
inline bool chosen_keyboard(const etude::SessionState& saved, etude::KeyRange* range,
                            std::string* label) {
    switch (saved.keyboard) {
    case etude::KeyboardChoice::Auto:
        return false;
    case etude::KeyboardChoice::Profile: {
        *range = saved.keyboard_range();
        std::string name = saved.keyboard_name;
        for (const etude::KeyboardProfile& p : etude::builtin_keyboard_profiles()) {
            if (p.name == saved.keyboard_name) {
                *range = p.range; // the profile's own range wins over the one saved with it
            }
        }
        *label = name + ", " + std::to_string(range->key_count()) + " keys";
        return true;
    }
    case etude::KeyboardChoice::Manual:
        *range = saved.keyboard_range();
        *label = "Keyboard, " + std::to_string(range->key_count()) + " keys";
        return true;
    }
    return false;
}

// Applies the saved keyboard choice on top of what the ports gave, and takes it off again when
// the choice goes back to Auto. Returns true when the layout changed.
inline bool apply_keyboard_choice(Session& s, const etude::SessionState& saved) {
    etude::KeyRange range{0, 0};
    std::string label;
    if (chosen_keyboard(saved, &range, &label)) {
        const bool same = s.overridden && s.override_range.lowest == range.lowest &&
                          s.override_range.highest == range.highest && s.label == label;
        if (same) {
            return false;
        }
        s.layout = KeyboardLayout(range);
        s.label = label;
        s.override_range = range;
        s.overridden = true;
        s.sent_shift = 0;
        return true;
    }
    if (!s.overridden) {
        return false;
    }
    s.overridden = false;
    s.sent_shift = 0;
    s.layout = KeyboardLayout(s.profile != nullptr ? s.profile->range : etude::kRange88);
    s.label = keyboard_label(s);
    return true;
}

} // namespace etude::app
