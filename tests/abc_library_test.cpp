// SPDX-License-Identifier: GPL-3.0-or-later
// ABC in the song library: a file of several tunes is one library song per tune, each with the
// content-hash identity of its own text, opened by an address after the file's path.
#include "song_library.hpp"

#include "etude/abc.hpp"
#include "etude/content_hash.hpp"
#include "etude/song_key.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

namespace fs = std::filesystem;

std::string data_path(const std::string& name) {
    return std::string(ETUDE_TEST_DATA_DIR) + "/" + name;
}

// A folder of this process alone, emptied at the start and removed at the end: another run of the
// tests at the same time (a second checkout's test run) would otherwise delete and rewrite the
// files of a name both use, and a song would fail to read.
struct ScratchDirs {
    std::vector<fs::path> made;
    ~ScratchDirs() {
        for (const fs::path& dir : made) {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    }
};

fs::path fresh_dir(const std::string& name) {
    static ScratchDirs scratch;
    static const std::string unique = std::to_string(std::random_device{}());
    const fs::path dir = fs::temp_directory_path() / (name + "_" + unique);
    fs::remove_all(dir);
    fs::create_directories(dir);
    scratch.made.push_back(dir);
    return dir;
}

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string read_text(const std::string& name) {
    std::ifstream in(data_path(name), std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::string> relatives(const SongLibrary& lib) {
    std::vector<std::string> out;
    for (const LibraryItem& item : lib.state().items) {
        out.push_back(item.relative);
    }
    return out;
}

} // namespace

TEST_CASE("abc library: .abc files are song files, any case") {
    CHECK(is_song_file_name("folk.abc"));
    CHECK(is_song_file_name("FOLK.ABC"));
    CHECK(is_song_file_name("a/b/tunes.Abc"));
    CHECK_FALSE(is_song_file_name("folk.abcd"));
    CHECK_FALSE(is_song_file_name("folk.ab"));
}

TEST_CASE("abc library: an ABC file is one entry until the worker has read it, then its tunes") {
    const fs::path dir = fresh_dir("etude_abc_library_collection");
    write(dir / "collection.abc", read_text("abc-collection.abc"));
    write(dir / "reel.abc", read_text("abc-reel-repeats.abc"));
    write(dir / "other.mid", "not a midi file");
    SongLibrary lib({dir});
    // The scan only lists the directory: no file is opened, so nothing is split yet.
    CHECK(lib.scan() == 3);
    CHECK(relatives(lib) == std::vector<std::string>{"collection.abc", "other.mid", "reel.abc"});
    CHECK(lib.state().pending() == 3);
    lib.load_all_now();
    // The file of three tunes became three entries in its place; a one-tune file kept its path.
    CHECK(relatives(lib) == std::vector<std::string>{"collection.abc#X=1", "collection.abc#X=2",
                                                     "collection.abc#X=10", "other.mid",
                                                     "reel.abc"});
    CHECK(lib.tune_at(0) == "X=1");
    CHECK(lib.tune_at(2) == "X=10");
    CHECK(lib.tune_at(3).empty());
    CHECK(lib.tune_at(4).empty());
    CHECK(lib.path_at(0) == lib.path_at(2));
    CHECK(lib.path_at(0) != lib.path_at(4));
    CHECK(lib.state().loaded() == 4);
    CHECK(lib.state().failed() == 1); // other.mid
    CHECK(lib.state().items[0].entry.title == "Scale Up");
    CHECK(lib.state().items[1].entry.title == "Scale Down");
    CHECK(lib.state().items[2].entry.title == "Arpeggio");
    CHECK(lib.state().items[4].entry.title == "Hillside Reel");
    // Each tune has the identity of its own text.
    const std::string bytes = read_text("abc-collection.abc");
    const std::vector<AbcTune> tunes =
        list_abc_tunes(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    REQUIRE(tunes.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(lib.state().items[i].entry.hash == tunes[i].hash);
    }
    CHECK(lib.state().items[0].entry.hash != lib.state().items[1].entry.hash);
    // A single-tune file without a header is hashed as the whole file, like any other format.
    const std::string reel = read_text("abc-reel-repeats.abc");
    CHECK(lib.state().items[4].entry.hash == content_hash(reel));
    // Stepping goes tune to tune.
    CHECK(lib.step(std::size_t{0}, 1) == std::size_t{1});
    CHECK(lib.step(std::size_t{2}, 1) == std::size_t{4}); // over the song that failed
}

TEST_CASE("abc library: the worker splits files in the background and poll() publishes them") {
    const fs::path dir = fresh_dir("etude_abc_library_background");
    for (int i = 0; i < 6; ++i) {
        write(dir / ("c" + std::to_string(i) + ".abc"), read_text("abc-collection.abc"));
        write(dir / ("d" + std::to_string(i) + ".abc"), read_text("abc-dorian.abc"));
    }
    SongLibrary lib({dir});
    CHECK(lib.scan() == 12);
    lib.start_loading();
    bool rearranged = false;
    while (true) {
        const std::size_t before = lib.size();
        lib.poll();
        rearranged = rearranged || lib.size() != before;
        if (lib.state().settled()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    lib.wait_until_settled();
    CHECK(rearranged);
    CHECK(lib.size() == 6 * 3 + 6);
    CHECK(lib.state().loaded() == lib.size());
    // The order is still the order of the names, the tunes of a file together and in tune order.
    for (std::size_t i = 1; i < lib.size(); ++i) {
        const AbcLocation prev = split_abc_location(lib.state().items[i - 1].relative);
        const AbcLocation now = split_abc_location(lib.state().items[i].relative);
        CHECK(prev.file <= now.file);
    }
    CHECK(lib.state().items[0].relative == "c0.abc#X=1");
    CHECK(lib.state().items[2].relative == "c0.abc#X=10");
    CHECK(lib.state().items[3].relative == "c1.abc#X=1");
}

TEST_CASE("abc library: a song's place is found again after its file is split") {
    const fs::path dir = fresh_dir("etude_abc_library_place");
    write(dir / "a.abc", read_text("abc-dorian.abc"));
    write(dir / "b.abc", read_text("abc-collection.abc"));
    write(dir / "c.abc", read_text("abc-dorian.abc"));
    SongLibrary lib({dir});
    lib.scan();
    const fs::path c = dir / "c.abc";
    const std::size_t before = *lib.index_of(c);
    CHECK(before == 2);
    lib.load_all_now();
    CHECK(*lib.index_of(c) == 4); // two more entries sit before it now
    CHECK(lib.path_at(*lib.index_of(c)) == lib.path_at(4));
    CHECK(lib.index_of(dir / "b.abc", "X=2") == std::size_t{2});
}

TEST_CASE("abc library: addresses resolve to the file and the tune, and back") {
    const fs::path dir = fresh_dir("etude_abc_library_resolve");
    write(dir / "sub" / "collection.abc", read_text("abc-collection.abc"));
    write(dir / "one.abc", read_text("abc-dorian.abc"));
    SongLibrary lib({dir});
    lib.scan();
    REQUIRE(lib.size() == 2);
    const fs::path file = dir / "sub" / "collection.abc";
    // Before the worker has read it, the collection is its file.
    CHECK(lib.relative_path(file, "X=2") == "sub/collection.abc");
    CHECK(lib.index_of(file, "X=2") == std::size_t{1});
    lib.load_all_now();
    REQUIRE(lib.size() == 4);
    CHECK(lib.relative_path(file, "X=2") == "sub/collection.abc#X=2");
    CHECK(lib.relative_path(file) == "sub/collection.abc#X=1"); // no address: the first tune
    CHECK(lib.relative_path(dir / "one.abc") == "one.abc");
    CHECK(lib.relative_path(dir / "one.abc", "X=1") == "one.abc"); // a one-tune file is not split
    CHECK(lib.relative_path("/etc/hosts").empty());
    // The list is one.abc, then the three tunes of sub/collection.abc.
    CHECK(lib.index_of(file, "X=10") == std::size_t{3});
    CHECK(lib.index_of(file) == std::size_t{1});
    CHECK(lib.index_of(file, "X=7") == std::size_t{1}); // a tune the file lacks: the file's first
    CHECK(lib.index_of(dir / "one.abc", "X=1") == std::size_t{0});
    CHECK_FALSE(lib.index_of(dir / "nope.abc").has_value());
    // A path that is not spelled canonically (a symlink in the way, "..") finds the same song.
    CHECK(lib.index_of(dir / "sub" / ".." / "sub" / "collection.abc", "X=10") == std::size_t{3});

    const auto song = lib.resolve_song("sub/collection.abc#X=10");
    REQUIRE(song.has_value());
    CHECK(fs::equivalent(song->path, file));
    CHECK(song->tune == "X=10");
    // resolve() is the file alone, and a path without an address resolves as before.
    CHECK(fs::equivalent(*lib.resolve("sub/collection.abc#X=10"), file));
    CHECK(lib.resolve_song("one.abc")->tune.empty());
    CHECK_FALSE(lib.resolve_song("../one.abc#X=1").has_value());
    CHECK_FALSE(lib.resolve_song("missing.abc#X=1").has_value());
    CHECK_FALSE(lib.resolve_song("#X=1").has_value());
    // A saved "file.abc#X=3" resolves while the file is still one entry in the list.
    SongLibrary fresh({dir});
    fresh.scan();
    const auto early = fresh.resolve_song("sub/collection.abc#X=2");
    REQUIRE(early.has_value());
    CHECK(early->tune == "X=2");
    const SongFileLoad opened =
        load_song_file(early->path, SongLoadOptions{early->tune, false, {}});
    REQUIRE(opened.ok);
    CHECK(opened.song.title == "Scale Down");
}

TEST_CASE("abc library: a song path typed on the command line or saved in a session") {
    const fs::path dir = fresh_dir("etude_abc_library_paths");
    write(dir / "tunes.abc", read_text("abc-collection.abc"));
    write(dir / "odd#X=3.abc", read_text("abc-dorian.abc")); // a file really named like an address
    const SongPath plain = split_song_path((dir / "tunes.abc").generic_string());
    CHECK(plain.path == dir / "tunes.abc");
    CHECK(plain.tune.empty());
    const SongPath tune = split_song_path((dir / "tunes.abc").generic_string() + "#X=2");
    CHECK(tune.path == dir / "tunes.abc");
    CHECK(tune.tune == "X=2");
    const SongPath odd = split_song_path((dir / "odd#X=3.abc").generic_string());
    CHECK(odd.path == dir / "odd#X=3.abc");
    CHECK(odd.tune.empty());
    // A file named "x.abc#X=3" as written wins over reading it as an address.
    write(dir / "x.abc#X=3", read_text("abc-dorian.abc"));
    const SongPath literal = split_song_path((dir / "x.abc#X=3").generic_string());
    CHECK(literal.path == dir / "x.abc#X=3");
    CHECK(literal.tune.empty());

    // load_song_file takes the address in the path too (the tools do).
    const SongFileLoad second = load_song_file((dir / "tunes.abc").generic_string() + "#X=2");
    REQUIRE(second.ok);
    CHECK(second.song.title == "Scale Down");
    CHECK(second.format == SongFileFormat::Abc);
    SongLoadOptions options;
    options.tune = "X=10";
    const SongFileLoad tenth = load_song_file(dir / "tunes.abc", options);
    REQUIRE(tenth.ok);
    CHECK(tenth.song.title == "Arpeggio");
    const SongFileLoad first = load_song_file(dir / "tunes.abc");
    REQUIRE(first.ok);
    CHECK(first.song.title == "Scale Up");
    CHECK(first.song.song.notes.size() == 8);
    CHECK_FALSE(first.song.hash == tenth.song.hash);
    options.tune = "X=99";
    const SongFileLoad none = load_song_file(dir / "tunes.abc", options);
    CHECK_FALSE(none.ok);
    CHECK(none.error.find("X=99") != std::string::npos);
}

TEST_CASE("abc library: the song's hash is the tune's, so the history is the tune's") {
    const std::string text = read_text("abc-collection.abc");
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    SongLoadOptions options;
    options.tune = "X=2";
    const SongFileLoad a = load_song_bytes(bytes, "tunes.abc", options);
    REQUIRE(a.ok);
    // Another tune appended to the file, and the first one edited: tune 2 keeps its history.
    std::string edited = text;
    edited.replace(edited.find("CDEF|GABc"), 9, "CDEF|GABB");
    edited += "\nX:11\nT:New\nK:C\nC|]\n";
    const std::vector<std::uint8_t> bytes2(edited.begin(), edited.end());
    const SongFileLoad b = load_song_bytes(bytes2, "tunes.abc", options);
    REQUIRE(b.ok);
    CHECK(a.song.hash == b.song.hash);
    CHECK(song_key(a.song.hash, "tunes.abc#X=2") == song_key(b.song.hash, "elsewhere.abc#X=2"));
    // The first tune changed, so it is another song.
    options.tune = "X=1";
    CHECK(load_song_bytes(bytes, "tunes.abc", options).song.hash !=
          load_song_bytes(bytes2, "tunes.abc", options).song.hash);
}

TEST_CASE("abc library: chord symbols as a left hand are a load option and a library setting") {
    const std::string reel = read_text("abc-reel-repeats.abc");
    const std::vector<std::uint8_t> bytes(reel.begin(), reel.end());
    const SongFileLoad plain = load_song_bytes(bytes, "reel.abc");
    REQUIRE(plain.ok);
    SongLoadOptions on;
    on.abc_chords = true;
    const SongFileLoad chords = load_song_bytes(bytes, "reel.abc", on);
    REQUIRE(chords.ok);
    CHECK(chords.song.song.notes.size() > plain.song.song.notes.size());
    CHECK(chords.song.hash == plain.song.hash);
    CHECK(chords.entry.left_notes > 0);
    CHECK(plain.entry.left_notes == 0);
    CHECK(chords.entry.right_notes == plain.entry.right_notes + plain.entry.left_notes);
    int left = 0;
    for (const Note& n : chords.song.song.notes) {
        left += n.hand == Hand::Left ? 1 : 0;
    }
    CHECK(left == chords.entry.left_notes);

    const fs::path dir = fresh_dir("etude_abc_library_chords");
    write(dir / "reel.abc", reel);
    write(dir / "more.abc", read_text("abc-collection.abc"));
    SongLibrary lib({dir});
    lib.scan();
    lib.load_all_now();
    REQUIRE(lib.state().settled());
    const std::size_t reel_at = *lib.index_of(dir / "reel.abc");
    CHECK(lib.state().items[reel_at].entry.left_notes == 0);
    CHECK_FALSE(lib.abc_chords());
    // Turning the setting on does not wait for anything: poll() starts the new pass.
    lib.set_abc_chords(true);
    CHECK(lib.abc_chords());
    lib.wait_until_settled();
    CHECK(lib.state().settled());
    CHECK(lib.state().items[*lib.index_of(dir / "reel.abc")].entry.left_notes > 0);
    // Turning it off again comes back to the melody alone.
    lib.set_abc_chords(false);
    lib.wait_until_settled();
    CHECK(lib.state().items[*lib.index_of(dir / "reel.abc")].entry.left_notes == 0);
    // Setting it to what it is changes nothing.
    lib.set_abc_chords(false);
    CHECK_FALSE(lib.poll());
    CHECK(lib.state().pending() == 0);
}

TEST_CASE("abc library: a setting changed while the worker runs leaves no result of the old one") {
    const fs::path dir = fresh_dir("etude_abc_library_stale");
    for (int i = 0; i < 80; ++i) {
        write(dir / ("r" + std::to_string(i) + ".abc"), read_text("abc-reel-repeats.abc"));
        write(dir / ("t" + std::to_string(i) + ".abc"), read_text("abc-collection.abc"));
    }
    for (const bool flip_back : {false, true}) {
        SongLibrary lib({dir});
        lib.scan();
        lib.start_loading();
        lib.set_abc_chords(true); // at once, with the worker busy: returns without waiting for it
        if (flip_back) {
            lib.set_abc_chords(false);
            lib.set_abc_chords(true);
        }
        lib.wait_until_settled();
        REQUIRE(lib.state().settled());
        std::size_t with_left = 0;
        std::size_t reels = 0;
        for (const LibraryItem& item : lib.state().items) {
            REQUIRE(item.state == EntryState::Loaded);
            if (item.relative.rfind("r", 0) == 0) {
                ++reels;
                with_left += item.entry.left_notes > 0 ? 1 : 0;
            }
        }
        CHECK(reels == 80);
        CHECK(with_left == 80); // every reel was parsed under the new setting
    }
}

TEST_CASE("abc library: opening a listed tune reads its text, not the whole file again") {
    const fs::path dir = fresh_dir("etude_abc_library_known");
    const std::string text = read_text("abc-collection.abc");
    write(dir / "tunes.abc", text);
    SongLibrary lib({dir});
    lib.scan();
    CHECK_FALSE(lib.abc_known(dir / "tunes.abc", "X=2").has_value()); // not read yet
    lib.load_all_now();
    const auto known = lib.abc_known(dir / "tunes.abc", "X=2");
    REQUIRE(known.has_value());
    CHECK(known->tune.address == "X=2");
    CHECK(known->tune_count == 3);
    CHECK(known->file_size == text.size());
    CHECK(lib.abc_known(dir / "tunes.abc").has_value()); // the first tune
    CHECK_FALSE(lib.abc_known(dir / "tunes.abc", "X=3").has_value());
    CHECK_FALSE(lib.abc_known(dir / "nope.abc").has_value());
    // By the list's own item, with the key its listing made: the same answer.
    for (std::size_t i = 0; i < lib.size(); ++i) {
        const auto at = lib.abc_known_at(i);
        const auto by_path = lib.abc_known(lib.path_at(i), lib.tune_at(i));
        REQUIRE(at.has_value() == by_path.has_value());
        if (at) {
            CHECK(at->tune.address == by_path->tune.address);
        }
    }
    CHECK_FALSE(lib.abc_known_at(lib.size()).has_value());

    SongLoadOptions options;
    options.tune = "X=2";
    const SongFileLoad whole = load_song_file(dir / "tunes.abc", options);
    options.known = known;
    const SongFileLoad quick = load_song_file(dir / "tunes.abc", options);
    REQUIRE(whole.ok);
    REQUIRE(quick.ok);
    CHECK(quick.song.title == whole.song.title);
    CHECK(quick.song.hash == whole.song.hash);
    CHECK(quick.song.hash == known->tune.hash);
    REQUIRE(quick.song.song.notes.size() == whole.song.song.notes.size());
    for (std::size_t i = 0; i < quick.song.song.notes.size(); ++i) {
        CHECK(quick.song.song.notes[i].pitch == whole.song.song.notes[i].pitch);
        CHECK(quick.song.song.notes[i].start_tick == whole.song.song.notes[i].start_tick);
    }

    // Same size and time but another content: make the first tune's number the same as the
    // second's. Read whole, "X=2" is now the first tune.
    std::string tampered = text;
    const std::size_t at = tampered.find("X:1\n");
    REQUIRE(at != std::string::npos);
    tampered[at + 2] = '2';
    const auto time = fs::last_write_time(dir / "tunes.abc");
    write(dir / "tunes.abc", tampered);
    fs::last_write_time(dir / "tunes.abc", time);
    // The listed tune's own bytes are untouched and hash as listed, so it is still the one opened
    // through the listing; read whole it is the first tune of the file.
    options.known = known;
    CHECK(load_song_file(dir / "tunes.abc", options).song.title == "Scale Down");
    options.known.reset();
    CHECK(load_song_file(dir / "tunes.abc", options).song.title == "Scale Up");

    // A file that changed since it was listed is read whole: a new time, or a new size.
    write(dir / "tunes.abc", tampered);
    fs::last_write_time(dir / "tunes.abc", time + std::chrono::hours(1));
    options.known = known;
    CHECK(load_song_file(dir / "tunes.abc", options).song.title == "Scale Up");
    write(dir / "tunes.abc", tampered + "\n%\n");
    fs::last_write_time(dir / "tunes.abc", time);
    CHECK(load_song_file(dir / "tunes.abc", options).song.title == "Scale Up");
}

TEST_CASE("abc library: the listing is not trusted when the bytes read no longer hash to it") {
    const fs::path dir = fresh_dir("etude_abc_library_swapped");
    const std::string one = "X:1\nT:Aaa\nM:4/4\nL:1/4\nK:C\nCDEF|]\n\n";
    const std::string two = "X:2\nT:Bbbb\nM:4/4\nL:1/4\nK:C\nGABcdefg|]\n";
    const auto stamp = [&](const std::string& text) {
        const fs::path file = dir / "pair.abc";
        const auto time = fs::exists(file) ? fs::last_write_time(file) : fs::file_time_type::clock::now();
        write(file, text);
        fs::last_write_time(file, time);
    };
    stamp(one + two);
    SongLibrary lib({dir});
    lib.scan();
    lib.load_all_now();
    const auto known = lib.abc_known(dir / "pair.abc", "X=2");
    REQUIRE(known.has_value());
    const std::uint64_t listed_hash = known->tune.hash;

    SongLoadOptions options;
    options.tune = "X=2";
    options.known = known;
    const SongFileLoad before = load_song_file(dir / "pair.abc", options);
    REQUIRE(before.ok);
    CHECK(before.song.title == "Bbbb");
    CHECK(before.song.hash == listed_hash);

    // The two tunes change places, the file keeping its size and time. The range the listing has
    // for X=2 now holds other bytes: the tune is read whole, and plays under the hash of its text
    // as it is now.
    stamp(two + one);
    const std::string swapped = two + one;
    const std::vector<AbcTune> now = list_abc_tunes(
        reinterpret_cast<const std::uint8_t*>(swapped.data()), swapped.size());
    REQUIRE(now.size() == 2);
    const SongFileLoad after = load_song_file(dir / "pair.abc", options);
    REQUIRE(after.ok);
    CHECK(after.song.title == "Bbbb");
    CHECK(after.song.hash == now[0].hash); // X=2 is the first tune now
    CHECK(after.song.hash == listed_hash);  // its text is the same, so its history is too

    // The tunes swap their bodies, keeping their numbers and sizes: X=2 holds the other tune's music
    // at the very same place. The bytes read hash differently, so the new hash is used.
    stamp("X:1\nT:Bbbb\nM:4/4\nL:1/4\nK:C\nGABcdefg|]\n\nX:2\nT:Aaa\nM:4/4\nL:1/4\nK:C\nCDEF|]\n");
    const SongFileLoad body = load_song_file(dir / "pair.abc", options);
    REQUIRE(body.ok);
    CHECK(body.song.title == "Aaa");
    CHECK(body.song.hash != listed_hash);
    const std::string text = "X:1\nT:Bbbb\nM:4/4\nL:1/4\nK:C\nGABcdefg|]\n\nX:2\nT:Aaa\nM:4/4\nL:1/4\nK:C\nCDEF|]\n";
    const std::vector<AbcTune> bodies =
        list_abc_tunes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    REQUIRE(bodies.size() == 2);
    CHECK(body.song.hash == bodies[1].hash);
    // The entry made for it carries that hash too.
    CHECK(body.entry.hash == body.song.hash);
}

TEST_CASE("abc library: the status of a saved tune, and what start-up does about it") {
    const fs::path dir = fresh_dir("etude_abc_library_status");
    write(dir / "tunes.abc", read_text("abc-collection.abc"));
    write(dir / "other.abc", "X:1\nT:Other\nK:C\nC|]\n");
    SongLibrary lib({dir});
    lib.scan();
    CHECK(lib.abc_status(dir / "tunes.abc", "X=2") == AbcStatus::Pending);
    CHECK(lib.abc_status(dir / "nope.abc", "X=1") == AbcStatus::NotInLibrary);
    CHECK(lib.abc_status(dir / "song.mid") == AbcStatus::NotInLibrary);
    lib.load_all_now();
    CHECK(lib.abc_status(dir / "tunes.abc", "X=2") == AbcStatus::Listed);
    CHECK(lib.abc_status(dir / "tunes.abc") == AbcStatus::Listed);
    CHECK(lib.abc_status(dir / "tunes.abc", "X=99") == AbcStatus::NoSuchTune);
    CHECK(lib.abc_status(dir / "nope.abc", "X=1") == AbcStatus::NotInLibrary);

    AbcRestoreState s;
    s.abc = true;
    // Waiting: the file is listed but not read, and the worker has work.
    s.status = AbcStatus::Pending;
    s.library_pending = true;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::Wait);
    // Cancelled by opening another song.
    s.cancelled = true;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::Cancel);
    s.cancelled = false;
    // A listing that nothing will fill in any more cannot be waited for.
    s.library_pending = false;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::NotFound);
    // An empty library (or a file outside its folders, or one that is gone): restore from the file
    // itself, which says whether it is there.
    s.status = AbcStatus::NotInLibrary;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::Restore);
    s.library_pending = true;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::Restore);
    // The file is read and has no such tune.
    s.status = AbcStatus::NoSuchTune;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::NotFound);
    // Read and listed: bring it back.
    s.status = AbcStatus::Listed;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::Restore);
    // A scripted run, or a song that is not ABC, never waits.
    s.status = AbcStatus::Pending;
    s.scripted = true;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::Restore);
    s.scripted = false;
    s.abc = false;
    CHECK(decide_abc_restore(s) == AbcRestoreStep::Restore);
}

TEST_CASE("abc library: a file with more tunes than are listed says so, once") {
    const fs::path dir = fresh_dir("etude_abc_library_many");
    std::string text;
    for (std::size_t i = 1; i <= kMaxAbcTunes + 30; ++i) {
        text += "X:" + std::to_string(i) + "\nT:t" + std::to_string(i) + "\nK:C\nC|]\n\n";
    }
    write(dir / "big.abc", text);
    SongLibrary lib({dir});
    lib.scan();
    CHECK(lib.take_notices().empty());
    lib.load_all_now();
    CHECK(lib.size() == kMaxAbcTunes);
    CHECK(lib.state().loaded() == kMaxAbcTunes);
    CHECK(lib.state().items.back().relative == "big.abc#X=5000");
    CHECK(lib.state().items.back().entry.title == "t5000");
    const std::vector<std::string> notices = lib.take_notices();
    REQUIRE(notices.size() == 1);
    CHECK(notices[0] == "Only the first 5000 tunes of big.abc are listed");
    CHECK(lib.take_notices().empty());
}

TEST_CASE("abc library: a file asked for first is read first, and everything still loads") {
    const fs::path dir = fresh_dir("etude_abc_library_priority");
    for (int i = 0; i < 60; ++i) {
        write(dir / ("f" + std::to_string(100 + i) + ".abc"), read_text("abc-collection.abc"));
    }
    SongLibrary lib({dir});
    lib.scan();
    lib.start_loading();
    lib.prioritize(dir / "f159.abc");
    lib.prioritize(dir / "missing.abc"); // no job for it: ignored
    lib.wait_until_settled();
    CHECK(lib.size() == 180);
    CHECK(lib.state().loaded() == 180);
    CHECK(lib.abc_known(dir / "f159.abc", "X=2").has_value());
}

TEST_CASE("abc library: a damaged tune of a collection fails alone") {
    const fs::path dir = fresh_dir("etude_abc_library_damaged");
    write(dir / "mixed.abc",
          "X:1\nT:good\nK:C\nCDEF|]\n\nX:2\nT:empty\nK:C\nz8|]\n\nX:3\nT:also good\nK:G\nGABc|]\n");
    SongLibrary lib({dir});
    lib.scan();
    lib.load_all_now();
    REQUIRE(lib.size() == 3);
    CHECK(lib.state().items[0].state == EntryState::Loaded);
    CHECK(lib.state().items[1].state == EntryState::Failed);
    CHECK_FALSE(lib.state().items[1].error.empty());
    CHECK(lib.state().items[2].state == EntryState::Loaded);
    CHECK(lib.unreadable() == 1);
    // Stepping passes over the empty tune.
    CHECK(lib.step(std::size_t{0}, 1) == std::size_t{2});
}

TEST_CASE("abc library: a rescan keeps a split file split, and a changed file parses again") {
    const fs::path dir = fresh_dir("etude_abc_library_rescan");
    write(dir / "t.abc", read_text("abc-collection.abc"));
    SongLibrary lib({dir});
    CHECK(lib.scan() == 1);
    lib.load_all_now();
    CHECK(lib.state().loaded() == 3);
    // Nothing changed: the three tunes are listed at once, with their results, and nothing parses.
    CHECK(lib.scan() == 3);
    CHECK(lib.state().pending() == 0);
    CHECK(lib.state().loaded() == 3);
    CHECK(lib.state().items[1].entry.title == "Scale Down");
    // The file grows by one tune: it is one entry again, and the worker splits it into four. The
    // tunes whose text is unchanged are found in the cache by their hash.
    {
        std::ofstream more(dir / "t.abc", std::ios::binary | std::ios::app);
        more << "\nX:20\nT:Added\nK:C\nC|]\n";
    }
    CHECK(lib.scan() == 1);
    CHECK(lib.state().pending() == 1);
    lib.load_all_now();
    CHECK(lib.size() == 4);
    CHECK(lib.state().loaded() == 4);
    CHECK(lib.cache_hits() == 2); // tunes 1 and 2; the last gained a blank line, so it is new text
    // One tune becomes the only tune: the file is then listed under its own path.
    write(dir / "t.abc", "X:5\nT:Alone\nK:C\nC|]\n");
    CHECK(lib.scan() == 1);
    lib.load_all_now();
    CHECK(relatives(lib) == std::vector<std::string>{"t.abc"});
    CHECK(lib.state().items[0].entry.title == "Alone");
}

TEST_CASE("abc library: a file with no tune is listed and fails with a message") {
    const fs::path dir = fresh_dir("etude_abc_library_notune");
    write(dir / "notes.abc", "just some text about tunes\n");
    write(dir / "big.abc", std::string(2000, ' ') + "\n");
    SongLibrary lib({dir});
    CHECK(lib.scan() == 2);
    lib.load_all_now();
    CHECK(lib.state().failed() == 2);
    for (const LibraryItem& item : lib.state().items) {
        CHECK_FALSE(item.error.empty());
    }
}
