// SPDX-License-Identifier: GPL-3.0-or-later
// What a song's practice tables are built from and the tables made ahead of loading, kept apart
// from SongController so the song opener does not rebuild with it.
#pragma once

#include "etude/evaluator.hpp"
#include "etude/hand_split.hpp"
#include "etude/hands_present.hpp"
#include "etude/note_window.hpp"
#include "etude/playback.hpp"
#include "etude/transpose.hpp"

#include <cstddef>
#include <cstdint>

namespace etude::app {

// What a loaded song's practice tables depend on besides the song: the hands asked for and the
// transposition in force, and how far ahead the notes are shown (to size the frame's lists).
struct SongLoadPlan {
    HandFilter hands = HandFilter::Both; // asked for; the song's hands limit what applies
    Transposition transposition;
    double ahead_s = 4.0;
};

// The tables SongController::load() builds from a song, made ahead by SongController::prepare() on
// another thread (the opener's) so the frame thread only takes them over. load() uses them when
// they were built for the hands and transposition it ends up with, else it builds its own.
struct PreparedSong {
    bool valid = false;
    std::uint64_t fingerprint = 0;       // song_fingerprint of the song the tables were built from
    HandFilter hands = HandFilter::Both; // what applies: the asked-for hands, limited to the song's
    Transposition transposition;
    HandsPresent present;           // the hands with notes in the whole song
    std::size_t densest_window = 0; // the most notes in view at once (densest_window)
    FlowEvaluator::Prepared flow;
    WaitEvaluator::Prepared wait;
    NoteWindow window;
    Playback::Prepared playback; // shared by the song's playback and its audition's
};

} // namespace etude::app
