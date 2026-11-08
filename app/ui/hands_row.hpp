// SPDX-License-Identifier: GPL-3.0-or-later
// What Settings > Practice's Hands row does with its taps, kept apart from the drawing so a test
// can run it. "Split at" is chosen first and takes effect with the first step of the stepper, so
// choosing it moves no note: until then the notes keep the hands the sheet gave them.
#pragma once

#include "ui/song_port.hpp"

#include <algorithm>
#include <cstdint>

namespace etude::app::ui {

// The row's own state, one per song opened: "Split at" is chosen and no step was taken yet.
struct HandsRowState {
    std::uint32_t pending_serial = 0; // the song_serial of the song it was chosen for, 0 for none

    bool pending(const SongView& view) const {
        return !view.split_fixed && pending_serial != 0 && pending_serial == view.song_serial;
    }
};

enum class HandsAction : std::uint8_t { None, Place, Reset };

struct HandsOutcome {
    HandsAction action = HandsAction::None;
    int key = 0; // the keyboard's key to place the split on
};

// Is "Split at" the cell shown selected (placed, or chosen and waiting for its first step).
inline bool hands_split_shown(const SongView& view, const HandsRowState& state) {
    return view.split_fixed || state.pending(view);
}

// A tap on a cell: "Split at" waits for the first step; "From the sheet" clears a placed split.
inline HandsOutcome hands_choose(const SongView& view, HandsRowState& state, bool split_at) {
    if (split_at) {
        state.pending_serial = view.split_fixed ? 0 : view.song_serial;
        return {};
    }
    state.pending_serial = 0;
    return view.split_fixed ? HandsOutcome{HandsAction::Reset, 0} : HandsOutcome{};
}

// The stepper's two directions: a key up or down from where the split stands (or would stand).
inline bool hands_can_step(const SongView& view, int step) {
    const int pitch = view.split_key - view.transpose; // the song's own pitch, 1..127
    return step < 0 ? pitch > 1 : (pitch < 127 && view.split_key < 127);
}

inline HandsOutcome hands_step(const SongView& view, HandsRowState& state, int step) {
    if (step == 0 || !hands_can_step(view, step)) {
        return {};
    }
    state.pending_serial = 0;
    return {HandsAction::Place, std::clamp(view.split_key + step, 0, 127)};
}

} // namespace etude::app::ui
