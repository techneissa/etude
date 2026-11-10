// SPDX-License-Identifier: GPL-3.0-or-later
// A labelled corpus for the harmony analysis (harmony.hpp): real songs with the chord of each bar
// as a musician reads it, checked against the notes. Accuracy and the number of times the name
// changes are asserted.
#include "etude/harmony.hpp"

#include "smf_builder.hpp"

#include "etude/hand_split.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;
using etude::test::Bytes;

namespace {

Song load_song(const char* name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in.good());
    const Bytes bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    Song song;
    if (bytes.size() >= 4 && bytes[0] == 'M' && bytes[1] == 'T') {
        MidiLoadResult r = load_midi(bytes);
        REQUIRE(r.ok);
        song = std::move(r.song);
    } else {
        ScoreLoadResult r = load_score(bytes);
        REQUIRE(r.ok);
        song = std::move(r.song);
    }
    return needs_hand_split(song) ? with_split_hands(song) : song;
}

// One labelled bar: the names a musician gives it in order ("C/G|G" is C/G, then G; "-" is no
// chord).
struct Bar {
    int number;
    const char* expected;
    const char* also = ""; // another reading of the same notes that is as right
};

// What a labelled set must reach: this share of its bars named as labelled, and no more name
// changes than this per bar (the label must not flicker).
constexpr double kGoalAccuracy = 0.90;
constexpr double kMostChangesPerBar = 1.2;

// The names that sound in a bar, in order, one for each change.
std::string names_in_bar(const Song& song, const std::vector<HarmonyStretch>& stretches,
                         std::size_t bar) {
    const std::int64_t start = song.measures[bar].start_tick;
    const std::int64_t end =
        bar + 1 < song.measures.size() ? song.measures[bar + 1].start_tick : song.end_tick;
    std::string out;
    std::string last;
    for (const HarmonyStretch& s : stretches) {
        if (s.end_tick <= start || s.start_tick >= end) {
            continue;
        }
        const std::string name = s.chord.found ? harmony_name(s) : "-";
        if (name != last) {
            out += out.empty() ? "" : "|";
            out += name;
            last = name;
        }
    }
    return out;
}

struct Score {
    int right = 0;
    int total = 0;
    double changes_per_bar = 0.0;
    std::vector<std::string> wrong;
};

Score score_corpus(const char* file, HarmonyScope scope, const std::vector<Bar>& bars, bool print) {
    const Song song = load_song(file);
    const std::vector<HarmonyStretch> stretches = analyse_harmony(song, scope);
    Score score;
    if (print) {
        std::printf("\n%s (%zu bars labelled)\n  bar | expected | got\n", file, bars.size());
    }
    for (const Bar& b : bars) {
        std::size_t index = 0;
        while (index < song.measures.size() && song.measures[index].number != b.number) {
            ++index;
        }
        REQUIRE(index < song.measures.size());
        const std::string got = names_in_bar(song, stretches, index);
        const bool ok = got == b.expected || (b.also[0] != '\0' && got == b.also);
        ++score.total;
        score.right += ok ? 1 : 0;
        if (!ok) {
            score.wrong.push_back("bar " + std::to_string(b.number) + ": expected " + b.expected +
                                  ", got " + got);
        }
        if (print) {
            std::printf("  %3d | %-12s | %s%s\n", b.number, b.expected, got.c_str(),
                        ok ? "" : "   <-- wrong");
        }
    }
    // Name changes per bar over the labelled range: how often the label would flicker.
    int changes = 0;
    std::string previous;
    int bar_count = 0;
    for (std::size_t i = 0; i < song.measures.size(); ++i) {
        const int number = song.measures[i].number;
        if (number < bars.front().number || number > bars.back().number) {
            continue;
        }
        ++bar_count;
        std::string current;
        std::string seen;
        const std::int64_t start = song.measures[i].start_tick;
        const std::int64_t end =
            i + 1 < song.measures.size() ? song.measures[i + 1].start_tick : song.end_tick;
        for (const HarmonyStretch& s : stretches) {
            if (s.end_tick <= start || s.start_tick >= end) {
                continue;
            }
            const std::string name = s.chord.found ? harmony_name(s) : "-";
            if (name != previous) {
                ++changes;
            }
            previous = name;
        }
    }
    score.changes_per_bar = bar_count > 0 ? static_cast<double>(changes) / bar_count : 0.0;
    if (print) {
        std::printf("  accuracy %d/%d = %.1f%%, name changes per bar %.2f\n", score.right,
                    score.total, 100.0 * score.right / score.total, score.changes_per_bar);
    }
    return score;
}

bool print_table() { return std::getenv("ETUDE_HARMONY_TABLE") != nullptr; }

// Bach, Prelude in C major BWV 846, bars 1 to 16, the textbook harmony checked against the notes of
// each bar (bar 1: C E G C E; bar 2: C D A D F, Dm7 over C; bar 3: B D G D F, G7 over B; ...). The
// diminished sevenths of bars 12 and 14 read the same from each tone; the root is the tone a
// semitone under the next bar's chord (C# into Dm, B into C).
const std::vector<Bar> kBach{
    {1, "C"},     {2, "Dm7/C"},    {3, "G7/B"}, {4, "C"},        {5, "Am/C"}, {6, "D7/C"},
    {7, "G/B"},   {8, "Cmaj7/B"},  {9, "Am7"},  {10, "D7"},      {11, "G"},   {12, "C#dim7/G"},
    {13, "Dm/F"}, {14, "Bdim7/F"}, {15, "C/E"}, {16, "Fmaj7/E"},
};

// The Lantern Waltz, the 3/4 tune of tests/data: a bass note and two chord tones in the left hand,
// the tune in the right.
const std::vector<Bar> kLantern{
    {1, "C"}, {2, "G"}, {3, "F"}, {4, "C"},  {5, "F"},  {6, "C"},
    {7, "G"}, {8, "C"}, {9, "C"}, {10, "G"}, {11, "G"}, {12, "C"},
};

// Beethoven, Op. 48 no. 4 (a hymn in 2/2 with two or three harmonies a bar). The bars chosen are
// those whose harmony is fixed by the notes; the bars of single notes in octaves (3, 7, 8, 17, 33,
// 34) name nothing, since they hold no chord. Bar 9 and 26 are the augmented-sixth chord, which a
// lead sheet writes as a dominant seventh on the flat sixth.
const std::vector<Bar> kBeethoven{
    {1, "C"},     {2, "C"},      {3, "-"},      {4, "C"},     {5, "C"},      {6, "C/G|G"},
    {7, "-"},     {8, "-"},      {9, "Ab7|G7"}, {11, "E"},    {12, "Am"},    {13, "F7"},
    {14, "Bb"},   {15, "D7/F#"}, {16, "Gm"},    {17, "-"},    {18, "Eb/G"},  {19, "Eb"},
    {20, "Eb"},   {21, "Eb"},    {22, "Eb"},    {23, "G7/D"}, {24, "G7/D"},  {25, "Cm"},
    {26, "Ab7"},  {27, "G"},     {28, "G7"},    {30, "C"},    {33, "-"},     {34, "-"},
    {35, "Am|G"}, {36, "C"},     {37, "A"},     {38, "Dm"},   {39, "C/G|G"}, {40, "C"},
};

} // namespace

TEST_CASE("harmony corpus: Bach Prelude BWV 846, the first sixteen bars") {
    const Score s =
        score_corpus("bach-bwv846-prelude.mid", HarmonyScope::All, kBach, print_table());
    for (const std::string& w : s.wrong) {
        MESSAGE(w);
    }
    CHECK(static_cast<double>(s.right) / s.total >= kGoalAccuracy);
    CHECK(s.changes_per_bar <= kMostChangesPerBar);
}

TEST_CASE("harmony corpus: Lantern Waltz, twelve bars of 3/4") {
    const Score s = score_corpus("lantern-waltz.mid", HarmonyScope::All, kLantern, print_table());
    for (const std::string& w : s.wrong) {
        MESSAGE(w);
    }
    CHECK(static_cast<double>(s.right) / s.total >= kGoalAccuracy);
    CHECK(s.changes_per_bar <= kMostChangesPerBar);
}

TEST_CASE("harmony corpus: Beethoven Op. 48 no. 4, the bars whose harmony the notes fix") {
    const Score s = score_corpus("beethoven-op48-4-ehre-gottes.mxl", HarmonyScope::All, kBeethoven,
                                 print_table());
    for (const std::string& w : s.wrong) {
        MESSAGE(w);
    }
    CHECK(static_cast<double>(s.right) / s.total >= kGoalAccuracy);
    CHECK(s.changes_per_bar <= kMostChangesPerBar);
}

TEST_CASE("harmony corpus: the same waltz a tone higher names the same chords in the new key") {
    const std::vector<Bar> in_d{{1, "D"}, {2, "A"}, {3, "G"}, {4, "D"},  {5, "G"},  {6, "D"},
                                {7, "A"}, {8, "D"}, {9, "D"}, {10, "A"}, {11, "A"}, {12, "D"}};
    const Score s = score_corpus("lantern-waltz-in-d.mid", HarmonyScope::All, in_d, print_table());
    CHECK(static_cast<double>(s.right) / s.total >= kGoalAccuracy);
}

TEST_CASE(
    "harmony corpus: Satie, Gymnopedie no. 1, the first sixteen bars alternate Gmaj7 and Dmaj7") {
    std::vector<Bar> satie;
    for (int bar = 1; bar <= 16; ++bar) {
        satie.push_back({bar, bar % 2 == 1 ? "Gmaj7" : "Dmaj7"});
    }
    const Score s = score_corpus("satie-gymnopedie-1.mid", HarmonyScope::All, satie, print_table());
    CHECK(static_cast<double>(s.right) / s.total >= kGoalAccuracy);
}

// Bars that were not looked at while the thresholds were set, labelled from their notes afterwards.
// Bach bars 17 to 35 (the rest of the prelude; bars 23, 28 and 34 are left out, a pedal note making
// two readings as good as each other) and Satie bars 17 to 40 (bars 25, 28 and 30 left out for the
// same reason). Bar 27 is one the analysis gets wrong: it names a Dm9 where the tune's E over a
// Dm7 is a passing ninth.
const std::vector<Bar> kBachRest{
    {17, "Dm7"}, {18, "G7"},  {19, "C"},      {20, "C7"}, {21, "Fmaj7"}, {22, "F#dim7"},
    {24, "G7"},  {25, "C/G"}, {26, "G7sus4"}, {27, "G7"}, {29, "C/G"},   {30, "G7sus4"},
    {31, "G7"},  {32, "C7"},  {33, "Dm7/C"},  {35, "C"},
};
const std::vector<Bar> kSatieRest{
    {17, "F#m"},   {18, "Bm"},    {19, "Em"}, {20, "Em7"}, {21, "Dm"},     {22, "Am"},
    {23, "Em7/D"}, {24, "Em7/D"}, {26, "D7"}, {27, "Dm7"}, {29, "Em7/D"},  {31, "D7"},
    {32, "Em"},    {33, "F#m"},   {34, "Bm"}, {35, "A/E"}, {36, "F#m7/E"}, {37, "Em9"},
    {38, "Am7"},   {39, "D"},     {40, "Em"},
};

TEST_CASE("harmony corpus: bars not used to set the thresholds") {
    const Score bach =
        score_corpus("bach-bwv846-prelude.mid", HarmonyScope::All, kBachRest, print_table());
    for (const std::string& w : bach.wrong) {
        MESSAGE(w);
    }
    CHECK(static_cast<double>(bach.right) / bach.total >= kGoalAccuracy);
    CHECK(bach.changes_per_bar <= kMostChangesPerBar);
    const Score satie =
        score_corpus("satie-gymnopedie-1.mid", HarmonyScope::All, kSatieRest, print_table());
    for (const std::string& w : satie.wrong) {
        MESSAGE(w);
    }
    CHECK(static_cast<double>(satie.right) / satie.total >= kGoalAccuracy);
    CHECK(satie.changes_per_bar <= kMostChangesPerBar);
}

// The bars left out above because a pedal note (or a passing note) makes two readings as good as
// each other. The first reading is the one a textbook gives; the second is the other.
const std::vector<Bar> kBachAmbiguous{
    {23, "Abdim7", "Bdim7/Ab"}, {28, "F#dim7/G", "Am7b5/G"}, {34, "G7/C", "G7sus4/C"}};
const std::vector<Bar> kSatieAmbiguous{
    {25, "Dsus2", "C6/9/D"}, {28, "C6/9/D", "Dsus2"}, {30, "Dsus2", "C6/9/D"}};

TEST_CASE("harmony corpus: the not-tuned-on bars with the ambiguous ones counted too") {
    // The ambiguous bars counted as wrong (the strict figure), and as right when the analysis gives
    // either of the two readings (the lenient one).
    const Score bach = score_corpus("bach-bwv846-prelude.mid", HarmonyScope::All, kBachRest, false);
    const Score bach_amb =
        score_corpus("bach-bwv846-prelude.mid", HarmonyScope::All, kBachAmbiguous, print_table());
    const Score satie =
        score_corpus("satie-gymnopedie-1.mid", HarmonyScope::All, kSatieRest, false);
    const Score satie_amb =
        score_corpus("satie-gymnopedie-1.mid", HarmonyScope::All, kSatieAmbiguous, print_table());
    const double bach_strict = static_cast<double>(bach.right) / (bach.total + bach_amb.total);
    const double bach_lenient =
        static_cast<double>(bach.right + bach_amb.right) / (bach.total + bach_amb.total);
    const double satie_strict = static_cast<double>(satie.right) / (satie.total + satie_amb.total);
    const double satie_lenient =
        static_cast<double>(satie.right + satie_amb.right) / (satie.total + satie_amb.total);
    if (print_table()) {
        std::printf("\nheld-out with the ambiguous bars: Bach strict %.1f%% lenient %.1f%%, "
                    "Satie strict %.1f%% lenient %.1f%%\n",
                    100 * bach_strict, 100 * bach_lenient, 100 * satie_strict, 100 * satie_lenient);
    }
    CHECK(bach_strict >= 0.80);
    CHECK(satie_strict >= 0.80);
    CHECK(bach_lenient >= kGoalAccuracy);
    CHECK(satie_lenient >= kGoalAccuracy);
}

TEST_CASE("harmony corpus: per hand, the hand with the chord names it and the tune names none") {
    // The waltz: the left hand holds the harmony, the right hand only the tune.
    const Song waltz = load_song("lantern-waltz.mid");
    const std::vector<HarmonyStretch> left = analyse_harmony(waltz, HarmonyScope::Left);
    const std::vector<HarmonyStretch> right = analyse_harmony(waltz, HarmonyScope::Right);
    int named_left = 0;
    for (const HarmonyStretch& s : left) {
        named_left += s.chord.found ? 1 : 0;
    }
    CHECK(named_left > 0);
    for (const HarmonyStretch& s : right) {
        CHECK_FALSE(s.chord.found);
    }
    // And the left hand's names are the combined names.
    const std::vector<HarmonyStretch> all = analyse_harmony(waltz, HarmonyScope::All);
    CHECK(left.size() == all.size());
    for (std::size_t i = 0; i < std::min(left.size(), all.size()); ++i) {
        CHECK(harmony_name(left[i]) == harmony_name(all[i]));
    }
}

TEST_CASE("harmony corpus: the Bach left hand alone holds two notes a bar and is named for none") {
    const Song bach = load_song("bach-bwv846-prelude.mid");
    int named = 0;
    for (const HarmonyStretch& s : analyse_harmony(bach, HarmonyScope::Left)) {
        named += s.chord.found ? 1 : 0; // bar 10's D and A are a bare fifth: no hand name
    }
    CHECK(named == 0);
}

TEST_CASE("harmony corpus: build_harmony covers the whole song in order") {
    const Song bach = load_song("bach-bwv846-prelude.mid");
    const HarmonyTimeline t = build_harmony(bach);
    CHECK_FALSE(t.all.empty());
    CHECK(t.all.front().start_tick == 0);
    CHECK(t.all.back().end_tick == bach.end_tick);
    for (std::size_t i = 1; i < t.all.size(); ++i) {
        CHECK(t.all[i].start_tick == t.all[i - 1].end_tick);
    }
}
