// SPDX-License-Identifier: GPL-3.0-or-later
// What the player's keys show at the keyboard line while a song is open: the falling note a held
// key is striking stays lit, and a wrong press flashes its key red. The evaluators decide what a
// press hit (FlowEvaluator::struck_start, WaitEvaluator::struck_start) and which presses were
// wrong (take_wrong_presses); this is the rule that turns those into what is drawn.
#pragma once

#include "etude/evaluator.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace etude {

// Two notes whose starts differ by no more than this are one strike on a key: a unison of both
// hands, or a note of a chord written a little apart (kChordSeconds), so a press that hit one hit
// them all.
inline constexpr double kStrikeSpreadS = kChordSeconds + 1e-6;

// Whether the falling note on a key, starting at `note_start_s`, is lit as struck: the key is held,
// its latest press hit a note (`struck_start_s`, from the evaluator) that is this one, and the
// note was not graded Missed (`grade` is its NoteGrade, or -1 while undecided). A key held with no
// press that hit a note, a wrong key or a press for another note, lights no falling note.
bool note_struck(std::optional<double> struck_start_s, bool key_down, double note_start_s,
                 int grade);

// How long a wrong press's flash on its key lasts, in ns.
inline constexpr std::int64_t kWrongFlashNs = 300'000'000;

// How strong a wrong press's flash is `age_ns` after it: 1 at first, falling to 0 at
// kWrongFlashNs (and 0 after). A reduced-effects look shows it steady for the whole time:
// `fade` false.
float wrong_flash_alpha(std::int64_t age_ns, bool fade = true);

// How opaque the player's rising blocks are: fully in free play, none while a song is open, and
// faint (kRisingInSongAlpha, so they do not compete with the falling notes) when the look asks to
// see them during a song (LookSettings::rising_in_song).
inline constexpr float kRisingInSongAlpha = 0.4f;
float rising_alpha(bool song_open, bool rising_in_song);

// Raises `levels[key & 127]` to the flash strength of a wrong press made `age_ns` ago, keeping
// the strongest of the presses on a key.
void add_wrong_flash(std::array<float, 128>& levels, int key, std::int64_t age_ns, bool fade);

} // namespace etude
