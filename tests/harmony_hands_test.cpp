// SPDX-License-Identifier: GPL-3.0-or-later
// harmony: hands (split from harmony_test.cpp).
#include "etude/harmony.hpp"
#include "etude/musicxml.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "harmony_test_support.hpp"

using namespace etude;
using namespace harmony_test_support;

// ---- Compound and unusual bars
// ----------------------------------------------------------------------

TEST_CASE("harmony: 6/8 counts two dotted beats and a waltz does not split") {
    Piece p(1, 6, 8);
    p.chord(0, 0.0, {C3, G3, E4}, 1.4);
    p.chord(0, 1.5, {G2, D3, B3, G4}, 1.4);
    CHECK(name_at(p.song(), HarmonyScope::All, 1, 0.0) == "C");
    CHECK(name_at(p.song(), HarmonyScope::All, 1, 1.5) == "G");

    Piece waltz(1, 3, 4);
    waltz.chord(0, 0.0, {C3}, 0.9).chord(0, 1.0, {G3, E4}, 0.9).chord(0, 2.0, {G3, E4}, 0.9);
    CHECK(analyse_harmony(waltz.song(), HarmonyScope::All).size() == 1);
    CHECK(name_at(waltz.song(), HarmonyScope::All, 1) == "C");
}

TEST_CASE("harmony: drums are never part of a chord and a doubled part counts once") {
    Piece p(1);
    p.whole(0, {C3, G3, E4});
    p.note(0, 0.0, 38, 3.9, Hand::Unknown);
    for (Note& n : p.song().notes) {
        n.drum = n.pitch == 38;
    }
    CHECK(all_at(p, 1) == "C");
    // The song's own accompaniment part doubles the right hand: the same notes again.
    Piece doubled(1);
    doubled.note(0, 0.0, C4, 1.0).note(0, 1.0, D5, 1.0).note(0, 2.0, E4, 1.0);
    doubled.chord(0, 0.0, {C3, G3, E3}, 3.9);
    Song& s = doubled.song();
    const std::size_t n = s.notes.size();
    for (std::size_t i = 0; i < n; ++i) {
        Note copy = s.notes[i];
        copy.accompaniment = true;
        copy.hand = Hand::Unknown;
        s.notes.push_back(copy);
    }
    std::stable_sort(s.notes.begin(), s.notes.end(), [](const Note& a, const Note& b) {
        return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
    });
    // D5 starts once in the tune; had the copy counted, it would start twice and be taken as a
    // ninth.
    CHECK(name_at(s, HarmonyScope::All, 1) == "C");
}

// ---- Hands
// ------------------------------------------------------------------------------------------

TEST_CASE("harmony: a hand that plays only the tune names no chord") {
    Piece p(1, 3, 4);
    p.chord(0, 0.0, {C2}, 0.9, Hand::Left).chord(0, 1.0, {E3, G3}, 0.9, Hand::Left);
    p.chord(0, 2.0, {E3, G3}, 0.9, Hand::Left);
    p.note(0, 0.0, G4, 1.0).note(0, 1.0, C5, 1.0).note(0, 2.0, E5, 1.0); // C E G, one after another
    Song& s = p.song();
    CHECK(name_at(s, HarmonyScope::Left, 1) == "C");
    CHECK(name_at(s, HarmonyScope::Right, 1) == "-");
    CHECK(name_at(s, HarmonyScope::All, 1) == "C");
}

TEST_CASE("harmony: a hand that plays a chord, or comes back to its notes, names it") {
    Piece block(1);
    block.chord(0, 0.0, {C4, E4, G4}, 3.9, Hand::Right);
    CHECK(name_at(block.song(), HarmonyScope::Right, 1) == "C");
    CHECK(name_at(block.song(), HarmonyScope::Left, 1) == "-");

    Piece broken(1);
    for (int i = 0; i < 8; ++i) {
        broken.note(0, i * 0.5, std::array<int, 4>{C4, E4, G4, E4}[static_cast<std::size_t>(i) % 4],
                    0.5, Hand::Right);
    }
    CHECK(name_at(broken.song(), HarmonyScope::Right, 1) == "C");
}

TEST_CASE("harmony: two tones are not a chord and a bare fifth is a power chord, for no hand") {
    Piece third(1);
    third.chord(0, 0.0, {C4, E4}, 3.9, Hand::Right);
    CHECK(name_at(third.song(), HarmonyScope::Right, 1) == "-");
    Piece fifth(1);
    fifth.chord(0, 0.0, {C3, G3}, 3.9, Hand::Left);
    CHECK(name_at(fifth.song(), HarmonyScope::All, 1) == "C5");
    CHECK(name_at(fifth.song(), HarmonyScope::Left, 1) == "-"); // a hand names a full chord only
    Piece upside_down(1); // the root must be the lowest
    upside_down.chord(0, 0.0, {G3, C4}, 3.9, Hand::Left);
    CHECK(name_at(upside_down.song(), HarmonyScope::Left, 1) == "-");
}

TEST_CASE("harmony: a hand's chord is the same whether the other hand plays or not") {
    Piece p(1);
    p.chord(0, 0.0, {C3, G3, E3}, 3.9, Hand::Left);
    p.chord(0, 0.0, {C4, E4, G4}, 3.9, Hand::Right);
    const HarmonyTimeline t = build_harmony(p.song());
    REQUIRE(t.all.size() == 1);
    REQUIRE(t.left.size() == 1);
    REQUIRE(t.right.size() == 1);
    CHECK(harmony_name(t.left[0]) == "C");
    CHECK(harmony_name(t.right[0]) == "C");
    CHECK(&t.of(HarmonyScope::Left) == &t.left);
    CHECK(&t.of(HarmonyScope::Right) == &t.right);
    CHECK(&t.of(HarmonyScope::All) == &t.all);
}

// ---- Where the label goes
// ---------------------------------------------------------------------------

TEST_CASE("harmony: the span runs from the lowest to the highest chord tone") {
    Piece p(1);
    p.whole(0, {C3, G3, E4});             // C3 .. E4
    p.note(0, 0.0, G4, 3.9, Hand::Right); // G4 held is a chord tone too
    const HarmonyStretch s = analyse_harmony(p.song(), HarmonyScope::All).front();
    CHECK(s.low_pitch == C3);
    CHECK(s.high_pitch == G4);
}

TEST_CASE("harmony: a tune an octave above the chord does not stretch the span") {
    Piece p(1, 3, 4);
    p.chord(0, 0.0, {C2}, 0.9).chord(0, 1.0, {E3, G3}, 0.9).chord(0, 2.0, {E3, G3}, 0.9);
    p.note(0, 0.0, G4, 1.0, Hand::Right)
        .note(0, 1.0, C5, 1.0, Hand::Right)
        .note(0, 2.0, 76, 1.0, Hand::Right);
    const HarmonyStretch s = analyse_harmony(p.song(), HarmonyScope::All).front();
    CHECK(harmony_name(s) == "C");
    CHECK(s.low_pitch == C2);
    CHECK(s.high_pitch == G3); // not the E5 of the tune
}

TEST_CASE("harmony: a chord struck as a block keeps its top note in the span") {
    Piece p(1);
    p.chord(0, 0.0, {C4, E4, G4, C5}, 3.9, Hand::Right);
    const HarmonyStretch s = analyse_harmony(p.song(), HarmonyScope::All).front();
    CHECK(s.low_pitch == C4);
    CHECK(s.high_pitch == C5);
}

TEST_CASE("harmony: a stretch with no chord has no span") {
    Piece p(1);
    const std::vector<HarmonyStretch> s = analyse_harmony(p.song(), HarmonyScope::All);
    for (const HarmonyStretch& x : s) {
        CHECK(x.low_pitch == -1);
        CHECK(x.high_pitch == -1);
    }
}

TEST_CASE("harmony: confidence is high for a plain chord and lower for a muddled one") {
    Piece clear(1);
    clear.whole(0, {C3, G3, E4});
    const float good = analyse_harmony(clear.song(), HarmonyScope::All).front().confidence;
    CHECK(good > 0.9f);
    CHECK(good <= 1.0f);
    Piece muddy(1);
    muddy.whole(0, {C3, G3, E4});
    muddy.note(0, 0.0, D4, 2.0, Hand::Right)
        .note(0, 2.0, D4, 2.0, Hand::Right)
        .note(0, 0.0, 66, 2.0, Hand::Right);
    const std::vector<HarmonyStretch> s = analyse_harmony(muddy.song(), HarmonyScope::All);
    if (s.front().chord.found) {
        CHECK(s.front().confidence < good);
    }
}

// ---- Spelling from the key
// ---------------------------------------------------------------------------

TEST_CASE("harmony: root and bass are spelled from the key at that point") {
    Piece sharp(1);
    sharp.key(6);
    sharp.whole(0, {65, 70, 73, 78}); // F A# C# F#: F#maj7 with its seventh in the bass
    CHECK(all_at(sharp, 1) == "F#maj7/E#");

    Piece flat(1);
    flat.key(-1);
    flat.whole(0, {46, 53, 57}); // Bb F A: a major seventh with no third names nothing
    CHECK(all_at(flat, 1) == "-");

    Piece f_major(1);
    f_major.key(-1);
    f_major.whole(0, {46, 50, 53}); // Bb D F
    CHECK(all_at(f_major, 1) == "Bb");

    Piece d_major(1);
    d_major.key(2);
    d_major.whole(0, {54, 61, 64, 69}); // F# C# E A
    CHECK(all_at(d_major, 1) == "F#m7");
}

TEST_CASE("harmony: the key can change between bars and the spelling follows") {
    Piece p(2);
    p.song().keys.push_back(key_from_signature(0, 0, KeyMode::Major));
    p.song().keys.push_back(key_from_signature(4 * kQuarter, -5, KeyMode::Major));
    p.whole(0, {54, 58, 61}); // the same three notes, F# A# C#, in both bars
    p.whole(1, {54, 58, 61});
    const std::vector<HarmonyStretch> s = analyse_harmony(p.song(), HarmonyScope::All);
    REQUIRE(s.size() == 2);
    CHECK(harmony_name(s[0]) == "F#"); // in C major
    CHECK(harmony_name(s[1]) == "Gb"); // in D-flat major
}

TEST_CASE("harmony: a diminished seventh is rooted a semitone under the chord it resolves to") {
    Piece p(2);
    p.key(0);
    p.whole(0, {G2, 58, 64, 73}); // G Bb E C#
    p.whole(1, {D3, F3, A3});     // D F A
    CHECK(all_at(p, 1) == "C#dim7/G");
    CHECK(all_at(p, 2) == "Dm");

    Piece to_c(2); // the same notes into C major: B D F Ab is not these notes; E G Bb Db leads to F
    to_c.key(0);
    to_c.whole(0, {G2, 58, 64, 73});
    to_c.whole(1, {F3, 57, 60}); // F A C
    CHECK(all_at(to_c, 1) == "Edim7/G");
}

TEST_CASE(
    "harmony: a diminished seventh with nothing after it is rooted on the tone nearest the key") {
    Piece p(1);
    p.key(0);
    p.whole(0, {G2, 58, 64, 73});
    const HarmonyStretch s = analyse_harmony(p.song(), HarmonyScope::All).front();
    REQUIRE(s.chord.found);
    CHECK(s.chord.quality == ChordQuality::Diminished7);
    CHECK(s.chord.bass_pc == 7);
    const std::string name = harmony_name(s);
    CHECK((name == "Edim7/G" || name == "C#dim7/G"));
}
