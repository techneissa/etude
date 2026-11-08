// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/melody_progress.hpp"
#include "etude/melody_shape.hpp"

#include "etude/level_stars.hpp"

#include <doctest/doctest.h>

#include <array>

using namespace etude;

TEST_CASE("melody progress: a piece passes with the library's mastered score") {
    CHECK(kMelodyPassPermille == kMasteredPermille);
    CHECK(melody_mark(-1) == MelodyMark::New);
    CHECK(melody_mark(0) == MelodyMark::Started); // played, nothing right yet
    CHECK(melody_mark(kMasteredPermille - 1) == MelodyMark::Started);
    CHECK(melody_mark(kMasteredPermille) == MelodyMark::Passed);
    CHECK(melody_mark(1000) == MelodyMark::Passed);
    // The same answer as the library's own progress rule.
    BrowseSong song;
    song.recency = 0;
    for (const int best : {0, 450, 899, 900, 1000}) {
        song.best_permille = static_cast<std::int16_t>(best);
        CHECK((melody_mark(best) == MelodyMark::Passed) == (progress_of(song) == Progress::Mastered));
    }
}

TEST_CASE("melody progress: the share fills toward the pass mark and stops at 1") {
    CHECK(melody_share(-1) == 0.0f);
    CHECK(melody_share(0) == 0.0f);
    CHECK(melody_share(450) == doctest::Approx(0.5f));
    CHECK(melody_share(900) == doctest::Approx(1.0f));
    CHECK(melody_share(1000) == 1.0f);
}

TEST_CASE("melody progress: the fill runs from yellow through orange to green") {
    CHECK(melody_fill_colour(0.0f) == level_tok::kMiddle);
    CHECK(melody_fill_colour(0.5f) == level_tok::kHarder);
    CHECK(melody_fill_colour(1.0f) == level_tok::kEasy);
    CHECK(melody_fill_colour(-3.0f) == melody_fill_colour(0.0f));
    CHECK(melody_fill_colour(7.0f) == melody_fill_colour(1.0f));
    // Less red as the share rises past half way: toward the green.
    CHECK(melody_fill_colour(0.9f).r < melody_fill_colour(0.6f).r);
}

TEST_CASE("melody progress: a path counts what is passed and names the next piece") {
    const std::array<int, 4> none{-1, -1, -1, -1};
    MelodyPathProgress p = melody_path_progress(none);
    CHECK(p.passed == 0);
    CHECK(p.total == 4);
    CHECK(p.next == 0);
    CHECK_FALSE(p.started);

    const std::array<int, 4> some{950, 920, 400, -1};
    p = melody_path_progress(some);
    CHECK(p.passed == 2);
    CHECK(p.next == 2);
    CHECK(p.started);

    // A later piece passed before an earlier one: the next is still the first not passed.
    const std::array<int, 3> gap{950, 300, 990};
    p = melody_path_progress(gap);
    CHECK(p.passed == 2);
    CHECK(p.next == 1);

    const std::array<int, 2> all{900, 1000};
    p = melody_path_progress(all);
    CHECK(p.passed == 2);
    CHECK(p.next == -1);
}

namespace {

Note note(int pitch, Hand hand, bool drum = false, bool accompaniment = false) {
    Note n;
    n.pitch = static_cast<std::uint8_t>(pitch);
    n.hand = hand;
    n.drum = drum;
    n.accompaniment = accompaniment;
    return n;
}

} // namespace

TEST_CASE("melody shape: each hand's lowest and highest key and how many keys that is") {
    Song song;
    song.notes = {note(60, Hand::Right), note(64, Hand::Right), note(52, Hand::Left),
                  note(43, Hand::Left),  note(100, Hand::Right, true),
                  note(20, Hand::Left, false, true)};
    const MelodyShape shape = melody_shape(song);
    CHECK(shape.right.lowest == 60);
    CHECK(shape.right.highest == 64);
    CHECK(shape.right.keys() == 5); // C D♭ D E♭ E: the black keys count
    CHECK(shape.left.lowest == 43);
    CHECK(shape.left.highest == 52);
    CHECK(shape.left.keys() == 10);
}

TEST_CASE("melody shape: a hand with no notes has no keys, and a note with no hand is the right's") {
    Song song;
    song.notes = {note(60, Hand::Unknown), note(62, Hand::Unknown)};
    const MelodyShape shape = melody_shape(song);
    CHECK(shape.right.keys() == 3);
    CHECK_FALSE(shape.left.used());
    CHECK(shape.left.keys() == 0);
}
