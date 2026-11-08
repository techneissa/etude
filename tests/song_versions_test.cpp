// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_versions.hpp"

#include "smf_builder.hpp"

#include "etude/library.hpp"
#include "etude/midi_file.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;
using etude::test::Bytes;
using etude::test::Track;

namespace {

Bytes read_data(const char* name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in.good());
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The library's entry for a test file: its title, rating and melody, as the loader makes them.
SongEntry entry_of(const char* name) {
    const Bytes bytes = read_data(name);
    LibraryLoad load = load_library_entry(bytes, name);
    REQUIRE(load.ok);
    return load.entry;
}

VersionSong grouping_song(const SongEntry& e, std::string composer, std::string file_name = {}) {
    VersionSong s;
    s.title = e.title;
    s.composer = std::move(composer);
    s.file_name = std::move(file_name);
    s.half_stars = e.half_stars;
    s.melody = e.melody;
    return s;
}

// A song from a melody given as (pitch, quarter notes), one note after the other in 4/4 at 480
// ticks to the quarter.
Song song_of(const std::vector<std::pair<int, double>>& melody) {
    Track t;
    t.name(0, "Melody");
    for (const auto& [pitch, beats] : melody) {
        const auto ticks = static_cast<std::uint32_t>(beats * 480);
        t.on(0, 0, pitch).off(ticks, 0, pitch);
    }
    t.end();
    MidiLoadResult r = load_midi(etude::test::smf({t}));
    REQUIRE(r.ok);
    return std::move(r.song);
}

const std::vector<std::pair<int, double>> kTune = {{67, 1}, {72, 1}, {76, 1}, {74, 2},
                                                   {71, 1}, {72, 1}, {69, 1}, {77, 1},
                                                   {76, 3}, {69, 1}, {74, 1}, {77, 1}};

} // namespace

TEST_CASE("the contour is the intervals of the top line, whatever the key") {
    const MelodyContour c = melody_contour(song_of(kTune));
    REQUIRE(c.notes() == kTune.size());
    CHECK(c.steps[0] == 5);
    CHECK(c.steps[1] == 4);
    CHECK(c.steps[2] == -2);
    std::vector<std::pair<int, double>> up = kTune;
    for (auto& n : up) {
        n.first += 5;
    }
    CHECK(melody_contour(song_of(up)) == c);
}

TEST_CASE("a repeated note is one note, so a simpler rhythm changes nothing") {
    const MelodyContour plain = melody_contour(song_of(kTune));
    std::vector<std::pair<int, double>> split;
    for (const auto& n : kTune) {
        split.push_back({n.first, n.second / 2});
        split.push_back({n.first, n.second / 2});
    }
    CHECK(melody_contour(song_of(split)) == plain);
}

TEST_CASE("only the first bars are read, and a song with no tune has no contour") {
    std::vector<std::pair<int, double>> long_tune;
    for (int bar = 0; bar < 20; ++bar) {
        for (const int p : {60, 64, 67, 72}) {
            long_tune.push_back({p + (bar % 5), 1.0});
        }
    }
    const MelodyContour c = melody_contour(song_of(long_tune));
    CHECK(c.notes() == 8 * 4);
    CHECK(melody_contour(Song{}).steps.empty());
    CHECK(melody_contour(song_of({{60, 1}})).steps.empty());
}

TEST_CASE("a lower note under a longer one still held is an inner voice, and is left out") {
    // Right hand: a high C held for a bar and a half; the left hand plays under it on every beat.
    Track hands;
    hands.name(0, "Piano");
    // Tick 0: C6 (84) held 3 quarters, beat 2 an E3 (52) under it, then D6 (86) on beat 4.
    hands.on(0, 0, 84).on(480, 0, 52).off(480, 0, 52).off(480, 0, 84).on(0, 0, 86).off(480, 0, 86);
    hands.on(0, 0, 88).off(480, 0, 88).end();
    MidiLoadResult r = load_midi(etude::test::smf({hands}));
    REQUIRE(r.ok);
    const MelodyContour c = melody_contour(r.song);
    REQUIRE(c.notes() == 3);
    CHECK(c.steps[0] == 2); // 84 to 86: the E3 was not the top line
    CHECK(c.steps[1] == 2);
}

TEST_CASE("a contour of too few notes or pitches says too little to compare") {
    MelodyContour few;
    few.steps = {1, 2, 3};
    CHECK_FALSE(few.usable());
    MelodyContour trill;
    trill.steps = {2, -2, 2, -2, 2, -2, 2, -2, 2, -2};
    CHECK_FALSE(trill.usable());
    CHECK(melody_similarity(trill, trill) == 0.0);
    CHECK(melody_contour(song_of(kTune)).usable());
}

TEST_CASE("the same piece in two keys has the same melody") {
    const SongEntry c = entry_of("lantern-waltz.mid");
    const SongEntry d = entry_of("lantern-waltz-in-d.mid");
    REQUIRE(c.melody.usable());
    CHECK(c.melody == d.melody);
    CHECK(melody_similarity(c.melody, d.melody) == doctest::Approx(1.0));
}

TEST_CASE("an easy arrangement is like the original in spite of its ornaments and rhythm") {
    const SongEntry original = entry_of("lantern-waltz.mid");
    const SongEntry easy = entry_of("lantern-waltz-easy.mid");
    REQUIRE(easy.melody.usable());
    CHECK(easy.melody.notes() < original.melody.notes());
    CHECK(melody_similarity(easy.melody, original.melody) >= kVersionMelody);
    CHECK(melody_similarity(easy.melody, original.melody) ==
          doctest::Approx(melody_similarity(original.melody, easy.melody)));
}

TEST_CASE("two different tunes are not alike, whether near in style or not") {
    const SongEntry waltz = entry_of("lantern-waltz.mid");
    const SongEntry march = entry_of("harbour-march.mid");
    const SongEntry bach = entry_of("bach-bwv846-prelude.mid");
    const SongEntry satie = entry_of("satie-gymnopedie-1.mid");
    CHECK(melody_similarity(waltz.melody, march.melody) < 0.4);
    CHECK(melody_similarity(waltz.melody, bach.melody) < 0.4);
    CHECK(melody_similarity(march.melody, satie.melody) < 0.4);
    CHECK(melody_similarity(bach.melody, satie.melody) < 0.4);
}

TEST_CASE("a melody that matches only a stretch of a longer one is not the same piece") {
    // Thirty notes of no pattern, and twelve of them from the middle.
    MelodyContour whole;
    std::uint32_t x = 12345;
    for (int i = 0; i < 29; ++i) {
        x = x * 1664525u + 1013904223u;
        int step = static_cast<int>((x >> 16) % 9) - 4;
        whole.steps.push_back(static_cast<std::int8_t>(step == 0 ? 2 : step));
    }
    MelodyContour middle;
    middle.steps.assign(whole.steps.begin() + 9, whole.steps.begin() + 20); // 12 notes
    REQUIRE(whole.usable());
    REQUIRE(middle.usable());
    CHECK(melody_similarity(whole, whole) == doctest::Approx(1.0));
    CHECK(melody_similarity(middle, whole) < kVersionConfirm);
    // The same stretch with the rest of the melody around it joined on by a few notes is alike.
    MelodyContour head;
    head.steps.assign(whole.steps.begin(), whole.steps.begin() + 24); // 25 notes, the start of it
    CHECK(melody_similarity(head, whole) >= kVersionConfirm);
}

namespace {

// A small deterministic generator, so the property tests are the same every run.
struct Lcg {
    std::uint32_t x;
    std::uint32_t next() {
        x = x * 1664525u + 1013904223u;
        return x >> 8;
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
    bool chance(int percent) { return below(100) < percent; }
};

std::vector<int> pitches_of_tune(const MelodyContour& c) {
    std::vector<int> p{0};
    for (const std::int8_t s : c.steps) {
        p.push_back(p.back() + s);
    }
    return p;
}

MelodyContour contour_of_pitches(const std::vector<int>& pitches) {
    MelodyContour c;
    std::vector<int> line;
    for (const int p : pitches) {
        if (line.empty() || line.back() != p) {
            line.push_back(p);
        }
    }
    for (std::size_t i = 1; i < line.size(); ++i) {
        c.steps.push_back(static_cast<std::int8_t>(line[i] - line[i - 1]));
    }
    return c;
}

// A made-up melody that wanders about a middle: mostly steps, some leaps.
MelodyContour wander(Lcg& rng, int notes) {
    static constexpr int kSteps[] = {1, 1, 2, 2, 2, 3, 4, 5, 7, 12};
    std::vector<int> p{0};
    for (int i = 1; i < notes; ++i) {
        int step = kSteps[rng.below(10)] * (rng.chance(50) ? 1 : -1);
        if (std::abs(p.back() + step) > 14) {
            step = -step;
        }
        p.push_back(p.back() + step);
    }
    return contour_of_pitches(p);
}

// The melody with a note added after about `percent` of its notes (a neighbour or two).
MelodyContour with_ornaments(Lcg& rng, const MelodyContour& tune, int percent) {
    std::vector<int> out;
    for (const int p : pitches_of_tune(tune)) {
        out.push_back(p);
        if (rng.chance(percent)) {
            const int extra = 1 + rng.below(2);
            for (int k = 0; k < extra; ++k) {
                out.push_back(p + (rng.chance(50) ? 1 : -1) * (1 + rng.below(2)));
            }
            out.push_back(p);
        }
    }
    return contour_of_pitches(out);
}

// The melody with about `percent` of its notes left out (the first and last stay).
MelodyContour without_notes(Lcg& rng, const MelodyContour& tune, int percent) {
    const std::vector<int> p = pitches_of_tune(tune);
    std::vector<int> out{p.front()};
    for (std::size_t i = 1; i + 1 < p.size(); ++i) {
        if (!rng.chance(percent)) {
            out.push_back(p[i]);
        }
    }
    out.push_back(p.back());
    return contour_of_pitches(out);
}

} // namespace

TEST_CASE("a melody with a fifth of its notes left out, or ornaments added, is the same piece") {
    Lcg rng{2026};
    for (int i = 0; i < 40; ++i) {
        const MelodyContour tune = wander(rng, 20 + rng.below(30));
        const MelodyContour simpler = without_notes(rng, tune, 20);
        const MelodyContour ornate = with_ornaments(rng, tune, 20);
        CAPTURE(i);
        REQUIRE(tune.usable());
        if (simpler.usable()) {
            CHECK(melody_similarity(tune, simpler) >= kVersionMelody);
        }
        CHECK(melody_similarity(tune, ornate) >= kVersionMelody);
        // And with both done to one and the other, still more like each other than like a third.
        const MelodyContour third = wander(rng, 20 + rng.below(30));
        CHECK(melody_similarity(tune, third) < kVersionConfirm);
        CHECK(melody_similarity(simpler, third) < kVersionConfirm);
    }
}

TEST_CASE("a file names its own level in its title or its file name") {
    CHECK(version_label("Fur Elise (Easy)") == VersionLabel::Easy);
    CHECK(version_label("Fur Elise - easy version") == VersionLabel::Easy);
    CHECK(version_label("Sonata, Intermediate") == VersionLabel::Medium);
    CHECK(version_label("Prelude [Advanced]") == VersionLabel::Hard);
    CHECK(version_label("Etude (Expert)") == VersionLabel::Expert);
    CHECK(version_label("Waltz Level 2") == VersionLabel::Easy);
    CHECK(version_label("Waltz level 5") == VersionLabel::Expert);
    CHECK(version_label("Waltz for Beginners") == VersionLabel::Beginner);
    CHECK(version_label("Waltz (Original)") == VersionLabel::Original);
    CHECK(version_label("Waltz") == VersionLabel::None);
    CHECK(version_label("Waltz", "waltz_simplified.mid") == VersionLabel::Easy);
    CHECK(version_label("Waltz (Hard)", "waltz_easy.mid") == VersionLabel::Hard); // the title wins
    CHECK(version_label("Level 9 Waltz") == VersionLabel::None);
    CHECK(version_label_name(VersionLabel::Medium) == "Medium");
}

TEST_CASE("a title's key leaves out the level, the number and the small words") {
    CHECK(piece_title_key("Fur Elise") == "fur elise");
    CHECK(piece_title_key("Für Elise (Easy Version)") == "fur elise");
    CHECK(piece_title_key("The Fur Elise - for Beginners") == "fur elise");
    CHECK(piece_title_key("Moonlight Sonata Op. 27 No. 2 (Easy)") == "moonlight sonata");
    CHECK(piece_title_key("Prelude in C Major") == "prelude in c major");
    CHECK(piece_title_key("Prelude in C Minor") != piece_title_key("Prelude in C Major"));
    CHECK(piece_title_key("Prelude in C#") == "prelude in c sharp");
    CHECK(piece_title_key("Untitled").empty());
    CHECK(piece_title_key("Piano Sonata") == "sonata");
}

TEST_CASE("an opus or catalogue number is the same however it is written") {
    CHECK(catalogue_number("Sonata Op. 27 No. 2") == "op27/2");
    CHECK(catalogue_number("Sonata op27 no2") == "op27/2");
    CHECK(catalogue_number("Sonata Opus 27, No 2") == "op27/2");
    CHECK(catalogue_number("Sonata Op.27") == "op27");
    CHECK(catalogue_number("Prelude BWV 846") == "bwv846");
    CHECK(catalogue_number("Sonata K. 331") == "kv331");
    CHECK(catalogue_number("Sonata KV331") == "kv331");
    CHECK(catalogue_number("Sonata No. 14") == "");
    CHECK(catalogue_number("Waltz") == "");
}

TEST_CASE("a composer is known by the surname however the name is written") {
    CHECK(composer_surname_key("Ludwig van Beethoven") == "beethoven");
    CHECK(composer_surname_key("Beethoven, Ludwig van") == "beethoven");
    CHECK(composer_surname_key("L. v. Beethoven") == "beethoven");
    CHECK(composer_surname_key("J.S. Bach") == "bach");
    CHECK(composer_surname_key("Frédéric Chopin (1810-1849)") == "chopin");
    CHECK(composer_surname_key("Johann Strauss Jr.") == "strauss");
    CHECK(composer_surname_key("Traditional").empty());
    CHECK(composer_surname_key("").empty());
}

TEST_CASE("versions with a matching title and composer are one piece, the melody confirming it") {
    const SongEntry original = entry_of("lantern-waltz.mid");
    const SongEntry easy = entry_of("lantern-waltz-easy.mid");
    const SongEntry march = entry_of("harbour-march.mid");
    VersionSong a = grouping_song(original, "Anna Keller");
    VersionSong b = grouping_song(easy, "A. Keller");
    CHECK(same_piece(a, b));
    // The same title and composer on a melody that is not alike is a different piece: "Minuet" and
    // "Prelude" are not names of one piece.
    VersionSong other = grouping_song(march, "Anna Keller");
    other.title = "Lantern Waltz";
    CHECK_FALSE(same_piece(a, other));
    // A catalogue number that matches carries it unless the melody is nothing like.
    a.title = "Lantern Waltz Op. 5";
    other.title = "Lantern Waltz Op. 5";
    CHECK_FALSE(same_piece(a, other));
    b.title = "Lantern Waltz Op. 5 No. 1 (Easy)";
    a.title = "Lantern Waltz, Opus 5 no 1";
    CHECK(same_piece(a, b));
    // Another composer is another piece, even with the same title and the same tune.
    VersionSong stranger = b;
    stranger.composer = "Boris Quill";
    CHECK_FALSE(same_piece(a, stranger));
}

TEST_CASE("files too short to compare by melody group on a title that matches alone") {
    VersionSong a;
    a.title = "Edge Pickup";
    VersionSong b = a;
    b.title = "Edge Pickup (Easy)";
    CHECK(same_piece(a, b));
    b.title = "Edge Sixteen Parts";
    CHECK_FALSE(same_piece(a, b));
    // A title that names no piece is not one.
    a.title = "Untitled";
    b.title = "Untitled";
    CHECK_FALSE(same_piece(a, b));
    // Another composer on each is two pieces whatever the melody.
    a.title = "Edge Pickup";
    b.title = "Edge Pickup";
    a.composer = "Anna Keller";
    b.composer = "Boris Quill";
    CHECK_FALSE(same_piece(a, b));
    // Too short on one side only: the title still counts, and the melody that is there is no veto.
    const SongEntry waltz = entry_of("lantern-waltz.mid");
    VersionSong full = grouping_song(waltz, "");
    full.title = "Edge Pickup";
    b.composer = "";
    b.melody = {};
    CHECK(same_piece(full, b));
}

TEST_CASE("the same piece in two keys groups, named differently") {
    const SongEntry c = entry_of("lantern-waltz.mid");
    const SongEntry d = entry_of("lantern-waltz-in-d.mid");
    REQUIRE(c.title != d.title);
    const std::vector<VersionSong> songs = {grouping_song(c, ""), grouping_song(d, "")};
    const std::vector<VersionGroup> groups = group_versions(songs);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].members.size() == 2);
}

TEST_CASE(
    "an easy arrangement and the original are one group, easiest first, named by their files") {
    const SongEntry original = entry_of("lantern-waltz.mid");
    const SongEntry easy = entry_of("lantern-waltz-easy.mid");
    // Listed original first: the group is still easy first.
    const std::vector<VersionSong> songs = {grouping_song(original, ""), grouping_song(easy, "")};
    const std::vector<VersionGroup> groups = group_versions(songs);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].members.size() == 2);
    CHECK(groups[0].members[0].song == 1);
    CHECK(groups[0].members[0].name == "Easy");
    CHECK(groups[0].members[0].labelled);
    CHECK(groups[0].members[1].song == 0);
    CHECK_FALSE(groups[0].members[1].labelled);
    CHECK(songs[1].half_stars <= songs[0].half_stars);
}

TEST_CASE("two different pieces by one composer do not group") {
    const SongEntry waltz = entry_of("lantern-waltz.mid");
    const SongEntry march = entry_of("harbour-march.mid");
    const SongEntry bach = entry_of("bach-bwv846-prelude.mid");
    const std::vector<VersionSong> songs = {grouping_song(waltz, "Anna Keller"),
                                            grouping_song(march, "Anna Keller"),
                                            grouping_song(bach, "Anna Keller")};
    CHECK(group_versions(songs).empty());
}

TEST_CASE("two parts of one opus, or two pieces of one name in other keys, are not one piece") {
    // Titles and composer alike, no melody to tell them apart: the numbers and the keys do.
    VersionSong a;
    a.composer = "Frédéric Chopin";
    VersionSong b = a;
    a.title = "Nocturne Op. 27 No. 1";
    b.title = "Nocturne Op. 27 No. 2";
    CHECK_FALSE(same_piece(a, b));
    const std::vector<VersionSong> two = {a, b};
    CHECK(group_versions(two).empty());
    // With the same tune in both, too: the number says they are two.
    const SongEntry waltz = entry_of("lantern-waltz.mid");
    a.melody = waltz.melody;
    b.melody = waltz.melody;
    CHECK_FALSE(same_piece(a, b));
    // The work and one of its parts are the one piece, and so is a part with the number written
    // another way.
    b.title = "Nocturne, Opus 27";
    CHECK(same_piece(a, b));
    b.title = "Nocturne op27 no1 (Easy)";
    CHECK(same_piece(a, b));
    // The same name in two keys is two pieces (a title with the key in it keeps it).
    VersionSong minor_a;
    minor_a.title = "Waltz in A minor";
    minor_a.composer = "Frédéric Chopin";
    VersionSong minor_e = minor_a;
    minor_e.title = "Waltz in E minor";
    CHECK_FALSE(same_piece(minor_a, minor_e));
    const std::vector<VersionSong> keys = {minor_a, minor_e};
    CHECK(group_versions(keys).empty());
}

TEST_CASE("a group holds songs that are each the same piece as every other, not a chain") {
    // The waltz and the march are not alike. A middle file with no composer and no tune is the
    // same piece as either on its title, but the two of them are not the same as each other.
    const SongEntry waltz = entry_of("lantern-waltz.mid");
    const SongEntry march = entry_of("harbour-march.mid");
    VersionSong a = grouping_song(waltz, "Anna Keller");
    VersionSong b;
    VersionSong c = grouping_song(march, "Anna Keller");
    a.title = b.title = c.title = "Dream Song";
    REQUIRE(same_piece(a, b));
    REQUIRE(same_piece(b, c));
    REQUIRE_FALSE(same_piece(a, c));
    const std::vector<VersionSong> songs = {a, b, c};
    const std::vector<VersionGroup> groups = group_versions(songs);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].members.size() == 2);
    std::vector<std::uint32_t> in;
    for (const VersionMember& m : groups[0].members) {
        in.push_back(m.song);
    }
    std::sort(in.begin(), in.end());
    CHECK(in == std::vector<std::uint32_t>{0, 1});
    // Listed the other way round the middle file goes with the first of them it meets: still two.
    const std::vector<VersionSong> other = {c, b, a};
    const std::vector<VersionGroup> back = group_versions(other);
    REQUIRE(back.size() == 1);
    CHECK(back[0].members.size() == 2);
}

TEST_CASE("a title that many songs share is not compared pair by pair, a tune two of them share is") {
    const SongEntry waltz = entry_of("lantern-waltz.mid");
    const SongEntry in_d = entry_of("lantern-waltz-in-d.mid");
    std::vector<VersionSong> songs;
    // More than kBucketMax songs of one title and composer, none with a tune: no title says
    // enough to join them, or the library of a series would be one group.
    for (std::size_t i = 0; i < 30; ++i) {
        VersionSong s;
        s.title = "Moonlit Study";
        s.composer = "Anna Keller";
        songs.push_back(std::move(s));
    }
    CHECK(group_versions(songs).empty());
    // Two of them with the same tune in two keys are found by the tune.
    songs[7].melody = waltz.melody;
    songs[21].melody = in_d.melody;
    const std::vector<VersionGroup> groups = group_versions(songs);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].members.size() == 2);
    CHECK(std::min(groups[0].members[0].song, groups[0].members[1].song) == 7);
    CHECK(std::max(groups[0].members[0].song, groups[0].members[1].song) == 21);
}

TEST_CASE("a piece's name is the title without its level words and what held them") {
    CHECK(piece_name("Lantern Waltz (Easy)") == "Lantern Waltz");
    CHECK(piece_name("Lantern Waltz - Easy Version") == "Lantern Waltz");
    CHECK(piece_name("Easy Fur Elise") == "Fur Elise");
    CHECK(piece_name("Sonata [for Beginners] Op. 49") == "Sonata Op. 49");
    CHECK(piece_name("Prelude, level 3") == "Prelude");
    CHECK(piece_name("Lantern Waltz in D") == "Lantern Waltz in D");
    CHECK(piece_name("Étude (Original)") == "Étude");
    // Nothing but a level word: the title stays.
    CHECK(piece_name("Easy") == "Easy");
    CHECK(piece_name("").empty());
}

TEST_CASE("a file with no metadata groups with its piece by the melody alone") {
    const SongEntry original = entry_of("lantern-waltz.mid");
    const SongEntry easy = entry_of("lantern-waltz-easy.mid");
    const SongEntry march = entry_of("harbour-march.mid");
    VersionSong bare = grouping_song(original, "");
    bare.title = "track 01";
    bare.file_name = "track01.mid";
    std::vector<VersionSong> songs = {bare, grouping_song(easy, "Anna Keller"),
                                      grouping_song(march, "Anna Keller")};
    songs[0].half_stars = 6;
    const std::vector<VersionGroup> groups = group_versions(songs);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].members.size() == 2);
    std::vector<std::uint32_t> in;
    for (const VersionMember& m : groups[0].members) {
        in.push_back(m.song);
    }
    std::sort(in.begin(), in.end());
    CHECK(in == std::vector<std::uint32_t>{0, 1});
}

TEST_CASE("a bare file does not join a piece of another composer by the melody") {
    const SongEntry original = entry_of("lantern-waltz.mid");
    const std::vector<VersionSong> songs = {grouping_song(original, "Anna Keller"),
                                            grouping_song(original, "Boris Quill")};
    CHECK(group_versions(songs).empty());
}

TEST_CASE("versions are ordered by our rating, a file's label breaking a tie, and named") {
    SongEntry base = entry_of("lantern-waltz.mid");
    std::vector<VersionSong> songs;
    const auto add = [&](const char* title, int half_stars) {
        VersionSong s = grouping_song(base, "Anna Keller");
        s.title = title;
        s.half_stars = half_stars;
        songs.push_back(std::move(s));
    };
    add("Lantern Waltz", 8);
    add("Lantern Waltz (Easy)", 3);
    add("Lantern Waltz", 4);
    add("Lantern Waltz (Original)", 8);
    const std::vector<VersionGroup> groups = group_versions(songs);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].members.size() == 4);
    const auto& m = groups[0].members;
    CHECK(m[0].song == 1); // 1.5 stars, labelled Easy
    CHECK(m[0].name == "Easy");
    CHECK(m[1].song == 2); // 2 stars, no label: the name comes from the stars
    CHECK(m[1].name == "Simple");
    CHECK_FALSE(m[1].labelled);
    CHECK(m[2].song == 0); // 4 stars: Hard, and before the Original at the same rating
    CHECK(m[2].name == "Hard");
    CHECK(m[3].song == 3);
    CHECK(m[3].name == "Original");
}

TEST_CASE("two versions that would get one name are told apart by their word, their file type, their folder, last a number") {
    SongEntry base = entry_of("lantern-waltz.mid");
    struct Spec {
        const char* title;
        const char* file;
        const char* folder;
        int half_stars = 6;
    };
    const auto names_of = [&](std::initializer_list<Spec> list) {
        std::vector<VersionSong> songs;
        for (const Spec& spec : list) {
            VersionSong s = grouping_song(base, "Anna Keller", spec.file);
            s.title = spec.title;
            s.folder = spec.folder;
            s.half_stars = spec.half_stars;
            songs.push_back(std::move(s));
        }
        const std::vector<VersionGroup> groups = group_versions(songs);
        REQUIRE(groups.size() == 1);
        std::vector<std::string> names;
        for (const VersionMember& m : groups[0].members) {
            names.push_back(m.name);
        }
        return names;
    };
    using Names = std::vector<std::string>;
    // The word one title has that the other's has not.
    CHECK(names_of({{"Lantern Waltz", "a.mid", ""}, {"Lantern Waltz in D", "b.mid", ""}}) ==
          Names{"Medium", "Medium D"});
    // The titles are alike: the kind of file.
    CHECK(names_of({{"Lantern Waltz", "a.mid", ""}, {"Lantern Waltz", "b.musicxml", ""}}) ==
          Names{"Medium MIDI", "Medium MusicXML"});
    CHECK(names_of({{"Lantern Waltz", "a.abc#X=2", ""}, {"Lantern Waltz", "b.MID", ""}}) ==
          Names{"Medium ABC", "Medium MIDI"});
    // The same kind of file too: the folder.
    CHECK(names_of({{"Lantern Waltz", "a.mid", "Songs"}, {"Lantern Waltz", "a.mid", "Songs/Lessons"}}) ==
          Names{"Medium Songs", "Medium Lessons"});
    // The word comes before the type, and the type before the folder: of three, the type names
    // them all, and the two of one type go on to their folders (one with no folder keeps its
    // name).
    CHECK(names_of({{"Lantern Waltz", "a.mid", "x"}, {"Lantern Waltz in D", "b.musicxml", "y"}}) ==
          Names{"Medium", "Medium D"});
    CHECK(names_of({{"Lantern Waltz", "a.mid", ""},
                    {"Lantern Waltz", "b.mid", "Lessons"},
                    {"Lantern Waltz", "c.musicxml", ""}}) ==
          Names{"Medium MIDI", "Medium MIDI Lessons", "Medium MusicXML"});
    // Nothing tells them apart: a number, the first keeping the bare name; their stars are not
    // put in front of it.
    CHECK(names_of({{"Lantern Waltz", "a.mid", "", 6}, {"Lantern Waltz", "b.mid", "", 5}}) ==
          Names{"Medium", "Medium 2"});
    CHECK(names_of({{"Lantern Waltz", "a.mid", ""}, {"Lantern Waltz", "b.mid", ""},
                    {"Lantern Waltz", "c.mid", ""}}) == Names{"Medium", "Medium 2", "Medium 3"});
    // A number is never stacked after another number: a name that ends in one takes the next in
    // brackets (a folder called "2024").
    CHECK(names_of({{"Lantern Waltz", "a.mid", "2024"}, {"Lantern Waltz", "b.mid", "2024"},
                    {"Lantern Waltz", "c.mid", "Other"}}) ==
          Names{"Medium 2024", "Medium 2024 (2)", "Medium Other"});
}

TEST_CASE("a key's note name and its mode word are kept or dropped together in a version's name (#286)") {
    SongEntry base = entry_of("lantern-waltz.mid");
    const auto names_of = [&](std::initializer_list<const char*> titles) {
        std::vector<VersionSong> songs;
        int n = 0;
        for (const char* title : titles) {
            VersionSong s = grouping_song(base, "Anna Keller", "v" + std::to_string(n++) + ".mid");
            s.title = title;
            s.half_stars = 4;
            songs.push_back(std::move(s));
        }
        const std::vector<VersionGroup> groups = group_versions(songs);
        REQUIRE(groups.size() == 1);
        std::vector<std::string> names;
        for (const VersionMember& m : groups[0].members) {
            names.push_back(m.name);
        }
        return names;
    };
    using Names = std::vector<std::string>;
    // The key is the one thing the other title shares: it was dropped and left "major" alone.
    CHECK(names_of({"Prelude in Bb major", "Bb"}) == Names{"Simple", "Simple Prelude Bb major"});
    CHECK(names_of({"Prelude in B\xE2\x99\xAD major", "B\xE2\x99\xAD"}) ==
          Names{"Simple", "Simple Prelude B\xE2\x99\xAD major"});
    // The note is shared, the mode is not: both stay.
    CHECK(names_of({"Prelude in Bb major", "Prelude Bb"}) == Names{"Simple", "Simple Bb major"});
    // A sharp is not a word byte, yet "C#" and "minor" go together.
    CHECK(names_of({"Etude in C# minor", "Etude in C#"}) == Names{"Simple", "Simple C# minor"});
    // Both shared: both dropped.
    CHECK(names_of({"Prelude in F# major", "Prelude in F# major in D"}) ==
          Names{"Simple", "Simple D"});
}

TEST_CASE("a group never holds more than kMaxVersions songs") {
    SongEntry base = entry_of("lantern-waltz.mid");
    std::vector<VersionSong> songs;
    for (std::size_t i = 0; i < kMaxVersions + 4; ++i) {
        VersionSong s = grouping_song(base, "Anna Keller");
        s.half_stars = 2 + static_cast<int>(i % 8);
        songs.push_back(std::move(s));
    }
    std::size_t total = 0;
    for (const VersionGroup& g : group_versions(songs)) {
        CHECK(g.members.size() <= kMaxVersions);
        CHECK(g.members.size() >= 2);
        total += g.members.size();
    }
    CHECK(total <= songs.size());
}

TEST_CASE("the test library's own pieces group as expected") {
    // Gymnopedie and the prelude are two pieces; the waltz is three files of one.
    const char* files[] = {"satie-gymnopedie-1.mid", "bach-bwv846-prelude.mid", "lantern-waltz.mid",
                           "lantern-waltz-easy.mid", "lantern-waltz-in-d.mid",  "harbour-march.mid",
                           "difficulty-etude.mid",   "difficulty-toccata.mid"};
    std::vector<VersionSong> songs;
    for (const char* f : files) {
        songs.push_back(grouping_song(entry_of(f), ""));
    }
    const std::vector<VersionGroup> groups = group_versions(songs);
    REQUIRE(groups.size() == 1);
    std::vector<std::uint32_t> in;
    for (const VersionMember& m : groups[0].members) {
        in.push_back(m.song);
    }
    std::sort(in.begin(), in.end());
    CHECK(in == std::vector<std::uint32_t>{2, 3, 4});
}

// ---- The player's corrections (#213)

TEST_CASE("overrides: a split drops the joins of the song, a join drops its split, a clear both") {
    VersionOverrides o;
    std::vector<VersionRow> rows;
    apply_version_edit(o, {VersionEdit::Kind::Join, 9, 3}, &rows);
    apply_version_edit(o, {VersionEdit::Kind::Join, 3, 9}, &rows); // the same pair again
    REQUIRE(o.joins.size() == 1);
    CHECK(o.joins[0] == std::make_pair<std::uint64_t, std::uint64_t>(3, 9)); // smaller first
    CHECK(o.joined(3));
    CHECK(o.joined(9));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].add);
    rows.clear();
    apply_version_edit(o, {VersionEdit::Kind::Split, 3, 0}, &rows);
    CHECK(o.joins.empty());
    CHECK(o.split(3));
    REQUIRE(rows.size() == 2); // the join went, the split came
    CHECK_FALSE(rows[0].add);
    CHECK(rows[1].add);
    CHECK(rows[1].b == 0);
    rows.clear();
    // The latest explicit action wins: joining a split song takes its split away ...
    apply_version_edit(o, {VersionEdit::Kind::Join, 3, 5}, &rows);
    CHECK_FALSE(o.split(3));
    CHECK(o.joined(5));
    // ... but only for the two songs the join names: another song's join never pulls a split
    // song back in.
    apply_version_edit(o, {VersionEdit::Kind::Split, 7, 0});
    apply_version_edit(o, {VersionEdit::Kind::Join, 5, 8});
    apply_version_edit(o, {VersionEdit::Kind::Join, 3, 9});
    CHECK(o.split(7));
    CHECK_FALSE(o.joined(7));
    apply_version_edit(o, {VersionEdit::Kind::Clear, 7, 0});
    apply_version_edit(o, {VersionEdit::Kind::Clear, 8, 0});
    apply_version_edit(o, {VersionEdit::Kind::Clear, 9, 0});
    // Clear: the files decide again.
    apply_version_edit(o, {VersionEdit::Kind::Clear, 5, 0});
    CHECK(o.empty());
    // Nonsense changes nothing.
    apply_version_edit(o, {VersionEdit::Kind::Join, 4, 4});
    apply_version_edit(o, {VersionEdit::Kind::Split, 0, 0});
    apply_version_edit(o, {VersionEdit::Kind::Join, 4, 0});
    CHECK(o.empty());
}

TEST_CASE("a split song is in no group, whatever the files say") {
    const SongEntry original = entry_of("lantern-waltz.mid");
    const SongEntry easy = entry_of("lantern-waltz-easy.mid");
    const SongEntry in_d = entry_of("lantern-waltz-in-d.mid");
    const std::vector<VersionSong> songs = {grouping_song(original, "Anna Keller"),
                                            grouping_song(easy, "Anna Keller"),
                                            grouping_song(in_d, "Anna Keller")};
    REQUIRE(group_versions(songs).size() == 1);
    REQUIRE(group_versions(songs)[0].members.size() == 3);
    // The easy one taken out: the other two are still a piece on their own.
    VersionLinks links;
    links.splits = {1};
    std::vector<VersionGroup> groups = group_versions(songs, links);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].members.size() == 2);
    for (const VersionMember& m : groups[0].members) {
        CHECK(m.song != 1);
    }
    // A split beats a join of the same song.
    links.joins = {{1, 0}};
    groups = group_versions(songs, links);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].members.size() == 2);
    // All three taken out: none.
    links.splits = {0, 1, 2};
    CHECK(group_versions(songs, links).empty());
}

TEST_CASE("a joined pair is one group even where the melody vetoes it, and the cap still holds") {
    const SongEntry waltz = entry_of("lantern-waltz.mid");
    const SongEntry march = entry_of("harbour-march.mid");
    VersionSong a = grouping_song(waltz, "Anna Keller");
    VersionSong b = grouping_song(march, "Anna Keller");
    a.title = b.title = "Lantern Waltz"; // the title and composer match, the melody says no
    REQUIRE_FALSE(same_piece(a, b));
    const std::vector<VersionSong> songs = {a, b};
    CHECK(group_versions(songs).empty());
    VersionLinks links;
    links.joins = {{0, 1}};
    const std::vector<VersionGroup> groups = group_versions(songs, links);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].members.size() == 2);
    // Joined pieces of other composers and no likeness at all: one group too.
    VersionSong c = grouping_song(march, "Boris Quill");
    c.title = "Harbour March";
    const std::vector<VersionSong> three = {a, c};
    links.joins = {{0, 1}};
    CHECK(group_versions(three, links).size() == 1);
    // A chain of joins stops at kMaxVersions.
    std::vector<VersionSong> many;
    for (std::size_t i = 0; i < kMaxVersions + 3; ++i) {
        VersionSong s = grouping_song(i % 2 == 0 ? waltz : march, "");
        s.title = "Piece " + std::to_string(i);
        many.push_back(std::move(s));
    }
    VersionLinks chain;
    for (std::uint32_t i = 0; i + 1 < many.size(); ++i) {
        chain.joins.emplace_back(i, i + 1);
    }
    for (const VersionGroup& g : group_versions(many, chain)) {
        CHECK(g.members.size() <= kMaxVersions);
    }
}

TEST_CASE("a joined song is a version of the group it joins only when every pair agrees") {
    // 0 and 1 are one piece by their files; 2 is another that the player joined to 1 only. The
    // pair (0, 2) is not the same piece, so 2 does not enter the group of 0 and 1 by that join:
    // the join holds, 2 is with 1.
    const SongEntry waltz = entry_of("lantern-waltz.mid");
    const SongEntry easy = entry_of("lantern-waltz-easy.mid");
    const SongEntry march = entry_of("harbour-march.mid");
    const std::vector<VersionSong> songs = {grouping_song(waltz, "Anna Keller"),
                                            grouping_song(easy, "Anna Keller"),
                                            grouping_song(march, "Anna Keller")};
    VersionLinks links;
    links.joins = {{1, 2}};
    const std::vector<VersionGroup> groups = group_versions(songs, links);
    REQUIRE(groups.size() == 1);
    std::vector<std::uint32_t> in;
    for (const VersionMember& m : groups[0].members) {
        in.push_back(m.song);
    }
    std::sort(in.begin(), in.end());
    CHECK(in == std::vector<std::uint32_t>{1, 2});
}
