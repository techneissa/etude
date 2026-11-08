// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hit_line.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {
constexpr int kUndecided = -1;
const int kMissed = static_cast<int>(NoteGrade::Missed);
const int kPerfect = static_cast<int>(NoteGrade::Perfect);
} // namespace

TEST_CASE("hit line: a held key lights the note its press struck") {
    CHECK(note_struck(1.0, true, 1.0, kUndecided));
    CHECK(note_struck(1.0, true, 1.0, kPerfect));
}

TEST_CASE("hit line: a released key lights nothing") {
    CHECK_FALSE(note_struck(1.0, false, 1.0, kPerfect));
}

TEST_CASE("hit line: a held wrong key lights no falling note") {
    // The key's latest press hit no note.
    CHECK_FALSE(note_struck(std::nullopt, true, 1.0, kUndecided));
    // It hit another note: a held key lights only the note it struck.
    CHECK_FALSE(note_struck(5.0, true, 1.0, kUndecided));
}

TEST_CASE("hit line: a note graded Missed is never lit") {
    CHECK_FALSE(note_struck(1.0, true, 1.0, kMissed));
}

TEST_CASE("hit line: notes of one strike share the light, a note further off does not") {
    CHECK(note_struck(1.0, true, 1.0 + kChordSeconds, kUndecided)); // a chord or unison spread
    CHECK_FALSE(note_struck(1.0, true, 1.0 + kChordSeconds + 0.001, kUndecided));
    CHECK(note_struck(1.0, true, 1.0 - kChordSeconds, kUndecided));
}

TEST_CASE("hit line: a wrong press's flash starts full and is gone after kWrongFlashNs") {
    CHECK(wrong_flash_alpha(0) == 1.0f);
    CHECK(wrong_flash_alpha(-5) == 1.0f);
    float last = 1.0f;
    for (std::int64_t t = 0; t < kWrongFlashNs; t += kWrongFlashNs / 20) {
        const float a = wrong_flash_alpha(t);
        CHECK(a <= last);
        CHECK(a > 0.0f);
        last = a;
    }
    CHECK(wrong_flash_alpha(kWrongFlashNs - 1'000'000) > 0.0f);
    CHECK(wrong_flash_alpha(kWrongFlashNs) == 0.0f);
    CHECK(wrong_flash_alpha(kWrongFlashNs * 4) == 0.0f);
}

TEST_CASE("hit line: with reduced effects the flash is steady, then gone") {
    CHECK(wrong_flash_alpha(kWrongFlashNs / 2, false) == 1.0f);
    CHECK(wrong_flash_alpha(kWrongFlashNs - 1, false) == 1.0f);
    CHECK(wrong_flash_alpha(kWrongFlashNs, false) == 0.0f);
}

TEST_CASE("hit line: rising blocks are full in free play, hidden in a song, faint when asked for") {
    CHECK(rising_alpha(false, false) == 1.0f);
    CHECK(rising_alpha(false, true) == 1.0f);
    CHECK(rising_alpha(true, false) == 0.0f);
    CHECK(rising_alpha(true, true) == kRisingInSongAlpha);
    CHECK(kRisingInSongAlpha > 0.0f);
    CHECK(kRisingInSongAlpha < 1.0f);
}

TEST_CASE("hit line: a key's flash level is its strongest wrong press, and nothing else moves") {
    std::array<float, 128> levels{};
    add_wrong_flash(levels, 62, kWrongFlashNs / 2, true);
    const float half = levels[62];
    CHECK(half > 0.0f);
    CHECK(half < 1.0f);
    add_wrong_flash(levels, 62, 0, true); // a fresher press on the same key
    CHECK(levels[62] == 1.0f);
    add_wrong_flash(levels, 62, kWrongFlashNs / 2, true); // an older one never lowers it
    CHECK(levels[62] == 1.0f);
    add_wrong_flash(levels, 64, kWrongFlashNs, true); // already over
    CHECK(levels[64] == 0.0f);
    add_wrong_flash(levels, 66, kWrongFlashNs / 2, false); // reduced effects: steady
    CHECK(levels[66] == 1.0f);
    for (int k = 0; k < 128; ++k) {
        if (k != 62 && k != 66) {
            CHECK(levels[static_cast<std::size_t>(k)] == 0.0f);
        }
    }
}
