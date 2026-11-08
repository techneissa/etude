// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/exercises.hpp"
#include "etude/scale_diagram.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;

namespace {

// The right or left hand's fingers shown, in key order, on the scale's keys.
std::vector<int> shown(const ScaleDiagram& d, bool right) {
    std::vector<int> out;
    for (const DiagramKey& k : d.keys) {
        if (k.in_scale) {
            out.push_back(right ? k.right : k.left);
        }
    }
    return out;
}

std::string name_of(const DiagramKey& k) { return k.name.data(); }

} // namespace

TEST_CASE("scale diagram: G major shows the fingering of the issue") {
    const ScaleDiagram d = scale_diagram(7, ScaleType::Major);
    CHECK(d.fingering);
    CHECK(shown(d, true) == std::vector<int>{1, 2, 3, 1, 2, 3, 4, 5});
    CHECK(shown(d, false) == std::vector<int>{5, 4, 3, 2, 1, 3, 2, 1});
    // G4 to G5: the keys from G up to the G above, every key between them.
    CHECK(d.keys.front().note == 67);
    CHECK(d.keys.back().note == 79);
    CHECK(d.keys.size() == 13);
    CHECK(d.white_keys == 8);
}

TEST_CASE("scale diagram: the fingers shown are scale_fingers for every key and type that has a "
          "table") {
    for (const ScaleType type : {ScaleType::Major, ScaleType::NaturalMinor,
                                 ScaleType::HarmonicMinor, ScaleType::MelodicMinor}) {
        for (int root = 0; root < 12; ++root) {
            CAPTURE(root);
            CAPTURE(static_cast<int>(type));
            const ScaleDiagram d = scale_diagram(root, type);
            CHECK(d.fingering);
            CHECK(shown(d, true) == scale_fingers(root, type, Hand::Right, 1));
            CHECK(shown(d, false) == scale_fingers(root, type, Hand::Left, 1));
        }
    }
}

TEST_CASE("scale diagram: a few keys, hands apart") {
    // F major: the right hand's 4th finger is on the Bb, the left hand's thumb on the fourth note.
    const ScaleDiagram f = scale_diagram(5, ScaleType::Major);
    CHECK(shown(f, true) == std::vector<int>{1, 2, 3, 4, 1, 2, 3, 4});
    // B major starts the right hand on 1 and the left hand on 4.
    const ScaleDiagram b = scale_diagram(11, ScaleType::Major);
    CHECK(shown(b, true).front() == 1);
    CHECK(shown(b, false).front() == 4);
    // C major: the thumb passes under on the fourth note.
    const ScaleDiagram c = scale_diagram(0, ScaleType::Major);
    CHECK(shown(c, true) == std::vector<int>{1, 2, 3, 1, 2, 3, 4, 5});
}

TEST_CASE("scale diagram: a type with no table shows no fingers") {
    for (const ScaleType type : {ScaleType::Dorian, ScaleType::MajorPentatonic, ScaleType::Blues,
                                 ScaleType::Chromatic, ScaleType::WholeTone}) {
        const ScaleDiagram d = scale_diagram(2, type);
        CHECK_FALSE(d.fingering);
        for (const DiagramKey& k : d.keys) {
            CHECK(k.right == 0);
            CHECK(k.left == 0);
        }
    }
}

TEST_CASE("scale diagram: the scale's notes are marked and named, the tonic at both ends") {
    // Db major: Db Eb F Gb Ab Bb C Db. The picture starts on the white key below the tonic and ends
    // on the white key above the top tonic, so each end is a whole white key.
    const ScaleDiagram d = scale_diagram(1, ScaleType::Major);
    CHECK(d.keys.front().note == 60);
    CHECK(d.keys.front().black == false);
    CHECK_FALSE(d.keys.front().in_scale);
    CHECK(d.keys.back().note == 74);
    CHECK_FALSE(d.keys.back().in_scale);
    std::vector<std::string> names;
    std::vector<int> roots;
    for (const DiagramKey& k : d.keys) {
        if (k.in_scale) {
            names.push_back(name_of(k));
        } else {
            CHECK(name_of(k).empty());
        }
        if (k.root) {
            roots.push_back(k.note);
        }
    }
    CHECK(names == std::vector<std::string>{"Db", "Eb", "F", "Gb", "Ab", "Bb", "C", "Db"});
    CHECK(roots == std::vector<int>{61, 73});
    // A black key is flagged as one.
    CHECK(d.keys[1].black);
    CHECK(d.keys[1].note == 61);
    // `white` counts the white keys below: C is 0, the Db after it 1 (on the edge between C and D),
    // the D 1, the Eb 2.
    CHECK(d.keys[0].white == 0);
    CHECK(d.keys[1].white == 1);
    CHECK(d.keys[2].white == 1);
    CHECK(d.keys[3].white == 2);
    CHECK(d.white_keys == 9);
}

TEST_CASE("scale diagram: every type marks its notes, and the count matches the scale") {
    for (const ScaleType type : kAllScaleTypes) {
        for (int root = 0; root < 12; ++root) {
            const ScaleDiagram d = scale_diagram(root, type);
            std::size_t in_scale = 0;
            for (const DiagramKey& k : d.keys) {
                in_scale += k.in_scale ? 1 : 0;
                CHECK(k.black == (std::string("\x01\x03\x06\x08\x0a").find(
                                      static_cast<char>(k.note % 12)) != std::string::npos));
            }
            CHECK(in_scale == scale_intervals(type).size() + 1);
            CHECK_FALSE(d.keys.front().black);
            CHECK_FALSE(d.keys.back().black);
        }
    }
}

TEST_CASE("scale diagram: a root past the octave wraps, and the title names the key") {
    CHECK(scale_diagram(19, ScaleType::Major).keys.front().note ==
          scale_diagram(7, ScaleType::Major).keys.front().note);
    CHECK(scale_diagram(-5, ScaleType::Major).keys.front().note ==
          scale_diagram(7, ScaleType::Major).keys.front().note);
    CHECK(scale_title(7, ScaleType::Major) == "G major");
    CHECK(scale_title(1, ScaleType::Major) == "Db major");
    CHECK(scale_title(9, ScaleType::NaturalMinor) == "A natural minor");
    CHECK(scale_title(6, ScaleType::HarmonicMinor) == "F# harmonic minor");
}
