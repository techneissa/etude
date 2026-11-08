// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/shift_offer.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

ShiftFits high_song() {
    std::vector<Note> notes;
    for (int p = 84; p <= 108; ++p) {
        Note n;
        n.pitch = static_cast<std::uint8_t>(p);
        notes.push_back(n);
    }
    return shift_fits(notes, kRange61);
}

ShiftResult shift_of(int offset) {
    ShiftResult r;
    r.status = ShiftStatus::Shift;
    r.offset_semitones = offset;
    r.confidence = 1.0;
    r.evidence = ShiftEvidence::Range;
    return r;
}

} // namespace

TEST_CASE("shift offer: a song with notes outside raises the octave down") {
    ShiftOfferState s;
    s.begin(false);
    CHECK_FALSE(s.offer().has_value());
    s.judge_song(high_song(), 0);
    REQUIRE(s.offer().has_value());
    CHECK(s.offer()->target == -12);
    CHECK(s.offer()->from == 0);
    CHECK(s.offer()->source == OfferSource::Song);
}

TEST_CASE(
    "shift offer: the song's offer follows the transposition and goes when it helps no more") {
    ShiftOfferState s;
    s.begin(false);
    const ShiftFits fits = high_song();
    s.judge_song(fits, 0);
    REQUIRE(s.offer().has_value());
    s.judge_song(fits, 2);
    REQUIRE(s.offer().has_value());
    CHECK(s.offer()->target == -10);
    s.judge_song(fits, -12); // applied by a saved choice arriving: nothing left to propose
    CHECK_FALSE(s.offer().has_value());
}

TEST_CASE("shift offer: dismiss hides the card until the song or keyboard is new") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_song(high_song(), 0);
    REQUIRE(s.offer().has_value());
    s.dismiss();
    CHECK_FALSE(s.offer().has_value());
    s.judge_song(high_song(), 0);
    CHECK_FALSE(s.offer().has_value());
    s.judge_keys(shift_of(-12), 0, kRange61);
    CHECK_FALSE(s.offer().has_value());
    s.begin(false); // another song, or the keyboard changed
    s.judge_song(high_song(), 0);
    CHECK(s.offer().has_value());
}

TEST_CASE(
    "shift offer: don't ask again keeps the song quiet, and begin() restores the saved choice") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_song(high_song(), 0);
    s.never();
    CHECK(s.never_ask());
    CHECK_FALSE(s.offer().has_value());
    s.judge_song(high_song(), 0);
    s.judge_keys(shift_of(-12), 0, kRange61);
    CHECK_FALSE(s.offer().has_value());
    s.begin(true); // the same song comes back with the saved choice
    s.judge_song(high_song(), 0);
    CHECK_FALSE(s.offer().has_value());
    CHECK(s.never_ask());
    s.begin(false); // a song that never asked
    CHECK_FALSE(s.never_ask());
}

TEST_CASE("shift offer: a transposition the player sets ends the offer") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_song(high_song(), 0);
    REQUIRE(s.offer().has_value());
    s.player_changed();
    CHECK_FALSE(s.offer().has_value());
    s.judge_song(high_song(), 3);
    CHECK_FALSE(s.offer().has_value());
}

TEST_CASE("shift offer: accepting ends the offer for good") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_song(high_song(), 0);
    s.accept();
    CHECK_FALSE(s.offer().has_value());
    s.judge_song(high_song(), 0);
    CHECK_FALSE(s.offer().has_value());
}

TEST_CASE("shift offer, keys: the detector's offset is answered by a transposition of the opposite "
          "sign") {
    ShiftOfferState s;
    s.begin(false);
    // The keyboard sent 12 too high: add -12 to get the key meant; the song moves +12 to meet it.
    s.judge_keys(shift_of(-12), 0, kRange61);
    REQUIRE(s.offer().has_value());
    CHECK(s.offer()->target == 12);
    CHECK(s.offer()->source == OfferSource::Keys);
    s.begin(false);
    s.judge_keys(shift_of(12), 0, kRange61);
    REQUIRE(s.offer().has_value());
    CHECK(s.offer()->target == -12);
}

TEST_CASE("shift offer, keys: the shift is added to the shift of the keyboard's notes in force") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_keys(shift_of(12), -5, kRange61); // the notes are 12 lower than the model says: -17
    REQUIRE(s.offer().has_value());
    CHECK(s.offer()->target == -17);
    CHECK(s.offer()->from == -5);
    s.begin(false);
    s.judge_keys(shift_of(-12), -5, kRange61); // -5 + 12 = 7
    REQUIRE(s.offer().has_value());
    CHECK(s.offer()->target == 7);
}

TEST_CASE("shift offer, keys: three octaves is the reach, one semitone past raises nothing") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_keys(shift_of(-12), 25, kRange61); // 37
    CHECK_FALSE(s.offer().has_value());
    s.judge_keys(shift_of(-12), 24, kRange61); // 36
    CHECK(s.offer().has_value());
}

TEST_CASE("shift offer, keys: a range the shift would take past MIDI raises nothing") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_keys(shift_of(-12), 0, KeyRange{60, 116}); // 72..128
    CHECK_FALSE(s.offer().has_value());
    s.judge_keys(shift_of(-12), 0, KeyRange{60, 115}); // 72..127
    CHECK(s.offer().has_value());
    s.begin(false);
    s.judge_keys(shift_of(12), 0, KeyRange{11, 100}); // -1..88
    CHECK_FALSE(s.offer().has_value());
    s.judge_keys(shift_of(12), 0, KeyRange{12, 100}); // 0..88
    CHECK(s.offer().has_value());
}

TEST_CASE("shifted range: the keys move together and never leave 0..127") {
    CHECK(shifted_range(kRange61, 12).value().lowest == 48);
    CHECK(shifted_range(kRange61, 12).value().highest == 108);
    CHECK(shifted_range(kRange61, -36).value().lowest == 0);
    CHECK_FALSE(shifted_range(kRange61, -37).has_value());
    CHECK(shifted_range(KeyRange{21, 108}, 19).value().highest == 127);
    CHECK_FALSE(shifted_range(KeyRange{21, 108}, 20).has_value());
}

TEST_CASE("shift offer, keys: NotSure changes nothing and NoShift withdraws the keys' offer") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_keys(shift_of(-12), 0, kRange61);
    REQUIRE(s.offer().has_value());
    s.judge_keys(ShiftResult{}, 0, kRange61); // NotSure
    CHECK(s.offer().has_value());
    ShiftResult none;
    none.status = ShiftStatus::NoShift;
    s.judge_keys(none, 0, kRange61);
    CHECK_FALSE(s.offer().has_value());
}

TEST_CASE("shift offer, keys: a shift of zero proposes nothing") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_keys(shift_of(0), 4, kRange61);
    CHECK_FALSE(s.offer().has_value());
}

TEST_CASE("shift offer: the song's judging leaves a card the keys raised alone") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_keys(shift_of(-12), 0, kRange61);
    REQUIRE(s.offer().has_value());
    s.judge_song(high_song(), 0);
    CHECK(s.offer()->source == OfferSource::Keys);
    CHECK(s.offer()->target == 12);
}

TEST_CASE("shift offer: a saved choice arriving later silences the song, and going lets it back") {
    ShiftOfferState s;
    s.begin(false);
    s.judge_song(high_song(), 0);
    REQUIRE(s.offer().has_value());
    s.set_never(true);
    CHECK_FALSE(s.offer().has_value());
    s.judge_song(high_song(), 0);
    CHECK_FALSE(s.offer().has_value());
    s.set_never(false);
    s.judge_song(high_song(), 0);
    CHECK(s.offer().has_value());
}
