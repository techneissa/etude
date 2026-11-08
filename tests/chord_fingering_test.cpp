// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/chord_diagram.hpp"
#include "etude/chord_fingering.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace etude;

namespace {

std::vector<int> right(const ChordFingering& f) {
    return {f.right.begin(), f.right.begin() + f.count};
}
std::vector<int> left(const ChordFingering& f) {
    return {f.left.begin(), f.left.begin() + f.count};
}

constexpr std::array kTriads{ChordQuality::Major, ChordQuality::Minor, ChordQuality::Diminished,
                             ChordQuality::Augmented};
constexpr std::array kSevenths{ChordQuality::Dominant7,       ChordQuality::Major7,
                               ChordQuality::Minor7,          ChordQuality::HalfDiminished7,
                               ChordQuality::Diminished7,     ChordQuality::MinorMajor7};

} // namespace

TEST_CASE("chord fingering: a triad is 1-3-5 over 5-3-1, with 1-2-5 in the first inversion") {
    for (const ChordQuality q : kTriads) {
        CAPTURE(static_cast<int>(q));
        REQUIRE(chord_has_fingering(q));
        const auto root = chord_fingering(q, 0);
        const auto first = chord_fingering(q, 1);
        const auto second = chord_fingering(q, 2);
        REQUIRE(root.has_value());
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(root->count == 3);
        CHECK(right(*root) == std::vector<int>{1, 3, 5});
        CHECK(left(*root) == std::vector<int>{5, 3, 1});
        CHECK(right(*first) == std::vector<int>{1, 2, 5});
        CHECK(left(*first) == std::vector<int>{5, 2, 1});
        CHECK(right(*second) == std::vector<int>{1, 3, 5});
        CHECK(left(*second) == std::vector<int>{5, 3, 1});
        // A triad has three inversions and no more.
        CHECK_FALSE(chord_fingering(q, 3).has_value());
    }
}

TEST_CASE("chord fingering: a seventh chord is 1-2-3-5 over 5-3-2-1, 1-2-4-5 in the second inversion") {
    for (const ChordQuality q : kSevenths) {
        CAPTURE(static_cast<int>(q));
        REQUIRE(chord_has_fingering(q));
        for (int inversion = 0; inversion < 4; ++inversion) {
            const auto f = chord_fingering(q, inversion);
            REQUIRE(f.has_value());
            CHECK(f->count == 4);
            CHECK(left(*f) == std::vector<int>{5, 3, 2, 1});
            CHECK(right(*f) == (inversion == 2 ? std::vector<int>{1, 2, 4, 5}
                                               : std::vector<int>{1, 2, 3, 5}));
        }
        CHECK_FALSE(chord_fingering(q, 4).has_value());
    }
}

TEST_CASE("chord fingering: only triads and sevenths have a standard, every other chord shows none") {
    for (const ChordQuality q : kChordCardQualities) {
        const bool listed = std::find(kTriads.begin(), kTriads.end(), q) != kTriads.end() ||
                            std::find(kSevenths.begin(), kSevenths.end(), q) != kSevenths.end();
        CAPTURE(static_cast<int>(q));
        CHECK(chord_has_fingering(q) == listed);
        for (int inversion = -1; inversion < 6; ++inversion) {
            const auto f = chord_fingering(q, inversion);
            if (!listed) {
                CHECK_FALSE(f.has_value());
            } else if (f) {
                // One finger a note, each 1 to 5, the right hand rising and the left falling.
                const int count = f->count;
                CHECK(count == chord_tone_count(q));
                for (int i = 0; i < count; ++i) {
                    CHECK(f->right[static_cast<std::size_t>(i)] >= 1);
                    CHECK(f->right[static_cast<std::size_t>(i)] <= 5);
                    CHECK(f->left[static_cast<std::size_t>(i)] >= 1);
                    CHECK(f->left[static_cast<std::size_t>(i)] <= 5);
                    if (i > 0) {
                        CHECK(f->right[static_cast<std::size_t>(i)] > f->right[static_cast<std::size_t>(i - 1)]);
                        CHECK(f->left[static_cast<std::size_t>(i)] < f->left[static_cast<std::size_t>(i - 1)]);
                    }
                }
            }
        }
    }
    CHECK_FALSE(chord_has_fingering(ChordQuality::None));
}
