// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord_diagram.hpp"
#include "etude/chord_fingering.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <set>
#include <string>
#include <vector>

using namespace etude;

namespace {

int pc_of(int note) { return ((note % 12) + 12) % 12; }

unsigned tone_mask(ChordQuality q) {
    return q == ChordQuality::Power ? 0x81u : chord_quality_mask(q);
}

// A spelled name ("Bb", "F#", "E##") as a pitch class; -1 when it is not one.
int name_pc(const std::string& name) {
    if (name.empty()) {
        return -1;
    }
    constexpr int kLetterPc[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    if (name[0] < 'A' || name[0] > 'G') {
        return -1;
    }
    int pc = kLetterPc[name[0] - 'A'];
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (name[i] == '#') {
            ++pc;
        } else if (name[i] == 'b') {
            --pc;
        } else {
            return -1;
        }
    }
    return pc_of(pc);
}

} // namespace

TEST_CASE("chord card: the qualities are every quality but None, once each") {
    std::set<ChordQuality> seen(kChordCardQualities.begin(), kChordCardQualities.end());
    CHECK(seen.size() == kChordCardQualities.size());
    CHECK(seen.count(ChordQuality::None) == 0);
    // ChordQuality runs None, then 23 more.
    CHECK(kChordCardQualities.size() == static_cast<std::size_t>(ChordQuality::Dominant13));
    for (int v = 1; v <= static_cast<int>(ChordQuality::Dominant13); ++v) {
        CHECK(seen.count(static_cast<ChordQuality>(v)) == 1);
    }
}

TEST_CASE("chord card: the bass may be the root, third, fifth or seventh, never an added tone") {
    for (const ChordQuality q : kChordCardQualities) {
        CAPTURE(static_cast<int>(q));
        const int tones = chord_tone_count(q);
        CHECK(tones == std::popcount(tone_mask(q)));
        const int inversions = chord_inversion_count(q);
        CHECK(inversions >= 1);
        CHECK(inversions <= 4);
        CHECK(inversions <= tones);
        // Inversion 0 is the root; the rest are chord tones other than the 9th, 11th and 13th.
        CHECK(chord_bass_tone(q, 0) == 0);
        for (int i = 1; i < inversions; ++i) {
            const int bass = chord_bass_tone(q, i);
            CHECK((tone_mask(q) & (1u << bass)) != 0);
            CHECK(bass != 0);
        }
    }
    CHECK(chord_inversion_count(ChordQuality::None) == 0);
    CHECK(chord_inversion_count(ChordQuality::Power) == 1);
    CHECK(chord_inversion_count(ChordQuality::Major) == 3);
    CHECK(chord_inversion_count(ChordQuality::Dominant7) == 4);
    CHECK(chord_inversion_count(ChordQuality::Add9) == 3);       // root, 3rd, 5th: not the 9th
    CHECK(chord_inversion_count(ChordQuality::Dominant9) == 4);  // up to the 7th, not the 9th
    CHECK(chord_inversion_count(ChordQuality::Dominant13) == 4);
    CHECK(chord_inversion_count(ChordQuality::Major69) == 4);    // the 6th takes the 7th's place
    // First inversion has the third (or the suspended note) in the bass, second the fifth, third the
    // seventh (or the sixth).
    const auto semitones = [](ChordQuality q, int inversion) { return chord_bass_tone(q, inversion); };
    CHECK(semitones(ChordQuality::Major, 1) == 4);
    CHECK(semitones(ChordQuality::Minor, 1) == 3);
    CHECK(semitones(ChordQuality::Sus2, 1) == 2);
    CHECK(semitones(ChordQuality::Sus4, 1) == 5);
    CHECK(semitones(ChordQuality::Dominant9, 1) == 4);
    CHECK(semitones(ChordQuality::Dominant9, 2) == 7);
    CHECK(semitones(ChordQuality::Dominant9, 3) == 10);
    CHECK(semitones(ChordQuality::Dominant7Flat9, 1) == 4);
    CHECK(semitones(ChordQuality::Dominant13, 3) == 10);
    CHECK(semitones(ChordQuality::Major6, 3) == 9);
    CHECK(semitones(ChordQuality::Add9, 2) == 7);
}

TEST_CASE("chord card: the voicing of every quality, root and inversion is the chord's notes, "
          "bass first, inside one octave") {
    for (const ChordQuality q : kChordCardQualities) {
        const unsigned mask = tone_mask(q);
        for (int root = 0; root < 12; ++root) {
            for (int inversion = 0; inversion < chord_inversion_count(q); ++inversion) {
                CAPTURE(static_cast<int>(q));
                CAPTURE(root);
                CAPTURE(inversion);
                const std::vector<int> notes = chord_voicing(root, q, inversion);
                REQUIRE(notes.size() == static_cast<std::size_t>(chord_tone_count(q)));
                CHECK(std::is_sorted(notes.begin(), notes.end()));
                CHECK(std::adjacent_find(notes.begin(), notes.end()) == notes.end());
                // The bass is in the octave of middle C and the notes lie within an octave.
                CHECK(notes.front() >= 60);
                CHECK(notes.front() <= 71);
                CHECK(notes.back() - notes.front() < 12);
                // Each note is a tone of the chord, once, and the bass is tone `inversion`.
                std::set<int> semitones;
                for (const int note : notes) {
                    const int above = ((pc_of(note) - root) % 12 + 12) % 12;
                    CHECK((mask & (1u << above)) != 0);
                    semitones.insert(above);
                }
                CHECK(semitones.size() == notes.size());
                CHECK(((pc_of(notes.front()) - root) % 12 + 12) % 12 ==
                      chord_bass_tone(q, inversion));
            }
        }
    }
    // An inversion past the chord's is its last, one before 0 its root position.
    CHECK(chord_voicing(0, ChordQuality::Major, 9) == chord_voicing(0, ChordQuality::Major, 2));
    CHECK(chord_voicing(0, ChordQuality::Major, -4) == chord_voicing(0, ChordQuality::Major, 0));
    // C major: C E G, then E G C, then G C E.
    CHECK(chord_voicing(0, ChordQuality::Major, 0) == std::vector<int>{60, 64, 67});
    CHECK(chord_voicing(0, ChordQuality::Major, 1) == std::vector<int>{64, 67, 72});
    CHECK(chord_voicing(0, ChordQuality::Major, 2) == std::vector<int>{67, 72, 76});
    CHECK(chord_voicing(11, ChordQuality::Dominant7, 0) == std::vector<int>{71, 75, 78, 81});
}

TEST_CASE("chord card: the diagram names the chosen chord, the notes' names are its tones, and the "
          "keys hold the voicing") {
    for (const ChordQuality q : kChordCardQualities) {
        for (int root = 0; root < 12; ++root) {
            for (int inversion = 0; inversion < chord_inversion_count(q); ++inversion) {
                CAPTURE(static_cast<int>(q));
                CAPTURE(root);
                CAPTURE(inversion);
                const ChordDiagram d = chord_diagram(root, q, inversion);
                REQUIRE(d.chord.found);
                // The chord is the chosen one, its bass the inversion's, on the notes shown.
                CHECK(d.chord.root_pc == root);
                CHECK(d.chord.quality == q);
                CHECK(pc_of(d.notes.front()) == d.chord.bass_pc);
                CHECK(d.chord.bass_pc == (root + chord_bass_tone(q, inversion)) % 12);
                std::set<int> chosen;
                for (const int note : d.notes) {
                    chosen.insert(pc_of(note));
                }
                std::set<int> named;
                const unsigned mask = tone_mask(q);
                for (int s = 0; s < 12; ++s) {
                    if (mask & (1u << s)) {
                        named.insert((root + s) % 12);
                    }
                }
                CHECK(chosen == named);
                CHECK_FALSE(d.name.empty());
                // The name's root is the chosen root, its quality the chosen quality's (as the
                // label writes it in root position), and a slash bass only for an inversion.
                const ChordDiagram root_position = chord_diagram(root, q, 0);
                CHECK(name_pc(d.name.root) == root);
                CHECK(d.name.root == root_position.name.root);
                CHECK(d.name.quality == root_position.name.quality);
                CHECK(d.name.bass.empty() == (inversion == 0));
                if (inversion > 0) {
                    CHECK(name_pc(d.name.bass) == d.chord.bass_pc);
                }
                // The keys: eight white keys at least, from a white key to a white key.
                const auto& keys = d.picture.keys;
                REQUIRE_FALSE(keys.empty());
                CHECK(d.picture.white_keys >= 8);
                CHECK_FALSE(keys.front().black);
                CHECK_FALSE(keys.back().black);
                CHECK(keys.front().note <= d.notes.front());
                CHECK(keys.back().note >= d.notes.back());
                int lit = 0;
                for (std::size_t i = 0; i < keys.size(); ++i) {
                    if (i > 0) {
                        CHECK(keys[i].note == keys[i - 1].note + 1);
                    }
                    if (!keys[i].in_scale) {
                        CHECK(keys[i].name[0] == '\0');
                        continue;
                    }
                    ++lit;
                    CHECK(std::find(d.notes.begin(), d.notes.end(), keys[i].note) != d.notes.end());
                    // Each lit key's name is its own pitch class, spelled with at most two signs.
                    const std::string name = keys[i].name.data();
                    CHECK(name_pc(name) == pc_of(keys[i].note));
                    CHECK(name.size() <= 3);
                    CHECK(keys[i].root == (pc_of(keys[i].note) == d.chord.root_pc));
                }
                CHECK(lit == static_cast<int>(d.notes.size()));
            }
        }
    }
}

TEST_CASE("chord card: the names of familiar chords, flats and sharps by the root's key") {
    const auto title = [](int root, ChordQuality q, int inversion) {
        return chord_diagram(root, q, inversion).name.plain();
    };
    CHECK(title(0, ChordQuality::Major, 0) == "C");
    CHECK(title(0, ChordQuality::Major, 1) == "C/E");
    CHECK(title(0, ChordQuality::Major, 2) == "C/G");
    CHECK(title(9, ChordQuality::Minor7, 0) == "Am7");
    CHECK(title(10, ChordQuality::Dominant7, 0) == "Bb7");
    CHECK(title(1, ChordQuality::Major, 0) == "Db");
    CHECK(title(3, ChordQuality::Minor, 0) == "Ebm");  // Eb minor, not D# minor
    CHECK(title(6, ChordQuality::Major, 0) == "F#");   // F# major, not Gb
    CHECK(title(6, ChordQuality::Minor, 0) == "F#m");
    CHECK(title(8, ChordQuality::Major, 0) == "Ab");
    CHECK(title(7, ChordQuality::HalfDiminished7, 0) == "Gm7b5");
    CHECK(title(0, ChordQuality::Dominant7Flat9, 0) == "C7b9");
    CHECK(title(0, ChordQuality::Major69, 0) == "C6/9");
    // The chosen chord over every inversion, the bass the inversion's: what the free-play label
    // would call another chord (Cm7/Eb is Eb6, C6/E is Am7/E) is not what the card says.
    CHECK(title(0, ChordQuality::Minor7, 1) == "Cm7/Eb");
    CHECK(title(0, ChordQuality::Major6, 1) == "C6/E");
    CHECK(title(0, ChordQuality::Sus2, 2) == "Csus2/G");
    CHECK(title(0, ChordQuality::Dominant9, 1) == "C9/E");
    CHECK(title(0, ChordQuality::Add9, 1) == "Cadd9/E");
    CHECK(title(0, ChordQuality::Major69, 3) == "C6/9/A");
    CHECK(title(0, ChordQuality::Dominant7Flat9, 1) == "C7b9/E");
    CHECK(title(0, ChordQuality::Augmented, 1) == "Caug/E");
    CHECK(title(0, ChordQuality::Diminished7, 2) == "Cdim7/Gb");
    CHECK(title(0, ChordQuality::Diminished7, 3) == "Cdim7/Bbb");
    // The notes' names inside the keys.
    const ChordDiagram d = chord_diagram(6, ChordQuality::Major7, 0); // F# A# C# E#
    std::vector<std::string> names;
    for (const DiagramKey& key : d.picture.keys) {
        if (key.in_scale) {
            names.push_back(key.name.data());
        }
    }
    CHECK(names == std::vector<std::string>{"F#", "A#", "C#", "E#"});
    const ChordDiagram eb = chord_diagram(3, ChordQuality::Minor7, 0); // Eb Gb Bb Db
    names.clear();
    for (const DiagramKey& key : eb.picture.keys) {
        if (key.in_scale) {
            names.push_back(key.name.data());
        }
    }
    CHECK(names == std::vector<std::string>{"Eb", "Gb", "Bb", "Db"});
}

TEST_CASE("chord card: the fingering is shown only for a triad or a seventh, on its notes in order") {
    for (const ChordQuality q : kChordCardQualities) {
        for (int inversion = 0; inversion < chord_inversion_count(q); ++inversion) {
            CAPTURE(static_cast<int>(q));
            CAPTURE(inversion);
            const ChordDiagram d = chord_diagram(7, q, inversion);
            CHECK(d.picture.fingering == chord_has_fingering(q));
            const auto f = chord_fingering(q, inversion);
            std::vector<int> right;
            std::vector<int> left;
            for (const DiagramKey& key : d.picture.keys) {
                if (key.in_scale) {
                    right.push_back(key.right);
                    left.push_back(key.left);
                } else {
                    CHECK(key.right == 0);
                    CHECK(key.left == 0);
                }
            }
            if (f) {
                CHECK(right == std::vector<int>(f->right.begin(), f->right.begin() + f->count));
                CHECK(left == std::vector<int>(f->left.begin(), f->left.begin() + f->count));
            } else {
                for (const int finger : right) {
                    CHECK(finger == 0);
                }
            }
        }
    }
}

TEST_CASE("chord card: an inversion the chord does not have is its last, and no root wraps wrong") {
    CHECK(chord_diagram(0, ChordQuality::Major, 7).notes == chord_diagram(0, ChordQuality::Major, 2).notes);
    CHECK(chord_diagram(12, ChordQuality::Major, 0).notes == chord_diagram(0, ChordQuality::Major, 0).notes);
    CHECK(chord_diagram(-1, ChordQuality::Major, 0).notes == chord_diagram(11, ChordQuality::Major, 0).notes);
    CHECK(chord_diagram(0, ChordQuality::None, 0).notes.empty());
}
