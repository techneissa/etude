// SPDX-License-Identifier: GPL-3.0-or-later
// The note and grade structs a frame carries, apart from Scene so the live preview and the
// window state can hold them without the whole renderer.
#pragma once

#include "etude/evaluator_results.hpp"
#include "etude/hand.hpp"

#include <cstdint>

namespace etude::app {

// A note of the song on its way down to the hit line: NoteWindow's VisibleNote, placed on a key.
struct SongBar {
    std::uint8_t key = 0; // after transposition
    std::uint8_t velocity = 80;
    Hand hand = Hand::Unknown;
    // Fractions of the note area: 0 is the hit line, 1 the top. A note that has reached the line
    // has bottom <= 0; the bed hides what is below it.
    float bottom = 0.0f;
    float top = 0.0f;
    bool practised = true;  // false: a hand you are not practising, drawn faint
    std::int8_t grade = -1; // its NoteGrade once graded, else -1
    std::int8_t fold = 0;   // moved by this many octaves to fit the keyboard: a marker shows it
    // The player's held key is striking it (note_struck): lit in its hand's colour while the part
    // above the hit line shortens into the key.
    bool struck = false;
    // The finger that plays it (1..5), shown on the block of a drill; 0: none.
    std::int8_t finger = 0;
};

// A grade shown briefly where a note met the hit line (Flow Mode).
struct GradePopup {
    std::uint8_t key = 0;
    NoteGrade grade = NoteGrade::Missed;
    float age = 0.0f; // 0 when it appears, 1 when it is gone
};

} // namespace etude::app
