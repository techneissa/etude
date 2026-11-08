// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/hand_split.hpp"
#include "etude/note_window.hpp"
#include "etude/session_state.hpp"
#include "etude/transport.hpp"
#include "etude/transpose.hpp"

#include <cstdint>
#include <vector>

namespace etude {

// The practice choices that belong to one song, kept by the song's content hash (so a renamed or
// moved file keeps them) and never written into the file: the speed, the bars of count-in, the
// hand(s) practised, the transposition, how the notes are split between the hands, which
// accompaniment parts are muted, the loop with the section and practice list item it came from, and
// whether the octave-shift offer is turned off. A song never opened has the defaults below.
struct SongPractice {
    int speed_percent = 100; // kMinSpeedPercent .. kMaxSpeedPercent
    int count_in_bars = kDefaultCountInBars; // 0 .. kMaxCountInBars
    HandFilter hands = HandFilter::Both;
    int transpose_semitones = 0; // -kMaxTransposeSemitones .. +kMaxTransposeSemitones
    // How the notes with no hand in the file are split between the hands: kAutoSplit (the smart
    // split), or the pitch, in the song's own pitches, from which notes are the right hand
    // (hand_split.hpp, options_for_split_point). Kept for any song; it changes only a song whose
    // file does not name the hands.
    int split_point = kAutoSplit; // kAutoSplit, or 1 .. kMaxSplitPoint

    // The accompaniment parts the player muted, as sorted part keys (accompaniment.hpp).
    // `parts_set` false: the player never chose, so the global default decides (all sound, or all
    // are muted) and `muted_parts` is empty.
    bool parts_set = false;
    std::vector<std::uint32_t> muted_parts;

    // The A-B loop as the session keeps it (SessionState): a loop exists when both ends are
    // 0 .. kMaxSessionSeconds and at least kMinLoopSeconds apart; without one both are 0 and it is
    // off. `section_id` is the section looped (0 none), `plan_item` the practice list item running
    // (1-based, 0 none); both are checked against the song's sections when they are applied.
    double loop_start_s = 0.0;
    double loop_end_s = 0.0;
    bool loop_enabled = false;
    int section_id = 0;
    int plan_item = 0;

    // The player asked never to be offered an octave shift for this song.
    bool no_shift_offer = false;

    bool has_loop() const { return loop_end_s - loop_start_s >= kMinLoopSeconds; }

    friend bool operator==(const SongPractice&, const SongPractice&) = default;
};

// The most muted parts kept for a song.
inline constexpr std::size_t kMaxSongPracticeParts = 32;

// The same with every field forced into its range.
SongPractice sanitized(const SongPractice& practice);
// In place: no allocation when `practice`'s parts already fit (the frame loop's per-frame look).
void sanitize(SongPractice& practice);

// The fields of a song's practice choices the player changed, with the values they gave them.
struct PracticeChanges {
    SongPractice value;
    bool speed = false;
    bool count_in = false;
    bool hands = false;
    bool transpose = false;
    bool split = false;
    bool parts = false; // parts_set and muted_parts
    bool loop = false;  // the loop, the section and the practice list item together
    bool shift_offer = false; // no_shift_offer

    bool any() const {
        return speed || count_in || hands || transpose || split || parts || loop || shift_offer;
    }
};

// `earlier` with the fields that differ between `at_open` (the choices a song was opened on) and
// `now` added, `now`'s value winning.
PracticeChanges note_changes(PracticeChanges earlier, const SongPractice& at_open,
                             const SongPractice& now);

// `base` with the changed fields taken from `changes`.
SongPractice apply_changes(const SongPractice& base, const PracticeChanges& changes);

// `stored` (what the history holds for the song) laid over a song that was opened with `at_open`
// before the history had loaded: each field the player changed since (`now` differs from
// `at_open`) keeps the player's value, every other takes the stored one.
SongPractice with_player_changes(const SongPractice& stored, const SongPractice& at_open,
                                 const SongPractice& now);

} // namespace etude
