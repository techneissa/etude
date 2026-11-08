// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_opening.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

Note note(double start, double len, int pitch, Hand hand = Hand::Right, bool drum = false) {
    Note n;
    n.start_s = start;
    n.duration_s = len;
    n.pitch = static_cast<std::uint8_t>(pitch);
    n.hand = hand;
    n.drum = drum;
    return n;
}

} // namespace

TEST_CASE("a song's opening: its first seconds from the first note, cut there, on whole octaves") {
    Song s;
    s.notes = {note(1.0, 0.5, 9, Hand::Unknown, true), // a drum: left out
               note(2.0, 1.0, 64), note(2.5, 0.5, 48, Hand::Left), note(5.5, 2.0, 67),
               note(9.0, 1.0, 72)}; // past the opening
    const SongOpening o = song_opening(s, 4.0);
    REQUIRE(o.notes.size() == 3);
    CHECK(o.notes[0].at == doctest::Approx(0.0f));
    CHECK(o.notes[0].key == 64);
    CHECK(o.notes[1].hand == Hand::Left);
    CHECK(o.notes[2].at == doctest::Approx(3.5f));
    CHECK(o.notes[2].len == doctest::Approx(0.5f)); // cut where the opening ends
    CHECK(o.length_s == doctest::Approx(4.0f));
    CHECK(o.keys.lowest == 48);
    CHECK(o.keys.highest == 72);
    CHECK(o.keys.valid());
    // One note still gets two octaves round it; no notes, nothing.
    Song one;
    one.notes = {note(0.0, 1.0, 61)};
    const SongOpening narrow = song_opening(one, 8.0);
    CHECK(narrow.keys.highest - narrow.keys.lowest == 24);
    CHECK(narrow.keys.contains(61));
    CHECK(song_opening(Song{}, 8.0).notes.empty());
}
