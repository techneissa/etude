// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord_diagram.hpp"

#include "etude/chord_fingering.hpp"
#include "etude/chord_spelling.hpp"
#include "etude/key_signature.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <string>

namespace etude {

namespace {

// A quality's tones as stacked thirds: the root, the third (or the suspension that stands for it),
// the fifth, then the seventh (or the sixth that stands for it), then the ninth or another added
// tone. The first `inversions` of them may be the bass; an added or extended tone never is.
struct Shape {
    ChordQuality quality;
    std::array<int, 6> tones; // semitones above the root, in stacking order
    int count;
    int inversions;
};

constexpr std::array kShapes{
    Shape{ChordQuality::Major, {0, 4, 7}, 3, 3},
    Shape{ChordQuality::Minor, {0, 3, 7}, 3, 3},
    Shape{ChordQuality::Diminished, {0, 3, 6}, 3, 3},
    Shape{ChordQuality::Augmented, {0, 4, 8}, 3, 3},
    Shape{ChordQuality::Sus2, {0, 2, 7}, 3, 3},
    Shape{ChordQuality::Sus4, {0, 5, 7}, 3, 3},
    Shape{ChordQuality::Dominant7, {0, 4, 7, 10}, 4, 4},
    Shape{ChordQuality::Major7, {0, 4, 7, 11}, 4, 4},
    Shape{ChordQuality::Minor7, {0, 3, 7, 10}, 4, 4},
    Shape{ChordQuality::HalfDiminished7, {0, 3, 6, 10}, 4, 4},
    Shape{ChordQuality::Diminished7, {0, 3, 6, 9}, 4, 4},
    Shape{ChordQuality::MinorMajor7, {0, 3, 7, 11}, 4, 4},
    Shape{ChordQuality::Major6, {0, 4, 7, 9}, 4, 4},
    Shape{ChordQuality::Minor6, {0, 3, 7, 9}, 4, 4},
    Shape{ChordQuality::Add9, {0, 4, 7, 2}, 4, 3},
    Shape{ChordQuality::Dominant9, {0, 4, 7, 10, 2}, 5, 4},
    Shape{ChordQuality::Major9, {0, 4, 7, 11, 2}, 5, 4},
    Shape{ChordQuality::Minor9, {0, 3, 7, 10, 2}, 5, 4},
    Shape{ChordQuality::Power, {0, 7}, 2, 1},
    Shape{ChordQuality::Dominant7Sus4, {0, 5, 7, 10}, 4, 4},
    Shape{ChordQuality::Dominant7Flat9, {0, 4, 7, 10, 1}, 5, 4},
    Shape{ChordQuality::Major69, {0, 4, 7, 9, 2}, 5, 4},
    Shape{ChordQuality::Dominant13, {0, 4, 7, 10, 9}, 5, 4},
};

const Shape* shape_of(ChordQuality quality) {
    for (const Shape& shape : kShapes) {
        if (shape.quality == quality) {
            return &shape;
        }
    }
    return nullptr;
}

bool is_black(int note) {
    switch (((note % 12) + 12) % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

} // namespace

bool chord_is_minor_like(ChordQuality quality) {
    switch (quality) {
    case ChordQuality::Minor:
    case ChordQuality::Diminished:
    case ChordQuality::Minor7:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
    case ChordQuality::MinorMajor7:
    case ChordQuality::Minor6:
    case ChordQuality::Minor9:
        return true;
    default:
        return false;
    }
}

int chord_tone_count(ChordQuality quality) {
    const Shape* shape = shape_of(quality);
    return shape != nullptr ? shape->count : 0;
}

int chord_inversion_count(ChordQuality quality) {
    const Shape* shape = shape_of(quality);
    return shape != nullptr ? shape->inversions : 0;
}

int chord_bass_tone(ChordQuality quality, int inversion) {
    const Shape* shape = shape_of(quality);
    if (shape == nullptr) {
        return 0;
    }
    return shape->tones[static_cast<std::size_t>(std::clamp(inversion, 0, shape->inversions - 1))];
}

std::vector<int> chord_voicing(int root_pc, ChordQuality quality, int inversion) {
    std::vector<int> notes;
    const Shape* shape = shape_of(quality);
    if (shape == nullptr) {
        return notes;
    }
    const int root = ((root_pc % 12) + 12) % 12;
    const int bass = chord_bass_tone(quality, inversion);
    // The other tones above the bass in the order they come, each the nearest above the last.
    std::vector<int> above; // semitones above the bass, ascending
    for (int i = 0; i < shape->count; ++i) {
        const int tone = shape->tones[static_cast<std::size_t>(i)];
        if (tone != bass) {
            above.push_back((tone - bass + 12) % 12);
        }
    }
    std::sort(above.begin(), above.end());
    const int low = kMiddleC + (root + bass) % 12;
    notes.push_back(low);
    for (const int step : above) {
        notes.push_back(low + step);
    }
    return notes;
}

ChordDiagram chord_diagram(int root_pc, ChordQuality quality, int inversion) {
    ChordDiagram d;
    const int root = ((root_pc % 12) + 12) % 12;
    const int count = chord_inversion_count(quality);
    const int inv = count > 0 ? std::clamp(inversion, 0, count - 1) : 0;
    d.notes = chord_voicing(root, quality, inv);
    if (d.notes.empty()) {
        return d;
    }
    // The chord the player chose, with the bass the inversion puts below it, spelled from the
    // root's key: "Cm7/Eb", "C6/E", "Csus2/G", "Cdim7/Bbb". (Free play names what it hears, which
    // can be another chord on the same notes; the card names the one chosen.)
    Chord chord{true, root, quality, (root + chord_bass_tone(quality, inv)) % 12};
    {
        const KeyChange key = key_from_pitch_class(
            0, root, chord_is_minor_like(quality) ? KeyMode::Minor : KeyMode::Major);
        const ChordSpelling spelling = spell_chord(chord, &key);
        d.name = chord_name_parts(chord, spelling);
        d.chord = chord;
        // The keys.
        int first = d.notes.front();
        while (is_black(first)) {
            --first;
        }
        int last = d.notes.back();
        while (is_black(last)) {
            ++last;
        }
        // Up to the white key at or above the top note, and to eight white keys at least.
        int whites = 0;
        for (int note = first;; ++note) {
            DiagramKey key_info;
            key_info.note = note;
            key_info.black = is_black(note);
            key_info.white = whites;
            whites += key_info.black ? 0 : 1;
            d.picture.keys.push_back(key_info);
            if (note >= last && whites >= 8 && !key_info.black) {
                break;
            }
        }
        d.picture.white_keys = whites;
        const std::optional<ChordFingering> fingers = chord_fingering(quality, inv);
        d.picture.fingering =
            fingers.has_value() && fingers->count == static_cast<int>(d.notes.size());
        for (std::size_t i = 0; i < d.notes.size(); ++i) {
            DiagramKey& k = d.picture.keys[static_cast<std::size_t>(d.notes[i] - first)];
            k.in_scale = true;
            const int semitones = ((k.note - chord.root_pc) % 12 + 12) % 12;
            k.root = semitones == 0;
            const std::string name = chord_tone_name(chord, spelling, semitones);
            std::snprintf(k.name.data(), k.name.size(), "%s", name.c_str());
            if (d.picture.fingering) {
                k.right = fingers->right[i];
                k.left = fingers->left[i];
            }
        }
    }
    return d;
}

} // namespace etude
