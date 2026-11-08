// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hand_split.hpp"

#include "etude/abc.hpp"
#include "etude/instrument.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;

namespace {

struct N {
    double beat;
    int pitch;
    double beats = 1.0;
};

// These tests are about the pitch rules, so the helpers below switch the single-line rule off
// (single_lines_whole = false): the notes they make are mostly one note at a time. The single-line
// rule has its own tests in one_hand_test.cpp.

// A song of notes at 120 BPM (0.5 s per beat), 480 ticks per quarter, all with Unknown hands.
Song make_song(const std::vector<N>& notes) {
    Song song;
    song.tempo.ticks_per_quarter = 480;
    for (const N& n : notes) {
        Note note;
        note.start_tick = static_cast<std::int64_t>(n.beat * 480.0);
        note.end_tick = note.start_tick + static_cast<std::int64_t>(n.beats * 480.0);
        note.start_s = n.beat * 0.5;
        note.duration_s = n.beats * 0.5;
        note.pitch = static_cast<std::uint8_t>(n.pitch);
        note.hand = Hand::Unknown;
        note.measure = 1 + static_cast<int>(n.beat / 4.0);
        song.notes.push_back(note);
    }
    return song;
}

HandSplitOptions fixed(int split = kMiddleC) {
    HandSplitOptions o;
    o.method = SplitMethod::Fixed;
    o.split_point = split;
    o.single_lines_whole = false;
    return o;
}

HandSplitOptions smart(int split = kMiddleC) {
    HandSplitOptions o;
    o.method = SplitMethod::Smart;
    o.split_point = split;
    o.single_lines_whole = false;
    return o;
}

} // namespace

TEST_CASE("hand split, fixed: notes from the split point up are the right hand") {
    Song song = make_song({{0, 48}, {1, 59}, {2, 60}, {3, 72}});
    std::vector<Hand> h = split_hands(song, fixed());
    REQUIRE(h.size() == 4);
    CHECK(h[0] == Hand::Left);
    CHECK(h[1] == Hand::Left);
    CHECK(h[2] == Hand::Right);
    CHECK(h[3] == Hand::Right);
}

TEST_CASE("hand split, fixed: the split point can be moved") {
    Song song = make_song({{0, 55}, {1, 57}, {2, 62}});
    std::vector<Hand> h = split_hands(song, fixed(56));
    CHECK(h[0] == Hand::Left);
    CHECK(h[1] == Hand::Right);
    CHECK(h[2] == Hand::Right);
    h = split_hands(song, fixed(63));
    CHECK(h[0] == Hand::Left);
    CHECK(h[1] == Hand::Left);
    CHECK(h[2] == Hand::Left);
}

TEST_CASE("hand split: the original song is not touched, the result has one hand per note") {
    Song song = make_song({{0, 48}, {1, 72}});
    Song copy = song;
    std::vector<Hand> h = split_hands(song, smart());
    CHECK(h.size() == song.notes.size());
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        CHECK(song.notes[i].hand == copy.notes[i].hand);
        CHECK(song.notes[i].hand == Hand::Unknown);
    }
    Song split = with_split_hands(song, smart());
    CHECK(split.notes[0].hand == Hand::Left);
    CHECK(split.notes[1].hand == Hand::Right);
    CHECK(song.notes[0].hand == Hand::Unknown);
}

TEST_CASE("hand split: notes that already have a hand keep it, drums stay unknown") {
    Song song = make_song({{0, 80}, {1, 40}, {2, 40}});
    song.notes[0].hand = Hand::Left; // named track says so, whatever the pitch
    song.notes[2].drum = true;
    for (SplitMethod m : {SplitMethod::Fixed, SplitMethod::Smart}) {
        HandSplitOptions o;
        o.method = m;
        o.single_lines_whole = false;
        std::vector<Hand> h = split_hands(song, o);
        CHECK(h[0] == Hand::Left);
        CHECK(h[1] == Hand::Left);
        CHECK(h[2] == Hand::Unknown);
    }
}

TEST_CASE("hand split: needs_hand_split is true while a playable note has no hand") {
    Song song = make_song({{0, 48}, {1, 72}});
    CHECK(needs_hand_split(song));
    song.notes[0].hand = Hand::Left;
    CHECK(needs_hand_split(song));
    song.notes[1].hand = Hand::Right;
    CHECK_FALSE(needs_hand_split(song));
    Song drums = make_song({{0, 36}});
    drums.notes[0].drum = true;
    CHECK_FALSE(needs_hand_split(drums));
    CHECK_FALSE(needs_hand_split(Song{}));
}

TEST_CASE("hand split: a chord stays in one hand even when the split point is inside it") {
    // C major triad 58 62 65 straddles middle C (60): one hand, chosen by where the chord sits.
    Song song = make_song({{0, 58, 2}, {0, 62, 2}, {0, 65, 2}});
    for (SplitMethod m : {SplitMethod::Fixed, SplitMethod::Smart}) {
        HandSplitOptions o;
        o.method = m;
        o.single_lines_whole = false;
        std::vector<Hand> h = split_hands(song, o);
        CHECK(h[0] == h[1]);
        CHECK(h[1] == h[2]);
        CHECK(h[0] == Hand::Right); // the middle note, 62, is above middle C
    }
}

TEST_CASE("hand split: a chord one hand cannot reach is split at the gap nearest the split point") {
    // 36 43 52 | 64 71 76: two chords played together, 40 semitones wide.
    Song song = make_song({{0, 36}, {0, 43}, {0, 52}, {0, 64}, {0, 71}, {0, 76}});
    for (SplitMethod m : {SplitMethod::Fixed, SplitMethod::Smart}) {
        HandSplitOptions o;
        o.method = m;
        o.single_lines_whole = false;
        std::vector<Hand> h = split_hands(song, o);
        CHECK(h[0] == Hand::Left);
        CHECK(h[1] == Hand::Left);
        CHECK(h[2] == Hand::Left);
        CHECK(h[3] == Hand::Right);
        CHECK(h[4] == Hand::Right);
        CHECK(h[5] == Hand::Right);
    }
}

TEST_CASE("hand split: a tenth is within reach, an eleventh is not") {
    // 48..64 is 16 semitones (a major tenth): one hand. 48..65 is 17: two hands.
    Song within = make_song({{0, 48}, {0, 64}});
    std::vector<Hand> h = split_hands(within, fixed());
    CHECK(h[0] == h[1]);
    Song beyond = make_song({{0, 48}, {0, 65}});
    h = split_hands(beyond, fixed());
    CHECK(h[0] == Hand::Left);
    CHECK(h[1] == Hand::Right);
}

TEST_CASE("hand split: notes that start within a few milliseconds are one chord") {
    // A rolled chord from a played-in file: 20 ms apart.
    Song song = make_song({{0, 58}, {0, 62}, {0, 65}});
    song.notes[1].start_s += 0.02;
    song.notes[2].start_s += 0.04;
    std::vector<Hand> h = split_hands(song, fixed());
    CHECK(h[0] == h[1]);
    CHECK(h[1] == h[2]);
}

TEST_CASE("hand split, smart: a melody that dips under middle C stays in the right hand") {
    // The right hand plays 64 62 60 58 57 58 60 62 over a left hand bass at 40 and 47.
    std::vector<N> notes;
    const int melody[] = {64, 62, 60, 58, 57, 58, 60, 62, 64, 65, 64, 62, 60, 58, 57, 58};
    for (int i = 0; i < 16; ++i) {
        notes.push_back({i * 1.0, melody[i]});
    }
    for (int i = 0; i < 8; ++i) {
        notes.push_back({i * 2.0 + 0.5, i % 2 == 0 ? 40 : 47});
    }
    Song song = make_song(notes);
    // Notes are sorted by tick in a real Song; make_song above is not, so sort like the loader.
    std::stable_sort(song.notes.begin(), song.notes.end(), [](const Note& a, const Note& b) {
        return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
    });
    std::vector<Hand> fixed_h = split_hands(song, fixed());
    std::vector<Hand> smart_h = split_hands(song, smart());
    int fixed_wrong = 0;
    int smart_wrong = 0;
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        bool is_bass = song.notes[i].pitch <= 47;
        Hand want = is_bass ? Hand::Left : Hand::Right;
        fixed_wrong += fixed_h[i] != want ? 1 : 0;
        smart_wrong += smart_h[i] != want ? 1 : 0;
    }
    CHECK(fixed_wrong > 0); // the plain split sends the 57s and 58s to the left hand
    CHECK(smart_wrong == 0);
}

TEST_CASE("hand split, smart: a lone melody keeps its hand through a short dip under the split") {
    // No other register in the window, so the split point stays at middle C. 57 is three
    // semitones under it: the line continues in the right hand. The plain split drops it.
    const int melody[] = {64, 62, 60, 58, 57, 58, 60, 62, 64};
    std::vector<N> notes;
    for (int i = 0; i < 9; ++i) {
        notes.push_back({i * 1.0, melody[i]});
    }
    Song song = make_song(notes);
    std::vector<Hand> smart_h = split_hands(song, smart());
    for (Hand h : smart_h) {
        CHECK(h == Hand::Right);
    }
    std::vector<Hand> fixed_h = split_hands(song, fixed());
    CHECK(fixed_h[4] == Hand::Left);
}

TEST_CASE("hand split, smart: a line that stays well under the split is the left hand") {
    // Down by steps to 50 and staying there: the line as a whole sits under the split, so it is
    // one hand, the left, from the point where the dip stops being a dip. The top of it is the
    // only part that belongs to the split point's side.
    const int line[] = {62, 60, 58, 57, 55, 53, 52, 52, 50, 52, 53, 52, 50};
    std::vector<N> notes;
    for (int i = 0; i < 13; ++i) {
        notes.push_back({i * 1.0, line[i]});
    }
    std::vector<Hand> h = split_hands(make_song(notes), smart());
    for (std::size_t i = 4; i < h.size(); ++i) {
        CHECK(h[i] == Hand::Left);
    }
}

TEST_CASE("hand split, smart: a gap in time ends the line") {
    // Right hand ends on 60; thirty beats later a bass note 57 starts fresh: left hand.
    Song song = make_song({{0, 62}, {1, 60}, {30, 57}});
    std::vector<Hand> h = split_hands(song, smart());
    CHECK(h[1] == Hand::Right);
    CHECK(h[2] == Hand::Left);
}

TEST_CASE("hand split, smart: a right hand scale up and down an octave is not cut in two") {
    // C major, 60 to 72 and back at two notes a beat, alone: one register, so the split point
    // decides, and every note is at or above it.
    const int scale[] = {60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62};
    std::vector<N> notes;
    for (int round = 0; round < 4; ++round) {
        for (int i = 0; i < 14; ++i) {
            notes.push_back({(round * 14 + i) * 0.5, scale[i]});
        }
    }
    std::vector<Hand> h = split_hands(make_song(notes), smart());
    for (Hand hand : h) {
        CHECK(hand == Hand::Right);
    }
}

TEST_CASE("hand split, smart: both hands in the same register follow where each is playing") {
    // The whole piece sits above middle C, in two registers: a bass at 61..64 and a melody
    // at 76..81. The local split moves up between them, so the bass is the left hand.
    std::vector<N> notes;
    for (int i = 0; i < 16; ++i) {
        notes.push_back({i * 1.0, 76 + (i % 4) * 2});
        notes.push_back({i * 1.0 + 0.5, 61 + (i % 3)});
    }
    Song song = make_song(notes);
    std::stable_sort(song.notes.begin(), song.notes.end(), [](const Note& a, const Note& b) {
        return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
    });
    std::vector<Hand> smart_h = split_hands(song, smart());
    std::vector<Hand> fixed_h = split_hands(song, fixed());
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        bool bass = song.notes[i].pitch < 70;
        CHECK(smart_h[i] == (bass ? Hand::Left : Hand::Right));
    }
    // The plain split calls all of it the right hand.
    for (Hand h : fixed_h) {
        CHECK(h == Hand::Right);
    }
}

TEST_CASE("hand split: compare_hands counts only notes whose true hand is known") {
    Song truth = make_song({{0, 48}, {1, 72}, {2, 50}, {3, 40}});
    truth.notes[0].hand = Hand::Left;
    truth.notes[1].hand = Hand::Right;
    truth.notes[2].hand = Hand::Left;
    // note 3 stays Unknown: not compared
    const std::vector<Hand> assigned{Hand::Left, Hand::Left, Hand::Left, Hand::Right};
    HandAgreement a = compare_hands(truth, assigned);
    CHECK(a.compared == 3);
    CHECK(a.matching == 2);
    CHECK(a.fraction() == doctest::Approx(2.0 / 3.0));
    CHECK(compare_hands(Song{}, {}).fraction() == 1.0);
}

namespace {

struct P {
    double beat;
    int pitch;
    double beats = 1.0;
    std::uint16_t track = 0;
    Hand hand = Hand::Unknown;
};

// Like make_song, with a track and a hand per note, sorted by start and pitch as a real Song is,
// and a Part for each of the two tracks.
Song make_tracks(std::vector<P> notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const P& a, const P& b) {
        return a.beat != b.beat ? a.beat < b.beat : a.pitch < b.pitch;
    });
    Song song;
    song.tempo.ticks_per_quarter = 480;
    for (const P& n : notes) {
        Note note;
        note.start_tick = static_cast<std::int64_t>(n.beat * 480.0);
        note.end_tick = note.start_tick + static_cast<std::int64_t>(n.beats * 480.0);
        note.start_s = n.beat * 0.5;
        note.duration_s = n.beats * 0.5;
        note.pitch = static_cast<std::uint8_t>(n.pitch);
        note.hand = n.hand;
        note.track = n.track;
        note.measure = 1 + static_cast<int>(n.beat / 4.0);
        song.notes.push_back(note);
    }
    for (std::uint16_t t = 0; t < 2; ++t) {
        Part part;
        part.track = t;
        song.parts.push_back(part);
    }
    return song;
}

} // namespace

TEST_CASE(
    "hand split: a held bass under an arpeggio in one track is two hands, not a single line") {
    // Each half bar: a bass note held two beats and seven sixteenths over it, all in one part, the
    // way a one-track file of a Bach prelude comes. Most of its onsets sound over a bass more than
    // an octave away, so the single-line rule must not give the whole part to one hand.
    const int arpeggio[] = {64, 67, 72, 76, 72, 67, 64};
    std::vector<P> notes;
    for (int half = 0; half < 8; ++half) {
        const double t = half * 2.0;
        notes.push_back({t, half % 2 == 1 ? 43 : 48, 2.0});
        for (int k = 0; k < 7; ++k) {
            notes.push_back({t + 0.25 * (k + 1), arpeggio[k], 0.25});
        }
    }
    const Song song = make_tracks(notes);
    const std::vector<Hand> h = split_hands(song); // the default options: the single-line rule on
    for (std::size_t i = 0; i < h.size(); ++i) {
        CHECK(h[i] == (song.notes[i].pitch < 50 ? Hand::Left : Hand::Right));
    }
}

TEST_CASE("hand split, smart: a chord too wide for a hand is cut at its widest gap") {
    // A left hand chord 53 57 60 with the melody note 76 starting with it: 23 semitones, two hands.
    // The cut nearest the split point would hand 60 to the right hand; the chord falls apart
    // between 60 and 76.
    std::vector<P> notes;
    for (int bar = 0; bar < 8; ++bar) {
        const double t = bar * 2.0;
        notes.push_back({t, 41});
        notes.push_back({t, 72});
        for (int pitch : {53, 57, 60, 76}) {
            notes.push_back({t + 1, pitch});
        }
    }
    const Song song = make_tracks(notes);
    const std::vector<Hand> h = split_hands(song, smart());
    for (std::size_t i = 0; i < h.size(); ++i) {
        CHECK(h[i] == (song.notes[i].pitch < 70 ? Hand::Left : Hand::Right));
    }
}

TEST_CASE("hand split, smart: notes that already have a hand are the context of the others") {
    // The melody is the right hand already (a named track), the chords under it have no hand. The
    // chord 55 59 62 sits where the right hand plays at other times, and goes left with the
    // others because the melody above it is taken.
    const int melody[] = {76, 72, 69, 74, 71, 72, 74, 76};
    const int chord[3][3] = {{48, 52, 55}, {53, 57, 60}, {55, 59, 62}};
    const int order[] = {0, 0, 1, 2, 0, 2, 1, 2};
    std::vector<P> notes;
    for (int bar = 0; bar < 8; ++bar) {
        const double t = bar * 3.0;
        for (int pitch : chord[order[bar]]) {
            notes.push_back({t, pitch, 3.0});
        }
        notes.push_back({t, melody[bar], 2.0, 0, Hand::Right});
        notes.push_back({t + 2, melody[(bar + 1) % 8] - 2, 1.0, 0, Hand::Right});
    }
    const Song song = make_tracks(notes);
    const std::vector<Hand> h = split_hands(song, smart());
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (song.notes[i].hand != Hand::Unknown) {
            CHECK(h[i] == song.notes[i].hand); // kept
        } else {
            CHECK(h[i] == Hand::Left);
        }
    }
}

TEST_CASE("hand split, staves: two unnamed tracks are a hand each, the lower one the left") {
    // An unnamed file with the left hand on one track and the right hand on the other, both with
    // chords, the left hand's rising to 64: the register alone would give 59 and 64 to the right
    // hand; the track's staff gives them to the left.
    std::vector<P> notes;
    for (int bar = 0; bar < 8; ++bar) {
        const double t = bar * 2.0;
        notes.push_back({t, 48, 1.0, 0});
        notes.push_back({t, 55, 1.0, 0});
        for (int pitch : {55, 59, 64}) {
            notes.push_back({t + 1, pitch, 1.0, 0});
        }
        notes.push_back({t, 67, 1.0, 1});
        notes.push_back({t, 72, 1.0, 1});
        notes.push_back({t + 1, 69, 1.0, 1});
        notes.push_back({t + 1, 74, 1.0, 1});
    }
    const Song song = make_tracks(notes);
    const std::vector<Hand> h = split_hands(song, smart());
    for (std::size_t i = 0; i < h.size(); ++i) {
        CHECK(h[i] == (song.notes[i].track == 0 ? Hand::Left : Hand::Right));
    }
}

TEST_CASE(
    "hand split, staves: a single line next to a chord track gives each track its staff's hand") {
    // The melody track is a single line, the chord track below it has chords up to 62: the melody
    // is the treble staff, the right hand, and the chords the bass staff, the left, 55 59 62 as
    // much as 48 52 55.
    const int melody[] = {76, 72, 69, 74, 71, 72, 74, 76};
    const int chord[3][3] = {{48, 52, 55}, {53, 57, 60}, {55, 59, 62}};
    const int order[] = {0, 0, 1, 2, 0, 2, 1, 2};
    std::vector<P> notes;
    for (int bar = 0; bar < 8; ++bar) {
        const double t = bar * 3.0;
        for (int pitch : chord[order[bar]]) {
            notes.push_back({t, pitch, 3.0, 0});
        }
        notes.push_back({t, melody[bar], 2.0, 1});
        notes.push_back({t + 2, melody[(bar + 1) % 8] - 2, 1.0, 1});
    }
    const Song song = make_tracks(notes);
    const std::vector<Hand> h = split_hands(song); // the default options
    for (std::size_t i = 0; i < h.size(); ++i) {
        CHECK(h[i] == (song.notes[i].track == 0 ? Hand::Left : Hand::Right));
    }
}

TEST_CASE("hand split, smart: a figure that alternates around the split point stays in one hand") {
    // The right hand plays 64 60 64 60 under which a bass sits at 40 and 47. The 60s are the same
    // hand as the 64s that come before and after them, however near the split point they are.
    std::vector<P> notes;
    for (int i = 0; i < 24; ++i) {
        notes.push_back({i * 0.5, i % 2 == 1 ? 60 : 64, 0.5});
    }
    for (int bar = 0; bar < 4; ++bar) {
        notes.push_back({bar * 3.0, 40, 3.0});
        notes.push_back({bar * 3.0 + 1.5, 47, 1.5});
    }
    const Song song = make_tracks(notes);
    const std::vector<Hand> h = split_hands(song, smart());
    for (std::size_t i = 0; i < h.size(); ++i) {
        CHECK(h[i] == (song.notes[i].pitch < 50 ? Hand::Left : Hand::Right));
    }
}

TEST_CASE(
    "hand split, smart: three thousand notes at one instant are cut once and all get a hand") {
    std::vector<P> notes;
    for (int i = 0; i < 3000; ++i) {
        notes.push_back({0.0, 24 + i % 96});
    }
    const Song song = make_tracks(notes);
    const std::vector<Hand> h = split_hands(song, smart());
    int highest_left = -1;
    int lowest_right = 128;
    for (std::size_t i = 0; i < h.size(); ++i) {
        REQUIRE(h[i] != Hand::Unknown);
        const int pitch = song.notes[i].pitch;
        if (h[i] == Hand::Left) {
            highest_left = std::max(highest_left, pitch);
        } else {
            lowest_right = std::min(lowest_right, pitch);
        }
    }
    CHECK(highest_left < lowest_right); // one cut, in pitch order
}

namespace {

std::vector<std::uint8_t> read_test_file(const char* name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in.good());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

struct RealFile {
    Song named;    // hands from the track names or staves (ground truth)
    Song stripped; // the same notes with every hand Unknown, tracks as in the file
    Song merged;   // ... and all of them in one track, as a format 0 file has them
};

// The notes of the playable parts in one track and channel, no hand.
Song merge_parts(Song song) {
    const bool keyboard = has_keyboard_part(song);
    auto is_hand_part = [&](const Part& p) {
        return !p.drum && !p.accompaniment && !is_accompaniment_part(p, keyboard);
    };
    std::vector<Part> parts;
    Part one; // the first hand part stands for all of them, so the song keeps its keyboard part
    bool have_one = false;
    for (const Part& p : song.parts) {
        if (!is_hand_part(p)) {
            parts.push_back(p);
        } else if (!have_one) {
            one = p;
            have_one = true;
        }
    }
    one.track = 60000;
    one.channel = 0;
    one.name.clear();
    parts.push_back(one);
    for (Note& n : song.notes) {
        for (const Part& p : song.parts) {
            if (p.track == n.track && p.channel == n.channel && is_hand_part(p) && !n.drum) {
                n.track = 60000;
                n.channel = 0;
                break;
            }
        }
    }
    song.parts = std::move(parts);
    return song;
}

RealFile load_real(const char* name) {
    std::vector<std::uint8_t> bytes = read_test_file(name);
    const std::string file = name;
    RealFile out;
    if (file.ends_with(".mid")) {
        MidiLoadResult named = load_midi(bytes);
        REQUIRE(named.ok);
        MidiLoadOptions strip;
        strip.assign_hands = false;
        MidiLoadResult plain = load_midi(bytes, strip);
        REQUIRE(plain.ok);
        REQUIRE(plain.song.notes.size() == named.song.notes.size());
        out.named = std::move(named.song);
        out.stripped = std::move(plain.song);
    } else {
        ScoreLoadResult score = file.ends_with(".abc") ? load_abc(bytes) : load_score(bytes);
        REQUIRE(score.ok);
        out.named = score.song;
        out.stripped = std::move(score.song);
        for (Note& n : out.stripped.notes) {
            n.hand = Hand::Unknown;
        }
        for (Part& p : out.stripped.parts) {
            p.hand = Hand::Unknown;
        }
    }
    for (const Note& n : out.stripped.notes) {
        REQUIRE(n.hand == Hand::Unknown);
    }
    out.merged = merge_parts(out.stripped);
    return out;
}

} // namespace

TEST_CASE("hand split, real files: agreement with the named tracks") {
    struct Case {
        const char* file;
        double fixed_floor;
        double smart_floor;
    };
    // The tracks of the file merged into one, so that only pitch and timing tell the hands apart.
    // Measured: Bach 77.0% fixed, 92.4% smart; Satie 89.7% fixed, 88.7% smart. Satie's reference
    // gives one hand chords wider than a hand reaches (59 62 66 with the melody note 78 over it)
    // and has the left hand's chords rise into the melody's register, which no pitch rule can tell
    // from the right hand's, so the plain split does as well there as the smart one.
    for (const Case& c : {Case{"bach-bwv846-prelude.mid", 0.75, 0.91},
                          Case{"satie-gymnopedie-1.mid", 0.85, 0.88}}) {
        CAPTURE(c.file);
        RealFile f = load_real(c.file);
        REQUIRE(needs_hand_split(f.merged));
        HandAgreement fixed_a = compare_hands(f.named, split_hands(f.merged, fixed()));
        HandAgreement smart_a = compare_hands(f.named, split_hands(f.merged, smart()));
        MESSAGE(c.file << ": fixed split at middle C agrees on " << fixed_a.fraction() * 100.0
                       << "% of " << fixed_a.compared << " notes, smart on "
                       << smart_a.fraction() * 100.0 << "%");
        CHECK(fixed_a.fraction() >= c.fixed_floor);
        CHECK(smart_a.fraction() >= c.smart_floor);
        if (std::string(c.file).starts_with("bach")) {
            CHECK(smart_a.fraction() >= fixed_a.fraction() + 0.10);
        }
        // With the tracks as in the file (one a hand), the smart split follows them.
        HandAgreement tracks_a = compare_hands(f.named, split_hands(f.stripped, smart()));
        CHECK(tracks_a.fraction() >= 0.90);
        // Splitting must not change anything but the hands.
        Song split = with_split_hands(f.merged, smart());
        CHECK_FALSE(needs_hand_split(split));
        REQUIRE(split.notes.size() == f.merged.notes.size());
        for (std::size_t i = 0; i < split.notes.size(); ++i) {
            CHECK(split.notes[i].pitch == f.merged.notes[i].pitch);
            CHECK(split.notes[i].start_tick == f.merged.notes[i].start_tick);
        }
    }
}

TEST_CASE("hand split, corpus: the smart split on every test song that has reference hands") {
    // The default options (single lines whole), the tracks merged into one. A floor sits two
    // points (or two notes, when that is more) under what the split measured when it was set
    // (just hand-eval tests/data), so a song of a few dozen notes does not fail on one note.
    // `before` is what the split before this one measured; a song that was under 100% may not lose
    // more than a point of it.
    struct Case {
        const char* file;
        double floor;
        double before;
    };
    const Case cases[] = {
        {"abc-two-voices.abc", 0.95, 0.925},
        {"bach-bwv846-prelude.mid", 0.90, 0.756},
        {"beethoven-op48-4-ehre-gottes.mxl", 0.96, 0.984},
        {"difficulty-arpeggio.mid", 0.76, 0.736},
        {"difficulty-etude.mid", 0.86, 0.816},
        {"difficulty-toccata.mid", 0.81, 0.826},
        {"harbour-march.mid", 0.96, 0.879},
        {"lantern-waltz-easy.mid", 0.94, 1.0},
        {"lantern-waltz-in-d.mid", 0.97, 1.0},
        {"lantern-waltz.mid", 0.97, 1.0},
        {"satie-gymnopedie-1.mid", 0.86, 0.887},
    };
    int compared = 0;
    int matching = 0;
    for (const Case& c : cases) {
        CAPTURE(c.file);
        const RealFile f = load_real(c.file);
        const HandAgreement a = compare_hands(f.named, split_hands(f.merged));
        CHECK(a.fraction() >= c.floor);
        if (c.before < 1.0) {
            CHECK(a.fraction() >= c.before - 0.01);
        }
        compared += a.compared;
        matching += a.matching;
    }
    CHECK(static_cast<double>(matching) / compared >= 0.91);
}

TEST_CASE("hand split: a split kept apart goes back on the same notes, and only on those") {
    const Song song = make_song({{0, 40}, {1, 76}, {2, 41}, {3, 77}, {4, 42}, {5, 78}});
    const std::uint64_t before = hand_split_runs();
    const HandSplit split = compute_hand_split(song);
    CHECK(hand_split_runs() == before + 1);
    Song again = song;
    REQUIRE(apply_hand_split(again, split));
    CHECK(hand_split_runs() == before + 1); // putting it back splits nothing
    const Song direct = with_split_hands(song);
    REQUIRE(again.notes.size() == direct.notes.size());
    for (std::size_t i = 0; i < direct.notes.size(); ++i) {
        CHECK(again.notes[i].hand == direct.notes[i].hand);
        CHECK(again.notes[i].accompaniment == direct.notes[i].accompaniment);
    }
    // The same number of notes, other ones: not this split's song.
    Song other = make_song({{0, 41}, {1, 76}, {2, 41}, {3, 77}, {4, 42}, {5, 78}});
    const Song untouched = other;
    CHECK_FALSE(apply_hand_split(other, split));
    for (std::size_t i = 0; i < other.notes.size(); ++i) {
        CHECK(other.notes[i].hand == untouched.notes[i].hand);
    }
    // Another number of notes, too.
    Song shorter = make_song({{0, 40}, {1, 76}});
    CHECK_FALSE(apply_hand_split(shorter, split));
}

TEST_CASE("hand split choice: Auto is the default options, a point is a fixed split") {
    const HandSplitOptions autos = options_for_split_point(kAutoSplit);
    CHECK(autos.method == SplitMethod::Smart);
    CHECK(autos.split_point == kMiddleC);
    CHECK(autos.single_lines_whole);

    const HandSplitOptions at = options_for_split_point(67);
    CHECK(at.method == SplitMethod::Fixed);
    CHECK(at.split_point == 67);
    // The player drew the line: every note is judged by it alone, lines and chords too.
    CHECK_FALSE(at.single_lines_whole);
    CHECK(at.by_pitch);
    CHECK_FALSE(autos.by_pitch);
}

TEST_CASE("hand split choice: a point outside 1..127 is forced into range, below 1 is Auto") {
    CHECK(sanitized_split_point(-5) == kAutoSplit);
    CHECK(sanitized_split_point(0) == kAutoSplit);
    CHECK(sanitized_split_point(1) == 1);
    CHECK(sanitized_split_point(127) == 127);
    CHECK(sanitized_split_point(300) == kMaxSplitPoint);
    CHECK(options_for_split_point(300).split_point == kMaxSplitPoint);
    CHECK(options_for_split_point(-1).method == SplitMethod::Smart);
}

TEST_CASE("hand split choice: a fixed point moves the notes from it up to the right hand") {
    const Song song = make_song({{0, 55}, {1, 62}, {2, 66}, {3, 72}});
    const auto right_from = [&](int point) {
        const std::vector<Hand> h = split_hands(song, options_for_split_point(point));
        std::vector<int> right;
        for (std::size_t i = 0; i < h.size(); ++i) {
            if (h[i] == Hand::Right) {
                right.push_back(song.notes[i].pitch);
            }
        }
        return right;
    };
    CHECK(right_from(56) == std::vector<int>{62, 66, 72});
    CHECK(right_from(63) == std::vector<int>{66, 72});
    CHECK(right_from(66) == std::vector<int>{66, 72}); // from the point itself
    CHECK(right_from(73).empty());
}

TEST_CASE(
    "hand split choice: a placed split obeys pitch alone, a chord too wide for a hand included") {
    // C2 E2 G2 C3 E3 G3 C4 struck together: 36 40 43 48 52 55 60, wider than a hand reaches.
    const Song chord = make_song(
        {{0, 36, 2}, {0, 40, 2}, {0, 43, 2}, {0, 48, 2}, {0, 52, 2}, {0, 55, 2}, {0, 60, 2}});
    const auto right_pitches = [&](int point) {
        const std::vector<Hand> h = split_hands(chord, options_for_split_point(point));
        std::vector<int> right;
        for (std::size_t i = 0; i < h.size(); ++i) {
            if (h[i] == Hand::Right) {
                right.push_back(chord.notes[i].pitch);
            }
        }
        return right;
    };
    CHECK(right_pitches(60) == std::vector<int>{60});
    CHECK(right_pitches(55) == std::vector<int>{55, 60});
    CHECK(right_pitches(36) ==
          std::vector<int>{36, 40, 43, 48, 52, 55, 60}); // from the lowest note
    CHECK(right_pitches(61).empty());
    // Auto keeps its own rule for the same chord: it is not by pitch alone.
    const std::vector<Hand> autos = split_hands(chord, options_for_split_point(kAutoSplit));
    int auto_right = 0;
    for (const Hand h : autos) {
        auto_right += h == Hand::Right ? 1 : 0;
    }
    CHECK(auto_right != 1);
}

TEST_CASE("hand split choice: a placed split leaves notes that have a hand, and drums, alone") {
    Song song = make_song({{0, 40}, {1, 80}, {2, 70}, {3, 36}});
    song.notes[0].hand = Hand::Right; // named: stays right though far below the point
    song.notes[1].hand = Hand::Left;
    song.notes[3].drum = true;
    const std::vector<Hand> h = split_hands(song, options_for_split_point(60));
    CHECK(h[0] == Hand::Right);
    CHECK(h[1] == Hand::Left);
    CHECK(h[2] == Hand::Right);
    CHECK(h[3] == Hand::Unknown);
}

TEST_CASE("hand split choice: a changed point gives another split of the same song") {
    const Song song = make_song({{0, 50}, {1, 60}, {2, 70}, {3, 80}});
    const HandSplit a = compute_hand_split(song, options_for_split_point(55));
    const HandSplit b = compute_hand_split(song, options_for_split_point(75));
    CHECK(a.fingerprint == b.fingerprint); // both were made from the same unsplit song
    CHECK(a.note_hands != b.note_hands);
    // Either goes onto the already split song: it was made from the unsplit one.
    Song split = with_split_hands(song, options_for_split_point(55));
    const std::vector<Hand> before = split_hands(song, options_for_split_point(55));
    CHECK(split.notes[1].hand == before[1]);
    CHECK(apply_hand_split(split, b, true));
    CHECK(split.notes[2].hand == Hand::Left);
    CHECK(split.notes[3].hand == Hand::Right);
}

TEST_CASE("hand split choice: the captured hands put a song's own hands back after a split") {
    // A song with a named hand, a drum part, an accompaniment part and open notes: the capture
    // holds all of it, and a split of any point is undone by it, whatever split is on the copy.
    Song song = make_song({{0, 48}, {1, 72}, {2, 50}, {3, 74}, {4, 36}, {5, 40}, {6, 55}});
    song.notes[0].hand = Hand::Right;
    song.notes[4].drum = true;
    song.notes[4].track = 1;
    song.notes[4].channel = 9;
    song.notes[5].track = 2; // the accompaniment part's notes
    song.notes[5].channel = 1;
    song.notes[6].track = 2;
    song.notes[6].channel = 1;
    Part piano;
    piano.track = 0;
    piano.channel = 0;
    piano.note_count = 4;
    Part drums;
    drums.track = 1;
    drums.channel = 9;
    drums.drum = true;
    drums.hand = Hand::Left; // a drum part keeps whatever hand it had
    drums.note_count = 1;
    Part backing;
    backing.track = 2;
    backing.channel = 1;
    backing.accompaniment = true;
    backing.note_count = 2;
    song.parts = {piano, drums, backing};
    const std::uint64_t given = song_fingerprint(song);
    const HandSplit captured = capture_hands(song);
    CHECK(captured.fingerprint == given);
    CHECK(captured.note_hands.size() == song.notes.size());
    CHECK(captured.part_hands.size() == song.parts.size());
    CHECK(captured.part_accompaniment == std::vector<bool>{false, false, true});

    for (const int point : {kAutoSplit, 50, 90}) {
        Song copy = with_split_hands(song, options_for_split_point(point));
        CHECK(song_fingerprint(copy) != given); // the split changed the notes
        CHECK(copy.notes[5].accompaniment);     // the backing part's notes are flagged now
        CHECK(apply_hand_split(copy, captured, true));
        CHECK(song_fingerprint(copy) == given); // and the capture undoes it exactly
        for (std::size_t i = 0; i < song.notes.size(); ++i) {
            CHECK(copy.notes[i].hand == song.notes[i].hand);
            CHECK(copy.notes[i].accompaniment == song.notes[i].accompaniment);
        }
        for (std::size_t i = 0; i < song.parts.size(); ++i) {
            CHECK(copy.parts[i].hand == song.parts[i].hand);
            CHECK(copy.parts[i].accompaniment == song.parts[i].accompaniment);
        }
    }
    // The capture is for these notes only.
    Song other = make_song({{0, 48}});
    CHECK_FALSE(apply_hand_split(other, captured, true));
}

TEST_CASE("hand split choice: the split decides only when a playable note has no hand") {
    Song song = make_song({{0, 48}, {1, 72}});
    CHECK(hand_split_decides(song));
    song.notes[0].hand = Hand::Left;
    CHECK(hand_split_decides(song)); // one note is still open
    song.notes[1].hand = Hand::Right;
    CHECK_FALSE(hand_split_decides(song)); // the file named both hands
    Song drums = make_song({{0, 36}});
    drums.notes[0].drum = true;
    CHECK_FALSE(hand_split_decides(drums));
    CHECK_FALSE(hand_split_decides(Song{}));
    // A split song stays a song the split decided nothing of only if it was named: the check is on
    // the song as it came, which is the one kept for re-splitting.
    const Song named = [&] {
        Song s = make_song({{0, 48}, {1, 72}});
        s.notes[0].hand = Hand::Left;
        s.notes[1].hand = Hand::Right;
        return s;
    }();
    CHECK_FALSE(hand_split_decides(named));
    CHECK(hand_split_decides(make_song({{0, 48}})));
}

TEST_CASE("hand split choice: the suggested point is the straight line that fits the hands best") {
    Song song = make_song({{0, 40}, {1, 45}, {2, 50}, {3, 72}, {4, 76}, {5, 80}});
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        song.notes[i].hand = i < 3 ? Hand::Left : Hand::Right;
    }
    // Any point from 51 to 72 puts everyone in the right hand: the middle of that run.
    CHECK(suggested_split_point(song) == (51 + 72) / 2);

    // One left-hand note above a right-hand one: the line goes where it costs fewest.
    song.notes[2].pitch = 74;
    const int point = suggested_split_point(song);
    CHECK(point >= 46);
    CHECK(point <= 72);
}

TEST_CASE("hand split choice: the suggested point with one hand only, or none, is middle C") {
    Song right = make_song({{0, 70}, {1, 72}});
    for (Note& n : right.notes) {
        n.hand = Hand::Right;
    }
    // Every point from 1 to 70 puts all notes in the right hand: open at the low end, so the
    // point of it nearest middle C.
    CHECK(suggested_split_point(right) == kMiddleC);
    Song left = make_song({{0, 40}, {1, 45}});
    for (Note& n : left.notes) {
        n.hand = Hand::Left;
    }
    CHECK(suggested_split_point(left) == kMiddleC);
    CHECK(suggested_split_point(Song{}) == kMiddleC);
}
