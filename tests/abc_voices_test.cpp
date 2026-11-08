// SPDX-License-Identifier: GPL-3.0-or-later
// abc: voices (split from abc_test.cpp).
#include "etude/abc.hpp"
#include "etude/content_hash.hpp"
#include "etude/hand_split.hpp"
#include "etude/library.hpp"
#include "etude/library_state.hpp"
#include "etude/musicxml.hpp"
#include "etude/song_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "abc_test_support.hpp"

using namespace etude;
using namespace abc_test_support;

// ---- Voices

TEST_CASE("abc: voices are parts, with names, clefs and an interleaved layout") {
    const std::string text =
        "X:1\nT:duet\nM:4/4\nL:1/4\nK:C\n"
        "V:1 name=\"Right hand\" clef=treble\nV:2 name=\"Left hand\" clef=bass\n"
        "V:1\nc d e f |\nV:2\nC, D, E, F, |\nV:1\ng a b c' |]\nV:2\nG, A, B, C, |]\n";
    const ScoreLoadResult r = abc(text);
    REQUIRE(r.ok);
    REQUIRE(r.song.parts.size() == 2);
    CHECK(r.song.parts[0].name == "Right hand");
    CHECK(r.song.parts[1].name == "Left hand");
    CHECK(r.song.parts[0].hand == Hand::Right);
    CHECK(r.song.parts[1].hand == Hand::Left);
    CHECK(r.song.notes.size() == 16);
    CHECK(r.song.measures.size() == 2);
    int right = 0;
    int left = 0;
    for (const Note& n : r.song.notes) {
        (n.hand == Hand::Right ? right : left) += 1;
        CHECK((n.hand == Hand::Right) == (n.pitch >= 60));
    }
    CHECK(right == 8);
    CHECK(left == 8);
}

TEST_CASE("abc: voices that differ in clef get hands, voices without a hint stay unknown") {
    const std::string clefs = "X:1\nT:t\nM:4/4\nL:1/4\nK:C\nV:s clef=treble\nV:b clef=bass\nV:s\nc "
                              "d e f|]\nV:b\nC D E F|]\n";
    const ScoreLoadResult a = abc(clefs);
    REQUIRE(a.ok);
    CHECK(a.song.parts[0].hand == Hand::Right);
    CHECK(a.song.parts[1].hand == Hand::Left);
    const std::string plain = "X:1\nT:t\nM:4/4\nL:1/4\nK:C\nV:1\nc d e f|]\nV:2\nC D E F|]\n";
    const ScoreLoadResult b = abc(plain);
    REQUIRE(b.ok);
    CHECK(b.song.parts[0].hand == Hand::Unknown);
    CHECK(b.song.parts[1].hand == Hand::Unknown);
    CHECK(b.song.parts[0].name == "Voice 1");
    // A lone bass clef names no pair: the single line stays unknown.
    const ScoreLoadResult c = abc("X:1\nT:t\nK:C clef=bass\nC D E F|]\n");
    REQUIRE(c.ok);
    CHECK(c.song.parts[0].hand == Hand::Unknown);
}

TEST_CASE("abc: inline [V:] switches voice in the middle of a line") {
    const ScoreLoadResult r = abc(
        "X:1\nT:t\nM:4/4\nL:1/4\nK:C\nV:1\nV:2\n[V:1] c d e f | [V:2] C D E F | [V:1] g g g g|]\n");
    REQUIRE(r.ok);
    REQUIRE(r.song.parts.size() == 2);
    CHECK(r.song.notes.size() == 12);
    // The second voice's bar is its first bar, so it plays with the first voice's first bar.
    CHECK(r.song.notes[0].start_tick == 0);
    CHECK(r.song.measures.size() == 2);
}

TEST_CASE("abc: transpose= and octave= on a voice shift its notes") {
    const ScoreLoadResult r =
        abc("X:1\nT:t\nM:4/4\nL:1/4\nK:C\nV:1\nV:2 octave=-1\nV:3 transpose=2\n"
            "V:1\nC|]\nV:2\nC|]\nV:3\nC|]\n");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{48, 60, 62});
}

TEST_CASE("abc: an overlay & starts a second line of notes in the same bar") {
    const ScoreLoadResult r = load_music("c4 d4 & C8 | e8 & E8 |]");
    REQUIRE(r.ok);
    CHECK(r.song.measures.size() == 2);
    CHECK(pitches(r.song) == std::vector<int>{60, 72, 74, 64, 76});
    // Both lines start with their bar.
    int at_start = 0;
    for (const Note& n : r.song.notes) {
        if (n.start_tick == 0) {
            ++at_start;
        }
    }
    CHECK(at_start == 2);
    // The overlay is no longer than the bar: nothing starts the second bar late.
    CHECK(r.song.measures[1].start_tick == 4 * r.song.tempo.ticks_per_quarter);
}

TEST_CASE("abc: the same pitch twice at once in one voice is one note, as in a MIDI file") {
    const ScoreLoadResult r = load_music("C4 z4 & C8 | D8 |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{60, 62});
    CHECK(length_of(r.song, r.song.notes[0]) == doctest::Approx(4.0));
    CHECK(r.stats.duplicates_merged == 1);
    // An overlapping repeat of the same pitch cuts the first note where the second starts.
    const ScoreLoadResult cut = load_music("C8 & z2 C6 |]");
    REQUIRE(cut.ok);
    REQUIRE(cut.song.notes.size() == 2);
    CHECK(length_of(cut.song, cut.song.notes[0]) == doctest::Approx(1.0));
    CHECK(r.stats.overlaps_split == 0);
    CHECK(cut.stats.overlaps_split == 1);
}

// ---- Chord symbols

TEST_CASE("abc: chord symbols become a left hand only when asked") {
    const std::string music = "\"C\"C8 | \"Am\"A8 | \"G7\"G4 \"C\"C4 |]";
    const ScoreLoadResult off = load_music(music);
    REQUIRE(off.ok);
    CHECK(off.song.notes.size() == 4);
    CHECK(off.song.parts.size() == 1);
    CHECK(off.song.parts[0].hand == Hand::Unknown);

    AbcOptions options;
    options.chords_as_left_hand = true;
    const ScoreLoadResult on = load_music(music, "M:4/4\nL:1/8\nK:C", options);
    REQUIRE(on.ok);
    REQUIRE(on.song.parts.size() == 2);
    CHECK(on.song.parts[1].name == "Chords");
    CHECK(on.song.parts[1].hand == Hand::Left);
    CHECK(on.song.parts[0].hand == Hand::Right);
    // Root and fifth for each chord symbol: C G | A E | G D | C G, in the octave from C2.
    std::vector<int> left;
    for (const Note& n : on.song.notes) {
        if (n.hand == Hand::Left) {
            left.push_back(n.pitch);
        }
    }
    CHECK(left == std::vector<int>{36, 43, 45, 52, 43, 50, 36, 43});
    // A chord lasts to the next symbol or the end of its bar.
    for (const Note& n : on.song.notes) {
        if (n.hand != Hand::Left) {
            continue;
        }
        const double s = start_of(on.song, n);
        const double l = length_of(on.song, n);
        if (s < 8.0) {
            CHECK(l == doctest::Approx(4.0));
        } else if (s < 12.0) {
            CHECK(l == doctest::Approx(2.0));
        }
    }
}

TEST_CASE("abc: a chord note is left out only where the melody sounds the same key") {
    AbcOptions options;
    options.chords_as_left_hand = true;
    // The left hand's notes as (start, length, pitch), the times in eighth notes.
    auto left_of = [&](const std::string& music, const char* header) {
        const ScoreLoadResult r = load_music(music, header, options);
        REQUIRE_MESSAGE(r.ok, music);
        std::vector<std::array<int, 3>> out;
        for (const Note& n : r.song.notes) {
            if (n.hand == Hand::Left) {
                out.push_back({static_cast<int>(std::lround(start_of(r.song, n) * 2.0)),
                               static_cast<int>(std::lround(length_of(r.song, n) * 2.0)),
                               static_cast<int>(n.pitch)});
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    using Notes = std::vector<std::array<int, 3>>;
    // The melody sits on C2 (36) and G2 (43) under a C chord (root 36, fifth 43): each chord note is
    // silent while the melody has its key, and goes on when the melody note has ended.
    const Notes bars = left_of("\"C\"C,,2 G,,2 C4 | \"C\"c8 |]", "M:4/4\nL:1/8\nK:C");
    CHECK(bars == Notes{{0, 2, 43}, {2, 6, 36}, {4, 4, 43}, {8, 8, 36}, {8, 8, 43}});
    // The mixed case: the G the melody holds for the first half is the chord's fifth, the
    // C it holds for the second half is the root; each comes back for the other half.
    CHECK(left_of("\"C\"G,,2 C,,2|", "M:4/4\nL:1/4\nK:C") == Notes{{0, 4, 36}, {4, 4, 43}});
    // A melody note on a key the chord does not use changes nothing.
    CHECK(left_of("\"C\"D,,2 E,,2|", "M:4/4\nL:1/4\nK:C") == Notes{{0, 8, 36}, {0, 8, 43}});
    const ScoreLoadResult r = load_music("\"C\"C,,2 G,,2 C4 | \"C\"c8 |]", "M:4/4\nL:1/8\nK:C", options);
    REQUIRE(r.ok);
    int right = 0;
    for (const Note& n : r.song.notes) {
        right += n.hand == Hand::Right ? 1 : 0;
    }
    CHECK(right == 4);
    // A melody note on the same key after the chord's note has ended does not remove it.
    const ScoreLoadResult after =
        load_music("\"C\"c2 \"G\"z2 C,,4 |]", "M:4/4\nL:1/8\nK:C", options);
    REQUIRE(after.ok);
    std::vector<int> low;
    for (const Note& n : after.song.notes) {
        if (n.hand == Hand::Left) {
            low.push_back(n.pitch);
        }
    }
    CHECK(low ==
          std::vector<int>{36, 43, 43, 50}); // C chord, then G chord: the C2 comes after the C
}

TEST_CASE("abc: chord symbol qualities, slash chords and things that are not chords") {
    AbcOptions options;
    options.chords_as_left_hand = true;
    auto left_of = [&](const std::string& symbol) {
        const ScoreLoadResult r =
            load_music("\"" + symbol + "\"C8 |]", "M:4/4\nL:1/8\nK:C", options);
        REQUIRE(r.ok);
        std::vector<int> out;
        for (const Note& n : r.song.notes) {
            if (n.hand == Hand::Left) {
                out.push_back(n.pitch);
            }
        }
        return out;
    };
    CHECK(left_of("F#m7") == std::vector<int>{36 + 6, 36 + 13});
    CHECK(left_of("Bb") == std::vector<int>{36 + 10, 36 + 17});
    CHECK(left_of("Bdim") == std::vector<int>{36 + 11, 36 + 17}); // diminished fifth
    CHECK(left_of("Bm7b5") == std::vector<int>{36 + 11, 36 + 17});
    CHECK(left_of("C+") == std::vector<int>{36, 36 + 8}); // augmented fifth
    CHECK(left_of("Caug") == std::vector<int>{36, 36 + 8});
    CHECK(left_of("Dsus4") == std::vector<int>{38, 45});
    CHECK(left_of("Cmaj7") == std::vector<int>{36, 43});
    CHECK(left_of("C/E") == std::vector<int>{36 + 4}); // a slash chord: the bass alone
    CHECK(left_of("Gb") == std::vector<int>{36 + 6, 36 + 13});
    CHECK(left_of("Dorian").empty()); // a word, not a chord
    CHECK(left_of("Chorus").empty());
    CHECK(left_of("^above").empty()); // an annotation
    CHECK(left_of("N.C.").empty());
}

TEST_CASE("abc: no chord symbols, no left hand, even when asked") {
    AbcOptions options;
    options.chords_as_left_hand = true;
    const ScoreLoadResult r = load_music("C8 | D8 |]", "M:4/4\nL:1/8\nK:C", options);
    REQUIRE(r.ok);
    CHECK(r.song.parts.size() == 1);
    CHECK(r.song.parts[0].hand == Hand::Unknown);
}

TEST_CASE("abc: N.C. ends the chord before it, and a chord plays again after a repeat") {
    AbcOptions options;
    options.chords_as_left_hand = true;
    const ScoreLoadResult r =
        load_music("|: \"C\"C4 \"N.C.\"D4 | \"G\"G8 :|", "M:4/4\nL:1/8\nK:C", options);
    REQUIRE(r.ok);
    int left = 0;
    for (const Note& n : r.song.notes) {
        if (n.hand == Hand::Left) {
            ++left;
            if (n.pitch == 36 || n.pitch == 43) {
                // The C chord lasts to the N.C. at the third beat.
                if (start_of(r.song, n) == doctest::Approx(0.0) ||
                    start_of(r.song, n) == doctest::Approx(8.0)) {
                    CHECK(length_of(r.song, n) == doctest::Approx(2.0));
                }
            }
        }
    }
    CHECK(left == 8); // the C chord and the G chord, two notes each, on both passes
    CHECK(r.song.measures.size() == 4);
}

TEST_CASE("abc: chord symbols in two voices are counted once") {
    AbcOptions options;
    options.chords_as_left_hand = true;
    const ScoreLoadResult r =
        abc("X:1\nT:t\nM:4/4\nL:1/4\nK:C\nV:1\nV:2\nV:1\n\"C\"c d e f|]\nV:2\n\"C\"C D E F|]\n",
            options);
    REQUIRE(r.ok);
    int chords = 0;
    for (const Part& p : r.song.parts) {
        if (p.name == "Chords") {
            chords = p.note_count;
        }
    }
    CHECK(chords == 2);
}
