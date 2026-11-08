// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_source.hpp"

#include "etude/abc.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

using namespace etude;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Bytes bytes_of(const std::string& text) { return Bytes(text.begin(), text.end()); }

// Each practised note as the written score places it: written bar, offset in quarters (to 1/960)
// and pitch, in the order they play.
std::vector<std::tuple<int, long, int>> written_notes(const Song& song) {
    std::vector<std::tuple<int, long, int>> out;
    const int first = song.measures.front().number;
    const double tpq = song.tempo.ticks_per_quarter;
    for (const Note& n : song.notes) {
        if (n.drum || n.accompaniment) {
            continue;
        }
        const auto k = static_cast<std::size_t>(n.measure - first);
        const double offset = static_cast<double>(n.start_tick - song.measures[k].start_tick) / tpq;
        out.emplace_back(n.written_measure(), std::lround(offset * 960.0), n.pitch);
    }
    std::sort(out.begin(), out.end());
    return out;
}

int count(const std::string& text, const std::string& what) {
    int n = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("sheet source: an ABC tune is written as a score that reads back to the same notes") {
    for (const char* name : {"abc-dorian.abc", "abc-reel-repeats.abc", "abc-triplets.abc",
                             "abc-two-voices.abc", "abc-collection.abc"}) {
        CAPTURE(name);
        const Bytes bytes = read_data(name);
        const ScoreLoadResult tune = load_abc(bytes);
        REQUIRE(tune.ok);
        const SheetSource source = sheet_source_for_score(bytes);
        REQUIRE(source.status == SheetSourceStatus::Ok);
        // The written score keeps its repeats as signs; read back, they expand the same way.
        const ScoreLoadResult back = load_musicxml(bytes_of(source.musicxml));
        REQUIRE(back.ok);
        CHECK(written_notes(back.song) == written_notes(tune.song));
        CHECK(back.song.measures.size() == tune.song.measures.size());
    }
}

TEST_CASE("sheet source: chord symbols as a left hand are written as the song has them") {
    const Bytes bytes = read_data("abc-dorian.abc");
    AbcOptions options;
    options.chords_as_left_hand = true;
    const ScoreLoadResult tune = load_abc(bytes, options);
    REQUIRE(tune.ok);
    const SheetSource source = sheet_source_for_score(bytes, options);
    REQUIRE(source.status == SheetSourceStatus::Ok);
    const ScoreLoadResult back = load_musicxml(bytes_of(source.musicxml));
    REQUIRE(back.ok);
    CHECK(written_notes(back.song) == written_notes(tune.song));
}

TEST_CASE("sheet source: a MuseScore file is written from what the loader read") {
    const Bytes bytes = read_data("repeats-voltas-musescore4.mscz");
    const ScoreLoadResult song = load_mscz(bytes);
    REQUIRE(song.ok);
    const SheetSource source = sheet_source_for_score(bytes);
    REQUIRE(source.status == SheetSourceStatus::Ok);
    const ScoreLoadResult back = load_musicxml(bytes_of(source.musicxml));
    REQUIRE(back.ok);
    CHECK(written_notes(back.song) == written_notes(song.song));
    // The written score keeps the endings and the repeat, never the expanded bars.
    CHECK(count(source.musicxml, "<ending ") >= 2);
    CHECK(count(source.musicxml, "<repeat direction=\"backward\"") >= 1);
    CHECK(back.stats.bars_written == song.stats.bars_written);
}

TEST_CASE("sheet source: MusicXML keeps its own text with the practised parts only") {
    const std::string xml =
        "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list>"
        "<score-part id=\"P1\"><part-name>Soprano</part-name><score-instrument id=\"I1\">"
        "<instrument-name>Voice</instrument-name></score-instrument><midi-instrument id=\"I1\">"
        "<midi-program>53</midi-program></midi-instrument></score-part>"
        "<score-part id=\"P2\"><part-name>Piano</part-name><part-abbreviation>Pno.</part-abbreviation>"
        "</score-part>"
        "<score-part id=\"P3\"><part-name>Drums</part-name></score-part></part-list>"
        "<part id=\"P1\"><measure number=\"1\"><attributes><divisions>1</divisions></attributes>"
        "<note><pitch><step>E</step><octave>5</octave></pitch><duration>4</duration><type>whole</type>"
        "</note></measure></part>"
        "<part id=\"P2\"><measure number=\"1\"><attributes><divisions>1</divisions></attributes>"
        "<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><type>whole</type>"
        "</note></measure></part>"
        "<part id=\"P3\"><measure number=\"1\"><note><unpitched><display-step>C</display-step>"
        "<display-octave>5</display-octave></unpitched><duration>4</duration></note></measure></part>"
        "</score-partwise>";
    const SheetSource source = sheet_source_for_score(bytes_of(xml));
    REQUIRE(source.status == SheetSourceStatus::Ok);
    CHECK(source.musicxml.find("<part id=\"P1\"") == std::string::npos); // the voice beside a piano
    CHECK(source.musicxml.find("<part id=\"P3\"") == std::string::npos); // no pitched note
    CHECK(source.musicxml.find("<part id=\"P2\"") != std::string::npos);
    CHECK(source.musicxml.find("print-object=\"no\"") != std::string::npos);
    CHECK(source.musicxml.find("Pno.") == std::string::npos);
}

TEST_CASE("sheet source: accidentals follow the key and last to the end of the bar") {
    const std::string abc = "X:1\nT:t\nM:4/4\nL:1/4\nK:G\n^c c =F F | c F =F2 |]\n";
    const SheetSource source = sheet_source_for_score(bytes_of(abc));
    REQUIRE(source.status == SheetSourceStatus::Ok);
    CHECK(source.musicxml.find("<fifths>1</fifths>") != std::string::npos);
    // Bar 1: C sharp (written), C sharp again (the bar's), F natural (written), F natural again.
    // Bar 2: C (the sharp ended with the bar), F sharp (the key's), F natural (written).
    CHECK(count(source.musicxml, "<accidental>sharp</accidental>") == 1);
    CHECK(count(source.musicxml, "<accidental>natural</accidental>") == 2);
    CHECK(count(source.musicxml, "<accidental>") == 3);
}

TEST_CASE("sheet source: a MIDI song on the beat grid is written; off it, the reason says why") {
    const Bytes bytes = read_data("satie-gymnopedie-1.mid");
    MidiLoadResult midi = load_midi(bytes);
    REQUIRE(midi.ok);
    const MidiGrid grid = midi_grid(midi.song);
    CHECK(grid.clean);
    CHECK(grid.on_grid == grid.notes);
    const SheetSource source = sheet_source_for_midi(midi.song);
    REQUIRE(source.status == SheetSourceStatus::Ok);
    const ScoreLoadResult back = load_musicxml(bytes_of(source.musicxml));
    REQUIRE(back.ok);
    CHECK(written_notes(back.song) == written_notes(midi.song));

    // One note a tick off the grid is still on it; two ticks off is not.
    Song song = midi.song;
    std::size_t first = 0;
    while (song.notes[first].drum || song.notes[first].accompaniment) {
        ++first;
    }
    song.notes[first].start_tick += 1;
    CHECK(midi_grid(song).clean);
    song.notes[first].start_tick += 1;
    const MidiGrid off = midi_grid(song);
    CHECK_FALSE(off.clean);
    CHECK(off.on_grid == off.notes - 1);
    const SheetSource refused = sheet_source_for_midi(song);
    CHECK(refused.status == SheetSourceStatus::MidiOffGrid);
    CHECK(refused.musicxml.empty());
    CHECK(refused.reason.find("1 of its") != std::string::npos);
}

TEST_CASE("sheet source: a MIDI note a tick off the grid is written on it, legato to the next") {
    // 480 ticks a quarter, one bar of 4/4: C ends just short of D, which starts one tick late.
    Song song;
    song.tempo.ticks_per_quarter = 480;
    Measure bar;
    bar.number = 1;
    song.measures.push_back(bar);
    song.end_tick = 1920;
    const auto add = [&](int pitch, std::int64_t start, std::int64_t end) {
        Note n;
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.start_tick = start;
        n.end_tick = end;
        n.measure = 1;
        song.notes.push_back(n);
    };
    add(60, 0, 238);
    add(62, 241, 478);
    add(64, 480, 1920);
    REQUIRE(midi_grid(song).clean);
    const SheetSource source = sheet_source_for_midi(song);
    REQUIRE(source.status == SheetSourceStatus::Ok);
    const ScoreLoadResult back = load_musicxml(bytes_of(source.musicxml));
    REQUIRE(back.ok);
    REQUIRE(back.song.notes.size() == 3);
    const double tpq = back.song.tempo.ticks_per_quarter;
    const auto quarters = [&](std::int64_t t) { return static_cast<double>(t) / tpq; };
    // C is an eighth reaching D, D starts on the half beat: no one-tick piece between them.
    CHECK(quarters(back.song.notes[0].end_tick) == doctest::Approx(0.5));
    CHECK(quarters(back.song.notes[1].start_tick) == doctest::Approx(0.5));
    CHECK(quarters(back.song.notes[1].end_tick) == doctest::Approx(1.0));
    CHECK(quarters(back.song.notes[2].start_tick) == doctest::Approx(1.0));
}

TEST_CASE("sheet source: a file that is not a score gives a reason") {
    const SheetSource source = sheet_source_for_score(bytes_of("not a score"));
    CHECK(source.status == SheetSourceStatus::Unreadable);
    CHECK_FALSE(source.reason.empty());
}
