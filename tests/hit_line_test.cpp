// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hit_line.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {
constexpr int kUndecided = -1;
const int kMissed = static_cast<int>(NoteGrade::Missed);
const int kPerfect = static_cast<int>(NoteGrade::Perfect);
} // namespace

TEST_CASE("hit line: a held key lights the note its press struck") {
    CHECK(note_struck(1.0, true, 1.0, kUndecided));
    CHECK(note_struck(1.0, true, 1.0, kPerfect));
}

TEST_CASE("hit line: a released key lights nothing") {
    CHECK_FALSE(note_struck(1.0, false, 1.0, kPerfect));
}

TEST_CASE("hit line: a held wrong key lights no falling note") {
    // The key's latest press hit no note.
    CHECK_FALSE(note_struck(std::nullopt, true, 1.0, kUndecided));
    // It hit another note: a held key lights only the note it struck.
    CHECK_FALSE(note_struck(5.0, true, 1.0, kUndecided));
}

TEST_CASE("hit line: a note graded Missed is never lit") {
    CHECK_FALSE(note_struck(1.0, true, 1.0, kMissed));
}

TEST_CASE("hit line: notes of one strike share the light, a note further off does not") {
    CHECK(note_struck(1.0, true, 1.0 + kChordSeconds, kUndecided)); // a chord or unison spread
    CHECK_FALSE(note_struck(1.0, true, 1.0 + kChordSeconds + 0.001, kUndecided));
    CHECK(note_struck(1.0, true, 1.0 - kChordSeconds, kUndecided));
}
