// SPDX-License-Identifier: GPL-3.0-or-later
// harmony: the first topics (split from harmony_test.cpp).
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

// ---- Weighting ----------------------------------------------------------------------------------

TEST_CASE("harmony: short off-beat passing notes do not change the name") {
    Piece p(1);
    p.whole(0, {C3, G3, E4});
    p.note(0, 1.5, D4, 0.25).note(0, 2.5, A4, 0.25); // passing notes in the tune
    CHECK(all_at(p, 1) == "C");
}

TEST_CASE("harmony: a seventh needs more than one short note") {
    Piece lone(1);
    lone.whole(0, {C3, G3, E4});
    lone.note(0, 3.0, 70, 0.5, Hand::Right); // one Bb for an eighth
    CHECK(all_at(lone, 1) == "C");

    Piece backed(1); // the Bb comes back, as the seventh of a broken chord does
    backed.whole(0, {C3, G3, E4});
    backed.note(0, 1.0, 70, 0.5).note(0, 3.0, 70, 0.5);
    CHECK(all_at(backed, 1) == "C7");

    Piece held(1); // or it is held
    held.whole(0, {C3, G3, E4});
    held.note(0, 0.0, 70, 3.9, Hand::Right);
    CHECK(all_at(held, 1) == "C7");
}

TEST_CASE("harmony: a pitch class that starts once counts from three quarters of a beat") {
    // C and G in the left hand, an E on beat 2 that sounds 0.7 beats (a passing note: C and G make
    // a bare fifth) or 0.75 beats (the third of the chord).
    Piece short_e(1);
    short_e.whole(0, {C3, G3});
    short_e.note(0, 1.0, E4, 0.7);
    CHECK(all_at(short_e, 1) == "C5");
    Piece long_e(1);
    long_e.whole(0, {C3, G3});
    long_e.note(0, 1.0, E4, 0.75);
    CHECK(all_at(long_e, 1) == "C");
    // Two short starts of the same pitch class come back, so they count.
    Piece again(1);
    again.whole(0, {C3, G3});
    again.note(0, 1.0, E4, 0.25).note(0, 3.0, E4, 0.25);
    CHECK(all_at(again, 1) == "C");
}

TEST_CASE("harmony: a low note of real weight is the bass and a chord that most of the weight "
          "misses names nothing") {
    Piece low(1);
    low.whole(0, {E3, G3, C4});
    low.note(0, 0.0, C2, 2.0, Hand::Left); // a bass C held two beats under the same chord
    CHECK(all_at(low, 1) == "C");

    Piece smeared(
        1); // a chord for one beat under a cluster held all bar: most of the weight is not its
    smeared.chord(0, 0.0, {C3, E3, G3}, 1.0, Hand::Left);
    smeared.chord(0, 0.0, {61, 62, 63}, 3.9, Hand::Right);
    CHECK(all_at(smeared, 1) == "-");
}

TEST_CASE(
    "harmony: notes that overlap a quarter of a beat sound together, a legato slip does not") {
    // A right-hand arpeggio, each note struck a beat after the last. Held 1.1 beats each, they
    // overlap by 0.1 and the hand plays a tune; held 1.25 beats they overlap by a quarter beat
    // and the hand plays a chord.
    Piece slip(1);
    slip.note(0, 0.0, C4, 1.1).note(0, 1.0, E4, 1.1).note(0, 2.0, G4, 1.1);
    CHECK(name_at(slip.song(), HarmonyScope::Right, 1) == "-");
    Piece legato(1);
    legato.note(0, 0.0, C4, 1.25).note(0, 1.0, E4, 1.25).note(0, 2.0, G4, 1.25);
    CHECK(name_at(legato.song(), HarmonyScope::Right, 1) == "C");
}

TEST_CASE("harmony: an attack counts once however many octaves it strikes") {
    // E struck in two octaves at once is one attack, so a short one is still a passing note; the
    // same two struck apart come back, and count.
    Piece together(1);
    together.whole(0, {C3, G3});
    together.note(0, 1.0, E4, 0.5).note(0, 1.0, 76, 0.5);
    CHECK(all_at(together, 1) == "C5");
    Piece apart(1);
    apart.whole(0, {C3, G3});
    apart.note(0, 1.0, E4, 0.5).note(0, 3.0, 76, 0.5);
    CHECK(all_at(apart, 1) == "C");
}

TEST_CASE("harmony: an extension held for a beat and a half is part of the chord") {
    // Gmaj7 for Satie's waltz: the F# is struck once but held two beats.
    Piece held(1, 3, 4);
    held.chord(0, 0.0, {43}, 3.0, Hand::Left);
    held.chord(0, 1.0, {B3, D4, G4, 74}, 2.0, Hand::Left);
    held.note(0, 1.0, 66, 2.0);
    CHECK(all_at(held, 1) == "Gmaj7");
    Piece brief(1, 3, 4); // the same F# for one beat is a passing note
    brief.chord(0, 0.0, {43}, 3.0, Hand::Left);
    brief.chord(0, 1.0, {B3, D4, G4, 74}, 2.0, Hand::Left);
    brief.note(0, 1.0, 66, 1.0);
    CHECK(all_at(brief, 1) == "G");
}

TEST_CASE("harmony: a sixth chord with its sixth in the bass is read as a triad on the bass") {
    // B D F# with an A in the tune: Bm, not D6/B.
    Piece p(1);
    p.chord(0, 0.0, {35}, 3.9, Hand::Left);
    p.chord(0, 0.0, {B3, D4, 66}, 3.9, Hand::Left);
    p.note(0, 1.0, A4, 1.0).note(0, 2.0, 71, 1.0);
    CHECK(all_at(p, 1) == "Bm");
    // With the A held and struck again it is the seventh chord on that bass.
    Piece seventh(1);
    seventh.chord(0, 0.0, {35}, 3.9, Hand::Left);
    seventh.chord(0, 0.0, {B3, D4, 66}, 3.9, Hand::Left);
    seventh.note(0, 0.0, A4, 3.9, Hand::Right);
    CHECK(all_at(seventh, 1) == "Bm7");
}

TEST_CASE("harmony: a root that is only a short chord tone above the bass is not a root") {
    // Gm for three beats and an Eb chord on the last: the bar is Gm, not Ebmaj7/G.
    Piece p(1);
    p.chord(0, 0.0, {31}, 3.0, Hand::Left);
    p.chord(0, 0.0, {58, 62}, 3.0, Hand::Left);
    p.chord(0, 3.0, {39, 55}, 0.9, Hand::Left);
    CHECK(all_at(p, 1) == "Gm");
}

TEST_CASE("harmony: five-eight counts quarter notes, so an eighth is as short there as anywhere") {
    Piece eighth(1, 5, 8);
    eighth.whole(0, {C3, G3});
    eighth.note(0, 1.0, E4, 0.5); // an eighth: half a quarter, a passing note
    CHECK(all_at(eighth, 1) == "C5");
    Piece quarter(1, 5, 8);
    quarter.whole(0, {C3, G3});
    quarter.note(0, 1.0, E4, 1.0);
    CHECK(all_at(quarter, 1) == "C");
}

TEST_CASE("harmony: a dense bar is analysed in a single pass") {
    Piece p(1);
    p.whole(0, {C3});
    const int pitches[3] = {C4, E4, G4};
    for (int i = 0; i < 6000; ++i) {
        p.note(0, 3.9 * i / 6000.0, pitches[i % 3], 0.5);
    }
    CHECK(all_at(p, 1) == "C");
}

TEST_CASE("harmony: a passing sixth or ninth in the tune stays a triad") {
    Piece p(1);
    p.whole(0, {F3, A3, C4});
    p.note(0, 0.0, A4, 1.0)
        .note(0, 1.0, D5, 1.0)
        .note(0, 2.0, 77, 2.0); // A D F: the D is the tune's
    CHECK(all_at(p, 1) == "F");
}

// ---- Naming
// ---------------------------------------------------------------------------------------

TEST_CASE("harmony: a broken chord in one hand is named whole") {
    Piece p(1);
    for (int i = 0; i < 8; ++i) {
        const int pitch = std::array<int, 4>{C4, E4, G4, E4}[static_cast<std::size_t>(i) % 4];
        p.note(0, i * 0.5, pitch, 0.5);
    }
    CHECK(all_at(p, 1) == "C");
}

TEST_CASE("harmony: the bass is the lowest note of real weight") {
    Piece inverted(1);
    inverted.whole(0, {E3, G3, C4});
    CHECK(all_at(inverted, 1) == "C/E");

    Piece passing(1); // a short low note on the off-beat is not the bass
    passing.whole(0, {C3, G3, E4});
    passing.note(0, 1.5, 38, 0.25, Hand::Left);
    CHECK(all_at(passing, 1) == "C");

    Piece seventh(1);
    seventh.whole(0, {B3, D4, F4, G4});
    CHECK(all_at(seventh, 1) == "G7/B");
}

TEST_CASE("harmony: the bass must be a tone of the chord to be named after the slash") {
    Piece p(1);
    p.whole(0, {C3, E4, G4});            // C E G over C
    p.note(0, 0.0, 42, 3.9, Hand::Left); // F#2 held under it: not a tone of any chord with these
    CHECK(all_at(p, 1) == "C");
}

TEST_CASE("harmony: the new qualities are found from the notes") {
    Piece sus(1);
    sus.whole(0, {C3, F3, G3, 58});
    CHECK(all_at(sus, 1) == "C7sus4");
    Piece flat9(1);
    flat9.whole(0, {C3, E3, 58, 61});
    CHECK(all_at(flat9, 1) == "C7b9");
    Piece sixnine(1);
    sixnine.whole(0, {C3, E3, G3, A3, D4});
    CHECK(all_at(sixnine, 1) == "C6/9");
    Piece thirteen(1);
    thirteen.whole(0, {C3, E3, G3, 58, A3, D4});
    CHECK(all_at(thirteen, 1) == "C13");
}

TEST_CASE("harmony: a bar of single notes names no chord") {
    Piece p(1);
    p.note(0, 0.0, C4, 1.0).note(0, 1.0, D4, 1.0).note(0, 2.0, E4, 1.0).note(0, 3.0, G4, 1.0);
    CHECK(all_at(p, 1) == "-");
    Piece rest(1);
    CHECK(all_at(rest, 1) == "-");
}

TEST_CASE("harmony: a stretch keeps the name of the chord it holds across bars") {
    Piece p(4);
    for (int b = 0; b < 4; ++b) {
        p.whole(b, {C3, G3, E4});
    }
    const std::vector<HarmonyStretch> s = analyse_harmony(p.song(), HarmonyScope::All);
    REQUIRE(s.size() == 1);
    CHECK(s[0].start_tick == 0);
    CHECK(s[0].end_tick == 4 * 4 * kQuarter);
    CHECK(s[0].start_s == doctest::Approx(0.0));
    CHECK(s[0].end_s == doctest::Approx(8.0));
    CHECK(harmony_name(s[0]) == "C");
}

TEST_CASE("harmony: stretches end where the next begins and a gap is a stretch with no chord") {
    Piece p(3);
    p.whole(0, {C3, G3, E4});
    p.whole(2, {G2, D3, B3});
    const std::vector<HarmonyStretch> s = analyse_harmony(p.song(), HarmonyScope::All);
    REQUIRE(s.size() == 3);
    CHECK(s[0].end_tick == s[1].start_tick);
    CHECK(s[1].end_tick == s[2].start_tick);
    CHECK_FALSE(s[1].chord.found);
    CHECK(s[1].state == HarmonyState::Unclear); // a rest says nothing either way
    CHECK(harmony_name(s[1]).empty());
    CHECK(harmony_name(s[2]) == "G");
}

// ---- Half bars and hysteresis
// ----------------------------------------------------------------------

TEST_CASE("harmony: a bar splits where its halves clearly name different chords") {
    Piece p(1);
    p.chord(0, 0.0, {C3, G3, E4}, 1.9);
    p.chord(0, 2.0, {G2, D3, B3, G4}, 1.9);
    CHECK(all_at(p, 1, 0.0) == "C");
    CHECK(all_at(p, 1, 2.0) == "G");
    const std::vector<HarmonyStretch> s = analyse_harmony(p.song(), HarmonyScope::All);
    REQUIRE(s.size() == 2);
    CHECK(s[0].end_tick == 2 * kQuarter);
}

TEST_CASE("harmony: a bar of one chord is never split") {
    Piece p(1);
    p.chord(0, 0.0, {C3, G3, E4}, 1.9);
    p.chord(0, 2.0, {C3, G3, E4}, 1.9);
    CHECK(analyse_harmony(p.song(), HarmonyScope::All).size() == 1);
}

TEST_CASE("harmony: a faint change in the second half does not rename the bar") {
    // C for the bar, with one short F and A after the chord: the name stays C.
    Piece p(1);
    p.whole(0, {C3, G3, E4});
    p.note(0, 2.5, F4, 0.25).note(0, 3.5, A4, 0.25);
    CHECK(all_at(p, 1, 0.0) == "C");
    CHECK(all_at(p, 1, 2.0) == "C");
    CHECK(analyse_harmony(p.song(), HarmonyScope::All).size() == 1);
}

TEST_CASE("harmony: a new chord that fits only a little better does not take over") {
    // The same four notes C E G A: over A they are Am7, over C they are C6. Alone, the bass C
    // gives C6; after a bar of Am7 the name stays Am7 (with the new bass) since C6 is not clearly
    // better.
    Piece fresh(1);
    fresh.whole(0, {C3, G3, E4, A4});
    CHECK(all_at(fresh, 1) == "C6");

    Piece p(2);
    p.whole(0, {45, G3, E4, C4}); // A2 under C E G
    p.whole(1, {C3, G3, E4, A4});
    CHECK(all_at(p, 1) == "Am7");
    CHECK(all_at(p, 2) == "Am7/C");
}

TEST_CASE("harmony: a clear change in a later bar renames it") {
    Piece p(2);
    p.whole(0, {C3, G3, E4});
    p.whole(1, {F3, C4, 69});
    CHECK(all_at(p, 1) == "C");
    CHECK(all_at(p, 2) == "F");
}

TEST_CASE("harmony: names change at most once a bar") {
    Piece p(8);
    const std::array<std::array<int, 3>, 4> chords{
        {{C3, G3, E4}, {F3, C4, 69}, {G2, D3, 71}, {C3, G3, E4}}};
    for (int b = 0; b < 8; ++b) {
        const auto& c = chords[static_cast<std::size_t>(b) % 4];
        p.whole(b, {c[0], c[1], c[2]});
    }
    const std::vector<HarmonyStretch> s = analyse_harmony(p.song(), HarmonyScope::All);
    // C F G C | C F G C: the two C bars between the cycles are one stretch.
    CHECK(s.size() == 7);
    for (std::size_t i = 1; i < s.size(); ++i) {
        CHECK(s[i].start_tick == s[i - 1].end_tick);
        CHECK_FALSE(s[i].chord == s[i - 1].chord);
    }
}

// ---- Rests and tunes: "unclear" is not "no chord"
// --------------------------------------------------

TEST_CASE("harmony: a chord held across a rest or a lone note is one stretch") {
    Piece rest(3);
    rest.whole(0, {G2, D3, B3});
    rest.whole(2, {G2, D3, B3});
    const std::vector<HarmonyStretch> s = analyse_harmony(rest.song(), HarmonyScope::All);
    REQUIRE(s.size() == 1);
    CHECK(harmony_name(s[0]) == "G");
    CHECK(s[0].state == HarmonyState::Chord);
    CHECK(s[0].end_tick == 3 * 4 * kQuarter);

    Piece note(3); // a single tune note in the middle bar
    note.whole(0, {G2, D3, B3});
    note.note(1, 1.0, 71, 0.5);
    note.whole(2, {G2, D3, B3});
    CHECK(analyse_harmony(note.song(), HarmonyScope::All).size() == 1);

    Piece half(2); // half a bar of silence inside a bar
    half.whole(0, {G2, D3, B3});
    half.chord(1, 0.0, {G2, D3, B3}, 1.9);
    CHECK(analyse_harmony(half.song(), HarmonyScope::All).size() == 1);
}

TEST_CASE("harmony: a bar of one note in octaves says there is no chord, a rest says nothing") {
    Piece p(3);
    p.whole(0, {G2, D3, B3});
    for (const int pitch : {55, 67, 79}) {
        p.note(1, 0.0, pitch, 3.9);
    }
    p.whole(2, {G2, D3, B3});
    const std::vector<HarmonyStretch> s = analyse_harmony(p.song(), HarmonyScope::All);
    REQUIRE(s.size() == 3);
    CHECK(s[1].state == HarmonyState::NoChord);
    CHECK_FALSE(s[1].chord.found);

    Piece silent(3);
    silent.whole(0, {G2, D3, B3});
    const std::vector<HarmonyStretch> q = analyse_harmony(silent.song(), HarmonyScope::All);
    REQUIRE(q.size() == 2);
    CHECK(q[0].state == HarmonyState::Chord);
    CHECK(q[1].state == HarmonyState::Unclear);
}

TEST_CASE("harmony: the held chord keeps its hold through a rest but not through a tune") {
    // Am7 (A C E G), a bar of rest, then the same four notes over C: C6 alone, Am7/C when Am7 is
    // held.
    Piece rested(3);
    rested.whole(0, {45, G3, E4, C4});
    rested.whole(2, {C3, G3, E4, A4});
    CHECK(all_at(rested, 3) == "Am7/C");

    Piece tune(3); // the same, with a bar of one note in octaves between: the hold is let go
    tune.whole(0, {45, G3, E4, C4});
    for (const int pitch : {55, 67, 79}) {
        tune.note(1, 0.0, pitch, 3.9);
    }
    tune.whole(2, {C3, G3, E4, A4});
    CHECK(all_at(tune, 3) == "C6");
}

TEST_CASE("harmony: a hand that rests is unclear, a hand that plays a tune has no chord") {
    Piece p(2, 3, 4);
    p.chord(0, 0.0, {C2}, 0.9, Hand::Left).chord(0, 1.0, {E3, G3}, 0.9, Hand::Left);
    p.chord(0, 2.0, {E3, G3}, 0.9, Hand::Left);
    p.note(0, 0.0, G4, 1.0).note(0, 1.0, C5, 1.0).note(0, 2.0, E5, 1.0);
    p.note(1, 0.0, G4, 3.0);
    const std::vector<HarmonyStretch> left = analyse_harmony(p.song(), HarmonyScope::Left);
    const std::vector<HarmonyStretch> right = analyse_harmony(p.song(), HarmonyScope::Right);
    CHECK(left.front().state == HarmonyState::Chord);
    CHECK(left.back().state == HarmonyState::Unclear); // the left hand rests in bar 2
    CHECK(right.front().state == HarmonyState::NoChord);
}
