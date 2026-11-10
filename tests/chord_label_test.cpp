// SPDX-License-Identifier: GPL-3.0-or-later
// The chord label's names (chord_label.hpp, harmony.hpp): each hand is named from its own notes
// only, for a full chord only, the file's chord symbols never used, spelled from the transposed
// key.
#include "etude/chord_label.hpp"

#include "etude/hand_split.hpp"
#include "etude/harmony.hpp"
#include "etude/midi_file.hpp"
#include "etude/toast.hpp"
#include "ui/toast_regions.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;

namespace {

constexpr std::int64_t kQuarter = 480;

// A song of `bars` bars, notes added by bar and beat (quarter notes from the bar's start), in the
// order the loaders leave it.
class Piece {
  public:
    explicit Piece(int bars, int numerator = 4, int denominator = 4)
        : bar_ticks_(kQuarter * 4 * numerator / denominator) {
        song_.tempo.ticks_per_quarter = static_cast<int>(kQuarter);
        for (int b = 0; b < bars; ++b) {
            Measure m;
            m.number = b + 1;
            m.start_tick = b * bar_ticks_;
            m.start_s = static_cast<double>(m.start_tick) / (2.0 * kQuarter); // 120 bpm
            m.numerator = static_cast<std::uint8_t>(numerator);
            m.denominator = static_cast<std::uint8_t>(denominator);
            song_.measures.push_back(m);
        }
        song_.end_tick = bars * bar_ticks_;
        song_.duration_s = static_cast<double>(song_.end_tick) / (2.0 * kQuarter);
    }
    Piece& note(int bar, double beat, int pitch, double length, Hand hand) {
        Note n;
        n.start_tick = bar * bar_ticks_ + static_cast<std::int64_t>(beat * kQuarter);
        n.end_tick = n.start_tick + static_cast<std::int64_t>(length * kQuarter);
        n.start_s = static_cast<double>(n.start_tick) / (2.0 * kQuarter);
        n.duration_s = static_cast<double>(n.end_tick - n.start_tick) / (2.0 * kQuarter);
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.hand = hand;
        n.measure = bar + 1;
        song_.notes.push_back(n);
        return *this;
    }
    // The chord struck at beats 0 and 2, each strike held two beats: every tone starts again.
    Piece& twice(int bar, std::initializer_list<int> pitches, Hand hand) {
        for (const double beat : {0.0, 2.0}) {
            for (const int p : pitches) {
                note(bar, beat, p, 1.9, hand);
            }
        }
        return *this;
    }
    // The chord held for the whole bar.
    Piece& whole(int bar, std::initializer_list<int> pitches, Hand hand) {
        for (const int p : pitches) {
            note(bar, 0.0, p, 3.9, hand);
        }
        return *this;
    }
    Piece& key(int fifths, KeyMode mode = KeyMode::Major) {
        song_.keys.push_back(key_from_signature(0, fifths, mode));
        return *this;
    }
    // A chord symbol the file writes at the start of `bar`.
    Piece& symbol(int bar, int root_pc, ChordQuality quality, int root_line) {
        ChordSymbol s;
        s.tick = bar * bar_ticks_;
        s.seconds = static_cast<double>(s.tick) / (2.0 * kQuarter);
        s.chord = Chord{true, root_pc, quality, root_pc};
        s.spelling = ChordSpelling{root_line, root_line};
        song_.chord_symbols.push_back(s);
        return *this;
    }
    Song& song() {
        std::stable_sort(song_.notes.begin(), song_.notes.end(), [](const Note& a, const Note& b) {
            return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
        });
        return song_;
    }

  private:
    Song song_;
    std::int64_t bar_ticks_;
};

// The label of a hand at the start of a bar, as text ("-" for none).
std::string label_at(const HandChordTimeline& timeline, const Song& song, int bar) {
    const int i =
        chord_label_at(timeline, song.measures[static_cast<std::size_t>(bar - 1)].start_s + 0.01);
    return i < 0 ? "-" : timeline[static_cast<std::size_t>(i)].name.plain();
}

std::string left_at(Piece& p, int bar) {
    const Song& s = p.song();
    return label_at(build_hand_chords(s, 0, false).left, s, bar);
}
std::string right_at(Piece& p, int bar) {
    const Song& s = p.song();
    return label_at(build_hand_chords(s, 0, false).right, s, bar);
}

constexpr int G2 = 43, B2 = 47, C3 = 48, D3 = 50, E3 = 52, F3 = 53, Fs3 = 54, G3 = 55, A3 = 57,
              Bb3 = 58, B3 = 59, C4 = 60, D4 = 62, E4 = 64, F4 = 65, Fs4 = 66, G4 = 67, A4 = 69,
              C5 = 72, A5 = 81;

std::string all_name(Piece& p, int bar = 1) {
    const Song& s = p.song();
    const std::vector<HarmonyStretch> stretches = analyse_harmony(s, HarmonyScope::All);
    const HarmonyStretch* h =
        harmony_at_tick(stretches, s.measures[static_cast<std::size_t>(bar - 1)].start_tick);
    return h != nullptr && h->chord.found ? harmony_name(*h) : "-";
}

Song load_song(const char* name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in.good());
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>()};
    MidiLoadResult r = load_midi(bytes);
    REQUIRE(r.ok);
    return needs_hand_split(r.song) ? with_split_hands(r.song) : r.song;
}

} // namespace

// ---- A hand is named for a full chord of its own notes -----------------------------------------

TEST_CASE("chord label: a hand that plays root, third and fifth is named") {
    Piece p(1);
    p.key(0);
    p.twice(0, {G2, B2, D3}, Hand::Left); // G B D
    CHECK(left_at(p, 1) == "G");
    CHECK(right_at(p, 1) == "-"); // the other hand plays nothing
}

TEST_CASE("chord label: a bare fifth is no chord, though the fifth is held all bar") {
    Piece p(1);
    p.key(0);
    p.whole(0, {G2, D3}, Hand::Left);
    CHECK(left_at(p, 1) == "-");
    // The same bar with the third added is named.
    Piece q(1);
    q.key(0);
    q.twice(0, {G2, D3}, Hand::Left);
    q.twice(0, {B2}, Hand::Left);
    CHECK(left_at(q, 1) == "G");
}

TEST_CASE("chord label: two notes and a melody are no chord") {
    Piece two(1);
    two.key(0);
    two.twice(0, {C3, E3}, Hand::Left); // a third alone
    CHECK(left_at(two, 1) == "-");

    Piece tune(1); // the right hand's tune: one note after another
    tune.key(0);
    tune.note(0, 0.0, D4, 1.0, Hand::Right)
        .note(0, 1.0, E4, 1.0, Hand::Right)
        .note(0, 2.0, Fs4, 1.0, Hand::Right)
        .note(0, 3.0, A4, 1.0, Hand::Right);
    CHECK(right_at(tune, 1) == "-");
}

TEST_CASE("chord label: the third and the fifth must both be there") {
    Piece no_fifth(1); // G and B twice: a third without a fifth
    no_fifth.key(0);
    no_fifth.twice(0, {G2, B2}, Hand::Left);
    CHECK(left_at(no_fifth, 1) == "-");

    Piece no_root(1); // B D F# is Bm: named for what it is, not for the G under it
    no_root.key(1);
    no_root.twice(0, {B3, D4, Fs4}, Hand::Right);
    CHECK(right_at(no_root, 1) == "Bm");
}

TEST_CASE("chord label: a note must start again or be held to count, a passing note does not") {
    // The third struck once for half a beat is a passing note: G and D alone name nothing.
    Piece passing(1);
    passing.key(0);
    passing.twice(0, {G2, D3}, Hand::Left);
    passing.note(0, 1.0, B2, 0.5, Hand::Left);
    CHECK(left_at(passing, 1) == "-");
    // Held for a beat it is the chord's third.
    Piece held(1);
    held.key(0);
    held.twice(0, {G2, D3}, Hand::Left);
    held.note(0, 1.0, B2, 1.0, Hand::Left);
    CHECK(left_at(held, 1) == "G");
}

TEST_CASE("chord label: a seventh chord needs its seventh") {
    Piece full(1);
    full.key(0);
    full.twice(0, {G2, B2, D3, F3}, Hand::Left); // G B D F
    CHECK(left_at(full, 1) == "G7");

    // A shell voicing (root, third, seventh) is G7 to the whole-song analysis, but the hand plays
    // no fifth, so the hand has no name.
    Piece shell(1);
    shell.key(0);
    shell.twice(0, {G2, B2, F3}, Hand::Left);
    CHECK(all_name(shell) == "G7");
    CHECK(left_at(shell, 1) == "-");

    // The seventh a passing note: the triad.
    Piece passing(1);
    passing.key(0);
    passing.twice(0, {G2, B2, D3}, Hand::Left);
    passing.note(0, 3.0, F3, 0.5, Hand::Left);
    CHECK(left_at(passing, 1) == "G");
}

TEST_CASE("chord label: the file's chord symbols are not used for a hand") {
    // The Study's first bar: the file says Gmaj7. The left hand plays G and D, the right hand B D
    // F#.
    Piece p(1);
    p.key(1);
    p.symbol(0, 7, ChordQuality::Major7, 1);
    p.twice(0, {G2, D3}, Hand::Left);
    p.twice(0, {B3, D4, Fs4}, Hand::Right);
    CHECK(left_at(p, 1) == "-");   // a bare fifth: not a Gmaj7
    CHECK(right_at(p, 1) == "Bm"); // B minor, not the written Gmaj7
    // The song's own naming still follows the symbol.
    const std::vector<HarmonyStretch> all = analyse_harmony(p.song(), HarmonyScope::All);
    REQUIRE_FALSE(all.empty());
    CHECK(harmony_name(all.front()) == "Gmaj7");
}

TEST_CASE("chord label: the other hand's notes never complete a hand's chord") {
    Piece p(1);
    p.key(0);
    p.twice(0, {G2, D3}, Hand::Left);
    p.twice(0, {B3}, Hand::Right); // the third is in the right hand
    CHECK(left_at(p, 1) == "-");
    CHECK(right_at(p, 1) == "-");
}

TEST_CASE("chord label: both hands play chords and each is named by its own") {
    // The Study's D bar: the left hand D F# A, the right hand A C D F# (D7 over A).
    Piece p(1);
    p.key(1);
    p.twice(0, {D3, Fs3, A3}, Hand::Left);
    p.twice(0, {A3, C4, D4, Fs4}, Hand::Right);
    CHECK(left_at(p, 1) == "D");
    CHECK(right_at(p, 1) == "D7/A");
}

// ---- A name comes from the harmony, never from the tune -----------------------------------------

TEST_CASE("chord label: a line that moves by step is a tune, whatever notes it touches") {
    // C D E F E G G over eight beats: the steps pass over C, E and G, each of them held or struck
    // again; played as a chord it would be C.
    Piece p(1, 8, 4);
    p.note(0, 0.0, C4, 1.0, Hand::Right)
        .note(0, 1.0, D4, 0.5, Hand::Right)
        .note(0, 1.5, E4, 1.0, Hand::Right)
        .note(0, 2.5, F4, 0.5, Hand::Right)
        .note(0, 3.0, E4, 1.0, Hand::Right)
        .note(0, 4.0, G4, 1.0, Hand::Right)
        .note(0, 5.0, G4, 1.0, Hand::Right);
    CHECK(right_at(p, 1) == "-");
    CHECK(all_name(p) == "C"); // the whole-song naming has no tune rule and hears a C chord
}

TEST_CASE("chord label: a line that moves by the intervals of a chord is harmony") {
    // A broken chord, C E G E C E G E, in the right hand.
    Piece broken(1);
    broken.key(0);
    const int line[] = {C4, E4, G4, E4, C4, E4, G4, E4};
    for (int i = 0; i < 8; ++i) {
        broken.note(0, 0.5 * i, line[i], 0.5, Hand::Right);
    }
    CHECK(right_at(broken, 1) == "C");
    // An Alberti bass, C G E G twice.
    Piece alberti(1);
    alberti.key(0);
    const int bass[] = {C3, G3, E3, G3, C3, G3, E3, G3};
    for (int i = 0; i < 8; ++i) {
        alberti.note(0, 0.5 * i, bass[i], 0.5, Hand::Left);
    }
    CHECK(left_at(alberti, 1) == "C");
}

TEST_CASE("chord label: a line with a few steps among its leaps is still the chord") {
    // C E G E C E G F E: two seconds in eight moves, the rest leaps.
    Piece p(1, 6, 4);
    p.key(0);
    const int line[] = {C4, E4, G4, E4, C4, E4, G4, F4, E4};
    for (int i = 0; i < 9; ++i) {
        p.note(0, 0.5 * i, line[i], 0.5, Hand::Right);
    }
    CHECK(right_at(p, 1) == "C");
}

TEST_CASE("chord label: a hand that plays chords under a top line is named without that line") {
    // A C triad struck twice with an A over it, repeated: C6 to the whole-song naming, C here.
    Piece p(1);
    p.key(0);
    p.twice(0, {C4, E4, G4}, Hand::Right);
    for (const double beat : {0.0, 1.0, 2.0, 3.0}) {
        p.note(0, beat, A5, 0.9, Hand::Right);
    }
    CHECK(all_name(p) == "C6");
    CHECK(right_at(p, 1) == "C");
    // The line stays out of the span too.
    const HandChords c = build_hand_chords(p.song(), 0, false);
    REQUIRE(c.right.size() == 1);
    CHECK(c.right[0].low_pitch == C4);
    CHECK(c.right[0].high_pitch == G4);

    // The same with the top note held once for the bar.
    Piece held(1);
    held.key(0);
    held.twice(0, {C4, E4, G4}, Hand::Right);
    held.note(0, 0.0, A5, 3.9, Hand::Right);
    CHECK(right_at(held, 1) == "C");
}

TEST_CASE("chord label: a chord's own top note, within an octave of the voice below, stays in it") {
    Piece p(1);
    p.key(0);
    p.twice(0, {C4, E4, G4, C5}, Hand::Right); // C5 is five semitones over G4
    const HandChords c = build_hand_chords(p.song(), 0, false);
    REQUIRE(c.right.size() == 1);
    CHECK(c.right[0].name.plain() == "C");
    CHECK(c.right[0].high_pitch == C5);
    // Exactly an octave over the voice below is a top line (a ninth is not): C3 E3 G3 with a G4.
    Piece octave(1);
    octave.key(0);
    octave.twice(0, {C3, E3, G3}, Hand::Right);
    octave.twice(0, {G3 + 12}, Hand::Right);
    CHECK(build_hand_chords(octave.song(), 0, false).right[0].high_pitch == G3);
    Piece ninth(1);
    ninth.key(0);
    ninth.twice(0, {C3, E3, G3}, Hand::Right);
    ninth.twice(0, {E4}, Hand::Right); // nine over G3
    CHECK(build_hand_chords(ninth.song(), 0, false).right[0].high_pitch == E4);
}

// ---- The gallery's cases ------------------------------------------------------------------------

TEST_CASE("chord label: the Lantern Waltz's G bar, left hand G = G + B + D, right hand none") {
    const Song waltz = load_song("lantern-waltz.mid");
    const HandChords chords = build_hand_chords(waltz, 0, false);
    // Bar 2 is the G bar.
    REQUIRE(waltz.measures.size() > 2);
    const Measure& bar = waltz.measures[1];
    REQUIRE(bar.number == 2);
    const int left = chord_label_at(chords.left, bar.start_s + 0.01);
    REQUIRE(left >= 0);
    const HandChordLabel& l = chords.left[static_cast<std::size_t>(left)];
    CHECK(l.name.plain() == "G");
    // The line spans the hand's G, B and D: the lowest chord note and the highest.
    CHECK(l.low_pitch >= 0);
    CHECK(l.high_pitch > l.low_pitch);
    CHECK(l.low_pitch % 12 == 7); // the G in the bass
    int tones = 0;
    for (const Note& n : waltz.notes) {
        if (n.hand == Hand::Left && n.start_s >= bar.start_s - 1e-6 &&
            n.start_s < waltz.measures[2].start_s - 1e-6) {
            tones |= 1 << (n.pitch % 12);
        }
    }
    CHECK((tones & ((1 << 7) | (1 << 11) | (1 << 2))) == ((1 << 7) | (1 << 11) | (1 << 2)));
    // The right hand plays the tune: no name anywhere in the song.
    CHECK(chords.right.empty());
}

TEST_CASE("chord label: the Study's D bar, left hand D, right hand D7/A") {
    Piece p(1);
    p.key(1);
    p.twice(0, {D3, Fs3, A3}, Hand::Left);
    p.twice(0, {A3, C4, D4, Fs4}, Hand::Right);
    const HandChords chords = build_hand_chords(p.song(), 0, false);
    REQUIRE(chords.left.size() == 1);
    REQUIRE(chords.right.size() == 1);
    CHECK(chords.left[0].name.plain() == "D");
    CHECK(chords.right[0].name.plain() == "D7/A");
    CHECK(chords.right[0].name.root == "D");
    CHECK(chords.right[0].name.quality == "7");
    CHECK(chords.right[0].name.bass == "A");
    // The spans: each hand's own chord notes.
    CHECK(chords.left[0].low_pitch == D3);
    CHECK(chords.left[0].high_pitch == A3);
    CHECK(chords.right[0].low_pitch == A3);
    CHECK(chords.right[0].high_pitch == Fs4);
}

TEST_CASE("chord label: the Study's first bar, left hand none, right hand Bm") {
    Piece p(1);
    p.key(1);
    p.symbol(0, 7, ChordQuality::Major7, 1); // the file writes Gmaj7
    p.twice(0, {G2, D3}, Hand::Left);
    p.twice(0, {B3, D4, Fs4}, Hand::Right);
    const HandChords chords = build_hand_chords(p.song(), 0, false);
    CHECK(chords.left.empty());
    REQUIRE(chords.right.size() == 1);
    CHECK(chords.right[0].name.plain() == "Bm");
}

// ---- Spelling follows the transposition ---------------------------------------------------------

TEST_CASE("chord label: names follow the transposition and are spelled from the transposed key") {
    Piece p(1);
    p.key(0); // C major
    p.twice(0, {C3, E3, G3}, Hand::Left);
    const Song& s = p.song();
    const auto name = [&](int semitones, bool flats = false) {
        const HandChords c = build_hand_chords(s, semitones, flats);
        REQUIRE(c.left.size() == 1);
        return c.left[0].name.plain();
    };
    CHECK(name(0) == "C");
    CHECK(name(2) == "D");
    CHECK(name(1) == "Db"); // D-flat major has five flats; C-sharp major seven sharps
    CHECK(name(-1) == "B"); // B major has five sharps; C-flat seven flats
    CHECK(name(3) == "Eb");
    CHECK(name(6) == "F#");       // F-sharp and G-flat major tie: sharps...
    CHECK(name(6, true) == "Gb"); // ...unless the look spells with flats
    // The span keeps the file's pitches; the keys they show are chord_label_keys.
    const HandChords up = build_hand_chords(s, 5, false);
    CHECK(up.left[0].low_pitch == C3);
    CHECK(up.left[0].high_pitch == G3);
}

TEST_CASE("chord label keys: the label's line spans the keys its notes' blocks use") {
    HandChordLabel label;
    label.low_pitch = C3;
    label.high_pitch = G3;
    const KeyRange all{36, 96};
    CHECK(chord_label_keys(label, Transposition(0, all), 36, 96) == ChordKeys{C3, G3});
    // Shifted with the song.
    CHECK(chord_label_keys(label, Transposition(5, all), 36, 96) == ChordKeys{C3 + 5, G3 + 5});
    CHECK(chord_label_keys(label, Transposition(-12, all), 36, 96) == ChordKeys{C3 - 12, G3 - 12});
    // Folded by whole octaves onto a short keyboard, like the blocks: C3 and G3 land on 60 and 67.
    const KeyRange octave{60, 71};
    CHECK(chord_label_keys(label, Transposition(0, octave), 60, 71) == ChordKeys{60, 67});
    // Folding can turn the span over (B3 goes up to 71, D4 stays): the lower key comes first.
    HandChordLabel over;
    over.low_pitch = B3;
    over.high_pitch = D4;
    CHECK(chord_label_keys(over, Transposition(0, octave), 60, 71) == ChordKeys{62, 71});
    // A pitch class the keyboard has no key for: no keys.
    CHECK_FALSE(chord_label_keys(label, Transposition(0, KeyRange{60, 62}), 60, 62).has_value());
    // The view shows part of the keyboard: a span cut to it, none when wholly outside it.
    HandChordLabel wide;
    wide.low_pitch = C3;
    wide.high_pitch = F4;
    CHECK(chord_label_keys(wide, Transposition(0, all), 60, 72) == ChordKeys{60, F4});
    CHECK_FALSE(chord_label_keys(label, Transposition(0, all), 60, 72).has_value());
    CHECK_FALSE(chord_label_keys(label, Transposition(0, all), 36, 40).has_value());
    // Exactly at the view's edge counts as in.
    CHECK(chord_label_keys(label, Transposition(0, all), G3, 90) == ChordKeys{G3, G3});
    // No span, no keys.
    CHECK_FALSE(chord_label_keys(HandChordLabel{}, Transposition(0, all), 36, 96).has_value());
}

TEST_CASE("chord label: the seventh and the bass are spelled from the transposed key too") {
    Piece p(1);
    p.key(-1, KeyMode::Major); // F major
    p.twice(0, {C3, E3, G3, Bb3}, Hand::Left);
    const Song& s = p.song();
    const HandChords c = build_hand_chords(s, 0, false);
    REQUIRE(c.left.size() == 1);
    CHECK(c.left[0].name.plain() == "C7");
    // A chord over its third shows its bass spelled from the root, in the transposed key.
    Piece q(1);
    q.key(0);
    q.twice(0, {E3, G3, C4}, Hand::Left); // E G C: C over E
    const HandChords inv = build_hand_chords(q.song(), 1, false);
    REQUIRE(inv.left.size() == 1);
    CHECK(inv.left[0].name.plain() == "Db/F");
}

// ---- The timeline -------------------------------------------------------------------------------

namespace {

HarmonyStretch stretch(double a, double b, HarmonyState state, int root_pc = 0, int root_line = 0,
                       ChordQuality quality = ChordQuality::Major, int low = 48, int high = 55,
                       double hold = -1.0) {
    HarmonyStretch s;
    s.start_s = a;
    s.end_s = b;
    s.start_tick = static_cast<std::int64_t>(a * 960);
    s.end_tick = static_cast<std::int64_t>(b * 960);
    s.state = state;
    if (state == HarmonyState::Chord) {
        s.chord = Chord{true, root_pc, quality, root_pc};
        s.spelling = ChordSpelling{root_line, root_line};
        s.low_pitch = low;
        s.high_pitch = high;
        s.hold_end_s = hold < 0.0 ? b : hold;
    }
    return s;
}

} // namespace

TEST_CASE("chord label timeline: no chord hides the label, an unclear stretch keeps it only to the "
          "hold") {
    const std::vector<HarmonyStretch> in{
        stretch(0, 1, HarmonyState::Chord, 0, 0, ChordQuality::Major, 48, 55, 2.0), // C, held to 2
        stretch(1, 2, HarmonyState::Unclear), // a rest inside the hold: C stays
        stretch(2, 3, HarmonyState::NoChord), // a tune: nothing
        stretch(3, 4, HarmonyState::Unclear), // a rest after no chord: nothing
        stretch(4, 5, HarmonyState::Chord, 7, 1),
    };
    const HandChordTimeline t = hand_chord_timeline(in, {}, 0, false);
    REQUIRE(t.size() == 2);
    CHECK(t[0].name.plain() == "C");
    CHECK(t[0].start_s == 0.0);
    CHECK(t[0].end_s == 2.0); // the rest is part of the chord up to the hold
    CHECK(t[1].name.plain() == "G");
    CHECK(t[1].start_s == 4.0);
    CHECK(chord_label_at(t, 1.5) == 0);
    CHECK(chord_label_at(t, 2.5) == -1);
    CHECK(chord_label_at(t, 3.5) == -1);
    CHECK(chord_label_at(t, 4.0) == 1);
}

TEST_CASE("chord label timeline: a rest keeps the name only to the hold, then hides it") {
    // A chord for a second, then four seconds of rests: the hold ends half a second after it.
    const std::vector<HarmonyStretch> in{
        stretch(0, 1, HarmonyState::Chord, 0, 0, ChordQuality::Major, 48, 55, 1.5),
        stretch(1, 2, HarmonyState::Unclear),
        stretch(2, 3, HarmonyState::Unclear),
        stretch(3, 4, HarmonyState::Unclear),
        stretch(4, 5, HarmonyState::Unclear),
    };
    const HandChordTimeline t = hand_chord_timeline(in, {}, 0, false);
    REQUIRE(t.size() == 1);
    CHECK(t[0].end_s == 1.5);
    CHECK(chord_label_at(t, 1.49) == 0);
    CHECK(chord_label_at(t, 1.5) == -1); // exactly at the hold: gone
    CHECK(chord_label_at(t, 4.5) == -1);
    // A chord with no hold set keeps nothing past its own stretch.
    const std::vector<HarmonyStretch> bare{stretch(0, 1, HarmonyState::Chord),
                                           stretch(1, 2, HarmonyState::Unclear)};
    CHECK(hand_chord_timeline(bare, {}, 0, false)[0].end_s == 1.0);
}

TEST_CASE(
    "chord label: the left hand's G in bar 1 and four silent bars is a name for bar 1 and a beat") {
    Piece p(6);
    p.key(0);
    p.twice(0, {G2, B2, D3}, Hand::Left);
    p.twice(5, {C3, E3, G3}, Hand::Right); // the song goes on, the left hand rests
    const Song& s = p.song();
    const HandChords c = build_hand_chords(s, 0, false);
    REQUIRE(c.left.size() == 1);
    CHECK(c.left[0].name.plain() == "G");
    // Bar 1 is two seconds at 120 bpm; the name holds a beat (half a second) into the rest.
    CHECK(c.left[0].end_s == doctest::Approx(2.5));
    CHECK(chord_label_at(c.left, 2.4) == 0);
    CHECK(chord_label_at(c.left, 2.6) == -1);
    CHECK(chord_label_at(c.left, 6.0) == -1);
    CHECK(chord_label_at(c.left, 9.0) == -1);
}

TEST_CASE("chord label: half a bar of rest after a chord in the same bar keeps the name") {
    Piece p(2);
    p.key(0);
    // The chord struck twice in the first beats, the second half bar silent.
    for (const double beat : {0.0, 1.0}) {
        for (const int pitch : {G2, B2, D3}) {
            p.note(0, beat, pitch, 0.9, Hand::Left);
        }
    }
    p.note(1, 0.0, C4, 0.9, Hand::Right);
    const Song& s = p.song();
    const HandChords c = build_hand_chords(s, 0, false);
    REQUIRE(c.left.size() == 1);
    CHECK(c.left[0].end_s >= 2.0 - 1e-9); // through the rest of its bar
}

TEST_CASE("chord label timeline: stretches with the same label and keys are one") {
    const std::vector<HarmonyStretch> same{
        stretch(0, 1, HarmonyState::Chord),
        stretch(1, 2, HarmonyState::Chord),
    };
    CHECK(hand_chord_timeline(same, {}, 0, false).size() == 1);
    // The same chord over other keys is another label: it fades, it never glides.
    const std::vector<HarmonyStretch> moved{
        stretch(0, 1, HarmonyState::Chord, 0, 0, ChordQuality::Major, 48, 55),
        stretch(1, 2, HarmonyState::Chord, 0, 0, ChordQuality::Major, 60, 67),
    };
    CHECK(hand_chord_timeline(moved, {}, 0, false).size() == 2);
}

TEST_CASE("chord label timeline: where a label starts and ends") {
    const std::vector<HarmonyStretch> in{stretch(1.0, 2.0, HarmonyState::Chord)};
    const HandChordTimeline t = hand_chord_timeline(in, {}, 0, false);
    CHECK(chord_label_at(t, 0.999) == -1);
    CHECK(chord_label_at(t, 1.0) == 0); // the start is in
    CHECK(chord_label_at(t, 1.999) == 0);
    CHECK(chord_label_at(t, 2.0) == -1); // the end is out
    CHECK(chord_label_at({}, 1.0) == -1);
}

TEST_CASE("chord label: the hands' labels of a song equal the labels of its harmony") {
    Piece p(2);
    p.key(1);
    p.twice(0, {Fs3, D3, A3}, Hand::Left);
    p.twice(0, {A3, C4, D4, Fs4}, Hand::Right);
    p.twice(1, {G2, B2, D3}, Hand::Left);
    const Song& s = p.song();
    const HarmonyTimeline harmony = build_hand_harmony(s);
    CHECK(harmony.all.empty());
    CHECK(hand_chords_of(harmony, s.keys, 3, false) == build_hand_chords(s, 3, false));
    CHECK(build_hand_chords(s, 0, false).of(Hand::Left).size() == 2);
    CHECK(build_hand_chords(s, 0, false).of(Hand::Right).size() == 1);
}

TEST_CASE("chord label: a song with no notes has no labels") {
    const Song empty;
    const HandChords c = build_hand_chords(empty, 0, false);
    CHECK(c.left.empty());
    CHECK(c.right.empty());
}
