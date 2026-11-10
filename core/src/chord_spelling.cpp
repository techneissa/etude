// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord_spelling.hpp"

#include "etude/pitch.hpp"

#include <array>
#include <cmath>
#include <cstdlib>

namespace etude {
namespace {

// Places on the line of fifths past which a note needs a double accidental (Fb is -8, B# is 12).
constexpr int kLowestSingle = -8;
constexpr int kHighestSingle = 12;
// What a chord tone with a double accidental adds to a spelling's cost, in steps of the line.
constexpr double kDoubleAccidentalCost = 4.0;

// Steps along the line of fifths from the root to the tone `semitones` above it, for the usual
// reading of each interval (a minor third is three steps down: C to Eb).
constexpr std::array<int, 12> kUsualSteps{0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};

int steps_of(ChordQuality quality, int semitones) {
    switch (quality) {
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
        if (semitones == 6) {
            return -6; // the flat fifth, not the sharp fourth
        }
        break;
    case ChordQuality::Diminished7:
        if (semitones == 6) {
            return -6;
        }
        if (semitones == 9) {
            return -9; // the doubly flat seventh
        }
        break;
    case ChordQuality::Augmented:
        if (semitones == 8) {
            return 8; // the sharp fifth, not the flat sixth
        }
        break;
    default:
        break;
    }
    return kUsualSteps[static_cast<std::size_t>(semitones)];
}

// The place of a pitch class nearest to `centre` among its places on the line (one per twelve).
int place_near(int pc, double centre) {
    const int base = (pc * 7) % 12;
    int best = base;
    double best_distance = std::abs(base - centre);
    for (int shift = -2; shift <= 1; ++shift) {
        const int line = base + 12 * shift;
        const double d = std::abs(line - centre);
        if (d < best_distance) {
            best = line;
            best_distance = d;
        }
    }
    return best;
}

// The middle of the key's own notes on the line of fifths: a major key's scale spans its tonic's
// place minus one to plus five, so the middle is its signature plus two. A major key leans a
// little flat (the lowered sixth and seventh are the common borrowed notes), a minor key sharp
// (the raised sixth and seventh are).
double centre_of(const KeyChange* key) {
    if (key == nullptr) {
        return 1.5;
    }
    const bool major_like = key->mode == KeyMode::Major || key->mode == KeyMode::Lydian ||
                            key->mode == KeyMode::Mixolydian;
    return key->fifths + 2.0 + (major_like ? -0.5 : 0.5);
}

// How far the chord's tones stand from the key's middle when its root is spelled at `root_line`;
// the root counts twice, so a chord is not rooted on B# to spare its seventh an accidental.
double cost_of(int root_line, ChordQuality quality, double centre) {
    const unsigned mask = chord_quality_mask(quality);
    double cost = std::abs(root_line - centre);
    for (int semitones = 0; semitones < 12; ++semitones) {
        if (!(mask & (1u << semitones))) {
            continue;
        }
        const int line = root_line + steps_of(quality, semitones);
        cost += std::abs(line - centre);
        if (line < kLowestSingle || line > kHighestSingle) {
            cost += kDoubleAccidentalCost;
        }
    }
    return cost;
}

int root_place(int root_pc, ChordQuality quality, double centre) {
    const int base = (root_pc * 7) % 12;
    int best = base;
    double best_cost = 1e9;
    for (int shift = -2; shift <= 1; ++shift) {
        const int line = base + 12 * shift;
        const double cost = cost_of(line, quality, centre);
        if (cost < best_cost - 1e-9 || (std::abs(cost - best_cost) <= 1e-9 &&
                                        std::abs(line - centre) < std::abs(best - centre))) {
            best = line;
            best_cost = cost;
        }
    }
    return best;
}

} // namespace

int line_pitch_class(int line) { return (((line * 7) % 12) + 12) % 12; }

std::string line_name(int line) {
    static constexpr char kLetters[] = {'F', 'C', 'G', 'D', 'A', 'E', 'B'};
    const int shifted = line + 1;
    const int letter = ((shifted % 7) + 7) % 7;
    const int accidentals = (shifted - letter) / 7; // whole sevens past F: sharps, or flats below
    std::string name(1, kLetters[letter]);
    name.append(static_cast<std::size_t>(std::abs(accidentals)), accidentals > 0 ? '#' : 'b');
    return name;
}

ChordSpelling spell_chord(const Chord& chord, const KeyChange* key) {
    if (!chord.found) {
        return {};
    }
    const double centre = centre_of(key);
    ChordSpelling spelling;
    spelling.root_line = root_place(chord.root_pc, chord.quality, centre);
    if (chord.bass_pc == chord.root_pc) {
        spelling.bass_line = spelling.root_line;
        return spelling;
    }
    const int semitones = ((chord.bass_pc - chord.root_pc) % 12 + 12) % 12;
    if (chord_quality_mask(chord.quality) & (1u << semitones)) {
        spelling.bass_line = spelling.root_line + steps_of(chord.quality, semitones);
    } else {
        spelling.bass_line = place_near(chord.bass_pc, centre);
    }
    return spelling;
}

std::string chord_tone_name(const Chord& chord, const ChordSpelling& spelling, int semitones) {
    if (!chord.found || semitones < 0 || semitones > 11) {
        return {};
    }
    // A power chord has no shape in the detection's list: its tones are the root and the fifth.
    const unsigned tones = chord.quality == ChordQuality::Power ? 0x81u
                                                                  : chord_quality_mask(chord.quality);
    if (!(tones & (1u << semitones))) {
        return {};
    }
    return line_name(spelling.root_line + steps_of(chord.quality, semitones));
}

Chord respell_symmetric(const Chord& chord, const KeyChange* key) {
    if (!chord.found) {
        return chord;
    }
    int step = 0;
    if (chord.quality == ChordQuality::Diminished7) {
        step = 3;
    } else if (chord.quality == ChordQuality::Augmented) {
        step = 4;
    } else {
        return chord;
    }
    const double centre = centre_of(key);
    Chord best = chord;
    double best_cost = 1e9;
    for (int root = chord.root_pc, n = 0; n < 12 / step; root = (root + step) % 12, ++n) {
        const double cost = cost_of(root_place(root, chord.quality, centre), chord.quality, centre);
        // A tie keeps the first root tried, the chord's own.
        if (cost < best_cost - 1e-9) {
            best_cost = cost;
            best.root_pc = root;
        }
    }
    return best;
}

} // namespace etude
