// SPDX-License-Identifier: GPL-3.0-or-later
// The bundled melodies themselves (app/assets/melodies): the manifest parses, every file it lists
// loads and has its source and licence line, the files on disk are the files listed, and the paths'
// order holds.
#include "melody_book.hpp"

#include "song_library.hpp"

#include "etude/content_hash.hpp"
#include "etude/melody_progress.hpp"
#include "etude/song_versions.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

std::string_view manifest_text() {
    const std::span<const unsigned char> text = embedded_melody_manifest();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

bool starts_with(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// What is wrong with the notes of a Standard MIDI File, read from its bytes (the loader mends some of
// it, so the loaded song would hide it): a note struck while the same pitch sounds, a note that ends
// where it starts or never starts or ends, or bytes that are not a file. Empty when all is well.
std::string note_problem(std::span<const unsigned char> d) {
    const auto be = [&](std::size_t at, int n) {
        std::size_t v = 0;
        for (int i = 0; i < n; ++i) {
            v = (v << 8) | d[at + static_cast<std::size_t>(i)];
        }
        return v;
    };
    if (d.size() < 14 || be(0, 4) != 0x4D546864) {
        return "not a MIDI file";
    }
    std::size_t at = 14;
    for (std::size_t track = 0, tracks = be(10, 2); track < tracks; ++track) {
        if (at + 8 > d.size() || be(at, 4) != 0x4D54726B) {
            return "a track is missing";
        }
        const std::size_t end = at + 8 + be(at + 4, 4);
        at += 8;
        std::size_t tick = 0;
        unsigned running = 0;
        std::map<unsigned, std::size_t> sounding; // channel and pitch -> the tick it began
        while (at < end) {
            std::size_t delta = 0;
            unsigned char b = 0;
            do {
                b = d[at++];
                delta = (delta << 7) | (b & 0x7Fu);
            } while ((b & 0x80u) != 0);
            tick += delta;
            if (d[at] == 0xFF || d[at] == 0xF0 || d[at] == 0xF7) {
                const bool meta = d[at] == 0xFF;
                at += meta ? 2 : 1;
                std::size_t len = 0;
                do {
                    b = d[at++];
                    len = (len << 7) | (b & 0x7Fu);
                } while ((b & 0x80u) != 0);
                at += len;
                continue;
            }
            if ((d[at] & 0x80u) != 0) {
                running = d[at++];
            }
            const unsigned status = running & 0xF0u;
            const unsigned key = ((running & 0x0Fu) << 8) | d[at];
            const bool on = status == 0x90 && d[at + 1] != 0;
            const bool off = status == 0x80 || (status == 0x90 && d[at + 1] == 0);
            at += status == 0xC0 || status == 0xD0 ? 1 : 2;
            if (on) {
                if (sounding.count(key) != 0) {
                    return "a note is struck while it sounds, at tick " + std::to_string(tick);
                }
                sounding[key] = tick;
            } else if (off) {
                const auto it = sounding.find(key);
                if (it == sounding.end()) {
                    return "a note ends that never started, at tick " + std::to_string(tick);
                }
                if (it->second >= tick) {
                    return "a note has no length, at tick " + std::to_string(tick);
                }
                sounding.erase(it);
            }
        }
        if (!sounding.empty()) {
            return "a note never ends";
        }
    }
    return {};
}

} // namespace

TEST_CASE("melody manifest: the bundled manifest parses and every file has a source and a licence") {
    const MelodyManifest m = parse_melody_manifest(manifest_text());
    for (const std::string& e : m.errors) {
        MESSAGE(e);
    }
    REQUIRE(m.ok());
    // The first set; a piece added later (README.md in the melodies folder) only makes these larger.
    CHECK(m.files.size() >= 14);
    CHECK(m.paths.size() >= 3);
    for (const MelodyFileEntry& f : m.files) {
        CAPTURE(f.file);
        CHECK_FALSE(f.source.empty());
        CHECK_FALSE(f.licence.empty());
        CHECK_FALSE(f.title.empty());
        CHECK_FALSE(f.composer.empty());
        // Only what can be bundled: our own dedication, or what the source's page calls public domain.
        CHECK((f.licence == "CC0-1.0" || starts_with(f.licence, "Public Domain")));
        if (starts_with(f.licence, "Public Domain")) {
            // The page that says so is in the line: a Mutopia piece or the HymnWiki page.
            CHECK((f.source.find("mutopiaproject.org/cgibin/piece-info.cgi?id=") != std::string::npos ||
                   f.source.find("https://www.hymnwiki.org/") != std::string::npos));
        } else {
            CHECK(starts_with(f.source, "Own transcription of the public-domain melody"));
        }
        // A file made from a source's notes says it was and what was changed (every one is cut, split
        // or moved to another key; the line says how).
        if (starts_with(f.source, "Derived from ")) {
            CHECK(starts_with(f.licence, "Public Domain"));
            CHECK((f.source.find("transposed") != std::string::npos ||
                   f.source.find(" in G major") != std::string::npos ||
                   f.source.find(" in A minor") != std::string::npos));
        }
    }
}

TEST_CASE("melody manifest: the files on disk are the files listed, and each is bundled") {
    const MelodyManifest m = parse_melody_manifest(manifest_text());
    REQUIRE(m.ok());
    std::set<std::string> listed;
    for (const MelodyFileEntry& f : m.files) {
        listed.insert(f.file);
        CHECK_FALSE(embedded_melody_file(f.file).empty());
    }
    std::set<std::string> on_disk;
    for (const auto& entry : std::filesystem::directory_iterator(
             std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / "assets" / "melodies")) {
        if (entry.path().extension() == ".mid") {
            on_disk.insert(entry.path().filename().string());
        }
    }
    CHECK(on_disk == listed);
}

TEST_CASE("melody files: no note of any bundled file has no length, overlaps itself or never ends") {
    const MelodyManifest m = parse_melody_manifest(manifest_text());
    REQUIRE(m.ok());
    for (const MelodyFileEntry& f : m.files) {
        CAPTURE(f.file);
        CHECK(note_problem(embedded_melody_file(f.file)) == "");
    }
}

TEST_CASE("melody files: the check finds a repeated pitch ended as it starts") {
    // Format 0, one track: C4 on, C4 off, then C4 on and off again at the same tick as the first
    // off (the second note is struck before the first ends) and a note of no length.
    const auto file = [](std::initializer_list<unsigned char> events) {
        std::vector<unsigned char> d{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k',
                                     0, 0, 0, static_cast<unsigned char>(events.size() + 4)};
        d.insert(d.end(), events.begin(), events.end());
        d.insert(d.end(), {0, 0xFF, 0x2F, 0});
        return d;
    };
    const std::vector<unsigned char> good = file({0, 0x90, 60, 80, 0x60, 0x80, 60, 0, 0, 0x90, 60, 80, 0x60, 0x80, 60, 0});
    CHECK(note_problem(good) == "");
    const std::vector<unsigned char> struck_first = file({0, 0x90, 60, 80, 0x60, 0x90, 60, 80, 0, 0x80, 60, 0, 0x60, 0x80, 60, 0});
    CHECK(note_problem(struck_first).find("struck while it sounds") != std::string::npos);
    const std::vector<unsigned char> no_length = file({0, 0x90, 60, 80, 0, 0x80, 60, 0});
    CHECK(note_problem(no_length).find("no length") != std::string::npos);
    const std::vector<unsigned char> never_ends = file({0, 0x90, 60, 80});
    CHECK(note_problem(never_ends).find("never ends") != std::string::npos);
    CHECK(note_problem({}) == "not a MIDI file");
}

TEST_CASE("melody book: every listed file loads, with its hands named, a rating and a range") {
    const MelodyBook book = load_melody_book();
    for (const std::string& p : book.problems) {
        MESSAGE(p);
    }
    REQUIRE(book.problems.empty());
    REQUIRE(book.pieces.size() >= 14);
    REQUIRE(book.paths.size() >= 3);
    std::set<std::uint64_t> hashes;
    for (const MelodyPiece& p : book.pieces) {
        CAPTURE(p.file);
        CHECK(hashes.insert(p.hash).second);
        CHECK(p.half_stars >= 2);
        CHECK(p.half_stars <= 10);
        CHECK(p.shape.right.keys() > 0);
        // The hands come from the file's own track names, so no hand split guesses at them.
        const SongFileLoad load = load_song_bytes(embedded_melody_files()(p.file), p.file);
        REQUIRE(load.ok);
        CHECK(load.entry.unknown_notes == 0);
        CHECK(load.song.hash == content_hash(reinterpret_cast<const char*>(embedded_melody_file(p.file).data()),
                                             embedded_melody_file(p.file).size()));
        // Steps 1 and 2 are right hand only, the rest use both.
        CHECK((p.step <= 2) == (p.shape.left.keys() == 0));
    }
}

namespace {

// What each of the first set's files asks of each hand, as MIDI notes (C4 is 60): the lowest and highest
// key of the right hand, then of the left (0, 0 for a piece with no left hand). A file whose hands were
// swapped, or one an octave off, no longer matches.
struct Expected {
    const char* file;
    int right_low;
    int right_high;
    int left_low;
    int left_high;
};
constexpr Expected kRanges[] = {
    {"hot-cross-buns.mid", 60, 64, 0, 0},
    {"twinkle-twinkle-little-star.mid", 60, 69, 0, 0},
    {"ode-to-joy.mid", 55, 67, 0, 0},
    {"oh-susanna.mid", 67, 76, 0, 0},
    {"jingle-bells.mid", 60, 67, 0, 0},
    {"amazing-grace.mid", 62, 74, 0, 0},
    {"greensleeves.mid", 64, 77, 52, 65},
    {"brahms-lullaby.mid", 60, 72, 36, 55},
    {"brahms-lullaby-easy.mid", 60, 72, 41, 48},
    {"minuet-in-g.mid", 59, 83, 43, 64},
    {"minuet-in-a-minor.mid", 64, 81, 36, 67},
    {"fur-elise.mid", 60, 88, 40, 76},
    {"von-fremden-laendern.mid", 59, 79, 42, 64},
    {"polonaise-in-f.mid", 65, 82, 36, 62},
    {"sonatina-op36-1.mid", 55, 86, 36, 65},
};

} // namespace

TEST_CASE("melody book: each piece's range per hand is what its file holds, the left below the right") {
    const MelodyBook book = load_melody_book();
    REQUIRE(book.problems.empty());
    REQUIRE(book.pieces.size() >= std::size(kRanges)); // a piece added later is not pinned here
    for (const Expected& e : kRanges) {
        CAPTURE(e.file);
        const auto it = std::find_if(book.pieces.begin(), book.pieces.end(),
                                     [&](const MelodyPiece& p) { return p.file == e.file; });
        REQUIRE(it != book.pieces.end());
        const MelodyShape& shape = it->shape;
        CHECK(shape.right.lowest == e.right_low);
        CHECK(shape.right.highest == e.right_high);
        if (e.left_high == 0) {
            CHECK_FALSE(shape.left.used());
            continue;
        }
        CHECK(shape.left.lowest == e.left_low);
        CHECK(shape.left.highest == e.left_high);
        // The hands are not swapped: the left reaches lower and lower at the top too.
        CHECK(shape.left.lowest < shape.right.lowest);
        CHECK(shape.left.highest < shape.right.highest);
    }
}

TEST_CASE("melody book: the paths hold every piece once, in order, with a goal each") {
    const MelodyBook book = load_melody_book();
    REQUIRE(book.problems.empty());
    std::set<std::size_t> seen;
    for (const MelodyPath& path : book.paths) {
        CAPTURE(path.id);
        CHECK_FALSE(path.title.empty());
        int last_step = 0;
        for (const std::size_t i : path.pieces) {
            REQUIRE(i < book.pieces.size());
            CHECK(seen.insert(i).second);
            CHECK_FALSE(book.pieces[i].goal.empty());
            CHECK(book.pieces[i].step >= last_step);
            last_step = book.pieces[i].step;
        }
    }
    CHECK(seen.size() == book.pieces.size());
}

TEST_CASE("melody book: the best score of a piece comes from the history's plays by file hash") {
    MelodyBook book = load_melody_book();
    REQUIRE_FALSE(book.empty());
    std::unordered_map<std::uint64_t, SongPlays> plays;
    plays[book.pieces[2].hash].best_permille = 930;
    plays[book.pieces[3].hash].best_permille = 410;
    plays[12345].best_permille = 999; // a library song, not a melody
    book.set_best(plays);
    CHECK(book.pieces[2].best_permille == 930);
    CHECK(book.pieces[3].best_permille == 410);
    CHECK(book.pieces[0].best_permille == -1);
    book.set_best({});
    CHECK(book.pieces[2].best_permille == -1);
}

TEST_CASE("melody book: a piece opens as a song under the manifest's title") {
    const MelodyBook book = load_melody_book();
    REQUIRE_FALSE(book.empty());
    const MelodyPiece& piece = book.pieces[0];
    const std::optional<OpenedMelody> opened = open_melody(piece, embedded_melody_files());
    REQUIRE(opened.has_value());
    const LoadedSong& song = opened->song;
    CHECK(song.title == piece.title);
    CHECK(opened->composer == piece.composer);
    CHECK(song.hash == piece.hash);
    CHECK_FALSE(song.song.notes.empty());
    CHECK(song.library_path.empty()); // it is not a file of the player's library
}

TEST_CASE("melody book: a file that is missing is left out and said, a bad manifest gives no book") {
    const MelodyFileFn none = [](std::string_view) { return std::span<const std::uint8_t>(); };
    const MelodyBook gone = load_melody_book(manifest_text(), none);
    CHECK(gone.empty());
    CHECK_FALSE(gone.problems.empty());
    const MelodyBook bad = load_melody_book("file | x\n", embedded_melody_files());
    CHECK(bad.empty());
    CHECK_FALSE(bad.problems.empty());
    CHECK_FALSE(open_melody(MelodyPiece{}, none).has_value());
}

TEST_CASE("melody manifest: a piece added without its licence or its source is caught, wherever its "
          "line is") {
    // What happens when someone adds a file and forgets the licence or the source: the real manifest
    // with that field of one `file` line emptied (or dropped), for each line in turn, is refused, and
    // an error starts with exactly that line's number ("line 1:" is not "line 14:"); the book then has
    // no pieces rather than a piece nobody may bundle.
    const std::string text(manifest_text());
    const auto fields_of = [](const std::string& line) {
        std::vector<std::string> fields;
        std::size_t from = 0;
        for (std::size_t at = line.find(" | "); at != std::string::npos; at = line.find(" | ", from)) {
            fields.push_back(line.substr(from, at - from));
            from = at + 3;
        }
        fields.push_back(line.substr(from));
        return fields;
    };
    const auto joined = [](const std::vector<std::string>& fields) {
        std::string out;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            out += (i > 0 ? " | " : "") + fields[i];
        }
        return out;
    };
    int file_lines = 0;
    int number = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        end = end == std::string::npos ? text.size() : end;
        const std::string line = text.substr(at, end - at);
        ++number;
        if (starts_with(line, "file | ")) {
            ++file_lines;
            const std::vector<std::string> fields = fields_of(line);
            REQUIRE(fields.size() == 7);
            for (const std::size_t field : {std::size_t{5}, std::size_t{6}}) { // the source, the licence
                std::vector<std::string> emptied = fields;
                emptied[field] = "";
                std::vector<std::string> dropped = fields;
                dropped.erase(dropped.begin() + static_cast<std::ptrdiff_t>(field));
                for (const std::vector<std::string>& changed : {emptied, dropped}) {
                    const std::string broken = text.substr(0, at) + joined(changed) + text.substr(end);
                    const MelodyManifest m = parse_melody_manifest(broken);
                    CAPTURE(line);
                    CAPTURE(field);
                    CHECK_FALSE(m.ok());
                    const std::string wanted = "line " + std::to_string(number) + ":";
                    CHECK(std::any_of(m.errors.begin(), m.errors.end(), [&](const std::string& e) {
                        return e.rfind(wanted, 0) == 0;
                    }));
                    CHECK(load_melody_book(broken, embedded_melody_files()).empty());
                }
            }
        }
        at = end + 1;
    }
    CHECK(file_lines >= 14);
}

namespace {

const MelodyPiece* find_piece(const MelodyBook& book, const char* file) {
    const auto it = std::find_if(book.pieces.begin(), book.pieces.end(),
                                 [&](const MelodyPiece& p) { return p.file == file; });
    return it == book.pieces.end() ? nullptr : &*it;
}

// A bundled file's notes of one hand as (start tick, end tick, pitch).
std::vector<std::array<std::int64_t, 3>> hand_notes(const char* file, Hand hand) {
    const SongFileLoad load = load_song_bytes(embedded_melody_files()(file), file);
    REQUIRE(load.ok);
    std::vector<std::array<std::int64_t, 3>> out;
    for (const Note& n : load.song.song.notes) {
        if (n.hand == hand) {
            out.push_back({n.start_tick, n.end_tick, n.pitch});
        }
    }
    return out;
}

} // namespace

TEST_CASE("melody book: Brahms' Lullaby easy and original are two pieces of the Both hands path, the easy first, "
          "each with its own score and tag") {
    MelodyBook book = load_melody_book();
    REQUIRE(book.problems.empty());
    const MelodyPiece* easy = find_piece(book, "brahms-lullaby-easy.mid");
    const MelodyPiece* original = find_piece(book, "brahms-lullaby.mid");
    REQUIRE(easy != nullptr);
    REQUIRE(original != nullptr);
    const auto path_it = std::find_if(book.paths.begin(), book.paths.end(),
                                      [](const MelodyPath& p) { return p.id == "both-hands"; });
    REQUIRE(path_it != book.paths.end());
    const MelodyPath& path = *path_it;
    CHECK(path.pieces.size() == 5); // the count is files: "of 5"
    const std::size_t easy_at = static_cast<std::size_t>(easy - book.pieces.data());
    const std::size_t original_at = static_cast<std::size_t>(original - book.pieces.data());
    const auto easy_pos = std::find(path.pieces.begin(), path.pieces.end(), easy_at);
    const auto original_pos = std::find(path.pieces.begin(), path.pieces.end(), original_at);
    REQUIRE(easy_pos != path.pieces.end());
    REQUIRE(original_pos != path.pieces.end());
    CHECK(original_pos == easy_pos + 1); // the easy one first
    // The title is the piece's on both; the goal line and the stars are each file's own.
    CHECK(easy->title == "Brahms' Lullaby");
    CHECK(original->title == "Brahms' Lullaby");
    CHECK(easy->goal != original->goal);
    CHECK(easy->half_stars < original->half_stars);
    CHECK(easy->tag == "Easy");
    CHECK(original->tag == "Original");
    // Each has its own progress: the history's best scores are by file hash.
    std::unordered_map<std::uint64_t, SongPlays> plays;
    plays[easy->hash].best_permille = 950;
    plays[original->hash].best_permille = 400;
    book.set_best(plays);
    CHECK(find_piece(book, "brahms-lullaby-easy.mid")->best_permille == 950);
    CHECK(find_piece(book, "brahms-lullaby.mid")->best_permille == 400);
    std::vector<int> best;
    for (const std::size_t i : path.pieces) {
        best.push_back(book.pieces[i].best_permille);
    }
    const MelodyPathProgress progress = melody_path_progress(best);
    CHECK(progress.total == 5);
    CHECK(progress.passed == 1); // the easy one only
}

TEST_CASE("melody book: only the two Brahms' Lullaby files have a tag, and a tagged piece opens under it") {
    const MelodyBook book = load_melody_book();
    REQUIRE(book.problems.empty());
    int tagged = 0;
    for (const MelodyPiece& p : book.pieces) {
        CAPTURE(p.file);
        if (p.file == "brahms-lullaby-easy.mid" || p.file == "brahms-lullaby.mid") {
            CHECK_FALSE(p.tag.empty());
            ++tagged;
        } else {
            CHECK(p.tag.empty());
        }
    }
    CHECK(tagged == 2);
    const MelodyPiece* easy = find_piece(book, "brahms-lullaby-easy.mid");
    REQUIRE(easy != nullptr);
    const std::optional<OpenedMelody> opened = open_melody(*easy, embedded_melody_files());
    REQUIRE(opened.has_value());
    CHECK(opened->song.title == "Brahms' Lullaby (Easy)");
}

TEST_CASE("melody book: the library's versions rule, run on the bundled files, groups just these two") {
    // The song library groups the two as versions of one piece (its own rule, title and composer, then
    // the melody), and nothing else of the bundled files.
    const MelodyManifest manifest = parse_melody_manifest(manifest_text());
    REQUIRE(manifest.ok());
    std::vector<VersionSong> songs;
    for (const MelodyFileEntry& f : manifest.files) {
        const SongFileLoad load = load_song_bytes(embedded_melody_files()(f.file), f.file);
        REQUIRE(load.ok);
        VersionSong v;
        v.title = f.title;
        v.composer = f.composer;
        v.file_name = f.file;
        v.half_stars = load.entry.half_stars;
        v.melody = load.entry.melody;
        songs.push_back(std::move(v));
    }
    const std::vector<VersionGroup> groups = group_versions(songs);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].members.size() == 2);
    const auto file_of = [&](const VersionMember& m) { return manifest.files[m.song].file; };
    CHECK(file_of(groups[0].members[0]) == "brahms-lullaby-easy.mid");
    CHECK(file_of(groups[0].members[1]) == "brahms-lullaby.mid");
    CHECK(groups[0].members[0].name == "Easy");
    // And the melody alone, whatever the titles say, would join them: the right hand is the same.
    const std::size_t easy = groups[0].members[0].song;
    const std::size_t original = groups[0].members[1].song;
    CHECK(melody_similarity(songs[easy].melody, songs[original].melody) >= kVersionMelody);
}

TEST_CASE("melody book: the easy Brahms' Lullaby has the original's right hand and one left-hand note a bar "
          "from the chord the original plays there") {
    const auto right_easy = hand_notes("brahms-lullaby-easy.mid", Hand::Right);
    const auto right_original = hand_notes("brahms-lullaby.mid", Hand::Right);
    REQUIRE_FALSE(right_original.empty());
    CHECK(right_easy == right_original); // note for note: the same starts, ends and pitches
    const auto left_easy = hand_notes("brahms-lullaby-easy.mid", Hand::Left);
    const auto left_original = hand_notes("brahms-lullaby.mid", Hand::Left);
    REQUIRE(left_easy.size() == 16);
    constexpr std::int64_t kBar = 3 * 480;
    constexpr std::int64_t kPickup = 480; // the voice's one-beat pickup, then the bars
    int lowest = 127;
    int highest = 0;
    for (std::size_t bar = 0; bar < left_easy.size(); ++bar) {
        CAPTURE(bar + 1);
        const std::int64_t from = kPickup + static_cast<std::int64_t>(bar) * kBar;
        CHECK(left_easy[bar][0] == from); // one note, on the bar's downbeat
        // The note's pitch class is one the original's left hand plays in the same bar.
        const bool in_original = std::any_of(left_original.begin(), left_original.end(), [&](const auto& n) {
            return n[0] >= from && n[0] < from + kBar && n[2] % 12 == left_easy[bar][2] % 12;
        });
        CHECK(in_original);
        // And it is the root of a chord of C, F or G.
        const int pc = static_cast<int>(left_easy[bar][2] % 12);
        CHECK((pc == 0 || pc == 5 || pc == 7));
        lowest = std::min(lowest, static_cast<int>(left_easy[bar][2]));
        highest = std::max(highest, static_cast<int>(left_easy[bar][2]));
    }
    // One hand position: no more than a fifth from the lowest note to the highest.
    CHECK(highest - lowest <= 7);
}
