// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/accidental_text.hpp"
#include "etude/chord_spelling.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace etude;

namespace {

KeyChange major(int fifths) { return key_from_signature(0, fifths, KeyMode::Major); }
KeyChange minor(int fifths) { return key_from_signature(0, fifths, KeyMode::Minor); }

std::string spelled(int root_pc, ChordQuality q, int bass_pc, const KeyChange* key) {
    const Chord c{true, root_pc, q, bass_pc};
    return spelled_chord_name(c, spell_chord(c, key));
}

} // namespace

TEST_CASE("a place on the line of fifths names its note") {
    CHECK(line_name(0) == "C");
    CHECK(line_name(1) == "G");
    CHECK(line_name(-1) == "F");
    CHECK(line_name(-2) == "Bb");
    CHECK(line_name(6) == "F#");
    CHECK(line_name(11) == "E#");
    CHECK(line_name(12) == "B#");
    CHECK(line_name(-7) == "Cb");
    CHECK(line_name(-8) == "Fb");
    CHECK(line_name(13) == "F##");
    CHECK(line_name(-9) == "Bbb");
    CHECK(line_pitch_class(0) == 0);
    CHECK(line_pitch_class(1) == 7);
    CHECK(line_pitch_class(-1) == 5);
    CHECK(line_pitch_class(11) == 5); // E# is F
    CHECK(line_pitch_class(-2) == 10);
}

TEST_CASE("the root takes the spelling nearest the key") {
    const KeyChange d_major = major(2);
    const KeyChange f_major = major(-1);
    const KeyChange c_major = major(0);
    CHECK(spelled(1, ChordQuality::Dominant7, 1, &d_major) == "C#7"); // the dominant of F# minor
    CHECK(spelled(10, ChordQuality::Major, 10, &f_major) == "Bb");
    CHECK(spelled(10, ChordQuality::Major, 10, &c_major) == "Bb"); // flat side in a major key
    CHECK(spelled(6, ChordQuality::Major, 6, &c_major) == "F#");
    CHECK(spelled(6, ChordQuality::Major, 6, &f_major) == "Gb"); // the flat side in F major
    CHECK(spelled(8, ChordQuality::Major, 8, &c_major) == "Ab"); // bVI in a major key
    const KeyChange a_minor = minor(0);
    CHECK(spelled(8, ChordQuality::Major, 8, &a_minor) == "Ab"); // bVI, even in a minor key
    CHECK(spelled(8, ChordQuality::Minor, 8, &a_minor) ==
          "G#m"); // its minor chord is spelled sharp
    CHECK(spelled(4, ChordQuality::Dominant7, 8, &a_minor) == "E7/G#");
}

TEST_CASE("the bass is spelled from the chord, F#maj7/E# and not F#maj7/F") {
    const KeyChange f_sharp_major = major(6);
    CHECK(spelled(6, ChordQuality::Major7, 5, &f_sharp_major) == "F#maj7/E#");
    CHECK(spelled(6, ChordQuality::Major7, 5, nullptr) ==
          "Gbmaj7/F"); // no key: C major's flat side
    const KeyChange c_major = major(0);
    CHECK(spelled(7, ChordQuality::Major, 11, &c_major) == "G/B");
    CHECK(spelled(0, ChordQuality::Minor, 3, &c_major) == "Cm/Eb");
    CHECK(spelled(2, ChordQuality::Dominant7, 6, &c_major) == "D7/F#");
    const KeyChange d_flat_major = major(-5);
    CHECK(spelled(1, ChordQuality::Dominant7, 8, &d_flat_major) == "Db7/Ab");
    CHECK(spelled(1, ChordQuality::Major7, 0, &d_flat_major) == "Dbmaj7/C");
}

TEST_CASE("a bass that is not a tone of the chord is spelled from the key") {
    const KeyChange c_major = major(0);
    CHECK(spelled(0, ChordQuality::Major, 1, &c_major) ==
          "C/C#"); // C# is the nearer place to C major
    CHECK(spelled(0, ChordQuality::Major, 10, &c_major) == "C/Bb");
    const KeyChange f_major = major(-1);
    CHECK(spelled(0, ChordQuality::Major, 10, &f_major) == "C/Bb");
}

TEST_CASE("the new qualities are spelled by their own intervals") {
    const KeyChange c_major = major(0);
    CHECK(spelled(7, ChordQuality::Dominant7Sus4, 7, &c_major) == "G7sus4");
    CHECK(spelled(7, ChordQuality::Dominant7Flat9, 7, &c_major) == "G7b9");
    CHECK(spelled(0, ChordQuality::Major69, 7, &c_major) == "C6/9/G");
    CHECK(spelled(7, ChordQuality::Dominant13, 7, &c_major) == "G13");
    CHECK(spelled(0, ChordQuality::HalfDiminished7, 6, &c_major) == "Cm7b5/Gb"); // the flat fifth
    CHECK(spelled(0, ChordQuality::Augmented, 8, &c_major) == "Caug/G#");        // the sharp fifth
    CHECK(spelled(0, ChordQuality::Diminished7, 9, &c_major) == "Cdim7/Bbb");
}

TEST_CASE("a chord that is not found has no name") {
    CHECK(spelled_chord_name(Chord{}, ChordSpelling{}).empty());
    CHECK(spell_chord(Chord{}, nullptr) == ChordSpelling{});
}

TEST_CASE("a diminished seventh takes the tone nearest the key as its root") {
    const KeyChange c_major = major(0);
    // C# E G Bb with G in the bass: E G Bb Db sits nearer C major than C# E G Bb.
    const Chord g_bass{true, 7, ChordQuality::Diminished7, 7};
    const Chord fixed = respell_symmetric(g_bass, &c_major);
    CHECK(fixed.root_pc == 4);
    CHECK(fixed.bass_pc == 7);
    CHECK(spelled_chord_name(fixed, spell_chord(fixed, &c_major)) == "Edim7/G");
    // B D F Ab is the nearest to A minor.
    const KeyChange a_minor = minor(0);
    const Chord g_sharp{true, 8, ChordQuality::Diminished7, 8};
    CHECK(respell_symmetric(g_sharp, &a_minor).root_pc == 11);
    // Whichever tone is the root, the chord is the same and so is its bass.
    CHECK(respell_symmetric(fixed, &c_major) == fixed);
    // An augmented triad too; any other chord is left alone.
    const Chord aug{true, 4, ChordQuality::Augmented, 4};
    CHECK(respell_symmetric(aug, &c_major).quality == ChordQuality::Augmented);
    const Chord plain{true, 7, ChordQuality::Major, 11};
    CHECK(respell_symmetric(plain, &c_major) == plain);
    CHECK_FALSE(respell_symmetric(Chord{}, &c_major).found);
}

TEST_CASE("a spelled chord name shows only real sharp and flat signs") {
    // The display text of every root, quality and bass keeps no '#' and no letter b.
    const ChordQuality qualities[] = {
        ChordQuality::Major,           ChordQuality::Minor,         ChordQuality::Diminished,
        ChordQuality::Augmented,       ChordQuality::Sus2,          ChordQuality::Sus4,
        ChordQuality::Dominant7,       ChordQuality::Major7,        ChordQuality::Minor7,
        ChordQuality::HalfDiminished7, ChordQuality::Diminished7,   ChordQuality::MinorMajor7,
        ChordQuality::Major6,          ChordQuality::Minor6,        ChordQuality::Add9,
        ChordQuality::Dominant9,       ChordQuality::Major9,        ChordQuality::Minor9,
        ChordQuality::Power,           ChordQuality::Dominant7Sus4, ChordQuality::Dominant7Flat9,
        ChordQuality::Major69,         ChordQuality::Dominant13,
    };
    int names = 0;
    int signs = 0;
    for (const ChordQuality quality : qualities) {
        for (int root = 0; root < 12; ++root) {
            for (int bass = 0; bass < 12; ++bass) {
                for (const int fifths : {-6, -3, 0, 3, 6}) {
                    const KeyChange key = major(fifths);
                    const std::string name = spelled(root, quality, bass, &key);
                    const std::string shown = accidental_display(name);
                    ++names;
                    signs += shown != name ? 1 : 0;
                    CHECK(shown.find('#') == std::string::npos);
                    CHECK(shown.find('b') == std::string::npos);
                }
            }
        }
    }
    CHECK(names == 23 * 12 * 12 * 5);
    CHECK(signs > 0);
}

TEST_CASE("the tones of a chord are named from the root by their interval") {
    const auto tone = [](int root_pc, ChordQuality q, int semitones, const KeyChange* key) {
        const Chord c{true, root_pc, q, root_pc};
        return chord_tone_name(c, spell_chord(c, key), semitones);
    };
    const KeyChange c_major = major(0);
    CHECK(tone(0, ChordQuality::Major, 0, &c_major) == "C");
    CHECK(tone(0, ChordQuality::Major, 4, &c_major) == "E");
    CHECK(tone(0, ChordQuality::Major, 7, &c_major) == "G");
    CHECK(tone(0, ChordQuality::Dominant7, 10, &c_major) == "Bb");
    CHECK(tone(4, ChordQuality::Dominant7, 4, nullptr) == "G#"); // the third of E7
    const KeyChange f_sharp = major(6);
    CHECK(tone(6, ChordQuality::Major7, 11, &f_sharp) == "E#"); // the seventh of F# major seventh
    CHECK(tone(6, ChordQuality::Major7, 4, &f_sharp) == "A#");
    CHECK(tone(0, ChordQuality::Augmented, 8, &c_major) == "G#"); // the sharp fifth
    CHECK(tone(0, ChordQuality::Diminished, 6, &c_major) == "Gb"); // the flat fifth
    CHECK(tone(0, ChordQuality::Diminished7, 9, &c_major) == "Bbb");
    CHECK(tone(0, ChordQuality::Power, 7, &c_major) == "G");
    // A semitone the chord does not have, or no chord, has no name.
    CHECK(tone(0, ChordQuality::Major, 3, &c_major).empty());
    CHECK(tone(0, ChordQuality::Power, 4, &c_major).empty());
    CHECK(chord_tone_name(Chord{}, ChordSpelling{}, 0).empty());
    CHECK(tone(0, ChordQuality::Major, 12, &c_major).empty());
    // Every tone of every quality, from every root, is its own pitch class and no more than a double
    // sign away.
    for (int q = 1; q <= static_cast<int>(ChordQuality::Dominant13); ++q) {
        for (int root = 0; root < 12; ++root) {
            const Chord c{true, root, static_cast<ChordQuality>(q), root};
            const ChordSpelling spelling = spell_chord(c, nullptr);
            for (int s = 0; s < 12; ++s) {
                const std::string name = chord_tone_name(c, spelling, s);
                if (name.empty()) {
                    continue;
                }
                CAPTURE(q);
                CAPTURE(root);
                CAPTURE(s);
                CHECK(name.size() <= 3);
                CHECK(name[0] >= 'A');
                CHECK(name[0] <= 'G');
            }
        }
    }
}
