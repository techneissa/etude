// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/shift_fit.hpp"

#include "etude/keyboard.hpp"
#include "etude/transpose.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

Note note(int pitch, bool drum = false, bool accompaniment = false) {
    Note n;
    n.pitch = static_cast<std::uint8_t>(pitch);
    n.drum = drum;
    n.accompaniment = accompaniment;
    return n;
}

std::vector<Note> every_key(KeyRange range) {
    std::vector<Note> notes;
    for (int p = range.lowest; p <= range.highest; ++p) {
        notes.push_back(note(p));
    }
    return notes;
}

} // namespace

TEST_CASE("shift fit: an empty song leaves nothing outside at any shift") {
    const ShiftFits fits = shift_fits({}, kRange61);
    CHECK(fits.notes == 0);
    CHECK(fits.lowest == -1);
    CHECK(fits.highest == -1);
    for (int s = -kMaxTransposeSemitones; s <= kMaxTransposeSemitones; ++s) {
        CHECK(fits.outside_at(s) == 0);
        CHECK_FALSE(fits.all_fit(s)); // nothing to fit is not "all fit"
    }
    CHECK_FALSE(suggest_octave_shift(fits, 0).has_value());
}

TEST_CASE("shift fit: a song that exactly fills the keyboard fits at 0 and loses a key's worth per "
          "step") {
    const std::vector<Note> notes = every_key(kRange61);
    const ShiftFits fits = shift_fits(notes, kRange61);
    CHECK(fits.notes == 61);
    CHECK(fits.lowest == 36);
    CHECK(fits.highest == 96);
    CHECK(fits.outside_at(0) == 0);
    CHECK(fits.all_fit(0));
    for (int s = 1; s <= kMaxTransposeSemitones; ++s) {
        CHECK(fits.outside_at(s) == s);  // the top s keys go past the highest
        CHECK(fits.outside_at(-s) == s); // the bottom s keys go under the lowest
    }
    CHECK(fits.outside_at(12) == 12);
    CHECK(fits.outside_at(-12) == 12);
}

TEST_CASE("shift fit: one note a semitone over the top edge, and under the bottom edge") {
    const std::vector<Note> over = {note(60), note(97)};
    const ShiftFits a = shift_fits(over, kRange61);
    CHECK(a.outside_at(0) == 1);  // 97 is one over 96
    CHECK(a.outside_at(-1) == 0); // 97 -> 96 sits on the last key
    CHECK(a.outside_at(1) == 1);
    const std::vector<Note> under = {note(60), note(35)};
    const ShiftFits b = shift_fits(under, kRange61);
    CHECK(b.outside_at(0) == 1); // 35 is one under 36
    CHECK(b.outside_at(1) == 0); // 35 -> 36 sits on the first key
    CHECK(b.outside_at(-1) == 1);
}

TEST_CASE("shift fit: a note on each edge is inside, and the whole octave moves count per note") {
    const std::vector<Note> edges = {note(36), note(96), note(96), note(60)};
    const ShiftFits fits = shift_fits(edges, kRange61);
    CHECK(fits.outside_at(0) == 0);
    CHECK(fits.outside_at(12) == 2);  // both 96s go to 108
    CHECK(fits.outside_at(-12) == 1); // 36 goes to 24
}

TEST_CASE("shift fit: a keyboard narrower than the song has notes outside at every shift") {
    const KeyRange narrow{60, 71}; // one octave
    std::vector<Note> notes;
    for (int p = 48; p <= 84; ++p) {
        notes.push_back(note(p));
    }
    const ShiftFits fits = shift_fits(notes, narrow);
    for (int s = -kMaxTransposeSemitones; s <= kMaxTransposeSemitones; ++s) {
        CHECK(fits.outside_at(s) > 0);
        CHECK_FALSE(fits.all_fit(s));
    }
    CHECK(fits.outside_at(0) == 37 - 12);
}

TEST_CASE("shift fit: drums and accompaniment parts are not counted") {
    const std::vector<Note> notes = {note(60), note(120, true), note(2, false, true),
                                     note(110, true, true)};
    const ShiftFits fits = shift_fits(notes, kRange61);
    CHECK(fits.notes == 1);
    CHECK(fits.lowest == 60);
    CHECK(fits.highest == 60);
    CHECK(fits.outside_at(0) == 0);
}

TEST_CASE("shift fit: a shift beyond an octave either way is not a shift this app has") {
    const ShiftFits fits = shift_fits(every_key(kRange61), kRange61);
    CHECK(fits.outside_at(13) == -1);
    CHECK(fits.outside_at(-13) == -1);
}

TEST_CASE("shift fit: the count is the notes the transposition folds, at every shift") {
    // Notes spread over the whole of MIDI: the count for a shift is how many notes
    // Transposition::map moves (or cannot place) on the same keyboard.
    std::vector<Note> notes;
    for (int p = 0; p <= 127; p += 3) {
        notes.push_back(note(p));
    }
    for (const KeyRange range : {kRange61, kRange49, kRange88, kRange76}) {
        const ShiftFits fits = shift_fits(notes, range);
        for (int s = -kMaxTransposeSemitones; s <= kMaxTransposeSemitones; ++s) {
            const Transposition t(s, range);
            int moved = 0;
            for (const Note& n : notes) {
                const TransposedPitch m = t.map(n.pitch);
                if (m.folded || !m.playable) {
                    ++moved;
                }
            }
            CHECK(fits.outside_at(s) == moved);
        }
    }
}

TEST_CASE("shift fit, suggestion: a song an octave too high proposes the octave down") {
    std::vector<Note> notes;
    for (int p = 84; p <= 108; ++p) {
        notes.push_back(note(p));
    }
    const ShiftFits fits = shift_fits(notes, kRange61);
    REQUIRE(fits.outside_at(0) > 0);
    REQUIRE(fits.all_fit(-12));
    const std::optional<int> s = suggest_octave_shift(fits, 0);
    REQUIRE(s.has_value());
    CHECK(*s == -12);
}

TEST_CASE("shift fit, suggestion: an octave is a whole octave from the shift in force") {
    std::vector<Note> notes;
    for (int p = 84; p <= 108; ++p) {
        notes.push_back(note(p));
    }
    const ShiftFits fits = shift_fits(notes, kRange61);
    // At +2 the notes sit even higher; the octave down is +2 - 12 = -10, never a shift of -12.
    const std::optional<int> s = suggest_octave_shift(fits, 2);
    REQUIRE(s.has_value());
    CHECK(*s == -10);
    CHECK((*s - 2) % 12 == 0);
}

TEST_CASE("shift fit, suggestion: nothing when the notes already fit, or no octave helps") {
    CHECK_FALSE(suggest_octave_shift(shift_fits(every_key(kRange61), kRange61), 0).has_value());
    // A song wider than the board that every octave leaves worse or equal.
    std::vector<Note> wide;
    for (int p = 24; p <= 108; ++p) {
        wide.push_back(note(p));
    }
    const ShiftFits fits = shift_fits(wide, kRange61);
    CHECK_FALSE(suggest_octave_shift(fits, 0).has_value());
}

TEST_CASE(
    "shift fit, suggestion: a stray note does not raise a card, a twentieth of the song does") {
    // 100 notes, 4 of them over the top edge: under 5%, no offer even though an octave down fits.
    std::vector<Note> few(96, note(60));
    for (int i = 0; i < 4; ++i) {
        few.push_back(note(100));
    }
    CHECK_FALSE(suggest_octave_shift(shift_fits(few, kRange61), 0).has_value());
    // 5 of 100: exactly the share.
    std::vector<Note> some(95, note(60));
    for (int i = 0; i < 5; ++i) {
        some.push_back(note(100));
    }
    const std::optional<int> s = suggest_octave_shift(shift_fits(some, kRange61), 0);
    REQUIRE(s.has_value());
    CHECK(*s == -12);
}

TEST_CASE("shift fit, suggestion: from +12 the whole-octave target is 0, the only one in reach") {
    const ShiftFits fits = shift_fits(every_key(kRange61), kRange61);
    const std::optional<int> s = suggest_octave_shift(fits, 12);
    REQUIRE(s.has_value());
    CHECK(*s == 0);
}

TEST_CASE("shift fit, suggestion: a tie in every respect goes down") {
    // 4 notes over the top, 4 under the bottom: an octave down and an octave up each leave 4
    // outside (the ones that were under, or the ones that were over), from 0 equally far: the lower
    // wins.
    std::vector<Note> notes;
    for (int i = 0; i < 4; ++i) {
        notes.push_back(note(100));
        notes.push_back(note(30));
    }
    for (int i = 0; i < 92; ++i) {
        notes.push_back(note(60));
    }
    const ShiftFits fits = shift_fits(notes, kRange61);
    REQUIRE(fits.outside_at(0) == 8);
    REQUIRE(fits.outside_at(-12) == 4);
    REQUIRE(fits.outside_at(12) == 4);
    const std::optional<int> s = suggest_octave_shift(fits, 0);
    REQUIRE(s.has_value());
    CHECK(*s == -12);
}

TEST_CASE("shift fit, suggestion: equal counts take the smaller move") {
    // 10 notes just over the bottom edge's reach at -12: at -12 they are out, at 0 and +12 they
    // fit. From -12 both 0 (one octave away) and +12 (two) leave none outside: the nearer one wins.
    std::vector<Note> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back(note(38));
    }
    for (int i = 0; i < 90; ++i) {
        notes.push_back(note(60));
    }
    const ShiftFits fits = shift_fits(notes, kRange61);
    REQUIRE(fits.outside_at(-12) == 10);
    REQUIRE(fits.outside_at(0) == 0);
    REQUIRE(fits.outside_at(12) == 0);
    const std::optional<int> s = suggest_octave_shift(fits, -12);
    REQUIRE(s.has_value());
    CHECK(*s == 0);
}

TEST_CASE("shift fit, suggestion: one stray note never raises a card, however short the song") {
    // 10 notes, one at 100: 10% of the song, but a single note.
    std::vector<Note> ten(9, note(60));
    ten.push_back(note(100));
    const ShiftFits fits = shift_fits(ten, kRange61);
    REQUIRE(fits.outside_at(0) == 1);
    CHECK_FALSE(suggest_octave_shift(fits, 0).has_value());
    // A single note alone.
    CHECK_FALSE(
        suggest_octave_shift(shift_fits(std::vector<Note>{note(100)}, kRange61), 0).has_value());
}

TEST_CASE("shift fit, suggestion: two stray notes in a short song raise the card") {
    // 20 notes, two at 100: two notes and 10%.
    std::vector<Note> twenty(18, note(60));
    twenty.push_back(note(100));
    twenty.push_back(note(100));
    const std::optional<int> s = suggest_octave_shift(shift_fits(twenty, kRange61), 0);
    REQUIRE(s.has_value());
    CHECK(*s == -12);
    // Two of 50 is 4%: two notes, but under the share.
    std::vector<Note> fifty(48, note(60));
    fifty.push_back(note(100));
    fifty.push_back(note(100));
    CHECK_FALSE(suggest_octave_shift(shift_fits(fifty, kRange61), 0).has_value());
}
