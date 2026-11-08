// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/roll_view.hpp"

#include "section_fixtures.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace etude;
using namespace etude::testing;

namespace {

// 32 bars of 4/4 at 120 BPM: 64 s, bar n starts at 2 (n - 1), a beat is 0.5 s.
Song song32() { return bars_song(32); }

} // namespace

TEST_CASE("the whole-song view covers the song") {
    const Song song = song32();
    const RollView v = whole_song_view(song);
    CHECK(v.start_s == 0.0);
    CHECK(v.end_s == doctest::Approx(64.0));
}

TEST_CASE("the narrowest zoom is two bars") {
    const Song song = song32();
    CHECK(min_view_span(song, 10.0) == doctest::Approx(4.0));
    // On the last bars there are only the bars before it to count.
    CHECK(min_view_span(song, 63.0) == doctest::Approx(4.0));
    // A short song: never wider than the song.
    const Song tiny = bars_song(1);
    CHECK(min_view_span(tiny, 0.5) <= tiny.duration_s + 1e-9);
}

TEST_CASE("zoom keeps the focus time in the same place on screen") {
    const Song song = song32();
    const RollView whole = whole_song_view(song);
    const RollView z = zoom_view(song, whole, 4.0, 32.0);
    CHECK(z.span_s() == doctest::Approx(16.0));
    // 32 s was at half the width; it still is.
    CHECK((32.0 - z.start_s) / z.span_s() == doctest::Approx(0.5));
    // Zooming out again returns to the whole song.
    const RollView back = zoom_view(song, z, 0.25, 32.0);
    CHECK(back.start_s == doctest::Approx(0.0));
    CHECK(back.end_s == doctest::Approx(64.0));
}

TEST_CASE("zoom stops at two bars and at the whole song") {
    const Song song = song32();
    RollView v = whole_song_view(song);
    for (int i = 0; i < 20; ++i) {
        v = zoom_view(song, v, 2.0, 10.0);
    }
    CHECK(v.span_s() == doctest::Approx(4.0));
    CHECK(v.start_s >= 0.0);
    CHECK(v.end_s <= 64.0 + 1e-9);
    for (int i = 0; i < 20; ++i) {
        v = zoom_view(song, v, 0.5, 10.0);
    }
    CHECK(v.start_s == doctest::Approx(0.0));
    CHECK(v.end_s == doctest::Approx(64.0));
}

TEST_CASE("zoom at the song's edge keeps the view inside the song") {
    const Song song = song32();
    const RollView z = zoom_view(song, whole_song_view(song), 8.0, 64.0);
    CHECK(z.end_s == doctest::Approx(64.0));
    CHECK(z.start_s >= 0.0);
    const RollView y = zoom_view(song, whole_song_view(song), 8.0, 0.0);
    CHECK(y.start_s == doctest::Approx(0.0));
}

TEST_CASE("zoom ignores a bad factor") {
    const Song song = song32();
    const RollView v = zoom_view(song, whole_song_view(song), 4.0, 10.0);
    CHECK(zoom_view(song, v, 0.0, 10.0) == v);
    CHECK(zoom_view(song, v, -1.0, 10.0) == v);
    CHECK(zoom_view(song, v, std::nan(""), 10.0) == v);
}

TEST_CASE("pan stops at the song's ends and keeps the zoom") {
    const Song song = song32();
    RollView v = zoom_view(song, whole_song_view(song), 4.0, 32.0);
    const double span = v.span_s();
    v = pan_view(song, v, 1000.0);
    CHECK(v.end_s == doctest::Approx(64.0));
    CHECK(v.span_s() == doctest::Approx(span));
    v = pan_view(song, v, -1000.0);
    CHECK(v.start_s == doctest::Approx(0.0));
    CHECK(v.span_s() == doctest::Approx(span));
    CHECK(pan_view(song, v, std::nan("")) == v);
}

TEST_CASE("view_around fits a section with a margin, and widens a short one") {
    const Song song = song32();
    RollView v = view_around(song, 16.0, 32.0, 0.0);
    CHECK(v.start_s == doctest::Approx(16.0));
    CHECK(v.end_s == doctest::Approx(32.0));
    v = view_around(song, 16.0, 32.0, 0.25);
    CHECK(v.span_s() == doctest::Approx(24.0));
    CHECK(v.start_s == doctest::Approx(12.0));
    // One bar: widened to two bars, around its middle.
    v = view_around(song, 20.0, 22.0, 0.0);
    CHECK(v.span_s() == doctest::Approx(4.0));
    CHECK((v.start_s + v.end_s) * 0.5 == doctest::Approx(21.0));
    // Reaching past the song's start: shifted in, not cut.
    v = view_around(song, 0.0, 2.0, 0.0);
    CHECK(v.start_s == doctest::Approx(0.0));
    CHECK(v.span_s() == doctest::Approx(4.0));
    // Reversed ends are the same.
    CHECK(view_around(song, 32.0, 16.0, 0.0) == view_around(song, 16.0, 32.0, 0.0));
}

TEST_CASE("reveal_time moves the view the least") {
    const Song song = song32();
    const RollView v{10.0, 20.0};
    CHECK(reveal_time(song, v, 15.0) == v);
    const RollView right = reveal_time(song, v, 25.0);
    CHECK(right.end_s == doctest::Approx(25.0));
    CHECK(right.span_s() == doctest::Approx(10.0));
    const RollView left = reveal_time(song, v, 4.0, 1.0);
    CHECK(left.start_s == doctest::Approx(3.0));
}

TEST_CASE("bars in view counts partial bars, and beats snap only when zoomed in") {
    const Song song = song32();
    CHECK(bars_in_view(song, RollView{0.0, 64.0}) == 32);
    CHECK(bars_in_view(song, RollView{1.0, 5.0}) == 3);  // bars 1, 2, 3
    CHECK(bars_in_view(song, RollView{2.0, 6.0}) == 2);  // exactly bars 2 and 3
    CHECK_FALSE(beat_snap_zoom(song, RollView{0.0, 64.0}));
    CHECK(beat_snap_zoom(song, RollView{0.0, 16.0}));    // 8 bars
    CHECK_FALSE(beat_snap_zoom(song, RollView{0.0, 18.0}));
    Song bare = bars_song(4);
    bare.measures.clear();
    CHECK(bars_in_view(bare, RollView{0.0, 4.0}) == 0);
    CHECK_FALSE(beat_snap_zoom(bare, RollView{0.0, 4.0}));
}

TEST_CASE("grid lines: bar lines, and beat lines on request") {
    const Song song = song32();
    std::vector<GridLine> lines;
    grid_lines(song, 2.0, 6.0, false, lines);
    REQUIRE(lines.size() == 3);  // bars 2, 3, 4 at 2, 4, 6 s
    CHECK(lines[0].time_s == doctest::Approx(2.0));
    CHECK(lines[0].bar_number == 2);
    CHECK(lines[0].written_number == 2);
    CHECK(lines[2].time_s == doctest::Approx(6.0));
    grid_lines(song, 2.0, 4.0, true, lines);
    REQUIRE(lines.size() == 5);  // bar 2, beats 2-4, bar 3
    CHECK(lines[0].bar);
    CHECK_FALSE(lines[1].bar);
    CHECK(lines[1].beat == 2);
    CHECK(lines[1].time_s == doctest::Approx(2.5));
    CHECK(lines[3].beat == 4);
    CHECK(lines[4].bar);
    grid_lines(song, 5.0, 5.0, true, lines);
    CHECK(lines.empty());
}

TEST_CASE("grid beats follow the time signature") {
    // 6/8 at 120 BPM (a quarter is 0.5 s): an eighth is 0.25 s, six beats to a bar of 1.5 s.
    const Song song = bars_song(8, 120, 6, 8);
    std::vector<GridLine> lines;
    grid_lines(song, 0.0, 1.5, true, lines);
    REQUIRE(lines.size() >= 6);
    CHECK(lines[5].beat == 6);
    CHECK(lines[5].time_s == doctest::Approx(1.25));
    CHECK(lines[1].time_s == doctest::Approx(0.25));
}

TEST_CASE("snapping goes to the nearest bar, or beat when asked") {
    const Song song = song32();
    CHECK(snap_to_grid(song, 4.9, false) == doctest::Approx(4.0));
    CHECK(snap_to_grid(song, 5.1, false) == doctest::Approx(6.0));
    CHECK(snap_to_grid(song, 4.9, true) == doctest::Approx(5.0));
    CHECK(snap_to_grid(song, 4.6, true) == doctest::Approx(4.5));
    CHECK(snap_to_grid(song, 4.2, true) == doctest::Approx(4.0));
    CHECK(snap_to_grid(song, -3.0, true) == doctest::Approx(0.0));
    CHECK(snap_to_grid(song, 99.0, true) == doctest::Approx(64.0));
    CHECK(snap_to_grid(song, std::nan(""), true) == doctest::Approx(0.0));
    Song bare = bars_song(4);
    bare.measures.clear();
    CHECK(snap_to_grid(bare, 3.3, true) == doctest::Approx(3.3));
}

TEST_CASE("the position label names the bar and beat") {
    const Song song = song32();
    CHECK(position_label(song, 0.0) == "Bar 1 \xC2\xB7 beat 1");
    CHECK(position_label(song, 22.0) == "Bar 12 \xC2\xB7 beat 1");
    CHECK(position_label(song, 23.0) == "Bar 12 \xC2\xB7 beat 3");
    CHECK(position_label(song, 23.9) == "Bar 12 \xC2\xB7 beat 4");
    CHECK(position_label(song, 64.0).starts_with("Bar 32"));
    Song bare = bars_song(4);
    bare.measures.clear();
    CHECK(position_label(bare, 7.5) == "0:07");
}

TEST_CASE("the pitch range covers the notes and never gets thinner than two octaves") {
    Song song = song32();
    PitchRange r = song_pitch_range(song);  // one note at 60
    CHECK(r.rows() >= 25);
    CHECK(r.lowest <= 60);
    CHECK(r.highest >= 60);
    Note low;
    low.pitch = 30;
    low.end_tick = 100;
    Note high;
    high.pitch = 100;
    high.end_tick = 100;
    song.notes.push_back(low);
    song.notes.push_back(high);
    r = song_pitch_range(song);
    CHECK(r.lowest == 30);
    CHECK(r.highest == 100);
    CHECK(song_pitch_range(Song{}).rows() == 61);
}

TEST_CASE("thumbnail rects: placement, hands, minimum width and clipping") {
    Song song;
    song.tempo = make_tempo_map(480, 120);
    song.time_signatures = {TimeSignature{0, 4, 4}};
    const auto add = [&](std::int64_t start_tick, std::int64_t end_tick, int pitch, Hand hand) {
        Note n;
        n.start_tick = start_tick;
        n.end_tick = end_tick;
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.hand = hand;
        song.notes.push_back(n);
    };
    add(0, 960, 72, Hand::Right);     // 0 - 1 s
    add(960, 1920, 48, Hand::Left);   // 1 - 2 s
    add(1920, 1921, 60, Hand::Right); // a 1-tick note at 2 s, 1 ms long
    REQUIRE(finish_song(song));
    const PitchRange pitches{48, 72};  // 25 rows
    std::vector<RollRect> rects;
    roll_note_rects(song, 0.0, 4.0, 400, 250, pitches, 1.0, rects);
    REQUIRE(rects.size() == 3);
    // 100 px per second, 10 px per pitch row; the highest pitch is at the top.
    CHECK(rects[0].x0 == doctest::Approx(0.0f));
    CHECK(rects[0].x1 == doctest::Approx(100.0f));
    CHECK(rects[0].y0 == doctest::Approx(0.0f));
    CHECK(rects[0].y1 == doctest::Approx(10.0f));
    CHECK(rects[0].hand == Hand::Right);
    CHECK(rects[1].hand == Hand::Left);
    CHECK(rects[1].y0 == doctest::Approx(240.0f));
    CHECK(rects[1].x0 == doctest::Approx(100.0f));
    // The short note still shows one pixel at least.
    CHECK(rects[2].x1 - rects[2].x0 >= 1.0f);
    // Sides against a split at 1 s: first ends at 1, second starts at 1, third is after.
    CHECK(rects[0].side == -1);
    CHECK(rects[1].side == 1);
    CHECK(rects[2].side == 1);
    // A window that cuts a note: clipped to the tile, and marked as crossing the split.
    roll_note_rects(song, 0.5, 1.5, 100, 250, pitches, 1.0, rects);
    REQUIRE(rects.size() == 2);
    CHECK(rects[0].x0 == doctest::Approx(0.0f));
    CHECK(rects[0].x1 == doctest::Approx(50.0f));
    CHECK(rects[1].x1 == doctest::Approx(100.0f));
    roll_note_rects(song, 0.0, 4.0, 400, 250, pitches, 0.5, rects);
    CHECK(rects[0].side == 0);  // crosses 0.5 s
    // Nothing outside the time window; empty for a bad tile.
    roll_note_rects(song, 3.0, 4.0, 100, 250, pitches, 3.5, rects);
    CHECK(rects.empty());
    roll_note_rects(song, 0.0, 4.0, 0, 250, pitches, 1.0, rects);
    CHECK(rects.empty());
}

TEST_CASE("thumbnail skips drums") {
    Song song = song32();
    Note drum;
    drum.drum = true;
    drum.pitch = 60;
    drum.end_tick = 100;
    song.notes.insert(song.notes.begin(), drum);
    std::vector<RollRect> rects;
    roll_note_rects(song, 0.0, 10.0, 100, 100, PitchRange{40, 80}, 0.0, rects);
    CHECK(rects.size() == 1);  // only the long note at 60
    CHECK(rects[0].fold == 0);
}

TEST_CASE("thumbnail folds pitches out of range by octaves and marks them") {
    Song song = song32();
    Note high;
    high.pitch = 120;
    high.end_tick = 100;
    Note low;
    low.pitch = 20;
    low.end_tick = 100;
    song.notes.insert(song.notes.begin(), {high, low});
    std::vector<RollRect> rects;
    const PitchRange range{40, 80};
    roll_note_rects(song, 0.0, 10.0, 100, 410, range, 0.0, rects);
    REQUIRE(rects.size() == 3); // the long note at 60 stays where it is
    int folded = 0;
    for (const RollRect& r : rects) {
        if (r.fold == -4) { // 120 lowered four octaves to 72
            ++folded;
            CHECK(r.y0 == doctest::Approx(static_cast<float>(80 - 72) * 10.0f));
        } else if (r.fold == 2) { // 20 raised two octaves to 44
            ++folded;
            CHECK(r.y0 == doctest::Approx(static_cast<float>(80 - 44) * 10.0f));
        } else {
            CHECK(r.fold == 0);
        }
    }
    CHECK(folded == 2);
    // A range narrower than an octave may have no place for a pitch class: that note is left out.
    roll_note_rects(song, 0.0, 10.0, 100, 100, PitchRange{61, 63}, 0.0, rects);
    for (const RollRect& r : rects) {
        CHECK(r.y0 >= 0.0f);
        CHECK(r.y1 <= 100.0f + 1e-3f);
    }
}

TEST_CASE("a tile covers the view with room to pan and is rebuilt when stale") {
    const Song song = song32();
    const RollView view{20.0, 28.0};
    const RollTile tile = tile_for_view(song, view, 800, 200);
    REQUIRE(tile.valid());
    CHECK(tile.t0_s == doctest::Approx(12.0));
    CHECK(tile.t1_s == doctest::Approx(36.0));
    CHECK(tile.w == 2400);
    CHECK(tile.h == 200);
    CHECK_FALSE(tile_stale(tile, view, 800));
    // A small pan stays inside; a big one leaves it.
    CHECK_FALSE(tile_stale(tile, pan_view(song, view, 3.0), 800));
    CHECK(tile_stale(tile, pan_view(song, view, 9.0), 800));
    // Zooming changes the density: a little is fine, a lot is stale.
    CHECK_FALSE(tile_stale(tile, RollView{21.0, 28.0}, 800));  // 1.14 x
    CHECK(tile_stale(tile, RollView{22.0, 26.0}, 800));        // 2 x
    CHECK(tile_stale(tile, RollView{12.0, 36.0}, 800));        // 0.33 x
    CHECK(tile_stale(RollTile{}, view, 800));
}

TEST_CASE("a tile is cut to the song and capped in width") {
    const Song song = song32();
    const RollTile whole = tile_for_view(song, whole_song_view(song), 1000, 100);
    CHECK(whole.t0_s == 0.0);
    CHECK(whole.t1_s == doctest::Approx(64.0));
    CHECK(whole.w == 1000);
    // A deep zoom at a wide window would need a huge texture: capped, still around the view.
    const RollView deep{30.0, 34.0};
    const RollTile capped = tile_for_view(song, deep, 3000, 100);
    CHECK(capped.w <= kMaxTileWidth);
    CHECK(capped.t0_s <= deep.start_s);
    CHECK(capped.t1_s >= deep.end_s);
    CHECK_FALSE(tile_stale(capped, deep, 3000));
    CHECK_FALSE(tile_for_view(song, deep, 0, 100).valid());
}

TEST_CASE("tile_uv shows the view's share of the tile") {
    const RollTile tile{10.0, 30.0, 2000, 100};
    float u0 = 0.0f;
    float u1 = 0.0f;
    tile_uv(tile, RollView{15.0, 25.0}, u0, u1);
    CHECK(u0 == doctest::Approx(0.25f));
    CHECK(u1 == doctest::Approx(0.75f));
    tile_uv(RollTile{}, RollView{0.0, 1.0}, u0, u1);
    CHECK(u0 == 0.0f);
    CHECK(u1 == 1.0f);
}

TEST_CASE("the loupe's pitch range hugs the notes in the window") {
    Song song;
    song.tempo = make_tempo_map(480, 120);
    song.time_signatures = {TimeSignature{0, 4, 4}};
    const auto add = [&](std::int64_t start_tick, int pitch) {
        Note n;
        n.start_tick = start_tick;
        n.end_tick = start_tick + 480;
        n.pitch = static_cast<std::uint8_t>(pitch);
        song.notes.push_back(n);
    };
    add(0, 40);
    add(960, 64);
    add(1920, 67);
    add(9600, 90);  // 10 s later
    REQUIRE(finish_song(song));
    const PitchRange near = window_pitch_range(song, 1.5, 3.0);  // the 64 and 67
    CHECK(near.lowest <= 64);
    CHECK(near.highest >= 67);
    CHECK(near.rows() == 14);
    // A wide window takes in the low and the high notes.
    const PitchRange wide = window_pitch_range(song, 0.0, 3.0);
    CHECK(wide.lowest == 40);
    CHECK(wide.highest == 67);
    // A window with no notes falls back to the song's range.
    CHECK(window_pitch_range(song, 5.0, 6.0).lowest == song_pitch_range(song).lowest);
}

TEST_CASE("a tile's pixels per second is its width over its time, and 0 when it is empty") {
    const Song song = song32();
    const RollView view{20.0, 28.0};
    const RollTile tile = tile_for_view(song, view, 800, 200);
    REQUIRE(tile.valid());
    CHECK(tile.px_per_s() == doctest::Approx(2400.0 / 24.0));
    // The tile is as sharp as the view needs: 800 px for 8 s.
    CHECK(tile.px_per_s() == doctest::Approx(800.0 / view.span_s()));
    CHECK(RollTile{}.px_per_s() == 0.0);
    RollTile no_time = tile;
    no_time.t1_s = no_time.t0_s;
    CHECK(no_time.px_per_s() == 0.0);
}

TEST_CASE("grid lines: a repeated bar keeps its played number and carries its written one") {
    Song song = song32();
    // The third bar plays the first bar again.
    song.measures[2].source_number = 1;
    std::vector<GridLine> lines;
    grid_lines(song, 4.0, 5.0, true, lines);
    REQUIRE(!lines.empty());
    CHECK(lines[0].bar_number == 3);
    CHECK(lines[0].written_number == 1);
    CHECK(lines.back().bar_number == 3);
    CHECK(lines.back().written_number == 1);
}

TEST_CASE("the position label adds the sheet's bar number only where it differs") {
    Song song = song32();
    CHECK(position_label(song, 4.0) == "Bar 3 \xC2\xB7 beat 1");
    // Bar 3 plays written bar 1 again; bar 4 is written bar 4 as played.
    song.measures[2].source_number = 1;
    song.measures[3].source_number = 4;
    CHECK(position_label(song, 4.0) == "Bar 3 \xC2\xB7 sheet bar 1 \xC2\xB7 beat 1");
    CHECK(position_label(song, 5.0) == "Bar 3 \xC2\xB7 sheet bar 1 \xC2\xB7 beat 3");
    CHECK(position_label(song, 6.0) == "Bar 4 \xC2\xB7 beat 1");
}
