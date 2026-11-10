// SPDX-License-Identifier: GPL-3.0-or-later
// A note's hand is its staff: the treble staff the right hand, the bass staff the left. One rule
// (staff_plan.hpp) puts the tracks on staves for the sheet-music writer and gives the hands to the
// unnamed tracks, so the engraved page and the falling notes never disagree. A named hand wins,
// an unknown hand stays unknown, a backing part is never graded, a stale split is never applied.
#include "hand_split_job.hpp"
#include "song_controller_test_support.hpp"
#include "song_library.hpp"

#include "etude/hands_present.hpp"
#include "etude/instrument.hpp"
#include "etude/staff_plan.hpp"

#include "etude/abc.hpp"
#include "etude/hand_split.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"
#include "etude/sheet_source.hpp"

#include "smf_builder.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;
using etude::test::Track;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Bytes bytes_of(const std::string& text) { return Bytes(text.begin(), text.end()); }

// A whole note of a MusicXML piano part, on `staff` (and in the voice of the same number).
std::string xml_note(const char* step, int octave, int staff) {
    return std::string("<note><pitch><step>") + step + "</step><octave>" + std::to_string(octave) +
           "</octave></pitch><duration>4</duration><voice>" + std::to_string(staff) +
           "</voice><staff>" + std::to_string(staff) + "</staff></note>";
}

// ---- MIDI built from quarter-note steps on the beat grid (480 ticks a quarter)

struct Step {
    int tick;
    int pitch;
    int length;
};

Step step(int quarter, int pitch, int quarters = 1) {
    return {quarter * 480, pitch, quarters * 480};
}

Track notes_track(const char* name, int channel, const std::vector<Step>& steps) {
    Track t;
    if (name != nullptr) {
        t.name(0, name);
    }
    t.program(0, channel, 0);
    std::vector<std::array<int, 3>> events; // tick, on (1) or off (0), pitch
    for (const Step& s : steps) {
        events.push_back({s.tick, 1, s.pitch});
        events.push_back({s.tick + s.length, 0, s.pitch});
    }
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a[0] != b[0] ? a[0] < b[0] : a[1] < b[1]; // an off before the next on
    });
    int at = 0;
    for (const auto& e : events) {
        const auto delta = static_cast<std::uint32_t>(e[0] - at);
        at = e[0];
        e[1] == 1 ? t.on(delta, channel, e[2]) : t.off(delta, channel, e[2]);
    }
    return t.end();
}

// Eight bars: a chord of `pitches` on every beat.
std::vector<Step> chords_on_beats(std::initializer_list<int> pitches, int bars = 8) {
    std::vector<Step> steps;
    for (int q = 0; q < bars * 4; ++q) {
        for (const int p : pitches) {
            steps.push_back(step(q, p));
        }
    }
    return steps;
}

// The notes of a file as one track of one channel with no name: a piano file that gives no hands.
Bytes one_track_of(const Bytes& file) {
    const MidiLoadResult r = load_midi(file);
    REQUIRE(r.ok);
    std::vector<Step> steps;
    for (const Note& n : r.song.notes) {
        if (!n.drum) {
            steps.push_back({static_cast<int>(n.start_tick), n.pitch,
                             static_cast<int>(n.end_tick - n.start_tick)});
        }
    }
    return test::smf({notes_track(nullptr, 0, steps)}, r.song.tempo.ticks_per_quarter);
}

Song split_song(const Bytes& bytes) {
    const MidiLoadResult r = load_midi(bytes);
    REQUIRE(r.ok);
    return with_split_hands(r.song);
}

// The practised notes as the written page places them (written bar, offset in 1/960 quarters,
// pitch) with their hand, sorted.
using Placed = std::vector<std::tuple<int, long, int, int>>;

Placed placed(const Song& song) {
    Placed out;
    const int first = song.measures.front().number;
    const double tpq = song.tempo.ticks_per_quarter;
    for (const Note& n : song.notes) {
        if (n.drum || n.accompaniment) {
            continue;
        }
        const auto k = static_cast<std::size_t>(n.measure - first);
        const double offset = static_cast<double>(n.start_tick - song.measures[k].start_tick) / tpq;
        out.emplace_back(n.written_measure(), std::lround(offset * 960.0), n.pitch,
                         static_cast<int>(n.hand));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The song's sheet, read back as a score: every note comes back on the staff its hand put it on,
// and a score gives its notes the hand of their staff.
Song sheet_read_back(const Song& song) {
    const SheetSource source = sheet_source_for_midi(song);
    REQUIRE(source.status == SheetSourceStatus::Ok);
    const ScoreLoadResult back = load_musicxml(bytes_of(source.musicxml));
    REQUIRE(back.ok);
    return back.song;
}

int count_hand(const Song& song, Hand hand) {
    return static_cast<int>(std::count_if(song.notes.begin(), song.notes.end(), [&](const Note& n) {
        return !n.drum && !n.accompaniment && n.hand == hand;
    }));
}

// Every note's hand is the staff the sheet draws it on.
void check_staff_is_hand(const Song& song) {
    const Song back = sheet_read_back(song);
    CHECK(placed(back) == placed(song));
}

} // namespace

TEST_CASE("staff plan: an accompaniment track is left out, the others go to a staff") {
    const StaffTrack tracks[] = {
        {Hand::Unknown, false, 10, 10 * 72}, // unnamed, high
        {Hand::Unknown, false, 10, 10 * 48}, // unnamed, low
        {Hand::Unknown, true, 10, 10 * 60},  // accompaniment
        {Hand::Unknown, false, 0, 0},        // no notes
    };
    const StaffPlan plan = plan_staves(tracks);
    CHECK(plan.staff[0] == Staff::Treble);
    CHECK(plan.staff[1] == Staff::Bass);
    CHECK(plan.staff[2] == Staff::None);
    CHECK(plan.staves == 2);
    CHECK(plan.used == 2);
    CHECK(plan.decides);
    CHECK(hand_of_staff(Staff::Treble) == Hand::Right);
    CHECK(hand_of_staff(Staff::Bass) == Hand::Left);
    CHECK(hand_of_staff(Staff::None) == Hand::Unknown);
}

TEST_CASE("staff plan: a named hand wins over the register, an unnamed track needs a neighbour") {
    const StaffTrack named[] = {
        {Hand::Left, false, 10, 10 * 72},  // named left hand, high
        {Hand::Right, false, 10, 10 * 48}, // named right hand, low
    };
    const StaffPlan plan = plan_staves(named);
    CHECK(plan.staff[0] == Staff::Bass);
    CHECK(plan.staff[1] == Staff::Treble);
    CHECK(plan.staves == 2);

    // One unnamed track alone is not cut by register: the smart split divides it.
    const StaffTrack alone[] = {{Hand::Unknown, false, 10, 10 * 40}};
    const StaffPlan single = plan_staves(alone);
    CHECK(single.staves == 1);
    CHECK_FALSE(single.decides);
    CHECK(single.single_bass); // low music on one staff takes the bass clef
    CHECK(single.staff[0] == Staff::Bass);

    // Tracks all on one side make one staff, and the staves do not decide.
    const StaffTrack both_high[] = {{Hand::Unknown, false, 10, 10 * 72},
                                    {Hand::Unknown, false, 10, 10 * 66}};
    const StaffPlan high = plan_staves(both_high);
    CHECK(high.staves == 1);
    CHECK_FALSE(high.decides);
    CHECK_FALSE(high.single_bass);
    CHECK(high.staff[0] == Staff::Treble);
}

TEST_CASE("staff = hand: a two-staff score gives its staves their hands") {
    // The treble staff dips under middle C and the bass staff climbs over it: only the staff
    // decides.
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<score-partwise version=\"3.1\"><part-list>"
        "<score-part id=\"P1\"><part-name>Piano</part-name></score-part></part-list>"
        "<part id=\"P1\"><measure number=\"1\"><attributes><divisions>1</divisions>"
        "<time><beats>4</beats><beat-type>4</beat-type></time><staves>2</staves></attributes>" +
        xml_note("A", 3, 1) + "<backup><duration>4</duration></backup>" + xml_note("E", 4, 2) +
        "</measure></part></score-partwise>";
    const ScoreLoadResult r = load_musicxml(bytes_of(xml));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    for (const Note& n : r.song.notes) {
        CHECK(n.hand == (n.pitch == 57 ? Hand::Right : Hand::Left)); // A3 on staff 1, E4 on staff 2
    }
    // The score's hands stay as they are: no split decides them.
    CHECK_FALSE(hand_split_decides(r.song));
    CHECK(with_split_hands(r.song).notes[0].hand == r.song.notes[0].hand);
}

TEST_CASE("staff = hand: two unnamed MIDI tracks, the lower is the left hand whole") {
    // The lower track climbs to 64, over middle C; the upper dips to 60. Register alone would
    // give those notes to the other hand.
    std::vector<Step> low = chords_on_beats({48, 55});
    for (int bar = 0; bar < 8; ++bar) {
        low.push_back(step(bar * 4 + 1, 59));
        low.push_back(step(bar * 4 + 1, 64));
    }
    std::vector<Step> high = chords_on_beats({67, 72});
    for (int bar = 0; bar < 8; ++bar) {
        high.push_back(step(bar * 4 + 2, 60));
    }
    const Bytes bytes = test::smf({notes_track(nullptr, 0, low), notes_track(nullptr, 1, high)});
    const Song song = split_song(bytes);
    REQUIRE(count_hand(song, Hand::Left) > 0);
    for (const Note& n : song.notes) {
        CHECK(n.hand == (n.track == 0 ? Hand::Left : Hand::Right));
    }
    check_staff_is_hand(song);
    CHECK(sheet_source_for_midi(song).musicxml.find("<staves>2</staves>") != std::string::npos);
}

TEST_CASE("staff = hand: named MIDI tracks keep their hands and the sheet draws them there") {
    // The right hand is the low track and the left hand the high one, by their names: the names
    // decide, and the sheet puts each track on its hand's staff.
    const Bytes bytes = test::smf({notes_track("Right hand", 0, chords_on_beats({52, 57})),
                                   notes_track("Left hand", 1, chords_on_beats({64, 69}))});
    const Song song = split_song(bytes);
    for (const Note& n : song.notes) {
        CHECK(n.hand == (n.track == 0 ? Hand::Right : Hand::Left));
    }
    check_staff_is_hand(song);
}

TEST_CASE("staff = hand: a one-track MIDI is split once and the sheet is a grand staff from it") {
    // Bach's prelude with its two named tracks merged into one that names nothing.
    const Bytes bytes = one_track_of(read_data("bach-bwv846-prelude.mid"));
    const Song song = split_song(bytes);
    CHECK(count_hand(song, Hand::Left) > 0);
    CHECK(count_hand(song, Hand::Right) > 0);
    const SheetSource source = sheet_source_for_midi(song);
    REQUIRE(source.status == SheetSourceStatus::Ok);
    CHECK(source.musicxml.find("<staves>2</staves>") != std::string::npos);
    check_staff_is_hand(song);
}

TEST_CASE("sheet source: an ABC tune and a MusicXML score keep every note and its accidental") {
    const std::string xml =
        "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list><score-part id=\"P1\">"
        "<part-name>Piano</part-name></score-part></part-list><part id=\"P1\"><measure "
        "number=\"1\">"
        "<attributes><divisions>1</divisions></attributes>"
        "<note><pitch><step>C</step><alter>1</alter><octave>4</octave></pitch><duration>2</"
        "duration>"
        "<accidental>sharp</accidental></note>"
        "<note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration></note>"
        "</measure></part></score-partwise>";
    const std::string abc = "X:1\nT:t\nM:4/4\nL:1/4\nK:G\n^c c =F F | c F =F2 |]\n";
    const auto pitches = [](const Song& song) {
        std::vector<int> out;
        for (const Note& n : song.notes) {
            out.push_back(n.pitch);
        }
        return out;
    };
    const auto count = [](const std::string& text, const std::string& what) {
        int n = 0;
        for (std::size_t at = text.find(what); at != std::string::npos;
             at = text.find(what, at + 1)) {
            ++n;
        }
        return n;
    };
    // {score text, is ABC, accidentals it must show: written ones only, the bar's last to its end}
    for (const auto& [text, is_abc, accidentals] :
         std::initializer_list<std::tuple<std::string, bool, int>>{{abc, true, 3},
                                                                   {xml, false, 1}}) {
        const Bytes bytes = bytes_of(text);
        const ScoreLoadResult in = is_abc ? load_abc(bytes) : load_musicxml(bytes);
        REQUIRE(in.ok);
        const SheetSource source = sheet_source_for_score(bytes);
        REQUIRE(source.status == SheetSourceStatus::Ok);
        const ScoreLoadResult back = load_musicxml(bytes_of(source.musicxml));
        REQUIRE(back.ok);
        CHECK(pitches(back.song) == pitches(in.song));
        CHECK(count(source.musicxml, "<accidental>") == accidentals);
    }
}

// ---- Hands from the notes: a named hand wins, an unknown one stays unknown

namespace {

struct N {
    double beat;
    int pitch;
    double beats = 1.0;
};

// Notes at 120 BPM, 480 ticks per quarter, all with Unknown hands.
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

HandSplitOptions split_options(SplitMethod method, int split = kMiddleC) {
    HandSplitOptions o;
    o.method = method;
    o.split_point = split;
    o.single_lines_whole = false;
    return o;
}

// One track of notes in a row, one every 240 ticks (a quarter), held 240 ticks.
Track line_track(int from, int to, int channel = 0, int program = -1, const char* name = nullptr) {
    Track t;
    if (name != nullptr) {
        t.name(0, name);
    }
    if (program >= 0) {
        t.program(0, channel, program);
    }
    for (int p = from; p <= to; ++p) {
        t.on(0, channel, p).off(240, channel, p);
    }
    return t.end();
}

int backing_count(const Song& s) {
    return static_cast<int>(std::count_if(s.notes.begin(), s.notes.end(),
                                          [](const Note& n) { return n.accompaniment; }));
}

} // namespace

TEST_CASE("hand split: from the split point up is the right hand, a named hand and drums stay") {
    Song song = make_song({{0, 48}, {1, 59}, {2, 60}, {3, 72}});
    const std::vector<Hand> h = split_hands(song, split_options(SplitMethod::Fixed));
    CHECK(h == std::vector<Hand>{Hand::Left, Hand::Left, Hand::Right, Hand::Right});
    CHECK(split_hands(song, split_options(SplitMethod::Fixed, 56))[1] == Hand::Right);
    CHECK(song.notes[0].hand == Hand::Unknown); // the song itself is not touched
    CHECK(needs_hand_split(song));

    Song named = make_song({{0, 80}, {1, 40}, {2, 40}});
    named.notes[0].hand = Hand::Left; // a named track says so, whatever the pitch
    named.notes[2].drum = true;
    for (const SplitMethod m : {SplitMethod::Fixed, SplitMethod::Smart}) {
        const std::vector<Hand> g = split_hands(named, split_options(m));
        CHECK(g[0] == Hand::Left);
        CHECK(g[1] == Hand::Left);
        CHECK(g[2] == Hand::Unknown);
    }
    named.notes[1].hand = Hand::Left;
    CHECK_FALSE(needs_hand_split(named));
}

TEST_CASE("hand split: a chord stays in one hand unless one hand cannot reach it") {
    const Song straddle = make_song({{0, 58, 2}, {0, 62, 2}, {0, 65, 2}}); // around middle C
    const Song wide = make_song({{0, 36}, {0, 43}, {0, 52}, {0, 64}, {0, 71}, {0, 76}});
    const Song tenth = make_song({{0, 48}, {0, 64}});    // 16 semitones: one hand
    const Song eleventh = make_song({{0, 48}, {0, 65}}); // 17: two hands
    for (const SplitMethod m : {SplitMethod::Fixed, SplitMethod::Smart}) {
        const std::vector<Hand> a = split_hands(straddle, split_options(m));
        CHECK(a[0] == a[1]);
        CHECK(a[1] == a[2]);
        const std::vector<Hand> b = split_hands(wide, split_options(m));
        CHECK(b[2] == Hand::Left);
        CHECK(b[3] == Hand::Right);
    }
    const std::vector<Hand> c = split_hands(tenth, split_options(SplitMethod::Fixed));
    CHECK(c[0] == c[1]);
    const std::vector<Hand> d = split_hands(eleventh, split_options(SplitMethod::Fixed));
    CHECK(d[0] == Hand::Left);
    CHECK(d[1] == Hand::Right);
}

TEST_CASE("hand split, smart: a melody that dips under middle C stays in the right hand") {
    std::vector<N> notes;
    int beat = 0;
    for (const int pitch : {64, 62, 60, 58, 57, 58, 60, 62, 64, 65, 64, 62, 60, 58, 57, 58}) {
        notes.push_back({beat++ * 1.0, pitch});
    }
    for (int k = 0; k < 8; ++k) {
        notes.push_back({k * 2.0 + 0.5, k % 2 == 0 ? 40 : 47});
    }
    Song song = make_song(notes);
    std::stable_sort(song.notes.begin(), song.notes.end(), [](const Note& a, const Note& b) {
        return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
    });
    const std::vector<Hand> smart_h = split_hands(song, split_options(SplitMethod::Smart));
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        CHECK(smart_h[i] == (song.notes[i].pitch <= 47 ? Hand::Left : Hand::Right));
    }
}

TEST_CASE("a single line goes whole to one hand; a part named for a hand keeps it") {
    const Song high = split_song(test::smf({line_track(60, 72)}));
    CHECK(count_hand(high, Hand::Right) == 13);
    CHECK(count_hand(high, Hand::Left) == 0);
    const Song low = split_song(test::smf({line_track(36, 48)}));
    CHECK(count_hand(low, Hand::Left) == 13);
    const Song named = split_song(test::smf({line_track(36, 48, 0, -1, "Right Hand")}));
    CHECK(count_hand(named, Hand::Right) == 13);
}

TEST_CASE("hands from names: a clear name sets the hand, an unclear one stays unknown") {
    for (const char* name : {"Right", "RH", "Piano RH", "Right Hand", "Treble"}) {
        CHECK(hand_from_name(name) == Hand::Right);
    }
    for (const char* name : {"Left", "LH", "Piano L", "Bass", "Lower"}) {
        CHECK(hand_from_name(name) == Hand::Left);
    }
    for (const char* name : {"", "Piano", "Track 1", "Melody", "Leftover", "R", "Right and Left"}) {
        CHECK(hand_from_name(name) == Hand::Unknown);
    }
}

// ---- A backing part is never a hand, never graded

TEST_CASE("backing parts: a voice beside a two-staff piano, and by MIDI program, are not hands") {
    const std::string attrs =
        "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>";
    // The name says voice, the program says piano (an exporter's default): the name wins.
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<score-partwise version=\"3.1\"><part-list>"
        "<score-part id=\"P1\"><part-name>Klavier</part-name></score-part>"
        "<score-part id=\"P2\"><part-name>Singstimme</part-name><midi-instrument id=\"x\">"
        "<midi-program>1</midi-program></midi-instrument></score-part></part-list>"
        "<part id=\"P1\"><measure number=\"1\">" +
        attrs + "<staves>2</staves></attributes>" + xml_note("E", 5, 1) +
        "<backup><duration>4</duration></backup>" + xml_note("C", 3, 2) +
        "</measure></part><part id=\"P2\"><measure number=\"1\">" + attrs + "</attributes>" +
        xml_note("G", 4, 1) + "</measure></part></score-partwise>";
    const ScoreLoadResult r = load_musicxml(bytes_of(xml));
    REQUIRE(r.ok);
    const Song s = with_split_hands(r.song);
    REQUIRE(s.notes.size() == 3);
    for (const Note& n : s.notes) {
        CHECK(n.accompaniment == (n.pitch == 67));
        CHECK(n.hand == (n.pitch == 67 ? Hand::Unknown : n.pitch == 76 ? Hand::Right : Hand::Left));
    }
    CHECK(hands_present(s).both());

    struct Row {
        int program_two; // the second track's General MIDI program
        const char* name;
        bool backing;
    };
    // Beside a piano line: a harmonica or a named voice is backing; a celesta or organ is a
    // keyboard.
    for (const Row& row :
         {Row{22, nullptr, true}, Row{8, nullptr, false}, Row{19, nullptr, false},
          Row{0, "Voice", true}, Row{52, nullptr, true}, Row{80, "Melody", true}}) {
        const Song m = split_song(test::smf(
            {line_track(60, 72, 0, 0), line_track(36, 48, 1, row.program_two, row.name)}));
        CAPTURE(row.program_two);
        CHECK((backing_count(m) > 0) == row.backing);
    }
    // No keyboard part in the file: nothing is backing, both lines are hands.
    const Song strings =
        split_song(test::smf({line_track(60, 72, 0, 40), line_track(36, 48, 1, 41)}));
    CHECK(backing_count(strings) == 0);
    CHECK(count_hand(strings, Hand::Right) == 13);
    CHECK(count_hand(strings, Hand::Left) == 13);
}

TEST_CASE("backing parts: Wait Mode waits for the melody only, and a run of only backing notes is "
          "not graded") {
    // Four notes: a melody (right hand) with a backing note under each, or backing notes only.
    const auto with_backing = [](bool melody) {
        Song s;
        s.tempo = make_tempo_map(480, 120.0);
        for (int i = 0; i < 4; ++i) {
            Note n;
            n.start_tick = i * 480;
            n.end_tick = n.start_tick + 400;
            n.velocity = 80;
            n.pitch = static_cast<std::uint8_t>(melody ? 60 + i : 40 + i);
            if (melody) {
                n.hand = Hand::Right;
                s.notes.push_back(n);
                n.pitch = static_cast<std::uint8_t>(40 + i);
            }
            n.hand = Hand::Unknown;
            n.accompaniment = true;
            n.channel = 2;
            n.track = 2;
            s.notes.push_back(n);
        }
        REQUIRE(finish_song(s));
        for (Part& part : s.parts) {
            part.accompaniment = part.channel == 2;
        }
        LoadedSong out;
        out.song = std::move(s);
        out.title = "Backing";
        out.hash = 0x80;
        return out;
    };
    SongController c;
    std::int64_t now = kSec;
    c.load(with_backing(true), now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    for (int i = 0; i < 40; ++i) {
        now += 16 * kMs;
        c.update(now, {});
    }
    REQUIRE(c.waiting());
    REQUIRE(c.waiting_keys().size() == 1);
    CHECK(c.waiting_keys()[0].key == 60); // the melody's C4, not the backing part's E2

    SongController only;
    FakePiano piano;
    now = kSec;
    only.load(with_backing(false), now);
    only.play(now);
    for (int i = 0; i < 4; ++i) {
        const std::array<MidiEvent, 1> pressed{
            note_on(40 + i, now)}; // the backing notes themselves
        run(only, piano, now, now + 500 * kMs, pressed);
    }
    run(only, piano, now, now + 4 * kSec);
    CHECK(only.take_finished_runs().empty());
}

// ---- A split the player chose is never applied to another song

namespace {

constexpr std::array<int, 12> kPitches{48, 72, 50, 74, 52, 76, 53, 77, 55, 79, 57, 81};

// Twelve notes on one track with no names: the split decides every hand.
Bytes unnamed_bytes() {
    Track t;
    for (std::size_t i = 0; i < kPitches.size(); ++i) {
        t.on(i == 0 ? 0 : 40, 0, kPitches[i]);
        t.off(200, 0, kPitches[i]);
    }
    return test::smf({t});
}

LoadedSong open(const Bytes& bytes) {
    SongFileLoad load = load_song_bytes(bytes, "split.mid");
    REQUIRE(load.ok);
    return std::move(load.song);
}

std::vector<Hand> hands_of(const SongController& c) {
    std::vector<Hand> out;
    for (const Note& n : c.song().song.notes) {
        out.push_back(n.hand);
    }
    return out;
}

} // namespace

TEST_CASE("hand split choice: a chosen point sets the hands; a new song forgets it and a split "
          "still on its way") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    REQUIRE(c.hand_split_available());
    c.set_hand_split(55, now);
    c.wait_hand_split(now);
    CHECK(c.song().song.notes[8].hand == Hand::Right); // the 55
    CHECK(c.song().song.notes[6].hand == Hand::Left);  // the 53
    CHECK(c.practice().split_point == 55);

    // A split still on its way when another unnamed song loads: the new song keeps its own auto
    // split (a line under middle C is all left hand), not the old point 51 that would cut it.
    c.set_hand_split(51, now);
    c.load(open(test::smf({line_track(40, 52)})), now);
    CHECK(c.hand_split() == kAutoSplit);
    REQUIRE(c.hand_split_available());
    c.wait_hand_split(now);
    c.update(now + 10 * kMs, {});
    const std::vector<Hand> hands = hands_of(c);
    REQUIRE(hands.size() == 13);
    CHECK(std::all_of(hands.begin(), hands.end(), [](Hand h) { return h == Hand::Left; }));
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}
