// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_map.hpp"

#include "etude/evaluator.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

constexpr int kTpq = 480;
constexpr std::int64_t kBar = 4 * kTpq; // 4/4

// A song of `written.size()` played bars of 4/4: bar k plays written bar written[k] (numbered from
// 1). Notes are added with `add`.
struct SongMaker {
    Song song;
    explicit SongMaker(const std::vector<int>& written) {
        song.tempo.ticks_per_quarter = kTpq;
        for (std::size_t k = 0; k < written.size(); ++k) {
            Measure m;
            m.number = static_cast<int>(k) + 1;
            m.start_tick = static_cast<std::int64_t>(k) * kBar;
            m.source_number = written[k];
            song.measures.push_back(m);
        }
        song.end_tick = static_cast<std::int64_t>(written.size()) * kBar;
    }
    // A note in played bar `bar` (1-based) at `beat` quarters, `beats` long.
    std::uint32_t add(int bar, double beat, int pitch, double beats = 1.0, Hand hand = Hand::Right) {
        Note n;
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.start_tick = static_cast<std::int64_t>(bar - 1) * kBar + static_cast<std::int64_t>(beat * kTpq);
        n.end_tick = n.start_tick + static_cast<std::int64_t>(beats * kTpq);
        n.measure = bar;
        n.source_measure = song.measures[static_cast<std::size_t>(bar - 1)].source_number;
        n.hand = hand;
        song.notes.push_back(n);
        return static_cast<std::uint32_t>(song.notes.size() - 1);
    }
};

SheetMark note_mark(int bar, double offset_q, int pitch, float x, int system = 0, int chord = -1) {
    SheetMark m;
    m.kind = SheetMarkKind::Note;
    m.system = system;
    m.bar = bar;
    m.offset_q = offset_q;
    m.pitch = pitch;
    m.head_x = x;
    m.box = {x - 5.0f, 10.0f, 10.0f, 8.0f};
    m.chord = chord;
    return m;
}

SheetLayout two_bars() {
    SheetLayout l;
    l.systems.push_back({400.0f, 100.0f, 20.0f, 80.0f});
    l.bars.push_back({0, 0, 0.0f, 200.0f});
    l.bars.push_back({1, 0, 200.0f, 400.0f});
    return l;
}

std::vector<std::uint32_t> marks_of(const SheetMap& map, std::uint32_t note) {
    return {map.note_marks.begin() + map.note_first[note], map.note_marks.begin() + map.note_first[note + 1]};
}

} // namespace

TEST_CASE("sheet map: a note matches the mark of its written bar, offset and pitch") {
    SongMaker s({1, 2});
    const auto c = s.add(1, 0.0, 60);
    const auto e = s.add(1, 2.0, 64);
    const auto g = s.add(2, 1.0, 67);
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 60, 20.0f));
    l.marks.push_back(note_mark(0, 2.0, 64, 120.0f));
    l.marks.push_back(note_mark(1, 1.0, 67, 260.0f));
    l.marks.push_back(note_mark(1, 1.0, 69, 260.0f)); // no note of that pitch there
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(map.practised == 3);
    CHECK(map.matched == 3);
    CHECK(marks_of(map, c) == std::vector<std::uint32_t>{0});
    CHECK(marks_of(map, e) == std::vector<std::uint32_t>{1});
    CHECK(marks_of(map, g) == std::vector<std::uint32_t>{2});
    CHECK(map.mark_first[3] == map.mark_first[4]); // the stray mark shows no note
    CHECK(map.usable());
}

TEST_CASE("sheet map: a note a tick from its mark still matches, two ticks do not") {
    // At 480 ticks a quarter one tick is two units of 1/960: the MIDI grid check lets a note sit
    // one tick off the grid, and the writer puts its mark on the grid.
    SongMaker s({1});
    const auto late = s.add(1, 0.5, 60);
    s.song.notes[late].start_tick += 1;  // a 32nd after beat 1 and a half, one tick late
    const auto early = s.add(1, 1.0, 62);
    s.song.notes[early].start_tick -= 1; // one tick early
    const auto far = s.add(1, 2.0, 64);
    s.song.notes[far].start_tick += 2;   // two ticks late: off the grid
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.5, 60, 50.0f));
    l.marks.push_back(note_mark(0, 1.0, 62, 70.0f));
    l.marks.push_back(note_mark(0, 2.0, 64, 90.0f));
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(map.matched == 2);
    CHECK(marks_of(map, late) == std::vector<std::uint32_t>{0});
    CHECK(marks_of(map, early) == std::vector<std::uint32_t>{1});
    CHECK(marks_of(map, far).empty());
}

TEST_CASE("sheet map: of two marks within the slack the nearer one is the note's") {
    SongMaker s({1});
    const auto n = s.add(1, 1.0, 60);
    s.song.notes[n].start_tick += 1;
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 1.0, 60, 50.0f));
    l.marks.push_back(note_mark(0, 1.0 + 2.0 / 960.0, 60, 52.0f)); // exactly where the note is
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(marks_of(map, n) == std::vector<std::uint32_t>{1});
}

TEST_CASE("sheet map: usable from 98% of the practised notes matched") {
    SongMaker s({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13});
    SheetLayout l;
    l.systems.push_back({400.0f, 100.0f, 20.0f, 80.0f});
    for (int i = 0; i < 50; ++i) {
        const int bar = i / 4 + 1;
        s.add(bar, static_cast<double>(i % 4), 60);
        if (i >= 1) { // the first note has no mark: 49 of 50
            l.marks.push_back(note_mark(bar - 1, static_cast<double>(i % 4), 60, 10.0f * static_cast<float>(i)));
        }
    }
    SheetMap map = build_sheet_map(l, s.song);
    CHECK(map.matched == 49);
    CHECK(map.usable()); // exactly 98%
    l.marks.erase(l.marks.begin()); // 48 of 50: 96%
    map = build_sheet_map(l, s.song);
    CHECK(map.matched == 48);
    CHECK_FALSE(map.usable());
}

TEST_CASE("sheet map: drums and accompaniment are not practised and match nothing") {
    SongMaker s({1});
    s.add(1, 0.0, 60);
    const auto drum = s.add(1, 1.0, 38);
    s.song.notes[drum].drum = true;
    const auto acc = s.add(1, 2.0, 64);
    s.song.notes[acc].accompaniment = true;
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 60, 20.0f));
    l.marks.push_back(note_mark(0, 2.0, 64, 120.0f));
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(map.practised == 1);
    CHECK(map.matched == 1);
    CHECK(marks_of(map, acc).empty());
}

TEST_CASE("sheet map: a tied head shows the note it continues, across the bar line") {
    SongMaker s({1, 2});
    const auto held = s.add(1, 2.0, 60, 4.0); // beat 3 of bar 1 to beat 3 of bar 2
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 2.0, 60, 120.0f));
    l.marks.push_back(note_mark(1, 0.0, 60, 220.0f)); // the tied head
    l.marks.back().tied = true;
    l.marks.push_back(note_mark(1, 2.0, 60, 300.0f)); // after the note ended: nothing
    l.marks.back().tied = true;
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(map.matched == 1);
    CHECK(marks_of(map, held) == std::vector<std::uint32_t>{0, 1});
    CHECK(map.mark_first[2] == map.mark_first[3]);
}

TEST_CASE("sheet map: a grace note the song lacks shows nothing, even over a held note") {
    SongMaker s({1});
    const auto held = s.add(1, 0.0, 60, 4.0);
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 60, 20.0f));
    l.marks.push_back(note_mark(0, 2.0, 60, 110.0f)); // a grace C under the held C, no tie
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(marks_of(map, held) == std::vector<std::uint32_t>{0});
    CHECK(map.mark_first[1] == map.mark_first[2]);
}

TEST_CASE("sheet map: the cursor moves through each onset in proportion to the ticks") {
    SongMaker s({1, 2});
    s.add(1, 0.0, 60);
    s.add(1, 2.0, 64);
    s.add(2, 0.0, 67);
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 60, 20.0f));
    l.marks.push_back(note_mark(0, 2.0, 64, 120.0f));
    l.marks.push_back(note_mark(1, 0.0, 67, 220.0f));
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(sheet_cursor_at(map, -100.0).x == doctest::Approx(20.0f)); // before the start: held there
    CHECK(sheet_cursor_at(map, 0.0).x == doctest::Approx(20.0f));
    CHECK(sheet_cursor_at(map, 1.0 * kTpq).x == doctest::Approx(70.0f)); // half way to beat 3
    CHECK(sheet_cursor_at(map, 2.0 * kTpq).x == doctest::Approx(120.0f));
    // The last beats slide to the bar line, then the next bar's first note takes over.
    CHECK(sheet_cursor_at(map, 3.0 * kTpq).x == doctest::Approx(160.0f));
    CHECK(sheet_cursor_at(map, static_cast<double>(kBar)).x == doctest::Approx(220.0f));
    CHECK(sheet_cursor_at(map, 1e9).x == doctest::Approx(400.0f)); // past the end: the last bar line
    CHECK(sheet_cursor_at(map, 0.0).system == 0);
}

TEST_CASE("sheet map: Wait Mode's hold on a chord stands the cursor on it") {
    SongMaker s({1});
    s.add(1, 1.5, 60);
    s.add(1, 1.5, 64);
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 0, 15.0f)); // the rest the bar starts with
    l.marks.back().kind = SheetMarkKind::Rest;
    l.marks.push_back(note_mark(0, 1.5, 60, 88.0f));
    l.marks.push_back(note_mark(0, 1.5, 64, 92.0f));
    const SheetMap map = build_sheet_map(l, s.song);
    // The song waits at the chord's own tick: the cursor is at its heads (their middle one).
    CHECK(sheet_cursor_at(map, 1.5 * kTpq).x == doctest::Approx(92.0f));
}

TEST_CASE("sheet map: on a new system the cursor jumps, it never slides across the page") {
    SongMaker s({1, 2});
    s.add(1, 0.0, 60);
    s.add(2, 0.0, 62);
    SheetLayout l;
    l.systems.push_back({400.0f, 100.0f, 20.0f, 80.0f});
    l.systems.push_back({400.0f, 100.0f, 20.0f, 80.0f});
    l.bars.push_back({0, 0, 0.0f, 400.0f});
    l.bars.push_back({1, 1, 40.0f, 400.0f});
    l.marks.push_back(note_mark(0, 0.0, 60, 50.0f, 0));
    l.marks.push_back(note_mark(1, 0.0, 62, 60.0f, 1));
    const SheetMap map = build_sheet_map(l, s.song);
    const SheetCursor late = sheet_cursor_at(map, static_cast<double>(kBar) - 1.0);
    CHECK(late.system == 0);
    CHECK(late.x > 390.0f);
    const SheetCursor next = sheet_cursor_at(map, static_cast<double>(kBar));
    CHECK(next.system == 1);
    CHECK(next.x == doctest::Approx(60.0f));
}

TEST_CASE("sheet map: a repeat jumps the cursor back to the written bar it plays") {
    // Written bars 1 and 2 under a repeat sign: played 1 2 1 2.
    SongMaker s({1, 2, 1, 2});
    s.add(1, 0.0, 60);
    s.add(2, 0.0, 62);
    s.add(3, 0.0, 60);
    s.add(4, 0.0, 62);
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 60, 20.0f));
    l.marks.push_back(note_mark(1, 0.0, 62, 220.0f));
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(map.matched == 4);
    // Each mark shows a note per pass.
    CHECK(map.mark_first[1] - map.mark_first[0] == 2);
    CHECK(map.bar_run == std::vector<int>{0, 0, 1, 1});
    // The end of written bar 2 on the first pass, then straight back to bar 1's note.
    CHECK(sheet_cursor_at(map, 2.0 * kBar - 1.0).x > 390.0f);
    CHECK(sheet_cursor_at(map, 2.0 * kBar).x == doctest::Approx(20.0f));
    CHECK(sheet_cursor_at(map, 2.5 * kBar).x == doctest::Approx(110.0f)); // half way to the bar line
    CHECK(sheet_cursor_at(map, 3.0 * kBar).x == doctest::Approx(220.0f));
}

TEST_CASE("sheet map: inside a repeat the notes still to come show as ahead again") {
    SongMaker s({1, 2, 1, 2});
    const auto a1 = s.add(1, 0.0, 60);
    const auto b1 = s.add(2, 0.0, 62);
    const auto a2 = s.add(3, 1.0, 60);
    const auto b2 = s.add(4, 0.0, 62);
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 60, 20.0f));
    l.marks.push_back(note_mark(0, 1.0, 60, 70.0f));
    l.marks.push_back(note_mark(1, 0.0, 62, 220.0f));
    const SheetMap map = build_sheet_map(l, s.song);
    // First pass, in bar 2: bar 1's note was played on this pass and shows its result.
    CHECK(sheet_mark_note(map, s.song, 0, 1.5 * kBar) == static_cast<int>(a1));
    CHECK(sheet_mark_note(map, s.song, 2, 1.5 * kBar) == static_cast<int>(b1));
    // Second pass, on bar 1 before its beat 2: its note is to come on this reading, so ahead; the
    // mark of bar 2 is to come too.
    CHECK(sheet_mark_note(map, s.song, 1, 2.0 * kBar + 0.5 * kTpq) == -1);
    CHECK(sheet_mark_note(map, s.song, 2, 2.0 * kBar + 0.5 * kTpq) == -1);
    // Once the cursor reaches them, the second pass's notes.
    CHECK(sheet_mark_note(map, s.song, 1, 2.0 * kBar + 1.0 * kTpq) == static_cast<int>(a2));
    CHECK(sheet_mark_note(map, s.song, 2, 3.0 * kBar) == static_cast<int>(b2));
    // Before anything is played: ahead.
    CHECK(sheet_mark_note(map, s.song, 0, -1.0) == -1);
}

TEST_CASE("sheet map: a first ending keeps its result while the second is read") {
    // Written: 1, 2 (first ending), 3 (second ending). Played: 1 2 1 3.
    SongMaker s({1, 2, 1, 3});
    s.add(1, 0.0, 60);
    const auto first_ending = s.add(2, 0.0, 62);
    s.add(3, 0.0, 60);
    s.add(4, 0.0, 64);
    SheetLayout l = two_bars();
    l.bars.push_back({2, 0, 400.0f, 600.0f});
    l.marks.push_back(note_mark(0, 0.0, 60, 20.0f));
    l.marks.push_back(note_mark(1, 0.0, 62, 220.0f));
    l.marks.push_back(note_mark(2, 0.0, 64, 420.0f));
    const SheetMap map = build_sheet_map(l, s.song);
    CHECK(map.matched == 4);
    // On the second reading, the first ending is not played again: it keeps its result.
    CHECK(sheet_mark_note(map, s.song, 1, 2.5 * kBar) == static_cast<int>(first_ending));
    // The second ending is ahead until reached.
    CHECK(sheet_mark_note(map, s.song, 2, 2.5 * kBar) == -1);
}

TEST_CASE("sheet map: grades turn into the colours a mark shows") {
    CHECK(sheet_state_for_grade(-1) == SheetMarkState::Ahead);
    CHECK(sheet_state_for_grade(static_cast<int>(NoteGrade::Missed)) == SheetMarkState::Missed);
    for (const NoteGrade g : {NoteGrade::Perfect, NoteGrade::Good, NoteGrade::Early, NoteGrade::Late,
                              NoteGrade::Played}) {
        CHECK(sheet_state_for_grade(static_cast<int>(g)) == SheetMarkState::Played);
    }
}

TEST_CASE("sheet map: a chord's stem follows its notes") {
    CHECK(sheet_stem_state(0, 0, 0) == SheetMarkState::Ahead);
    CHECK(sheet_stem_state(3, 1, 0) == SheetMarkState::Ahead);
    CHECK(sheet_stem_state(3, 0, 3) == SheetMarkState::Missed);
    CHECK(sheet_stem_state(3, 0, 1) == SheetMarkState::Played);
    CHECK(sheet_stem_state(3, 0, 0) == SheetMarkState::Played);
}

TEST_CASE("sheet map: a chord's notes are listed together for its stem") {
    SongMaker s({1});
    s.add(1, 0.0, 60);
    s.add(1, 0.0, 64);
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 60, 20.0f, 0, 0));
    l.marks.push_back(note_mark(0, 0.0, 64, 20.0f, 0, 0));
    SheetMark stem = note_mark(0, 0.0, -1, 25.0f, 0, 0);
    stem.kind = SheetMarkKind::Stem;
    l.marks.push_back(stem);
    const SheetMap map = build_sheet_map(l, s.song);
    REQUIRE(map.chord_first.size() == 2);
    CHECK(map.chord_first[1] - map.chord_first[0] == 2); // the two heads, not the stem
}

TEST_CASE("sheet map: a pickup bar is written bar 0") {
    Song song;
    song.tempo.ticks_per_quarter = kTpq;
    Measure pickup;
    pickup.number = 0;
    pickup.start_tick = 0;
    Measure full;
    full.number = 1;
    full.start_tick = kTpq;
    song.measures = {pickup, full};
    song.end_tick = kTpq + kBar;
    Note n;
    n.pitch = 67;
    n.start_tick = 0;
    n.end_tick = kTpq;
    n.measure = 0;
    song.notes.push_back(n);
    n.pitch = 72;
    n.start_tick = kTpq;
    n.end_tick = 2 * kTpq;
    n.measure = 1;
    song.notes.push_back(n);
    SheetLayout l = two_bars();
    l.marks.push_back(note_mark(0, 0.0, 67, 20.0f));
    l.marks.push_back(note_mark(1, 0.0, 72, 220.0f));
    const SheetMap map = build_sheet_map(l, song);
    CHECK(map.matched == 2);
}
