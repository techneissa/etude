// SPDX-License-Identifier: GPL-3.0-or-later
// A note's hand is its staff: the treble staff the right hand, the bass staff the left. One rule
// (staff_plan.hpp) puts the tracks on staves for the sheet-music writer and gives the hands to the
// unnamed tracks, so the engraved page and the falling notes never disagree.
#include "etude/staff_plan.hpp"

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
using etude::test::Track;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Bytes bytes_of(const std::string& text) { return Bytes(text.begin(), text.end()); }

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

Song split_song(const Bytes& bytes, const HandSplitOptions& options = {}) {
    MidiLoadResult r = load_midi(bytes);
    REQUIRE(r.ok);
    return with_split_hands(r.song, options);
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

TEST_CASE("staff plan: one staff takes the bass clef below A3, named hand or not") {
    // As the score writer always read it: the clef follows the music's register.
    const StaffTrack low_left[] = {{Hand::Left, false, 10, 10 * 50}};
    CHECK(plan_staves(low_left).single_bass);
    const StaffTrack high_left[] = {{Hand::Left, false, 10, 10 * 70}};
    const StaffPlan plan = plan_staves(high_left);
    CHECK(plan.staves == 1);
    CHECK_FALSE(plan.single_bass);
    CHECK(plan.staff[0] == Staff::Treble);
}

TEST_CASE("staff plan: for the hands, unnamed tracks well apart are ranked, a score's are not") {
    StaffOptions hands;
    hands.for_hands = true;
    // A left line at 63 and a right line at 79: both over the register line, two hands.
    const StaffTrack high_side[] = {{Hand::Unknown, false, 10, 10 * 63},
                                    {Hand::Unknown, false, 10, 10 * 79}};
    const StaffPlan ranked = plan_staves(high_side, hands);
    CHECK(ranked.staves == 2);
    CHECK(ranked.decides);
    CHECK(ranked.staff[0] == Staff::Bass);
    CHECK(ranked.staff[1] == Staff::Treble);
    // The mirror, both under middle C.
    const StaffTrack low_side[] = {{Hand::Unknown, false, 10, 10 * 40},
                                   {Hand::Unknown, false, 10, 10 * 55}};
    const StaffPlan mirror = plan_staves(low_side, hands);
    CHECK(mirror.decides);
    CHECK(mirror.staff[0] == Staff::Bass);
    CHECK(mirror.staff[1] == Staff::Treble);
    // Close together they stay one staff: the smart split decides.
    const StaffTrack close[] = {{Hand::Unknown, false, 10, 10 * 70},
                                {Hand::Unknown, false, 10, 10 * 74}};
    CHECK_FALSE(plan_staves(close, hands).decides);
    // A score's parts keep the register rule alone: both on the treble staff.
    const StaffPlan score = plan_staves(high_side);
    CHECK(score.staves == 1);
    CHECK_FALSE(score.decides);
}

TEST_CASE("staff plan: for the hands, a stray track is left out and follows the nearest track") {
    StaffOptions hands;
    hands.for_hands = true;
    const StaffTrack tracks[] = {
        {Hand::Unknown, false, 100, 100 * 56}, // left
        {Hand::Unknown, false, 100, 100 * 79}, // right
        {Hand::Unknown, false, 1, 1 * 77},     // one stray note, near the right line
        {Hand::Unknown, false, 1, 1 * 50},     // another, near the left
    };
    const StaffPlan plan = plan_staves(tracks, hands);
    CHECK(plan.decides);
    CHECK(plan.used == 2);
    CHECK(plan.staff[2] == Staff::Treble);
    CHECK(plan.staff[3] == Staff::Bass);
    // The piano alone with a stray beside it is one real track: nothing to decide by.
    const StaffTrack piano[] = {{Hand::Unknown, false, 400, 400 * 62},
                                {Hand::Unknown, false, 3, 3 * 40}};
    CHECK_FALSE(plan_staves(piano, hands).decides);
}

TEST_CASE("staff = hand: a two-staff score gives its staves their hands") {
    const auto note = [](const char* step, int octave, int staff) {
        return "<note><pitch><step>" + std::string(step) + "</step><octave>" +
               std::to_string(octave) + "</octave></pitch><duration>4</duration><voice>" +
               std::to_string(staff) + "</voice><staff>" + std::to_string(staff) + "</staff></note>";
    };
    // The treble staff dips under middle C and the bass staff climbs over it: only the staff decides.
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<score-partwise version=\"3.1\"><part-list>"
        "<score-part id=\"P1\"><part-name>Piano</part-name></score-part></part-list>"
        "<part id=\"P1\"><measure number=\"1\"><attributes><divisions>1</divisions>"
        "<time><beats>4</beats><beat-type>4</beat-type></time><staves>2</staves></attributes>" +
        note("A", 3, 1) + "<backup><duration>4</duration></backup>" + note("E", 4, 2) +
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

TEST_CASE("staff = hand: a split the player places is the line between the staves") {
    const Bytes bytes = one_track_of(read_data("bach-bwv846-prelude.mid"));
    constexpr int kPoint = 62;
    const Song song = split_song(bytes, options_for_split_point(kPoint));
    for (const Note& n : song.notes) {
        CHECK(n.hand == (n.pitch >= kPoint ? Hand::Right : Hand::Left));
    }
    check_staff_is_hand(song);
}

TEST_CASE("staff = hand: a stray track beside the piano leaves the hands to the smart split") {
    // One track with both hands (a bass line under a melody) and a spare channel with three notes:
    // the piano track is cut by the smart split, not given whole to one hand.
    std::vector<Step> piano = chords_on_beats({45, 52}, 8);
    for (int q = 0; q < 32; ++q) {
        piano.push_back(step(q, 76 + (q % 2) * 2));
    }
    const Bytes bytes =
        test::smf({notes_track(nullptr, 0, piano), notes_track(nullptr, 1, {step(0, 40), step(8, 41)})});
    const Song song = split_song(bytes);
    int piano_left = 0;
    int piano_right = 0;
    for (const Note& n : song.notes) {
        if (n.track == 0) {
            (n.hand == Hand::Left ? piano_left : piano_right) += 1;
        }
    }
    CHECK(piano_left > 0);
    CHECK(piano_right > 0);
    check_staff_is_hand(song);
}

// One quarter-note line cycling over `pitches`, eight bars.
std::vector<Step> line_over(std::initializer_list<int> pitches) {
    std::vector<Step> steps;
    const std::vector<int> cycle(pitches);
    for (int q = 0; q < 32; ++q) {
        steps.push_back(step(q, cycle[static_cast<std::size_t>(q) % cycle.size()]));
    }
    return steps;
}

TEST_CASE("staff = hand: two unnamed single lines on the treble side are still a hand each") {
    // A left line around 63 and a right line around 79: both above middle C, so no register line
    // parts them, but a line 16 semitones under the other is the left hand.
    const Bytes bytes = test::smf({notes_track(nullptr, 0, line_over({60, 63, 66, 63})),
                                   notes_track(nullptr, 1, line_over({76, 79, 82, 79}))});
    const Song song = split_song(bytes);
    for (const Note& n : song.notes) {
        CHECK(n.hand == (n.track == 0 ? Hand::Left : Hand::Right));
    }
    check_staff_is_hand(song);
}

TEST_CASE("staff = hand: two unnamed single lines under middle C are still a hand each") {
    const Bytes bytes = test::smf({notes_track(nullptr, 0, line_over({38, 40, 43, 40})),
                                   notes_track(nullptr, 1, line_over({53, 55, 58, 55}))});
    const Song song = split_song(bytes);
    for (const Note& n : song.notes) {
        CHECK(n.hand == (n.track == 0 ? Hand::Left : Hand::Right));
    }
    check_staff_is_hand(song);
}

TEST_CASE("staff = hand: one stray note on a third track does not turn the rule off") {
    // A left line at 56, a right line at 79 and a single note of a spare track near the left one.
    const Bytes bytes = test::smf({notes_track(nullptr, 0, line_over({53, 56, 59, 56})),
                                   notes_track(nullptr, 1, line_over({76, 79, 82, 79})),
                                   notes_track(nullptr, 2, {step(8, 52)})});
    const Song song = split_song(bytes);
    for (const Note& n : song.notes) {
        CHECK(n.hand == (n.track == 1 ? Hand::Right : Hand::Left));
    }
    check_staff_is_hand(song);
}
