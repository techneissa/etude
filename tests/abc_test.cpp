// SPDX-License-Identifier: GPL-3.0-or-later
// ABC notation: the header, keys and accidentals, lengths and tuplets, chords and ties, bars and
// the pickup, several tunes in one file, damaged input, and two real tunes against MusicXML twins.
// Repeats and endings are in repeats_test.cpp.
#include "song_library.hpp"

#include "etude/abc.hpp"
#include "etude/content_hash.hpp"
#include "etude/musicxml.hpp"
#include "etude/song_builder.hpp"
#include "etude/song_key.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

using namespace etude;

namespace {

using Bytes = std::vector<std::uint8_t>;

ScoreLoadResult abc(const std::string& text, const AbcOptions& options = {}) {
    return load_abc(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), options);
}

// A one-tune file: the header fields, then the music.
ScoreLoadResult music(const std::string& body, const std::string& header = "M:4/4\nL:1/8\nK:C",
                      const AbcOptions& options = {}) {
    return abc("X:1\nT:test\n" + header + "\n" + body + "\n", options);
}

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing test data " << name);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<int> pitches(const Song& song) {
    std::vector<int> out;
    for (const Note& n : song.notes) {
        out.push_back(n.pitch);
    }
    return out;
}

// Lengths of the notes in quarter notes.
std::vector<double> lengths(const Song& song) {
    std::vector<double> out;
    for (const Note& n : song.notes) {
        out.push_back(static_cast<double>(n.end_tick - n.start_tick) /
                      song.tempo.ticks_per_quarter);
    }
    return out;
}

double start_of(const Song& song, std::size_t note) {
    return static_cast<double>(song.notes[note].start_tick) / song.tempo.ticks_per_quarter;
}

void expect_lengths(const Song& song, const std::vector<double>& want) {
    const std::vector<double> got = lengths(song);
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i);
        CHECK(got[i] == doctest::Approx(want[i]));
    }
}

Bytes read_data_at(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool mentions(const std::vector<std::string>& lines, const std::string& word) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& s) { return s.find(word) != std::string::npos; });
}

} // namespace

TEST_CASE("abc: the header gives title, composer, meter, tempo and the default note length") {
    const ScoreLoadResult r = abc("X:3\nT:First title\nT:Second title\nC:A. Composer\nM:3/4\n"
                                  "L:1/4\nQ:1/4=90\nK:C\nCDE|]\n");
    REQUIRE(r.ok);
    CHECK(r.title == "First title");
    CHECK(r.composer == "A. Composer");
    CHECK(r.song.time_signatures[0].numerator == 3);
    CHECK(r.song.tempo.changes[0].us_per_quarter == 666667); // 90 quarter notes a minute

    // Meter forms, as (numerator, denominator) of the first bar.
    struct Meter {
        const char* text;
        int numerator, denominator;
    };
    for (Meter m : {Meter{"C", 4, 4}, Meter{"C|", 2, 2}, Meter{"6/8", 6, 8},
                    Meter{"(2+3+2)/8", 7, 8}, Meter{"none", 4, 4}}) {
        CAPTURE(m.text);
        const ScoreLoadResult t = music("CDEF|", std::string("M:") + m.text + "\nL:1/4\nK:C");
        REQUIRE(t.ok);
        CHECK(t.song.measures[0].numerator == m.numerator);
        CHECK(t.song.measures[0].denominator == m.denominator);
    }

    // Tempo forms, as microseconds per quarter note.
    struct Tempo {
        const char* text;
        std::uint32_t us;
    };
    for (Tempo t : {Tempo{"1/4=120", 500000}, Tempo{"1/2=60", 500000}, Tempo{"3/8=60", 666667},
                    Tempo{"\"Allegro\" 1/4=240", 250000}, Tempo{"120", 500000}}) {
        CAPTURE(t.text);
        const ScoreLoadResult q =
            music("CDEF|", std::string("M:4/4\nL:1/4\nQ:") + t.text + "\nK:C");
        REQUIRE(q.ok);
        CHECK(q.song.tempo.changes[0].us_per_quarter == t.us);
    }

    // With no L:, the default is a sixteenth under 3/4 and an eighth from 3/4 up.
    CHECK(lengths(music("CDEF|", "M:2/4\nK:C").song)[0] == doctest::Approx(0.25));
    CHECK(lengths(music("CDE|", "M:3/4\nK:C").song)[0] == doctest::Approx(0.5));
    // The file header sets defaults for every tune, and a tune's own fields win.
    CHECK(lengths(abc("L:1/4\nQ:1/4=60\n\nX:1\nT:t\nK:C\nCDEF|]\n").song)[0] ==
          doctest::Approx(1.0));
    CHECK(abc("L:1/4\nQ:1/4=60\n\nX:1\nT:t\nL:1/8\nQ:1/4=120\nK:C\nCDEF|]\n")
              .song.tempo.changes[0]
              .us_per_quarter == 500000);
}

TEST_CASE("abc: note letters, octaves, keys and modes") {
    CHECK(pitches(music("CDEF GABc | c' d' C, D,, |]").song) ==
          std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72, 84, 86, 48, 38});
    auto scale = [](const std::string& key) {
        const ScoreLoadResult r = music("CDEFGABc|", "M:4/4\nL:1/8\nK:" + key);
        REQUIRE_MESSAGE(r.ok, key);
        return pitches(r.song);
    };
    CHECK(scale("C") == std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72});
    CHECK(scale("G") == std::vector<int>{60, 62, 64, 66, 67, 69, 71, 72});  // F#
    CHECK(scale("D") == std::vector<int>{61, 62, 64, 66, 67, 69, 71, 73});  // F# C#
    CHECK(scale("F") == std::vector<int>{60, 62, 64, 65, 67, 69, 70, 72});  // Bb
    CHECK(scale("Cm") == std::vector<int>{60, 62, 63, 65, 67, 68, 70, 72}); // Bb Eb Ab
    CHECK(scale("Am") == scale("C"));
    CHECK(scale("D Dorian") == scale("C"));
    CHECK(scale("Amix") == scale("D"));
    CHECK(scale("Em") == scale("G"));
    CHECK(scale("gm") == scale("Bb"));
    CHECK(scale("D exp _b") == std::vector<int>{60, 62, 64, 65, 67, 69, 70, 72}); // only Bb
    CHECK(scale("none") == scale("C"));
    CHECK(scale("D M") == scale("D")); // a lone M is major (with a warning)
    CHECK(scale("D m") == scale("F")); // a lone m is minor
}

TEST_CASE("abc: an accidental lasts to the end of the bar, for that pitch in that octave") {
    // ^F makes the second F sharp too; the F an octave up and the F in the next bar are not.
    CHECK(pitches(music("^F F f F | F =F F ^^F _B B |]").song) ==
          std::vector<int>{66, 66, 77, 66, 65, 65, 65, 67, 70, 70});
    // An accidental belongs to the note a tie leads to in the next bar: the tied F stays sharp and
    // joins; the next F in that bar is plain again.
    const ScoreLoadResult tied = music("A3 ^F5- | F3 F5 |]");
    REQUIRE(tied.ok);
    CHECK(pitches(tied.song) == std::vector<int>{69, 66, 65});
    expect_lengths(tied.song, {1.5, 4.0, 2.5});
}

TEST_CASE("abc: lengths, rests, broken rhythm and tuplets place the notes") {
    const char* quarters = "M:4/4\nL:1/4\nK:C";
    struct Row {
        const char* music;
        const char* header;
        std::vector<double> lengths; // in quarter notes
    };
    const Row rows[] = {
        {"C2 D/2 E3/2 F/ G// A3 B/4 c|", quarters, {2.0, 0.5, 1.5, 0.5, 0.25, 3.0, 0.25, 1.0}},
        {"A>B c<d e>>f g<<a |]",
         "M:4/4\nL:1/8\nK:C",
         {0.75, 0.25, 0.25, 0.75, 0.875, 0.125, 0.125, 0.875}},
        {"[CE]>G z<A |]", quarters, {1.5, 1.5, 0.5, 1.5}},
        {"(3CDE F2 (3:2:3 GAB c2 |]",
         "M:4/4\nL:1/8\nK:C",
         {1.0 / 3, 1.0 / 3, 1.0 / 3, 1.0, 1.0 / 3, 1.0 / 3, 1.0 / 3, 1.0}},
        {"(3:2:2CD EF |]", "M:4/4\nL:1/8\nK:C", {1.0 / 3, 1.0 / 3, 0.5, 0.5}},
        {"[C2E]G|]", "M:4/4\nL:1/8\nK:C", {1.0, 0.5, 0.5}},
    };
    for (const Row& row : rows) {
        CAPTURE(row.music);
        const ScoreLoadResult r = music(row.music, row.header);
        REQUIRE(r.ok);
        expect_lengths(r.song, row.lengths);
    }
    // Rests move time and make no note; Z is a whole bar of rest.
    const ScoreLoadResult rests = music("C z D2 z2 E x F/ z/ G |]");
    REQUIRE(rests.ok);
    REQUIRE(rests.song.notes.size() == 5);
    CHECK(start_of(rests.song, 2) == doctest::Approx(3.0));
    CHECK(start_of(rests.song, 4) == doctest::Approx(4.5));
    const ScoreLoadResult whole = music("C4 z4 | Z | Z2 | G8 |]");
    REQUIRE(whole.ok);
    CHECK(start_of(whole.song, 1) == doctest::Approx(16.0));
    // A tuplet inside a bar keeps the bar full.
    const ScoreLoadResult tuplets = music("(3CDE (3FGA (3Bcd (3efg | C8 |]");
    REQUIRE(tuplets.ok);
    CHECK(tuplets.song.measures[1].start_tick == 4 * tuplets.song.tempo.ticks_per_quarter);
}

TEST_CASE("abc: chords play together and ties make one note") {
    const ScoreLoadResult chords = music("[CEG]2 [C2E2G2]3/2 [C-E-G-]2 [CEG] |]");
    REQUIRE(chords.ok);
    CHECK(pitches(chords.song) == std::vector<int>{60, 64, 67, 60, 64, 67, 60, 64, 67});
    CHECK(start_of(chords.song, 3) == doctest::Approx(1.0));
    CHECK(lengths(chords.song)[3] == doctest::Approx(1.5));

    const ScoreLoadResult ties = music("C2-C2 D2-D2 | E8- | E8 | [CG]8- | [CG]8 |]");
    REQUIRE(ties.ok);
    CHECK(pitches(ties.song) == std::vector<int>{60, 62, 64, 60, 67});
    expect_lengths(ties.song, {2.0, 2.0, 8.0, 8.0, 8.0});
    // A tie written after a space or before a bar line still ties the note before it.
    const ScoreLoadResult spaced = music("C4 -C4 | D8 -| D8 | E4 - z4 | F8 |]");
    REQUIRE(spaced.ok);
    expect_lengths(spaced.song, {4.0, 8.0, 2.0, 4.0});
    // A tie to a different pitch joins nothing.
    const ScoreLoadResult other = music("C2-D2 E-z F2 |]");
    REQUIRE(other.ok);
    CHECK(other.stats.ties_merged == 0);
    CHECK(other.song.notes.size() == 4);
}

TEST_CASE("abc: transpose= and octave= on a voice shift its notes") {
    // The same C in four voices: plain, an octave down, an octave up, two semitones up.
    const ScoreLoadResult r =
        abc("X:1\nT:t\nM:4/4\nL:1/4\nK:C\nV:1\nV:2 octave=-1\nV:3 octave=1\nV:4 transpose=2\n"
            "V:1\nC|]\nV:2\nC|]\nV:3\nC|]\nV:4\nC|]\n");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{48, 60, 62, 72});
}

TEST_CASE("abc: the same pitch twice at once is one note, a later overlap cuts the first") {
    // C4 with a rest, overlaid with C8: one note. An overlay C that starts at the third eighth
    // cuts the first where the second starts.
    const ScoreLoadResult same = music("C4 z4 & C8 | D8 |]");
    REQUIRE(same.ok);
    CHECK(pitches(same.song) == std::vector<int>{60, 62});
    CHECK(lengths(same.song)[0] == doctest::Approx(4.0));
    CHECK(same.stats.duplicates_merged == 1);
    CHECK(same.stats.overlaps_split == 0);
    const ScoreLoadResult cut = music("C8 & z2 C6 |]");
    REQUIRE(cut.ok);
    REQUIRE(cut.song.notes.size() == 2);
    CHECK(lengths(cut.song)[0] == doctest::Approx(1.0));
    CHECK(cut.stats.overlaps_split == 1);
}

TEST_CASE("abc: chord symbols are a left hand only when asked, with root and fifth") {
    AbcOptions asked;
    asked.chords_as_left_hand = true;
    auto left_of = [&](const std::string& music_text, const AbcOptions& options) {
        const ScoreLoadResult r = music(music_text, "M:4/4\nL:1/8\nK:C", options);
        REQUIRE(r.ok);
        std::vector<int> out;
        for (const Note& n : r.song.notes) {
            if (n.hand == Hand::Left) {
                out.push_back(n.pitch);
            }
        }
        return out;
    };
    // Off: one part, no left hand. On: a "Chords" part in the left hand, a root and a fifth from
    // the octave of C2 for each symbol.
    const std::string tune = "\"C\"C8 | \"Am\"A8 | \"G7\"G4 \"C\"C4 |]";
    CHECK(music(tune).song.parts.size() == 1);
    CHECK(left_of(tune, {}).empty());
    CHECK(left_of(tune, asked) == std::vector<int>{36, 43, 45, 52, 43, 50, 36, 43});
    const ScoreLoadResult on = music(tune, "M:4/4\nL:1/8\nK:C", asked);
    REQUIRE(on.song.parts.size() == 2);
    CHECK(on.song.parts[1].name == "Chords");
    CHECK(left_of("C8 | D8 |]", asked).empty()); // no symbols, no left hand even when asked

    // Qualities, slash chords, and words that are not chords: the symbol over one C.
    struct Row {
        const char* symbol;
        std::vector<int> left;
    };
    const Row rows[] = {{"F#m7", {42, 49}}, {"Bb", {46, 53}},    {"Gb", {42, 49}},
                        {"Bdim", {47, 53}}, {"Bm7b5", {47, 53}}, {"C+", {36, 44}},
                        {"Caug", {36, 44}}, {"Dsus4", {38, 45}}, {"Cmaj7", {36, 43}},
                        {"C/E", {40}},      {"Dorian", {}},      {"Chorus", {}},
                        {"^above", {}},     {"N.C.", {}}};
    for (const Row& row : rows) {
        CAPTURE(row.symbol);
        CHECK(left_of(std::string("\"") + row.symbol + "\"C8 |]", asked) == row.left);
    }
}

TEST_CASE("abc: grace notes, decorations, annotations, lyrics and other fields make no notes") {
    const ScoreLoadResult r = music("{g}A {gab}B {/c}C (D E) .F ~G !trill!A +fermata+B H c T d u e "
                                    "\"Am\"A \"^up\"B y z\nw: la la\n%% a directive\n% a comment\n"
                                    "A:somebody\nN:a note\nR:reel\nU:T=!trill!\n+: more\nc d |]");
    REQUIRE(r.ok);
    CHECK(r.stats.grace_notes_dropped == 5);
    CHECK(pitches(r.song) ==
          std::vector<int>{69, 71, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 69, 71, 72, 74});
    CHECK(r.warnings.empty());
    // A grace note takes no time.
    CHECK(start_of(r.song, 1) == doctest::Approx(0.5));
}

TEST_CASE("abc: bars, the pickup, inline fields and text forms") {
    // A one-eighth first bar is a pickup, bar 0; a first bar that fills the meter is bar 1.
    const ScoreLoadResult pickup = music("G | c4 B2 A2 | G6 z2 |]");
    REQUIRE(pickup.ok);
    REQUIRE(pickup.song.measures.size() == 3);
    CHECK(pickup.song.measures[0].number == 0);
    CHECK(pickup.song.notes[1].measure == 1);
    CHECK(pickup.song.measures[1].start_tick == pickup.song.tempo.ticks_per_quarter / 2);
    CHECK(music("C8 | D4 |]").song.measures[0].number == 1);

    // Inline fields change the key, meter, length and tempo from there.
    const ScoreLoadResult inline_fields =
        music("C2 D2 [K:G] F2 G2 | [M:3/4] [L:1/4] [Q:1/4=60] F G A | c3 |]");
    REQUIRE(inline_fields.ok);
    CHECK(pitches(inline_fields.song) == std::vector<int>{60, 62, 66, 67, 66, 67, 69, 72});
    CHECK(inline_fields.song.time_signatures[1].numerator == 3);
    CHECK(inline_fields.song.tempo.changes[1].us_per_quarter == 1000000);
    CHECK(lengths(inline_fields.song)[4] == doctest::Approx(1.0));

    // Line ends, a byte order mark, comments and continuation lines do not change the music.
    const std::string unix_text = "X:1\nT:t\nM:4/4\nL:1/8\nK:C\nCDEF GABc |]\n";
    const std::vector<int> want = pitches(abc(unix_text).song);
    std::string dos;
    for (char c : unix_text) {
        dos += c == '\n' ? std::string("\r\n") : std::string(1, c);
    }
    CHECK(pitches(abc(dos).song) == want);
    CHECK(pitches(abc("\xEF\xBB\xBF" + unix_text).song) == want);
    CHECK(pitches(abc("X:1 % the number\nT:t\nK:C\nCDEF % to here\\\nGABc |]\n").song) == want);
}

TEST_CASE("abc: several tunes in one file have addresses, and a tune's identity is its own text") {
    const Bytes data = read_data("abc-collection.abc");
    const std::vector<AbcTune> tunes = list_abc_tunes(data);
    REQUIRE(tunes.size() == 3);
    CHECK(tunes[0].address == "X=1");
    CHECK(tunes[2].address == "X=10");
    CHECK(tunes[2].title == "Arpeggio");
    CHECK(tunes[0].end == tunes[1].begin);
    CHECK(tunes[2].end == data.size());

    AbcOptions options;
    options.tune = "X=2";
    const ScoreLoadResult two = load_abc(data, options);
    REQUIRE(two.ok);
    CHECK(two.title == "Scale Down");
    CHECK(pitches(two.song) == std::vector<int>{72, 71, 69, 67, 65, 64, 62, 60});
    expect_lengths(two.song, {1, 1, 1, 1, 1, 1, 1, 1}); // the file header says L:1/4
    CHECK(load_abc(data).title == "Scale Up");          // no address: the first tune
    options.tune = "X=3";
    CHECK(load_abc(data, options).error == ScoreFileError::NotScore); // never another tune
    AbcTune stale = tunes[2];
    stale.end = data.size() + 100;
    AbcOptions listed;
    listed.listed = &stale;
    CHECK_FALSE(load_abc(data, listed).ok); // a listing that does not fit the bytes is refused

    // A repeated tune number gets a second address.
    const std::string repeated = "X:1\nT:a\nK:C\nC|]\n\nX:1\nT:b\nK:C\nD|]\n\nX:2\nT:c\nK:C\nE|]\n";
    const std::vector<AbcTune> r = list_abc_tunes(Bytes(repeated.begin(), repeated.end()));
    REQUIRE(r.size() == 3);
    CHECK(r[1].address == "X=1.2");

    // Identity: the hash of the file header and the tune's own text. Another tune added to the
    // file, or edited, leaves it alone; the tune's own text changing makes another song.
    const std::string text(data.begin(), data.end());
    std::string edited = text;
    edited.replace(edited.find("CDEF|GABc"), 9, "CDEF|GABB");
    edited += "\nX:11\nT:New\nK:C\nC|]\n";
    const Bytes edited_bytes(edited.begin(), edited.end());
    app::SongLoadOptions load_options;
    load_options.tune = "X=2";
    const app::SongFileLoad a = app::load_song_bytes(data, "tunes.abc", load_options);
    const app::SongFileLoad b = app::load_song_bytes(edited_bytes, "tunes.abc", load_options);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(a.song.hash == b.song.hash);
    CHECK(song_key(a.song.hash, "tunes.abc#X=2") == song_key(b.song.hash, "elsewhere.abc#X=2"));
    load_options.tune = "X=1";
    CHECK(app::load_song_bytes(data, "tunes.abc", load_options).song.hash !=
          app::load_song_bytes(edited_bytes, "tunes.abc", load_options).song.hash);
}

TEST_CASE("abc: a file swapped in place after the library listed it is read again, not trusted") {
    namespace fs = std::filesystem;
    const fs::path dir =
        fs::temp_directory_path() / ("etude_abc_swap_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / "pair.abc";
    const std::string one = "X:1\nT:Aaa\nM:4/4\nL:1/4\nK:C\nCDEF|]\n\n";
    const std::string two = "X:2\nT:Bbbb\nM:4/4\nL:1/4\nK:C\nGABcdefg|]\n";
    auto write = [&](const std::string& text) { std::ofstream(file, std::ios::binary) << text; };
    write(one + two);
    const std::vector<AbcTune> tunes = list_abc_tunes(read_data_at(file));
    REQUIRE(tunes.size() == 2);
    // What the library keeps of X=2 when it lists the file: the tune, the file's size and time.
    const auto time = fs::last_write_time(file);
    app::AbcKnown known;
    known.tune = tunes[1];
    known.tune_count = 2;
    known.file_size = fs::file_size(file);
    known.file_mtime = static_cast<std::int64_t>(time.time_since_epoch().count());
    app::SongLoadOptions options;
    options.tune = "X=2";
    options.known = known;
    const app::SongFileLoad before = app::load_song_file(file, options);
    REQUIRE(before.ok);
    CHECK(before.song.title == "Bbbb");
    CHECK(before.song.hash == tunes[1].hash);

    // The tunes swap bodies and names; the file keeps its size and time. X=2 now holds Aaa's music
    // at the same place: it plays as what it is, under the hash of its own text.
    const std::string swapped = "X:1\nT:Bbbb\nM:4/4\nL:1/4\nK:C\nGABcdefg|]\n\n"
                                "X:2\nT:Aaa\nM:4/4\nL:1/4\nK:C\nCDEF|]\n";
    REQUIRE(swapped.size() == (one + two).size());
    write(swapped);
    fs::last_write_time(file, time);
    const app::SongFileLoad after = app::load_song_file(file, options);
    REQUIRE(after.ok);
    CHECK(after.song.title == "Aaa");
    CHECK(after.song.hash != tunes[1].hash);
    CHECK(after.song.hash == list_abc_tunes(Bytes(swapped.begin(), swapped.end()))[1].hash);
    fs::remove_all(dir);
}

TEST_CASE("abc: damaged and odd input loads what it can, or says why, and never hangs") {
    const ScoreLoadResult junk = music("C D # E ? F @ G ] A [ B c");
    REQUIRE(junk.ok);
    CHECK(pitches(junk.song) == std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72});
    CHECK(mentions(junk.warnings, "could not read"));
    CHECK(music("C D [EG F \"unclosed A {gab B !oops C |]").ok);

    const ScoreLoadResult empty = music("z8 | z8 |]");
    CHECK(empty.error == ScoreFileError::NoNotes);
    CHECK(abc("X:1\nT:t\nK:C\n").error == ScoreFileError::NoNotes);
    CHECK(abc("T:no number\nK:C\nCDEF|]\n").error == ScoreFileError::NotScore);
    CHECK_FALSE(abc("").ok);

    // A note outside MIDI 0..127 is left out with a count and a warning, never folded: the lowest
    // and highest note are kept, one semitone past either is dropped.
    struct Range {
        const char* music;
        int pitch; // -1: left out
    };
    for (Range r :
         {Range{"C,,,,,", 0}, Range{"_C,,,,,", -1}, Range{"g''''", 127}, Range{"^g''''", -1}}) {
        CAPTURE(r.music);
        const ScoreLoadResult note = music(std::string(r.music) + " C |]");
        REQUIRE(note.ok);
        CHECK(pitches(note.song) ==
              (r.pitch < 0 ? std::vector<int>{60} : std::vector<int>{r.pitch, 60}));
        CHECK(note.stats.unreadable_notes == (r.pitch < 0 ? 1 : 0));
        CHECK(mentions(note.warnings, "range") == (r.pitch < 0));
    }

    // Absurd numbers are clamped; a very long tune stops at the note cap with a warning.
    CHECK(music("C99999999999999999999 D/99999999999999 E999999/1 Z999999999 F |]").ok);
    CHECK(music("(99999999:99999999:99999999 CDEF |]").ok);
    CHECK(music("CDEF|]", "M:99999999999/4\nL:99999999999/1\nK:C").ok);
    std::string big = "X:1\nT:big\nM:4/4\nL:1/8\nK:C\n";
    for (std::size_t i = 0; i < kMaxScoreNotes / 8 + 2000; ++i) {
        big += "CDEF GABc |";
        big += i % 6 == 5 ? "\n" : "";
    }
    const ScoreLoadResult capped = abc(big + "]\n");
    REQUIRE(capped.ok);
    CHECK(capped.song.notes.size() <= kMaxScoreNotes);
    CHECK(mentions(capped.warnings, "more notes"));

    // A very long line, a huge number of bars and of voices stay linear.
    CHECK(music(std::string(100000, 'C')).ok);
    std::string bars;
    for (int i = 0; i < 120000; ++i) {
        bars += "C8|";
    }
    const ScoreLoadResult many = music(bars);
    REQUIRE(many.ok);
    CHECK(many.song.measures.size() <= kMaxSongMeasures);
    std::string voices = "X:1\nT:t\nK:C\n";
    for (int i = 0; i < 200; ++i) {
        voices += "V:" + std::to_string(i) + "\nC4|]\n";
    }
    CHECK(abc(voices).song.parts.size() <= 64);

    // A cut tune and a tune with garbage bytes in it load or say why.
    const Bytes reel = read_data("abc-reel-repeats.abc");
    std::string cut(reel.begin(), reel.begin() + static_cast<std::ptrdiff_t>(reel.size() / 2));
    std::string noisy(reel.begin(), reel.end());
    for (std::size_t i = 20; i < noisy.size(); i += 11) {
        noisy[i] = static_cast<char>(0x80 + i % 100);
    }
    for (const std::string& text : {cut, noisy}) {
        const ScoreLoadResult r = abc(text);
        for (const Note& n : r.song.notes) {
            CHECK(n.end_tick > n.start_tick);
        }
    }
}

TEST_CASE("abc: the real tunes play like their MusicXML twins") {
    // Typed out again as MusicXML from the music itself, so both loaders must hear the same notes,
    // bars, bar numbers in playing order and tempo: a reel with repeats and endings, and a 6/8
    // tune in D dorian with a pickup, an accidental carried through its bar and a tie across the
    // last barline.
    for (const char* name : {"abc-reel-repeats", "abc-dorian"}) {
        CAPTURE(name);
        const Bytes text = read_data(std::string(name) + ".abc");
        const Bytes xml = read_data(std::string(name) + ".musicxml");
        const ScoreLoadResult a = load_abc(text);
        const ScoreLoadResult x = load_musicxml(xml);
        REQUIRE(a.ok);
        REQUIRE(x.ok);
        REQUIRE(a.song.notes.size() == x.song.notes.size());
        for (std::size_t i = 0; i < a.song.notes.size(); ++i) {
            const Note& p = a.song.notes[i];
            const Note& q = x.song.notes[i];
            CAPTURE(i);
            CHECK(p.pitch == q.pitch);
            CHECK(p.start_s == doctest::Approx(q.start_s).epsilon(1e-9));
            CHECK(p.duration_s == doctest::Approx(q.duration_s).epsilon(1e-9));
            CHECK(p.measure == q.measure);
            CHECK(p.written_measure() == q.written_measure());
        }
        REQUIRE(a.song.measures.size() == x.song.measures.size());
        for (std::size_t i = 0; i < a.song.measures.size(); ++i) {
            CHECK(a.song.measures[i].number == x.song.measures[i].number);
            CHECK(a.song.measures[i].start_s ==
                  doctest::Approx(x.song.measures[i].start_s).epsilon(1e-9));
        }
        CHECK(a.song.tempo.changes[0].us_per_quarter == x.song.tempo.changes[0].us_per_quarter);
        CHECK(a.stats.bars_played == x.stats.bars_played);
    }
    const ScoreLoadResult dorian = load_abc(read_data("abc-dorian.abc"));
    CHECK(dorian.song.measures[0].number == 0); // the pickup
    CHECK(dorian.stats.ties_merged == 1);
}
