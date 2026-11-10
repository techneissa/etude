// SPDX-License-Identifier: GPL-3.0-or-later
// Transposition and key ranges: one offset, folds, the octave offer, typing and screen keys.
#include "song_controller_test_support.hpp"

#include "etude/computer_keyboard.hpp"
#include "etude/key_state.hpp"
#include "etude/keyboard.hpp"
#include "etude/keyboard_identity.hpp"
#include "etude/library.hpp"
#include "etude/note_window.hpp"
#include "etude/octave_shift.hpp"
#include "etude/pitch.hpp"
#include "etude/pointer_keys.hpp"
#include "etude/shift_fit.hpp"
#include "etude/shift_offer.hpp"
#include "etude/transpose.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <optional>
#include <random>

using namespace etude;
using namespace song_test;

namespace {

Note note_at(int pitch) {
    Note n;
    n.pitch = static_cast<std::uint8_t>(pitch);
    return n;
}

// A song's notes, `count` of them with pitch `pitch_of(i)`.
std::vector<Note> notes_of(int count, const std::function<int(int)>& pitch_of) {
    std::vector<Note> notes;
    for (int i = 0; i < count; ++i) {
        notes.push_back(note_at(pitch_of(i)));
    }
    return notes;
}

} // namespace

TEST_CASE("transpose: a shift moves a note, past the keyboard's edge it folds one octave") {
    CHECK(Transposition(7, kRange61).map(60).pitch == 67);
    CHECK_FALSE(Transposition(7, kRange61).map(60).folded);
    for (const KeyRange range : {kRange61, kRange88}) {
        for (int s = 1; s <= 12; ++s) {
            const TransposedPitch top = Transposition(s, range).map(range.highest);
            CHECK(top.folded);
            CHECK(top.fold_octaves == -1);
            CHECK(top.pitch == range.highest + s - 12);
            const TransposedPitch bottom = Transposition(-s, range).map(range.lowest);
            CHECK(bottom.folded);
            CHECK(bottom.fold_octaves == 1);
            CHECK(bottom.pitch == range.lowest - s + 12);
        }
        // The key at the edge stays; the one past it folds.
        const Transposition t(3, range);
        CHECK_FALSE(t.map(range.highest - 3).folded);
        CHECK(t.map(range.highest - 2).folded);
    }
}

TEST_CASE("transpose: a song for 88 keys folds into 61 by the octaves it needs, keeping the pitch "
          "class") {
    const Transposition t(0, kRange61);
    CHECK(t.map(21).pitch == 45); // A0 up two octaves
    CHECK(t.map(21).fold_octaves == 2);
    CHECK(t.map(108).pitch == 96); // C8 down one
    CHECK(t.map(127).fold_octaves == -3);
    for (int s = -12; s <= 12; ++s) {
        for (int p = 0; p <= 127; ++p) {
            const TransposedPitch m = Transposition(s, kRange61).map(p);
            REQUIRE(m.playable);
            CHECK(kRange61.contains(m.pitch));
            CHECK(m.pitch == p + s + 12 * m.fold_octaves);
        }
    }
}

TEST_CASE("transpose: a note no key can play is dropped, never guessed; drums stay") {
    const Transposition tiny(0, KeyRange{60, 64});
    CHECK(tiny.map(50).pitch == 62);    // D3 folds into D4
    CHECK_FALSE(tiny.map(66).playable); // no F# in 60..64
    CHECK(tiny.map(66).pitch == -1);
    CHECK_FALSE(Transposition(0, KeyRange{80, 60}).map(70).playable);
    Note drum = note_at(38);
    drum.drum = true;
    CHECK(Transposition(5, kRange61).expected_pitch(drum).pitch == 38);
}

TEST_CASE("drawn key range: every note as drawn, folded ones where they land, backing left out") {
    std::vector<Note> notes;
    for (const int pitch : {33, 38, 43, 81, 30}) {
        notes.push_back(note_at(pitch));
    }
    notes[4].accompaniment = true;
    // A1 (33) folds up to A2 (45), above the D2 and G2 the song also has.
    const std::optional<KeyRange> r = drawn_key_range(notes, Transposition(0, KeyRange{36, 96}));
    REQUIRE(r.has_value());
    CHECK(r->lowest == 38);
    CHECK(r->highest == 81);
    CHECK(drawn_key_range(notes, Transposition(2, KeyRange{36, 96}))->lowest == 40);
    for (Note& n : notes) {
        n.accompaniment = true;
    }
    CHECK_FALSE(drawn_key_range(notes, Transposition(0, KeyRange{36, 96})).has_value());
}

TEST_CASE("key range: black-key ends widen, a bad range falls back to 88 keys, a good one stays") {
    const KeyboardLayout widened(KeyRange{37, 97});
    CHECK(widened.range().lowest == 36);
    CHECK(widened.range().highest == 98);
    CHECK(widened.adjusted());
    for (const KeyRange r : {KeyRange{-1, 60}, KeyRange{60, 128}, KeyRange{96, 36}}) {
        CHECK(KeyboardLayout(r).range().lowest == kRange88.lowest);
        CHECK(KeyboardLayout(r).range().highest == kRange88.highest);
    }
    for (const KeyRange r : {kRange88, kRange61, kRange49, KeyRange{0, 127}}) {
        CHECK(KeyboardLayout(r).range().lowest == r.lowest);
        CHECK_FALSE(KeyboardLayout(r).adjusted());
        CHECK(r.valid());
    }
    CHECK_FALSE(KeyRange{37, 96}.valid());
    CHECK(kRange61.key_count() == 61);
}

TEST_CASE("fit: the smallest shift that fits, an octave at most, else too wide") {
    struct Row {
        int lowest, highest;
        KeyRange range;
        KeyFit fit;
        int semitones;
    };
    const Row rows[] = {
        {48, 84, kRange61, KeyFit::Fits, 0},
        {33, 60, kRange61, KeyFit::FitsWithShift, 3},          // 3 under 36
        {60, 100, kRange61, KeyFit::FitsWithShift, -4},        // 4 over 96
        {36, 40, KeyRange{48, 72}, KeyFit::FitsWithShift, 12}, // exactly an octave
        {35, 40, KeyRange{48, 72}, KeyFit::TooWide, 0},        // one semitone past
        {21, 108, kRange61, KeyFit::TooWide, 0},
    };
    for (const Row& r : rows) {
        const KeyboardFit f = fit_to_keyboard(r.lowest, r.highest, r.range);
        CHECK(f.fit == r.fit);
        CHECK(f.semitones == r.semitones);
    }
}

TEST_CASE("keyboard identity: a USB id first, a model name on word edges, never a guess") {
    const std::array profiles = {
        KeyboardProfile{"PSR-E383", kRange61, {}, {"PSR-E383"}},
        KeyboardProfile{"PSR-E38", kRange61, {}, {"PSR-E38"}},
        KeyboardProfile{"Keystation", kRange88, {0x0763, 0x0192}, {"Keystation 88es"}},
        KeyboardProfile{"Twin A", kRange49, {0x1234, 0x0001}, {}},
        KeyboardProfile{"Twin B", kRange61, {0x1234, 0x0001}, {}},
        KeyboardProfile{"Digital", kRange88, {}, {"Digital Piano"}},
    };
    const auto id = [&profiles](std::string port, std::uint16_t vendor = 0,
                                std::uint16_t product = 0) {
        KeyboardIdentity k;
        k.port_name = std::move(port);
        if (vendor != 0) {
            k.usb_vendor = vendor;
            k.usb_product = product;
        }
        return match_keyboard(k, profiles);
    };
    CHECK(id("", 0x0763, 0x0192).profile->name == "Keystation");
    CHECK(id("PSR-E383", 0x0763, 0x0192).profile->name == "Keystation"); // the id wins
    CHECK(id("PSR-E383 MIDI 1").profile->name == "PSR-E383");
    CHECK(id("PSR-E38 MIDI 1").profile->name == "PSR-E38");
    CHECK(id("PSR-E3830").profile == nullptr);
    CHECK(id("Keystation 88es", 0x0763, 0x0999).profile == nullptr); // another product id
    CHECK(id("", 0x1234, 0x0001).reason == KeyboardMatchReason::Ambiguous);
    CHECK(id("Digital Piano").profile == nullptr); // a generic name
    CHECK(id("PSR-E383 / Keystation 88es").profile == nullptr);
    KeyboardIdentity psr;
    psr.port_name = "PSR-E383 MIDI 1";
    const auto built_in = match_keyboard(psr); // the built-in profiles
    REQUIRE(built_in.profile != nullptr);
    CHECK(built_in.profile->range.lowest == 36);
    CHECK(built_in.profile->range.highest == 96);
}

// ---- The octave offer

namespace {

constexpr std::array<int, 8> kScale{60, 62, 64, 65, 67, 69, 71, 72};

int scale_at(int i) { return kScale[static_cast<std::size_t>(i) % kScale.size()]; }

// The scale played `delta` away from what the song asks, `n` notes, with an optional slip.
ShiftResult play_scale(int n, int delta, int slip_every = 0) {
    OctaveShiftDetector d(kRange61);
    for (int i = 0; i < n; ++i) {
        const bool slip = slip_every != 0 && i % slip_every == slip_every - 1;
        d.observe(slip ? scale_at(i) - 12 : scale_at(i) + delta, std::array<int, 1>{scale_at(i)});
    }
    return d.result();
}

std::vector<int> seq(int count, const std::function<int(int)>& f) {
    std::vector<int> v;
    for (int i = 0; i < count; ++i) {
        v.push_back(f(i));
    }
    return v;
}

} // namespace

TEST_CASE(
    "octave offer, range only: notes outside the keyboard propose the fewest octaves that fit") {
    std::vector<int> inside;
    for (int i = 0; i < 40; ++i) {
        inside.push_back(scale_at(i));
        if (i % 7 == 6) {
            inside.push_back(100); // a stray note over the top
        }
    }
    struct Row {
        std::vector<int> notes;
        ShiftStatus status;
        int offset;
    };
    const Row rows[] = {
        {seq(12, [](int i) { return 97 + i % 6; }), ShiftStatus::Shift, -12},
        {seq(12, [](int i) { return 24 + i % 6; }), ShiftStatus::Shift, 12},
        {seq(12, [](int i) { return 10 + i % 6; }), ShiftStatus::Shift, 36},
        {seq(12, [](int i) { return i % 2 == 0 ? 97 : 108; }), ShiftStatus::Shift, -12},
        {seq(12, [](int i) { return i % 2 == 0 ? 100 : 20; }), ShiftStatus::NotSure, 0},
        // Six notes, all outside, are the least that counts; one fewer does not.
        {seq(kOctaveShiftMinRangeNotes - 1, [](int i) { return 100 + i; }), ShiftStatus::NotSure,
         0},
        {seq(kOctaveShiftMinRangeNotes, [](int i) { return 100 + i; }), ShiftStatus::Shift, -12},
        // The window holds 12 notes: 9 outside is the 75% share, 8 is just under it.
        {seq(kOctaveShiftWindow, [](int i) { return i < 9 ? 97 + i % 3 : 60; }), ShiftStatus::Shift,
         -12},
        {seq(kOctaveShiftWindow, [](int i) { return i < 8 ? 97 + i % 3 : 60; }),
         ShiftStatus::NotSure, 0},
        {inside, ShiftStatus::NoShift, 0},
    };
    for (const Row& r : rows) {
        OctaveShiftDetector d(kRange61);
        for (const int p : r.notes) {
            d.observe(p);
        }
        const ShiftResult res = d.result();
        if (r.status == ShiftStatus::Shift) {
            CHECK(res.status == ShiftStatus::Shift);
            CHECK(res.offset_semitones == r.offset);
            CHECK(res.evidence == ShiftEvidence::Range);
        } else {
            CHECK(res.status != ShiftStatus::Shift);
        }
    }
}

TEST_CASE("octave offer, with the song: the offset that explains the notes, or no shift") {
    CHECK(play_scale(12, 0).status == ShiftStatus::NoShift);
    const ShiftResult down = play_scale(12, -12); // octave-down button: still inside the range
    CHECK(down.status == ShiftStatus::Shift);
    CHECK(down.offset_semitones == 12);
    CHECK(down.evidence == ShiftEvidence::Song);
    CHECK(play_scale(12, 2).offset_semitones == -2); // a transpose button
    CHECK(play_scale(kOctaveShiftMinSongNotes - 1, -12).status != ShiftStatus::Shift);
    CHECK(play_scale(kOctaveShiftMinSongNotes, -12).status == ShiftStatus::Shift);
    CHECK(play_scale(60, 0, 5).status != ShiftStatus::Shift); // a slip every fifth note

    // A student who gets 60 percent right is not shifted.
    OctaveShiftDetector student(kRange61);
    std::mt19937 rng(7);
    for (int i = 0; i < 200; ++i) {
        student.observe(rng() % 10 < 6 ? scale_at(i)
                                       : scale_at(i) + 1 + static_cast<int>(rng() % 10),
                        std::array<int, 1>{scale_at(i)});
        CHECK(student.result().status != ShiftStatus::Shift);
    }

    // C and C an octave up asked together, C3 played: +12 and +24 explain the notes equally.
    OctaveShiftDetector tie(kRange61);
    for (int i = 0; i < 12; ++i) {
        tie.observe(48, std::array<int, 2>{60, 72});
    }
    CHECK(tie.result().status == ShiftStatus::NotSure);

    // A chord played an octave high: every one of its notes counts as expected.
    OctaveShiftDetector chords(kRange61);
    const std::array<std::array<int, 3>, 4> triads{
        {{60, 64, 67}, {65, 69, 72}, {67, 71, 74}, {69, 72, 76}}};
    for (int round = 0; round < 3; ++round) {
        for (const auto& chord : triads) {
            for (const int p : chord) {
                chords.observe(p + 12, chord);
            }
        }
    }
    CHECK(chords.result().offset_semitones == -12);
}

TEST_CASE("octave offer, mid-session: a real shift is found once it fills the window, not at the "
          "first note") {
    OctaveShiftDetector d(kRange61);
    for (int i = 0; i < 24; ++i) {
        d.observe(scale_at(i), std::array<int, 1>{scale_at(i)});
    }
    REQUIRE(d.result().status == ShiftStatus::NoShift);
    int first = -1;
    for (int i = 0; i < 24; ++i) {
        d.observe(scale_at(i) + 12, std::array<int, 1>{scale_at(i)});
        if (first < 0 && d.result().status == ShiftStatus::Shift) {
            first = i + 1;
        }
    }
    CHECK(first > 3);
    CHECK(first <= kOctaveShiftWindow);
    CHECK(d.result().offset_semitones == -12);
}

TEST_CASE("shift fit: the notes outside follow the transposition; an offer needs a real share") {
    const auto song = [](int low_count, int stray_count, int stray) {
        std::vector<Note> notes(static_cast<std::size_t>(low_count), note_at(60));
        notes.insert(notes.end(), static_cast<std::size_t>(stray_count), note_at(stray));
        return shift_fits(notes, kRange61);
    };
    // The count for a shift is what Transposition::map moves, at every shift.
    const std::vector<Note> spread = notes_of(43, [](int i) { return i * 3; });
    for (const KeyRange range : {kRange61, kRange88}) {
        const ShiftFits fits = shift_fits(spread, range);
        for (int s = -kMaxTransposeSemitones; s <= kMaxTransposeSemitones; ++s) {
            int moved = 0;
            for (const Note& n : spread) {
                const TransposedPitch m = Transposition(s, range).map(n.pitch);
                moved += m.folded || !m.playable ? 1 : 0;
            }
            CHECK(fits.outside_at(s) == moved);
        }
        CHECK(fits.outside_at(13) == -1); // not a shift this app has
    }
    // An octave down for a song too high; from +2 it is -10, a whole octave from the shift in
    // force.
    const ShiftFits high = shift_fits(notes_of(25, [](int i) { return 84 + i; }), kRange61);
    CHECK(suggest_octave_shift(high, 0) == -12);
    CHECK(suggest_octave_shift(high, 2) == -10);
    // Four strays in 100 notes raise nothing, five do; one in ten or two in fifty do not, two in
    // twenty do.
    CHECK_FALSE(suggest_octave_shift(song(96, 4, 100), 0).has_value());
    CHECK(suggest_octave_shift(song(95, 5, 100), 0) == -12);
    CHECK_FALSE(suggest_octave_shift(song(9, 1, 100), 0).has_value());
    CHECK_FALSE(suggest_octave_shift(song(48, 2, 100), 0).has_value());
    CHECK(suggest_octave_shift(song(18, 2, 100), 0) == -12);
    // A tie goes down; nothing when the song already fits.
    std::vector<Note> tie(92, note_at(60));
    tie.insert(tie.end(), 4, note_at(100));
    tie.insert(tie.end(), 4, note_at(30));
    CHECK(suggest_octave_shift(shift_fits(tie, kRange61), 0) == -12);
    CHECK_FALSE(suggest_octave_shift(song(50, 0, 60), 0).has_value());
}

TEST_CASE("shift offer: the song's card, the keys' card, never ask again and the reach") {
    const ShiftFits high = shift_fits(notes_of(25, [](int i) { return 84 + i; }), kRange61);
    const auto shift_of = [](int offset) {
        ShiftResult r;
        r.status = ShiftStatus::Shift;
        r.offset_semitones = offset;
        r.confidence = 1.0;
        r.evidence = ShiftEvidence::Range;
        return r;
    };
    ShiftOfferState s;
    s.begin(false);
    s.judge_song(high, 0);
    REQUIRE(s.offer().has_value());
    CHECK(s.offer()->target == -12);
    s.judge_song(high, -12); // applied by a saved choice: nothing left to propose
    CHECK_FALSE(s.offer().has_value());
    s.judge_song(high, 0);
    s.never();
    s.judge_keys(shift_of(-12), 0, kRange61);
    CHECK_FALSE(s.offer().has_value());
    s.begin(true); // the same song comes back with the saved choice
    s.judge_song(high, 0);
    CHECK_FALSE(s.offer().has_value());

    // The keyboard sent 12 too high: the answer is the opposite sign, added to the one in force.
    s.begin(false);
    s.judge_keys(shift_of(-12), 0, kRange61);
    CHECK(s.offer()->target == 12);
    CHECK(s.offer()->source == OfferSource::Keys);
    s.begin(false);
    s.judge_keys(shift_of(12), -5, kRange61);
    CHECK(s.offer()->target == -17);
    // Three octaves is the reach, one semitone past raises nothing; so does a range past MIDI.
    s.begin(false);
    s.judge_keys(shift_of(-12), 25, kRange61);
    CHECK_FALSE(s.offer().has_value());
    s.judge_keys(shift_of(-12), 24, kRange61);
    CHECK(s.offer().has_value());
    s.begin(false);
    s.judge_keys(shift_of(-12), 0, KeyRange{60, 116}); // 72..128
    CHECK_FALSE(s.offer().has_value());
    s.judge_keys(shift_of(-12), 0, KeyRange{60, 115}); // 72..127
    CHECK(s.offer().has_value());
    CHECK_FALSE(shifted_range(kRange61, -37).has_value());
    CHECK(shifted_range(KeyRange{21, 108}, 19).value().highest == 127);
}

namespace {

// Plays two_hand_song() on the 61-key keyboard an octave high from `source` (every right-hand
// note, and the left hand's at each bar) for 8 beats, or until an offer shows.
void play_octave_high(SongController& c, FakePiano& piano, std::int64_t& now, InputSource source,
                      bool stop_at_offer = false) {
    c.set_keyboard(kRange61, now);
    c.load(two_hand_song(), now);
    c.play(now);
    for (int beat = 0; beat < 8 && !(stop_at_offer && c.shift_offer().has_value()); ++beat) {
        const std::int64_t at = kSec + beat * 500 * kMs;
        run(c, piano, now, at);
        std::vector<MidiEvent> played{midi_note_on(72 + beat % 4 * 2 + 12, at)};
        if (beat % 4 == 0) {
            played.push_back(midi_note_on(48 + 12, at));
        }
        for (MidiEvent& ev : played) {
            ev.source = source;
        }
        run(c, piano, now, at + 16 * kMs, played);
    }
}

} // namespace

TEST_CASE("octave offer: only a MIDI keyboard's note-ons raise it, never typing or screen keys") {
    for (const InputSource source :
         {InputSource::Midi, InputSource::ComputerKeyboard, InputSource::Pointer}) {
        SongController c;
        FakePiano piano;
        std::int64_t now = kSec;
        play_octave_high(c, piano, now, source);
        CHECK(c.shift_offer().has_value() == (source == InputSource::Midi));
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
}

TEST_CASE("octave offer: an accepted keyboard shift is never saved into the song or the session") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    play_octave_high(c, piano, now, InputSource::Midi, true);
    REQUIRE(c.shift_offer().has_value());
    REQUIRE(c.shift_offer()->source == OfferSource::Keys);
    c.accept_shift_offer(now);
    int shift = 0;
    CHECK(c.take_keyboard_shift(&shift));
    CHECK(shift == 12);
    CHECK_FALSE(c.take_keyboard_shift(&shift)); // handed over once
    CHECK(c.practice().transpose_semitones == 0);
    CHECK(c.session_state({}).transpose_semitones == 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

// ---- Typing keys and screen keys send the right note

namespace {

using Events = std::vector<MidiEvent>;

struct Mapped {
    int code;
    int offset;
};
constexpr Mapped kMap[] = {
    {scancode::kA, 0},          {scancode::kW, 1},           {scancode::kS, 2},  {scancode::kE, 3},
    {scancode::kD, 4},          {scancode::kF, 5},           {scancode::kT, 6},  {scancode::kG, 7},
    {scancode::kY, 8},          {scancode::kH, 9},           {scancode::kU, 10}, {scancode::kJ, 11},
    {scancode::kK, 12},         {scancode::kO, 13},          {scancode::kL, 14}, {scancode::kP, 15},
    {scancode::kSemicolon, 16}, {scancode::kApostrophe, 17},
};

Events key_event(ComputerKeyboard& k, int code, bool down, std::int64_t t) {
    Events out;
    k.key(code, down, false, t, out);
    return out;
}
Events press(ComputerKeyboard& k, int code, std::int64_t t = 1) {
    return key_event(k, code, true, t);
}
Events release(ComputerKeyboard& k, int code, std::int64_t t = 2) {
    return key_event(k, code, false, t);
}
void tap(ComputerKeyboard& k, int code) {
    press(k, code, 0);
    release(k, code, 0);
}

} // namespace

TEST_CASE("typing keys: each mapped key plays its note from middle C, others play nothing") {
    ComputerKeyboard k(kRange88);
    for (const Mapped& m : kMap) {
        CAPTURE(m.code);
        const Events down = press(k, m.code);
        REQUIRE(down.size() == 1);
        CHECK(down[0].type == MidiEventType::NoteOn);
        CHECK(down[0].data1 == 60 + m.offset);
        CHECK(down[0].source == InputSource::ComputerKeyboard);
        const Events up = release(k, m.code);
        REQUIRE(up.size() == 1);
        CHECK(up[0].type == MidiEventType::NoteOff);
        CHECK(up[0].data1 == 60 + m.offset);
    }
    for (const int code : {0, 5, 20, 41, 44, 225, 1000, -3}) {
        CHECK(press(k, code).empty());
        CHECK(release(k, code).empty());
    }
    CHECK(press(k, scancode::kA).size() == 1);
    CHECK(press(k, scancode::kA).empty()); // auto-repeat is not a new note
}

TEST_CASE("typing keys: the octave keys stop at the keyboard's edge, so every note is on it") {
    for (const KeyRange range : {kRange49, kRange61, kRange76, kRange88}) {
        ComputerKeyboard k(range);
        for (int i = 0; i < 12; ++i) {
            tap(k, scancode::kX);
        }
        CHECK(k.octave_shift() == k.max_octave_shift());
        CHECK(*k.note_for(scancode::kApostrophe) <= range.highest);
        for (int i = 0; i < 12; ++i) {
            tap(k, scancode::kZ);
        }
        CHECK(k.octave_shift() == k.min_octave_shift());
        CHECK(*k.note_for(scancode::kA) >= range.lowest);
    }
    ComputerKeyboard narrow(KeyRange{60, 72}); // too narrow for the span: what fits plays
    CHECK(press(narrow, scancode::kA).size() == 1);
    CHECK(press(narrow, scancode::kApostrophe).empty());
}

TEST_CASE("typing keys: a key held across an octave change releases the note it started") {
    ComputerKeyboard k(kRange88);
    CHECK(press(k, scancode::kA)[0].data1 == 60);
    tap(k, scancode::kX); // octave up while A is held
    const Events up = release(k, scancode::kA, 5);
    REQUIRE(up.size() == 1);
    CHECK(up[0].data1 == 60); // not 72
    CHECK(press(k, scancode::kA, 6)[0].data1 == 72);
    CHECK(release(k, scancode::kA, 7)[0].data1 == 72);
    CHECK(k.held_count() == 0);
}

namespace {

constexpr RectF kArea{0.0f, 500.0f, 1200.0f, 200.0f}; // keys y 500..700, black keys to y 624

PointerGeometry whole(KeyRange range = kRange61, BlockLayout blocks = BlockLayout::KeyWidth) {
    KeyViewFollower f;
    f.snap(range, range);
    const KeyViewFrame fr = f.frame();
    return {KeyboardLayout(fr.layout_range, blocks), fr, kArea};
}

float centre_x(const PointerGeometry& g, int note) {
    const KeyRect r = g.view.to_window(g.layout.key(note), g.layout.white_key_count());
    return kArea.x + (r.x + r.width / 2.0f) * kArea.w;
}

} // namespace

TEST_CASE("screen keys: a touch is the right key at every key edge, in front and at the back") {
    for (const BlockLayout blocks : {BlockLayout::KeyWidth, BlockLayout::EvenLanes}) {
        for (const KeyRange range : {kRange88, kRange61, KeyRange{40, 77}}) {
            const PointerGeometry g = whole(range, blocks);
            const int whites = g.layout.white_key_count();
            const auto px = [&](float u) { return kArea.x + u * kArea.w; };
            for (int note = range.lowest; note <= range.highest; ++note) {
                const KeyRect k = g.view.to_window(g.layout.key(note), whites);
                const float x0 = px(k.x);
                const float x1 = px(k.x + k.width);
                INFO("range " << range.lowest << " note " << note);
                if (is_black_key(note)) {
                    CHECK(hit_test_key(g, x0 + 0.25f, 560.0f)->note == note);
                    CHECK(hit_test_key(g, x1 - 0.25f, 560.0f)->note == note);
                    CHECK(hit_test_key(g, x0 - 0.25f, 560.0f)->note == note - 1);
                    CHECK(hit_test_key(g, x1 + 0.25f, 560.0f)->note == note + 1);
                    continue;
                }
                CHECK(hit_test_key(g, x0 + 0.25f, 690.0f)->note == note);
                const KeyRect s = g.view.to_window(g.layout.slot(note), whites);
                CHECK(hit_test_key(g, px(s.x + 0.5f * s.width), 560.0f)->note == note);
            }
        }
    }
    const PointerGeometry g = whole();
    CHECK_FALSE(hit_test_key(g, 600.0f, 499.0f).has_value());
    CHECK_FALSE(hit_test_key(g, 600.0f, 700.0f).has_value());
    CHECK_FALSE(hit_test_key(g, 1200.0f, 600.0f).has_value());
}

TEST_CASE("screen keys: a chord plays every finger's key, a slide moves the note, nothing sticks") {
    const PointerGeometry g = whole(kRange88);
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 650.0f, g, 1, out);
    p.down(2, centre_x(g, 64), 650.0f, g, 2, out);
    CHECK(p.held_notes() == 2);
    out.clear();
    p.move(1, centre_x(g, 62), 650.0f, g, 3, out); // a glissando: off the old key, on the new
    REQUIRE(out.size() == 2);
    CHECK(out[0].type == MidiEventType::NoteOff);
    CHECK(out[0].data1 == 60);
    CHECK(out[1].data1 == 62);

    // Random touches: the notes sounding always agree with the fingers, and all end up released.
    std::mt19937 rng(42);
    for (int round = 0; round < 50; ++round) {
        PointerKeys q;
        KeyState state;
        Events ev;
        for (int i = 0; i < 300; ++i) {
            const std::uint64_t id = rng() % 6;
            const float x = static_cast<float>(rng() % 1300) - 50.0f;
            const float y = 480.0f + static_cast<float>(rng() % 260);
            switch (rng() % 3) {
            case 0:
                q.down(id, x, y, g, i, ev);
                break;
            case 1:
                q.move(id, x, y, g, i, ev);
                break;
            default:
                q.up(id, i, ev);
                break;
            }
            for (const MidiEvent& e : ev) {
                state.apply(e);
            }
            REQUIRE(state.held_count() == q.held_notes());
            ev.clear();
        }
        q.release_all(999, ev);
        for (const MidiEvent& e : ev) {
            state.apply(e);
        }
        CHECK(state.held_count() == 0);
    }
}

TEST_CASE(
    "falling notes: the key shown is the transposed one, folded at the edges, unplayable marked") {
    struct Row {
        KeyRange range;
        int shift;
        int pitch;
        int shown; // -1: no key
        bool folded;
    };
    const Row rows[] = {
        {kRange61, 5, 60, 65, false},         {kRange61, 0, 96, 96, false}, // exactly the top key
        {kRange61, 0, 97, 85, true}, // one over: down an octave
        {kRange61, 5, 94, 87, true},          {KeyRange{60, 64}, 0, 60, 60, false},
        {KeyRange{60, 64}, 0, 59, -1, false}, // just under the edge: no key for that note
        {KeyRange{60, 64}, 0, 50, 62, true},
    };
    for (const Row& row : rows) {
        CAPTURE(row.pitch);
        Song song;
        Note n = note_at(row.pitch);
        n.duration_s = 0.5;
        n.hand = Hand::Right;
        song.notes = {n};
        NoteWindowOptions o;
        o.ahead_s = 8.0;
        o.transposition = Transposition(row.shift, row.range);
        std::vector<VisibleNote> out;
        NoteWindow(song).query(0.0, o, out);
        REQUIRE(out.size() == 1);
        CHECK(out[0].pitch == row.shown);
        CHECK(out[0].playable == (row.shown >= 0));
        CHECK(out[0].folded == row.folded);
    }
}
