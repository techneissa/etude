// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_target.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

using namespace etude;

namespace {

constexpr int kTpq = 480;

// Played bars of 4/4 (or `den`ths): bar k reads written bar written[k] (numbered from 1). Each
// written bar is drawn on system 0 at x 200*(w-1) .. 200*w, with a quarter note on every beat at
// x 20, 70, 120, 170 into the bar.
struct Sheet {
    Song song;
    SheetLayout layout;
    SheetMap map;

    explicit Sheet(const std::vector<int>& written, int den = 4) {
        song.tempo.ticks_per_quarter = kTpq;
        const std::int64_t bar = 4 * kTpq * 4 / den;
        int written_bars = 0;
        for (std::size_t k = 0; k < written.size(); ++k) {
            Measure m;
            m.number = static_cast<int>(k) + 1;
            m.start_tick = static_cast<std::int64_t>(k) * bar;
            m.source_number = written[k];
            m.denominator = static_cast<std::uint8_t>(den);
            song.measures.push_back(m);
            written_bars = std::max(written_bars, written[k]);
            const std::int64_t beat = bar / 4;
            for (int b = 0; b < 4; ++b) {
                Note n;
                n.pitch = 60;
                n.start_tick = m.start_tick + b * beat;
                n.end_tick = n.start_tick + beat;
                n.measure = m.number;
                n.source_measure = written[k];
                song.notes.push_back(n);
            }
        }
        song.end_tick = static_cast<std::int64_t>(written.size()) * bar;
        layout.systems.push_back({1000.0f, 100.0f, 20.0f, 80.0f});
        for (int w = 0; w < written_bars; ++w) {
            const float x0 = 200.0f * static_cast<float>(w);
            layout.bars.push_back({w, 0, x0, x0 + 200.0f});
            for (int b = 0; b < 4; ++b) {
                SheetMark mk;
                mk.bar = w;
                mk.offset_q = static_cast<double>(b) * 4.0 / den;
                mk.pitch = 60;
                mk.head_x = x0 + 20.0f + 50.0f * static_cast<float>(b);
                mk.box = {mk.head_x - 5.0f, 40.0f, 10.0f, 8.0f};
                layout.marks.push_back(mk);
            }
        }
        map = build_sheet_map(layout, song);
    }
};

} // namespace

TEST_CASE("sheet target: a point means the bar under it, between two bars the nearer one") {
    const Sheet s({1, 2});
    CHECK(sheet_written_bar_at(s.layout, 0, 50.0f) == 0);
    CHECK(sheet_written_bar_at(s.layout, 0, 250.0f) == 1);
    CHECK(sheet_written_bar_at(s.layout, 0, 420.0f) == 1); // past the last bar
    CHECK(sheet_written_bar_at(s.layout, 1, 50.0f) == -1); // no such system
}

TEST_CASE("sheet target: the nearest beat along the cursor's path, and the nearest onset") {
    const Sheet s({1, 2});
    REQUIRE(s.map.usable());
    // Bar 2 (x 200..400): x 280 is between beat 2 (x 270) and beat 3 (x 320), a fifth of the way.
    SheetTarget t = sheet_target_at(s.layout, s.map, s.song, 0, 280.0f, 0.0);
    CHECK(t.played_bar == 1);
    CHECK(t.beat_tick == 4 * kTpq + 1 * kTpq);
    CHECK(t.onset_tick == 4 * kTpq + 1 * kTpq);
    // Half way between beats 2 and 3 rounds up; the onset nearest x 296 is still beat 2's.
    t = sheet_target_at(s.layout, s.map, s.song, 0, 295.0f, 0.0);
    CHECK(t.beat_tick == 4 * kTpq + 2 * kTpq);
    t = sheet_target_at(s.layout, s.map, s.song, 0, 290.0f, 0.0);
    CHECK(t.onset_tick == 4 * kTpq + 1 * kTpq);
    // Left of the first note: beat 1.
    t = sheet_target_at(s.layout, s.map, s.song, 0, 202.0f, 0.0);
    CHECK(t.beat_tick == 4 * kTpq);
    // Near the bar line on the right: the bar's last beat, never the next bar's first.
    t = sheet_target_at(s.layout, s.map, s.song, 0, 398.0f, 0.0);
    CHECK(t.played_bar == 1);
    CHECK(t.beat_tick == 4 * kTpq + 3 * kTpq);
}

TEST_CASE("sheet target: a beat is a 1/denominator note (6/8 has eighths)") {
    const Sheet s({1}, 8); // bars of 4/8 here: beats of 240 ticks at x 20, 70, 120, 170
    const SheetTarget t = sheet_target_at(s.layout, s.map, s.song, 0, 72.0f, 0.0);
    CHECK(t.beat_tick == kTpq / 2);
}

TEST_CASE("sheet target: a repeated bar means the pass being read, else the next, else the last") {
    // Played: written 1, 2, 1, 2 (a repeat): the second pass is a new reading run.
    const Sheet s({1, 2, 1, 2});
    const std::int64_t bar = 4 * kTpq;
    // Playing bar 1: written bar 2 is in this run -> played bar 2 (index 1).
    CHECK(sheet_played_bar_for(s.map, 1, 0.0) == 1);
    // Playing the second pass (index 3): written bar 1 -> index 2, in the same run.
    CHECK(sheet_played_bar_for(s.map, 0, static_cast<double>(3 * bar + 10)) == 2);
    const SheetTarget t = sheet_target_at(s.layout, s.map, s.song, 0, 20.0f, static_cast<double>(3 * bar));
    CHECK(t.played_bar == 2);
    CHECK(t.beat_tick == 2 * bar);
    // A loop dragged from the second pass's first bar keeps to that pass.
    CHECK(sheet_played_bar_near(s.map, 1, 2) == 3);
    CHECK(sheet_played_bar_near(s.map, 0, 1) == 0);
    CHECK(sheet_played_bar_for(s.map, 5, 0.0) == -1);
}

TEST_CASE("sheet target: a loop covers whole bars, in either order") {
    const Sheet s({1, 2, 3, 4});
    const std::int64_t bar = 4 * kTpq;
    SheetLoopRange r = sheet_loop_range(s.map, s.song, 3, 1);
    CHECK(r.first_bar == 1);
    CHECK(r.last_bar == 3);
    CHECK(r.start_tick == bar);
    CHECK(r.end_tick == 4 * bar); // the song's end
    r = sheet_loop_range(s.map, s.song, 2, 2);
    CHECK(r.start_tick == 2 * bar);
    CHECK(r.end_tick == 3 * bar);
    r = sheet_loop_range(s.map, s.song, 2, 9);
    CHECK(r.first_bar == -1);
}

TEST_CASE("sheet target: a loop dragged across a repeat keeps to one pass") {
    // Played: written 1, 2, 1, 2, 3: the repeat's second pass goes on to bar 3.
    const Sheet s({1, 2, 1, 2, 3});
    // Bar 3 is only in the second pass: from the first pass's bar 1 nothing reads it there.
    CHECK(sheet_played_bar_near(s.map, 2, 0) == -1);
    CHECK(sheet_played_bar_near(s.map, 1, 0) == 1);
    // So the loop moves to the second pass: bar 1 there (index 2) to bar 3 (index 4), once.
    SheetLoopEnds ends = sheet_loop_ends(s.map, 0, 2);
    CHECK(ends.anchor == 2);
    CHECK(ends.last == 4);
    // Inside one pass nothing moves.
    ends = sheet_loop_ends(s.map, 0, 1);
    CHECK(ends.anchor == 0);
    CHECK(ends.last == 1);
}

TEST_CASE("sheet target: no pass holding both bars leaves the drag where it was") {
    // Played: written 1, 2, 4, then a jump back to 3: bar 3 is read alone.
    const Sheet s({1, 2, 4, 3});
    const SheetLoopEnds ends = sheet_loop_ends(s.map, 3, 0);
    CHECK(ends.anchor == -1);
    CHECK(ends.last == -1);
}

TEST_CASE("sheet target: Wait Mode's chord is never a rest") {
    Sheet s({1, 2});
    // Bar 2's second beat becomes a rest: its note goes, its mark (drawn as a rest) stays a point
    // of the cursor's path.
    const std::int64_t beat2 = 4 * kTpq + kTpq;
    s.song.notes.erase(std::remove_if(s.song.notes.begin(), s.song.notes.end(),
                                      [&](const Note& n) { return n.start_tick == beat2; }),
                       s.song.notes.end());
    for (SheetMark& mk : s.layout.marks) {
        if (mk.bar == 1 && mk.offset_q == 1.0) {
            mk.kind = SheetMarkKind::Rest;
        }
    }
    s.map = build_sheet_map(s.layout, s.song);
    // A tap by the rest (x 270) goes to the nearest chord, beat 1 (x 220) here, not the rest.
    const SheetTarget t = sheet_target_at(s.layout, s.map, s.song, 0, 262.0f, 0.0);
    CHECK(t.onset_tick != beat2);
    CHECK(t.onset_tick == 4 * kTpq);
}
