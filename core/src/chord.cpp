// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord.hpp"

#include "etude/pitch.hpp"

#include <array>
#include <initializer_list>

namespace etude {
namespace {

// A chord shape as a bit mask of semitones above the root (bit 0 is the root).
constexpr unsigned mask(std::initializer_list<int> semitones) {
    unsigned m = 0;
    for (int s : semitones) {
        m |= 1u << s;
    }
    return m;
}

using Template = ChordShape;

constexpr unsigned kFifth = 1u << 7;

// List order is the priority when a note set fits several chords and none is rooted on the bass.
constexpr std::array kTemplates{
    Template{mask({0, 4, 7}), ChordQuality::Major, 0},
    Template{mask({0, 3, 7}), ChordQuality::Minor, 0},
    Template{mask({0, 3, 6}), ChordQuality::Diminished, 0},
    Template{mask({0, 4, 8}), ChordQuality::Augmented, 0},
    Template{mask({0, 2, 7}), ChordQuality::Sus2, 0},
    Template{mask({0, 5, 7}), ChordQuality::Sus4, 0},
    Template{mask({0, 4, 7, 10}), ChordQuality::Dominant7, kFifth},
    Template{mask({0, 4, 7, 11}), ChordQuality::Major7, kFifth},
    Template{mask({0, 3, 7, 10}), ChordQuality::Minor7, kFifth},
    Template{mask({0, 3, 6, 10}), ChordQuality::HalfDiminished7, 0},
    Template{mask({0, 3, 6, 9}), ChordQuality::Diminished7, 0},
    Template{mask({0, 3, 7, 11}), ChordQuality::MinorMajor7, kFifth},
    Template{mask({0, 4, 10}), ChordQuality::Dominant7, 0}, // no fifth
    Template{mask({0, 5, 7, 10}), ChordQuality::Dominant7Sus4, kFifth},
    Template{mask({0, 2, 4, 7}), ChordQuality::Add9, 0},
    Template{mask({0, 2, 4, 7, 10}), ChordQuality::Dominant9, kFifth},
    Template{mask({0, 2, 4, 7, 11}), ChordQuality::Major9, kFifth},
    Template{mask({0, 2, 3, 7, 10}), ChordQuality::Minor9, kFifth},
    Template{mask({0, 2, 4, 10}), ChordQuality::Dominant9, 0}, // no fifth
    Template{mask({0, 2, 4, 11}), ChordQuality::Major9, 0},    // no fifth
    Template{mask({0, 2, 3, 10}), ChordQuality::Minor9, 0},    // no fifth
    Template{mask({0, 1, 4, 7, 10}), ChordQuality::Dominant7Flat9, kFifth},
    Template{mask({0, 1, 4, 10}), ChordQuality::Dominant7Flat9, 0}, // no fifth
    Template{mask({0, 2, 4, 7, 9}), ChordQuality::Major69, kFifth},
    Template{mask({0, 4, 7, 9, 10}), ChordQuality::Dominant13, kFifth},
    Template{mask({0, 2, 4, 7, 9, 10}), ChordQuality::Dominant13, kFifth | (1u << 2)},
    // Last, so Am7 wins over C6 when the bass is neither root.
    Template{mask({0, 4, 7, 9}), ChordQuality::Major6, 0},
    Template{mask({0, 3, 7, 9}), ChordQuality::Minor6, 0},
};

constexpr unsigned kPowerShape = mask({0, 7});

// The shape of `pcs` seen from `root`, as a semitone mask.
unsigned shape_from(unsigned pcs, int root) {
    unsigned shape = 0;
    for (int pc = 0; pc < 12; ++pc) {
        if (pcs & (1u << pc)) {
            shape |= 1u << ((pc - root + 12) % 12);
        }
    }
    return shape;
}

// Index into kTemplates for `pcs` rooted on `root`, or -1.
int match(unsigned pcs, int root) {
    const unsigned shape = shape_from(pcs, root);
    for (std::size_t i = 0; i < kTemplates.size(); ++i) {
        if (kTemplates[i].mask == shape) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

Chord detect_chord(const int* notes, std::size_t count) {
    if (notes == nullptr || count == 0) {
        return {};
    }
    unsigned pcs = 0;
    int lowest = notes[0];
    for (std::size_t i = 0; i < count; ++i) {
        if (!is_valid_note(notes[i])) {
            return {};
        }
        pcs |= 1u << pitch_class(notes[i]);
        if (notes[i] < lowest) {
            lowest = notes[i];
        }
    }
    const int bass = pitch_class(lowest);

    // A reading rooted on the bass beats any inversion.
    if (const int index = match(pcs, bass); index >= 0) {
        return Chord{true, bass, kTemplates[static_cast<std::size_t>(index)].quality, bass};
    }

    // Otherwise the highest-priority template wins; ties go to the root nearest above the bass.
    int best_index = -1;
    int best_root = 0;
    for (int step = 1; step < 12; ++step) {
        const int root = (bass + step) % 12;
        if (!(pcs & (1u << root))) {
            continue;
        }
        const int index = match(pcs, root);
        if (index >= 0 && (best_index < 0 || index < best_index)) {
            best_index = index;
            best_root = root;
        }
    }
    if (best_index < 0) {
        // A bare root and fifth is a power chord, but only with the root in the bass.
        if (shape_from(pcs, bass) == kPowerShape) {
            return Chord{true, bass, ChordQuality::Power, bass};
        }
        return {};
    }
    return Chord{true, best_root, kTemplates[static_cast<std::size_t>(best_index)].quality, bass};
}

std::span<const ChordShape> chord_shapes() { return kTemplates; }

unsigned chord_quality_mask(ChordQuality quality) {
    for (const Template& t : kTemplates) {
        if (t.quality == quality) {
            return t.mask;
        }
    }
    return 0;
}

const char* chord_suffix(ChordQuality quality) {
    switch (quality) {
    case ChordQuality::Major:
        return "";
    case ChordQuality::Minor:
        return "m";
    case ChordQuality::Diminished:
        return "dim";
    case ChordQuality::Augmented:
        return "aug";
    case ChordQuality::Sus2:
        return "sus2";
    case ChordQuality::Sus4:
        return "sus4";
    case ChordQuality::Dominant7:
        return "7";
    case ChordQuality::Major7:
        return "maj7";
    case ChordQuality::Minor7:
        return "m7";
    case ChordQuality::HalfDiminished7:
        return "m7b5";
    case ChordQuality::Diminished7:
        return "dim7";
    case ChordQuality::MinorMajor7:
        return "mMaj7";
    case ChordQuality::Major6:
        return "6";
    case ChordQuality::Minor6:
        return "m6";
    case ChordQuality::Add9:
        return "add9";
    case ChordQuality::Dominant9:
        return "9";
    case ChordQuality::Major9:
        return "maj9";
    case ChordQuality::Minor9:
        return "m9";
    case ChordQuality::Power:
        return "5";
    case ChordQuality::Dominant7Sus4:
        return "7sus4";
    case ChordQuality::Dominant7Flat9:
        return "7b9";
    case ChordQuality::Major69:
        return "6/9";
    case ChordQuality::Dominant13:
        return "13";
    case ChordQuality::None:
        break;
    }
    return "";
}

namespace {

// "C", "C#" or "Db" for a pitch class, borrowing the spelling from note_name().
std::string pitch_class_name(int pc, bool prefer_flats) {
    std::string name = note_name(kMiddleC + pc, prefer_flats);
    name.pop_back(); // octave digit, always a single digit for notes 60..71
    return name;
}

} // namespace

std::string chord_name(const Chord& chord, bool prefer_flats) {
    if (!chord.found) {
        return {};
    }
    std::string name = pitch_class_name(chord.root_pc, prefer_flats);
    name += chord_suffix(chord.quality);
    if (chord.bass_pc != chord.root_pc) {
        name += '/';
        name += pitch_class_name(chord.bass_pc, prefer_flats);
    }
    return name;
}

} // namespace etude
