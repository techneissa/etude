// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: the octave offer raised by the notes a keyboard sends.
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

TEST_CASE(
    "octave offer: a song with notes outside the keyboard raises the octave down, and accepting "
    "moves the keys to press") {
    SongController c;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);                   // 36..96
    c.load(two_hand_song(2, false, 100, 0x51), now); // right hand 100..106: all over the top
    c.update(now + 16 * kMs, {});
    REQUIRE(c.shift_fits() != nullptr);
    CHECK(c.shift_fits()->outside_at(0) == 8);
    CHECK(c.shift_fits()->outside_at(-12) == 0);
    REQUIRE(c.shift_offer().has_value());
    CHECK(c.shift_offer()->target == -12);
    CHECK(c.shift_offer()->from == 0);
    CHECK(c.shift_offer()->source == OfferSource::Song);
    const auto right_hand = [&] {
        for (const Note& n : c.song().song.notes) {
            if (n.pitch == 100) {
                return n;
            }
        }
        FAIL("no note on 100");
        return Note{};
    };
    CHECK(c.transposition().expected_pitch(right_hand()).folded); // today: 100 folds down to 88
    c.accept_shift_offer(now + 32 * kMs);
    CHECK(c.transpose() == -12);
    CHECK_FALSE(c.shift_offer().has_value());
    // The key to press is the shifted one, shown, expected and played from the one transposition.
    const TransposedPitch key = c.transposition().expected_pitch(right_hand());
    CHECK(key.pitch == 100 - 12);
    CHECK_FALSE(key.folded);
    c.update(now + 48 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value()); // accepting is for good
}

TEST_CASE("octave offer: a song that fits the keyboard raises nothing, and a stray note neither") {
    SongController c;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(), now); // 48 and 72..78
    c.update(now + 16 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value());
    CHECK(c.shift_fits()->outside_at(0) == 0);
}

TEST_CASE(
    "octave offer: dismiss hides it until the song or the keyboard is new, never keeps it away") {
    SongController c;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(2, false, 100, 0x52), now);
    c.update(now + 16 * kMs, {});
    REQUIRE(c.shift_offer().has_value());
    c.dismiss_shift_offer();
    CHECK_FALSE(c.shift_offer().has_value());
    c.update(now + 32 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value());
    // The keyboard changes (another is plugged in): judged again.
    c.set_keyboard(kRange76,
                   now + 48 * kMs); // 28..103: the top two notes of each bar are still over
    c.update(now + 64 * kMs, {});
    CHECK(c.shift_offer().has_value());
    // "Don't ask again" is a choice kept with the song's practice choices: it comes back with them
    // when the song is opened again (the sync applies them), and another song has none.
    c.never_offer_shift();
    CHECK_FALSE(c.shift_offer().has_value());
    SongPractice saved;
    c.practice_into(saved);
    CHECK(saved.no_shift_offer);
    c.load(two_hand_song(2, false, 100, 0x52), now + 80 * kMs);
    c.update(now + 96 * kMs, {});
    CHECK(c.shift_offer().has_value()); // opened without its choices: nothing is known yet
    c.apply_practice(saved, now + 100 * kMs);
    CHECK_FALSE(c.shift_offer().has_value());
    c.update(now + 112 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value());
    SongPractice again;
    c.practice_into(again);
    CHECK(again.no_shift_offer);
    c.load(two_hand_song(2, false, 100, 0x53), now + 128 * kMs);
    c.update(now + 144 * kMs, {});
    CHECK(c.shift_offer().has_value());
    SongPractice other;
    c.practice_into(other);
    CHECK_FALSE(other.no_shift_offer);
    // The choice taken back (the player's own, or a stored one that says so) lets it offer again.
    c.apply_practice(saved, now + 160 * kMs);
    CHECK_FALSE(c.shift_offer().has_value());
    c.apply_practice(other, now + 176 * kMs);
    c.update(now + 192 * kMs, {});
    CHECK(c.shift_offer().has_value());
}

TEST_CASE(
    "octave offer: a transposition the player sets ends it, and so does one the song was saved "
    "with, also when it comes after the first frames") {
    SongController c;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(2, false, 100, 0x54), now);
    c.update(now + 16 * kMs, {});
    REQUIRE(c.shift_offer().has_value());
    // The player's own transposition.
    c.player_set_transpose(5, now + 64 * kMs);
    CHECK_FALSE(c.shift_offer().has_value());
    c.update(now + 80 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value());
    CHECK(c.transpose() == 5);

    // The saved choices arrive before the first frame.
    SongPractice saved;
    saved.transpose_semitones = -12;
    c.load(two_hand_song(2, false, 100, 0x55), now + 100 * kMs);
    c.apply_practice(saved, now + 100 * kMs);
    c.update(now + 116 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value());
    // Back to no shift by hand's own set_transpose (not the player's): still settled.
    c.set_transpose(0, now + 132 * kMs);
    c.update(now + 148 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value());

    // The saved choices arrive after the offer was raised (the history was still loading).
    c.load(two_hand_song(2, false, 100, 0x56), now + 200 * kMs);
    c.update(now + 216 * kMs, {});
    REQUIRE(c.shift_offer().has_value());
    c.apply_practice(saved, now + 232 * kMs);
    CHECK_FALSE(c.shift_offer().has_value());
    c.update(now + 248 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value());
    // A new keyboard starts the offer over, but the saved transposition still settles it.
    c.set_keyboard(kRange76, now + 264 * kMs);
    c.update(now + 280 * kMs, {});
    CHECK_FALSE(c.shift_offer().has_value());

    // A song saved with no transposition (the default) is offered as ever.
    c.load(two_hand_song(2, false, 100, 0x57), now + 300 * kMs);
    c.apply_practice(SongPractice{}, now + 300 * kMs);
    c.update(now + 316 * kMs, {});
    CHECK(c.shift_offer().has_value());
}

TEST_CASE("octave offer: a keyboard sending every note an octave high raises the offer from its "
          "note-ons") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(), now); // 48 and 72..78: all inside, so the song alone offers nothing
    c.play(now);
    // The player plays every note on time, the keyboard sending it 12 higher.
    int played = 0;
    for (int bar = 0; bar < 2 && !c.shift_offer().has_value(); ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const std::int64_t at = kSec + (bar * 4 + beat) * 500 * kMs;
            run(c, piano, now, at);
            std::vector<MidiEvent> events{midi_note_on(72 + beat * 2 + 12, at)};
            if (beat == 0) {
                events.push_back(midi_note_on(48 + 12, at));
            }
            run(c, piano, now, at + 16 * kMs, events);
            played += static_cast<int>(events.size());
        }
    }
    REQUIRE(c.shift_offer().has_value());
    CHECK(played >= 8); // the song evidence needs eight notes that came with an expectation
    CHECK(c.shift_offer()->source == OfferSource::Keys);
    CHECK(c.shift_offer()->target == 12); // the keyboard's notes are 12 above what its profile says
    CHECK(c.shift_offer()->from == 0);
    // The song stays as it is: accepting asks the app to take the keyboard's notes as 12 higher.
    c.accept_shift_offer(now);
    CHECK(c.transpose() == 0);
    int asked = 0;
    REQUIRE(c.take_keyboard_shift(&asked));
    CHECK(asked == 12);
    CHECK_FALSE(c.take_keyboard_shift(&asked)); // once
    // The detector forgot its notes with the acceptance: nothing is offered again.
    run(c, piano, now, now + 500 * kMs);
    CHECK_FALSE(c.shift_offer().has_value());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE(
    "octave offer: the typing keys and the screen's keys are never read as a shifted keyboard") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(), now);
    c.play(now);
    for (int bar = 0; bar < 2; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const std::int64_t at = kSec + (bar * 4 + beat) * 500 * kMs;
            run(c, piano, now, at);
            // note_on() is a typing key's.
            const std::array<MidiEvent, 2> events{note_on(72 + beat * 2 + 12, at), note_on(60, at)};
            run(c, piano, now, at + 16 * kMs, events);
        }
    }
    CHECK_FALSE(c.shift_offer().has_value());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("octave offer: notes played where the song asks, over the same stretch, raise nothing") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(), now);
    c.play(now);
    for (int bar = 0; bar < 2; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const std::int64_t at = kSec + (bar * 4 + beat) * 500 * kMs;
            run(c, piano, now, at);
            const std::array<MidiEvent, 2> events{midi_note_on(72 + beat * 2, at),
                                                  midi_note_on(48, at)};
            run(c, piano, now, at + 16 * kMs, events);
        }
    }
    CHECK_FALSE(c.shift_offer().has_value());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

// What the screen shows a player of a keyboard whose octave button is on: they press the key that
// sends the lit pitch. `bars` bars of two_hand_song(2, false, 102): the right hand 102..108, the
// left hand 48, each pressed at its own pitch.
static RunRecord play_by_the_screen(SongController& c, std::int64_t& now) {
    c.set_practice_hands(HandFilter::Both, now);
    c.play(now);
    const std::int64_t start = now;
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int bar = 0; bar < 2; ++bar) {
        const std::int64_t at = start + bar * 2 * kSec;
        script.push_back({at, midi_note_on(48, at)});
        for (int beat = 0; beat < 4; ++beat) {
            const std::int64_t t = at + beat * 500 * kMs;
            script.push_back({t, midi_note_on(102 + beat * 2, t)});
        }
    }
    std::size_t next = 0;
    std::vector<MidiEvent> frame;
    std::vector<SongSoundEvent> out;
    while (c.state() != TransportState::Ended && now < start + 10 * kSec) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        out.clear();
        c.take_sound_events(out);
    }
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    return runs[0].record;
}

TEST_CASE(
    "octave offer: after accepting, a keyboard that sends everything an octave high plays the song "
    "with no note wrong, at both edges of what it sends") {
    // The base keyboard is 61 keys (36..96); the octave button is on, so it sends 48..108.
    const KeyRange sent = *shifted_range(kRange61, 12);
    REQUIRE(sent.lowest == 48);
    REQUIRE(sent.highest == 108);
    // Before: the app thinks it sends 36..96, so 102..108 fold down and the presses are wrong.
    {
        SongController c;
        std::int64_t now = kSec;
        c.set_keyboard(kRange61, now);
        c.load(two_hand_song(2, false, 102, 0x61), now);
        const RunRecord r = play_by_the_screen(c, now);
        CHECK(r.wrong > 0);
    }
    // After accepting: the range the keyboard sends is the layout, the song is not moved.
    SongController c;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(2, false, 72, 0x62), now); // the song the offer came from
    c.play(now);
    FakePiano piano;
    for (int bar = 0; bar < 2 && !c.shift_offer().has_value(); ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const std::int64_t at = kSec + (bar * 4 + beat) * 500 * kMs;
            run(c, piano, now, at);
            std::vector<MidiEvent> events{midi_note_on(72 + beat * 2 + 12, at)};
            if (beat == 0) {
                events.push_back(midi_note_on(48 + 12, at));
            }
            run(c, piano, now, at + 16 * kMs, events);
        }
    }
    REQUIRE(c.shift_offer().has_value());
    REQUIRE(c.shift_offer()->source == OfferSource::Keys);
    c.accept_shift_offer(now);
    int shift = 0;
    REQUIRE(c.take_keyboard_shift(&shift)); // what main.cpp does with it
    CHECK(shift == 12);
    CHECK(c.transpose() == 0);
    c.set_keyboard_shift(shift);
    c.set_keyboard(sent, now);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    SongController d;
    now = kSec;
    d.set_keyboard_shift(shift);
    d.set_keyboard(sent, now);
    d.load(two_hand_song(2, false, 102, 0x61), now);
    CHECK(d.transposition().expected_pitch(d.song().song.notes.back()).pitch ==
          d.song().song.notes.back().pitch); // the top of the range is a key: 108 is not folded
    const RunRecord r = play_by_the_screen(d, now);
    CHECK(r.wrong == 0);
    CHECK(r.right.graded == 8);
    CHECK(r.right.hit == 8);
    CHECK(r.left.graded == 2);
    CHECK(r.left.hit == 2);
}

TEST_CASE("octave offer: with the keyboard shifted, notes under its lowest key fold up to it") {
    SongController c;
    std::int64_t now = kSec;
    c.set_keyboard_shift(12);
    c.set_keyboard(*shifted_range(kRange61, 12), now); // 48..108
    c.load(two_hand_song(2, false, 72, 0x63), now);
    // The left hand's C3 (48) is the lowest key now; a note a semitone under would fold up an
    // octave.
    CHECK(c.transposition().expected_pitch(c.song().song.notes.front()).folded == false);
    Note under;
    under.pitch = 47;
    const TransposedPitch folded = c.transposition().expected_pitch(under);
    CHECK(folded.folded);
    CHECK(folded.pitch == 59);
}

TEST_CASE("octave offer: a keys offer is judged against the shift in force, and past MIDI raises "
          "nothing") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    // A keyboard already known to send 12 high, now sending 12 more: the offer names 24 in all.
    c.set_keyboard_shift(12);
    c.set_keyboard(*shifted_range(kRange61, 12), now);
    c.load(two_hand_song(), now);
    c.play(now);
    for (int bar = 0; bar < 2 && !c.shift_offer().has_value(); ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const std::int64_t at = kSec + (bar * 4 + beat) * 500 * kMs;
            run(c, piano, now, at);
            std::vector<MidiEvent> events{midi_note_on(72 + beat * 2 + 12, at)};
            if (beat == 0) {
                events.push_back(midi_note_on(48 + 12, at));
            }
            run(c, piano, now, at + 16 * kMs, events);
        }
    }
    REQUIRE(c.shift_offer().has_value());
    CHECK(c.shift_offer()->target == 24);
    CHECK(c.shift_offer()->from == 12);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("octave offer: expected_around is the practised notes near the clock, shifted, and none "
          "while paused") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(), now);
    std::array<int, kOctaveShiftMaxExpected> out{};
    CHECK(c.expected_around(out) == 0); // paused
    c.play(now);
    run(c, piano, now, now + 16 * kMs);
    const int n = c.expected_around(out);
    REQUIRE(n == 2); // the chord at the start: the left hand's 48 and the right hand's 72
    CHECK(((out[0] == 48 && out[1] == 72) || (out[0] == 72 && out[1] == 48)));
    c.set_practice_hands(HandFilter::Right, now);
    CHECK(c.expected_around(out) == 1);
    CHECK(out[0] == 72);
    c.set_transpose(5, now);
    CHECK(c.expected_around(out) == 1);
    CHECK(out[0] == 77); // after the transposition, as the evaluator expects it
    std::vector<SongSoundEvent> sound;
    c.shutdown(now, sound);
}

// ---- What the player played, note by note
