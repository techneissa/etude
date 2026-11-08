// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord_fingering.hpp"

namespace etude {

namespace {

// Triads, by inversion. The right hand takes 1-3-5 in root position and second inversion and 1-2-5 in
// first inversion; the left hand mirrors it, 5-3-1 and 5-2-1.
constexpr std::array<std::array<int, 3>, 3> kTriadRight{{{1, 3, 5}, {1, 2, 5}, {1, 3, 5}}};
constexpr std::array<std::array<int, 3>, 3> kTriadLeft{{{5, 3, 1}, {5, 2, 1}, {5, 3, 1}}};

// Seventh chords, by inversion: 1-2-3-5 in the right hand (1-2-4-5 with the seventh and root
// together in the second inversion), 5-3-2-1 in the left.
constexpr std::array<std::array<int, 4>, 4> kSeventhRight{
    {{1, 2, 3, 5}, {1, 2, 3, 5}, {1, 2, 4, 5}, {1, 2, 3, 5}}};
constexpr std::array<std::array<int, 4>, 4> kSeventhLeft{
    {{5, 3, 2, 1}, {5, 3, 2, 1}, {5, 3, 2, 1}, {5, 3, 2, 1}}};

enum class Family { None, Triad, Seventh };

Family family_of(ChordQuality quality) {
    switch (quality) {
    case ChordQuality::Major:
    case ChordQuality::Minor:
    case ChordQuality::Diminished:
    case ChordQuality::Augmented:
        return Family::Triad;
    case ChordQuality::Dominant7:
    case ChordQuality::Major7:
    case ChordQuality::Minor7:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
    case ChordQuality::MinorMajor7:
        return Family::Seventh;
    default:
        return Family::None;
    }
}

} // namespace

bool chord_has_fingering(ChordQuality quality) { return family_of(quality) != Family::None; }

std::optional<ChordFingering> chord_fingering(ChordQuality quality, int inversion) {
    const Family family = family_of(quality);
    ChordFingering f;
    if (family == Family::Triad && inversion >= 0 && inversion < 3) {
        f.count = 3;
        for (std::size_t i = 0; i < 3; ++i) {
            f.right[i] = kTriadRight[static_cast<std::size_t>(inversion)][i];
            f.left[i] = kTriadLeft[static_cast<std::size_t>(inversion)][i];
        }
        return f;
    }
    if (family == Family::Seventh && inversion >= 0 && inversion < 4) {
        f.count = 4;
        for (std::size_t i = 0; i < 4; ++i) {
            f.right[i] = kSeventhRight[static_cast<std::size_t>(inversion)][i];
            f.left[i] = kSeventhLeft[static_cast<std::size_t>(inversion)][i];
        }
        return f;
    }
    return std::nullopt;
}

} // namespace etude
