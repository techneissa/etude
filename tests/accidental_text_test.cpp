// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/accidental_text.hpp"

#include <array>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#include "etude/library.hpp"
#include "etude/pitch.hpp"

using namespace etude;

namespace {

constexpr std::string_view kFlat = "\xE2\x99\xAD";
constexpr std::string_view kSharp = "\xE2\x99\xAF";

// "B♭" from "B" and the flat sign, so the cases read the way the screen does.
std::string flat(std::string_view letter, std::string_view after = "") {
    return std::string(letter) + std::string(kFlat) + std::string(after);
}
std::string sharp(std::string_view letter, std::string_view after = "") {
    return std::string(letter) + std::string(kSharp) + std::string(after);
}

} // namespace

TEST_CASE("the signs are the music shapes U+266D and U+266F") {
    CHECK(kFlatSign == "♭");
    CHECK(kSharpSign == "♯");
}

TEST_CASE("all twelve pitch classes, in both spellings, show ♭ and ♯ and never b or #") {
    constexpr std::array<std::string_view, 12> kShownSharp{"C",  "C♯", "D",  "D♯", "E",  "F",
                                                           "F♯", "G",  "G♯", "A",  "A♯", "B"};
    constexpr std::array<std::string_view, 12> kShownFlat{"C",  "D♭", "D",  "E♭", "E",  "F",
                                                          "G♭", "G",  "A♭", "A",  "B♭", "B"};
    for (int pc = 0; pc < 12; ++pc) {
        const auto i = static_cast<std::size_t>(pc);
        std::string sharps = note_name(60 + pc, false);
        sharps.pop_back(); // the octave digit
        std::string flats = note_name(60 + pc, true);
        flats.pop_back();
        CHECK(accidental_display(sharps) == kShownSharp[i]);
        CHECK(accidental_display(flats) == kShownFlat[i]);
        // With the octave number after it.
        CHECK(accidental_display(note_name(60 + pc, true)) == std::string(kShownFlat[i]) + "4");
        CHECK(accidental_display(note_name(60 + pc, false)) == std::string(kShownSharp[i]) + "4");
    }
}

TEST_CASE("the note B is not a flat sign, and a flat is not the letter b") {
    CHECK(accidental_display("B") == "B");
    CHECK(accidental_display("B4") == "B4");
    CHECK(accidental_display("Bb") == flat("B"));
    CHECK(accidental_display("Bb4") == flat("B", "4"));
    CHECK(accidental_display("BB") == "BB");
    CHECK(accidental_display("Bm7") == "Bm7");
    CHECK(accidental_display("B major") == "B major");
    CHECK(accidental_display("Bbb") == flat("B") + std::string(kFlat)); // a double flat
}

TEST_CASE("solfège names take the signs too") {
    CHECK(accidental_display("Reb") == flat("Re"));
    CHECK(accidental_display("Mib") == flat("Mi"));
    CHECK(accidental_display("Solb") == flat("Sol"));
    CHECK(accidental_display("Lab") == flat("La"));
    CHECK(accidental_display("Sib") == flat("Si"));
    CHECK(accidental_display("Do#") == sharp("Do"));
    CHECK(accidental_display("Fa#") == sharp("Fa"));
    CHECK(accidental_display("Sol#") == sharp("Sol"));
    CHECK(accidental_display("La#4") == sharp("La", "4"));
    CHECK(accidental_display("Si") == "Si");
    CHECK(accidental_display("Sol major") == "Sol major");
}

TEST_CASE("chord names show ♭5, ♭9 and ♯11 as signs, and a slash bass keeps its own") {
    CHECK(accidental_display("Cm7b5") == "Cm7" + std::string(kFlat) + "5");
    CHECK(accidental_display("Ebm7b5/Bb") ==
          flat("E", "m7") + std::string(kFlat) + "5/" + flat("B"));
    CHECK(accidental_display("C7b9") == "C7" + std::string(kFlat) + "9");
    CHECK(accidental_display("C7#11") == "C7" + std::string(kSharp) + "11");
    CHECK(accidental_display("F#m7b5") == sharp("F", "m7") + std::string(kFlat) + "5");
    CHECK(accidental_display("Bbmaj7") == flat("B", "maj7"));
    CHECK(accidental_display("Dbdim7/Gb") == flat("D", "dim7/") + flat("G"));
    CHECK(accidental_display("G7(b9)") == "G7(" + std::string(kFlat) + "9)");
    CHECK(accidental_display("G7(#9,b13)") ==
          "G7(" + std::string(kSharp) + "9," + std::string(kFlat) + "13)");
    CHECK(accidental_display("Ab/C") == flat("A", "/C"));
    CHECK(accidental_display("C/Bb") == "C/" + flat("B"));
}

TEST_CASE("keys and scales read as a name then a word") {
    CHECK(accidental_display("Bb major") == flat("B", " major"));
    CHECK(accidental_display("Eb minor") == flat("E", " minor"));
    CHECK(accidental_display("F# dorian") == sharp("F", " dorian"));
    CHECK(accidental_display("C# natural minor") == sharp("C", " natural minor"));
    CHECK(accidental_display("Bb Blues") == flat("B", " Blues")); // "Blues" has no sign to change
    CHECK(accidental_display("Sol major") == "Sol major");
    CHECK(accidental_display("Aeolian") == "Aeolian");
    CHECK(accidental_display("Bebop") == "Bebop");
    CHECK(accidental_display("") == "");
}

TEST_CASE("text with nothing to change comes back as it was") {
    for (const std::string_view s : {"", "C", "Am7", "Dorian", "major", "Bm7"}) {
        CHECK(accidental_display(s) == s);
    }
    CHECK(accidental_display_text("Piano Sonata No. 14") == "Piano Sonata No. 14");
    CHECK(accidental_display_text("Abbey Road") == "Abbey Road");
    CHECK(accidental_display_text("BB King") == "BB King");
    CHECK(accidental_display_text("Bach") == "Bach");
    CHECK(accidental_display_text("Ab") == flat("A"));
}

TEST_CASE("a note named in a title shows the sign; other words do not") {
    CHECK(accidental_display_text("Prelude in Bb major") == "Prelude in " + flat("B", " major"));
    CHECK(accidental_display_text("Sonata in C# minor, Op. 27") ==
          "Sonata in " + sharp("C", " minor, Op. 27"));
    CHECK(accidental_display_text("Nocturne in Eb") == "Nocturne in " + flat("E"));
    CHECK(accidental_display_text("Etude (Gb)") == "Etude (" + flat("G") + ")");
    CHECK(accidental_display_text("Ab, Bb and Db") ==
          flat("A", ", ") + flat("B", " and ") + flat("D"));
    CHECK(accidental_display_text("Prelude_in_Bb_major") == "Prelude_in_" + flat("B", "_major"));
    // Not a note: lower case, inside a word, a capital H, a sign with a letter after it.
    CHECK(accidental_display_text("bb") == "bb");
    CHECK(accidental_display_text("Dbx") == "Dbx");
    CHECK(accidental_display_text("ABb") == "ABb");
    CHECK(accidental_display_text("Hb") == "Hb");
    CHECK(accidental_display_text("B") == "B");
    CHECK(accidental_display_text("Track #1") == "Track #1");
    CHECK(has_accidental_spelling("Prelude in Bb"));
    CHECK_FALSE(has_accidental_spelling("Prelude in B"));
    CHECK_FALSE(has_accidental_spelling("Abbey"));
}

TEST_CASE("the plain spelling comes back for matching") {
    CHECK(plain_accidentals(flat("B")) == "Bb");
    CHECK(plain_accidentals(sharp("F", " minor")) == "F# minor");
    CHECK(plain_accidentals(accidental_display("Ebm7b5/Bb")) == "Ebm7b5/Bb");
    CHECK(plain_accidentals("no signs") == "no signs");
}

TEST_CASE("searching Bb and B♭ finds the same text") {
    const std::string a = fold_for_search("Prelude in Bb major");
    const std::string b = fold_for_search("Prelude in " + flat("B", " major"));
    CHECK(a == b);
    CHECK(fold_for_search("Bb") == fold_for_search(flat("B")));
    CHECK(fold_for_search("F#") == fold_for_search(sharp("F")));
    CHECK(matches_search(a, flat("B")));
    CHECK(matches_search(b, "Bb"));
}

TEST_CASE("a chord named in a title keeps its alterations and slash bass") {
    const std::string f(kFlat);
    const std::string s(kSharp);
    CHECK(accidental_display_text("Bbm7b5 vamp") == "B" + f + "m7" + f + "5 vamp");
    CHECK(accidental_display_text("Cm7b5 arpeggio") == "Cm7" + f + "5 arpeggio");
    CHECK(accidental_display_text("F#7#9") == "F" + s + "7" + s + "9");
    CHECK(accidental_display_text("C7#11 and Eb13b9") ==
          "C7" + s + "11 and E" + f + "13" + f + "9");
    CHECK(accidental_display_text("Bb7/D") == "B" + f + "7/D");
    CHECK(accidental_display_text("C#m/G#") == "C" + s + "m/G" + s);
    CHECK(accidental_display_text("Ebmaj7/Bb groove") == "E" + f + "maj7/B" + f + " groove");
    CHECK(accidental_display_text("Dm7b5, G7b9") == "Dm7" + f + "5, G7" + f + "9");
}

TEST_CASE("words that only look like note names stay plain") {
    for (const std::string_view w :
         {"Bebop", "Abba", "Bbq", "Abbey", "Bbb2x", "Cb/", "A/B", "And/or", "Bach/Bb7x", "Dbx"}) {
        CHECK(accidental_display_text(w) == w);
        CHECK_FALSE(has_accidental_spelling(w));
    }
}

TEST_CASE("UTF-8 punctuation ends a word, accented letters do not") {
    const std::string f(kFlat);
    // Curly quotes, dashes and guillemets round a name.
    CHECK(accidental_display_text("\xE2\x80\x9C"
                                  "Bb major\xE2\x80\x9D") == "\xE2\x80\x9C"
                                                             "B" +
                                                                 f + " major\xE2\x80\x9D");
    CHECK(accidental_display_text("Prelude\xE2\x80\x94"
                                  "Bb major") == "Prelude\xE2\x80\x94"
                                                 "B" +
                                                     f + " major");
    CHECK(accidental_display_text("Sonata \xE2\x80\x93 Eb\xE2\x80\x94op. 5") ==
          "Sonata \xE2\x80\x93 E" + f + "\xE2\x80\x94op. 5");
    CHECK(accidental_display_text("\xC2\xAB"
                                  "Bb\xC2\xBB") == "\xC2\xAB"
                                                   "B" +
                                                       f + "\xC2\xBB");
    CHECK(accidental_display_text("\xE2\x80\x98"
                                  "Ab\xE2\x80\x99s tune") == "\xE2\x80\x98"
                                                             "A" +
                                                                 f + "\xE2\x80\x99s tune");
    // An accented letter belongs to the word: no name is cut out of it, and a name still ends at
    // a space round it.
    CHECK(accidental_display_text("Caf\xC3\xA9 Bb") == "Caf\xC3\xA9 B" + f);
    CHECK(accidental_display_text("\xC3\x89t\xC3\xA9") == "\xC3\x89t\xC3\xA9");
    CHECK(accidental_display_text("Bb\xC3\xA9") == "Bb\xC3\xA9");
    CHECK(accidental_display_text("\xC3\xA9"
                                  "Bb") == "\xC3\xA9"
                                           "Bb");
    CHECK(has_accidental_spelling("\xE2\x80\x9C"
                                  "Bb\xE2\x80\x9D"));
    CHECK_FALSE(has_accidental_spelling("Bb\xC3\xA9"));
}
