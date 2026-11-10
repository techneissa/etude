// SPDX-License-Identifier: GPL-3.0-or-later
// The song library: entries by content hash, song prefs, groups, the file mover,
// restoring a saved song by its hash, opening a song off the frame thread, and copying songs from a
// stick (a temporary folder) into the Inbox.
#include "../app/library_binding.hpp"
#include "../app/library_files.hpp"
#include "../app/song_library.hpp"
#include "../app/usb_import.hpp"
#include "../app/usb_scan.hpp"
#include "etude/content_hash.hpp"
#include "etude/library.hpp"
#include "etude/library_groups.hpp"
#include "etude/midi_file.hpp"
#include "etude/session_restore.hpp"
#include "etude/song_key.hpp"
#include "etude/song_prefs.hpp"
#include "library_row.hpp"
#include "song_opener.hpp"
#include "smf_builder.hpp"
#include "upload_test_support.hpp"
#include <algorithm>
#include <chrono>
#include <doctest/doctest.h>
#include <future>
#include <new>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace etude;
using etude::test::Bytes;
using etude::test::Track;
using namespace etude::app;
using namespace upload_test;

namespace {

SongEntry entry_of(const Bytes& bytes, std::string_view file_name = "song.mid") {
    LibraryLoad r = load_library_entry(bytes, file_name);
    REQUIRE(r.ok);
    return r.entry;
}

SongPrefEdit edit(std::uint64_t hash, SongPrefEdit::Field field, std::string text = {}, bool on = false) {
    SongPrefEdit e;
    e.hash = hash;
    e.field = field;
    e.text = std::move(text);
    e.on = on;
    return e;
}

GroupEdit group_edit(GroupEdit::Kind kind, std::uint64_t id, std::uint64_t hash, std::string name = {}, int position = 0) {
    GroupEdit e;
    e.kind = kind;
    e.group.id = id;
    e.group.name = std::move(name);
    e.group.position = position;
    e.hash = hash;
    return e;
}

// A read song with the facts the sort reads.
LibraryItem song(std::uint64_t hash, std::string title, int half_stars, bool left, int keys) {
    LibraryItem item;
    item.relative = title + ".mid";
    item.stamp = hash * 7 + 1;
    item.state = EntryState::Loaded;
    item.entry.hash = hash;
    item.entry.title = std::move(title);
    item.entry.half_stars = half_stars;
    item.entry.difficulty = level_of_half_stars(half_stars);
    item.entry.right_notes = 20;
    item.entry.left_notes = left ? 10 : 0;
    item.entry.right_keys = keys;
    item.entry.left_keys = left ? keys : 0;
    item.entry.bar_count = 24;
    item.entry.note_count = 40;
    return item;
}

struct Fixture {
    LibraryState library;
    std::vector<RunRecord> runs;
    LibraryCatalogue::Prefs prefs;
    LibraryCatalogue cat;

    LibraryRow row(const MelodyAdded& added = {}) {
        cat.library_changed();
        cat.prefs_changed();
        cat.history_changed();
        REQUIRE(cat.sync(library, runs, prefs));
        cat.refresh_versions(); // the groups of versions laid over the songs, as the Melodies page asks
        LibraryRow out;
        build_library_row(cat, added, out);
        return out;
    }
};

// The answer of the opener, polled the way the frame loop does (no clock, no sleep).
SongOpener::Opened take_answer(SongOpener& opener) {
    for (;;) {
        if (auto opened = opener.take()) {
            return std::move(*opened);
        }
        std::this_thread::yield();
    }
}

SongFileLoad fake_song(const fs::path& path) {
    SongFileLoad out;
    out.ok = true;
    out.song.title = path.filename().string();
    return out;
}


struct StickRig {
    TempDir stick;
    TempDir songs;
    ImportJob job() const {
        ImportJob j;
        j.stick = stick.path;
        j.songs_folder = songs.path;
        for (const StickSong& s : scan_stick(stick.path).songs) {
            j.songs.push_back(s.path);
        }
        return j;
    }
};

std::string tune_bytes(const std::string& tag) { return midi_bytes(tag); }

} // namespace

TEST_CASE("entry: hash, duration, notes per hand and pitch range; a file with no notes or not MIDI is refused") {
    Track right;
    right.name(0, "Right Hand").on(0, 0, 72).off(480, 0, 72).on(0, 0, 76).off(480, 0, 76).end();
    Track left;
    left.name(0, "Left Hand").on(0, 1, 40).off(960, 1, 40).end();
    Bytes file = etude::test::smf({Track().bpm(0, 120).end(), right, left});
    SongEntry e = entry_of(file, "Two_Hands.mid");
    CHECK(e.hash == content_hash(file.data(), file.size()));
    CHECK(e.title == "Two Hands");
    CHECK(e.title_key == "two hands");
    CHECK(e.duration_s == doctest::Approx(1.0));
    CHECK(e.note_count == 3);
    CHECK(e.right_notes == 2);
    CHECK(e.left_notes == 1);
    CHECK(e.unknown_notes == 0);
    CHECK(e.hands_known());
    CHECK(e.lowest_pitch == 40);
    CHECK(e.highest_pitch == 76);
    CHECK(e.bar_count >= 1);

    LibraryLoad empty = load_library_entry(etude::test::smf({Track().bpm(0, 120).end()}), "empty.mid");
    CHECK_FALSE(empty.ok);
    CHECK(empty.error == MidiFileError::NoNotes);
    CHECK(empty.message.find("no notes") != std::string::npos);
    LibraryLoad junk = load_library_entry(Bytes{1, 2, 3, 4}, "junk.mid"); // reported, never thrown
    CHECK_FALSE(junk.ok);
    CHECK(junk.error == MidiFileError::NotMidi);
}

TEST_CASE("content_hash: the FNV-1a 64 test vectors; one flipped bit, an appended byte or a swap changes it") {
    CHECK(content_hash("") == 0xcbf29ce484222325ULL);
    CHECK(content_hash("a") == 0xaf63dc4c8601ec8cULL);
    CHECK(content_hash("foobar") == 0x85944171f73967e8ULL);
    std::string song("MThd\x00\x00\x00\x06 song bytes", 20);
    REQUIRE(song.size() == 20);
    const std::uint64_t base = content_hash(song);
    std::string flipped = song;
    flipped[7] = static_cast<char>(flipped[7] ^ 0x01);
    CHECK(content_hash(flipped) != base);
    CHECK(content_hash(song + '\0') != base);
    CHECK(content_hash("ab") != content_hash("ba"));
}

TEST_CASE("song prefs: edits held before the store was read keep what it holds") {
    // A favourite tapped while the history loads must not wipe the stored name and composer.
    const std::unordered_map<std::uint64_t, SongPrefs> stored{{7, SongPrefs{"Gymnopédie 1", "Erik Satie", "Classical", false, 500}},
                                                              {8, SongPrefs{"", "", "", true, 600}}};
    const std::vector<SongPrefEdit> held{edit(7, SongPrefEdit::Field::Favourite, {}, true), edit(9, SongPrefEdit::Field::Title, "New one"),
                                         edit(7, SongPrefEdit::Field::Title, "Gymnopédie No. 1"),
                                         edit(8, SongPrefEdit::Field::Favourite, {}, false), edit(0, SongPrefEdit::Field::Title, "no hash")};
    const auto rows = apply_edits(stored, held);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].first == 7);
    CHECK(rows[0].second == SongPrefs{"Gymnopédie No. 1", "Erik Satie", "Classical", true, 500});
    CHECK(rows[1].first == 9);
    CHECK(rows[1].second == SongPrefs{"New one", "", "", false, 0});
    CHECK(rows[2].first == 8);
    CHECK(rows[2].second == SongPrefs{"", "", "", false, 600});
}

TEST_CASE("groups: edits made before the store was read go over what it held") {
    // One stored group with two songs. Meanwhile the player made a group, added a song to the stored one and took another out.
    const std::vector<SongGroup> stored{{5, "Exam pieces", GroupKind::User, "", 0, 100}, {6, "Mutopia", GroupKind::Catalogue, "mutopia", 1, 90}};
    const std::vector<LibraryGroups::Member> members{{5, 0x1, 10}, {5, 0x2, 20}, {6, 0x3, 5}, {9, 0x4, 1}}; // 9: no such group
    std::vector<GroupEdit> edits{group_edit(GroupEdit::Kind::Create, 7, 0, "Sight reading", 2), group_edit(GroupEdit::Kind::Add, 5, 0x3),
                                 group_edit(GroupEdit::Kind::Drop, 5, 0x1), group_edit(GroupEdit::Kind::Add, 7, 0x2)};
    LibraryGroups g;
    const std::uint64_t before = g.version();
    g.replay(stored, members, edits);
    CHECK(g.version() != before);
    std::vector<std::string> names;
    for (const SongGroup& s : g.groups()) {
        names.push_back(s.name);
    }
    CHECK(names == std::vector<std::string>{"Exam pieces", "Mutopia", "Sight reading"});
    CHECK(g.find(6)->kind == GroupKind::Catalogue);
    CHECK(g.find(6)->source == "mutopia");
    const std::span<const std::uint64_t> exam = g.songs_of(5);
    CHECK(std::vector<std::uint64_t>(exam.begin(), exam.end()) == std::vector<std::uint64_t>{0x2, 0x3});
    CHECK(g.songs_of(9).empty());
}

TEST_CASE("library row: an added song whose file was edited is found by its path and follows its new hash") {
    Fixture f;
    f.library.items = {song(1, "A", 3, false, 8), song(44, "D", 3, false, 8)}; // D was hash 4
    MelodyAdded added;
    added.set(4, 2, "D.mid");
    const LibraryRow row = f.row(added);
    REQUIRE(row.followed.size() == 1);
    CHECK(row.followed[0].old_hash == 4);
    CHECK(row.followed[0].new_hash == 44);
    CHECK(row.followed[0].path == "D.mid");
    CHECK(row.hidden.empty());
    REQUIRE(row.songs.size() == 2); // it keeps the step the player chose
    CHECK(row.songs[1].hash == 44);
    CHECK(row.songs[1].added);
    CHECK(row.songs[1].step == 2);
    MelodyAdded followed = added; // the list follows (what the screen does with `followed`)
    CHECK(followed.rebind(4, 44, "D.mid"));
    const LibraryRow again = f.row(followed);
    CHECK(again.followed.empty());
    REQUIRE(again.songs.size() == 2);
    CHECK(again.songs[1].hash == 44);
    CHECK(again.songs[1].step == 2);
}

TEST_CASE("library row: an added song with neither its hash nor its path in the library is hidden, and counts for nothing") {
    Fixture f;
    f.library.items = {song(1, "A", 3, false, 8)};
    MelodyAdded added;
    added.set(4, 2, "D.mid");
    added.set(6, 3); // no path to find it by
    const LibraryRow row = f.row(added);
    CHECK(row.hidden == std::vector<std::uint64_t>{4, 6});
    CHECK(row.songs.size() == 1); // only A
    CHECK(hidden_added(f.cat, added) == std::vector<std::uint64_t>{4, 6});
    f.library.items.push_back(song(4, "D", 3, false, 8)); // the file comes back under its old hash: it shows again, where it was
    const LibraryRow back = f.row(added);
    CHECK(back.hidden == std::vector<std::uint64_t>{6});
    REQUIRE(back.songs.size() == 2);
    CHECK(back.songs[1].hash == 4);
    CHECK(back.songs[1].step == 2);
    CHECK(back.songs[1].added);
}

TEST_CASE("move_into keeps the name, numbers it when the folder has one, and never writes over a file") {
    TempDir t;
    const fs::path dir = t.path;
    write_file(dir / "a" / "song.mid", "one");
    write_file(dir / "b" / "song.mid", "two");
    std::string error;
    const MoveDone moved = move_into(dir / "a" / "song.mid", dir / "b", &error);
    CHECK(error.empty());
    CHECK(moved.outcome == MoveOutcome::Moved);
    CHECK(moved.now == dir / "b" / "song (2).mid");
    CHECK_FALSE(fs::exists(dir / "a" / "song.mid"));
    CHECK(read_file(dir / "b" / "song.mid") == "two"); // the file that was there is untouched
    CHECK(read_file(dir / "b" / "song (2).mid") == "one");
    CHECK(move_into(dir / "b" / "song.mid", dir / "c" / "d", &error).now == dir / "c" / "d" / "song.mid"); // a folder not there is made
    const MoveDone gone = move_into(dir / "nothing.mid", dir / "b", &error); // a failure with a reason, nothing changes
    CHECK(gone.outcome == MoveOutcome::Failed);
    CHECK(gone.now.empty());
    CHECK_FALSE(error.empty());
    const MoveDone there = move_into(dir / "b" / "song (2).mid", dir / "b", &error); // already in the folder: not a move
    CHECK(there.outcome == MoveOutcome::AlreadyThere);
    CHECK(there.now == dir / "b" / "song (2).mid");

    write_file(dir / "a" / "x.mid", "new");
    for (const char* name : {"x.mid", "x (2).mid", "x (3).mid"}) {
        write_file(dir / "b" / name, name);
    }
    const MoveDone done = move_into(dir / "a" / "x.mid", dir / "b", &error);
    REQUIRE(done.outcome == MoveOutcome::Moved);
    CHECK(done.now == dir / "b" / "x (4).mid");
    for (const char* name : {"x.mid", "x (2).mid", "x (3).mid"}) {
        CHECK(read_file(dir / "b" / name) == name);
    }
    CHECK(read_file(dir / "b" / "x (4).mid") == "new");
}

TEST_CASE("the file mover runs its jobs off the caller's thread and reports each once") {
    TempDir t;
    const fs::path dir = t.path;
    write_file(dir / "Songs" / "x.mid");
    write_file(dir / "Songs" / "y.mid");
    LibraryFiles files;
    CHECK_FALSE(files.busy());
    FileJob job;
    job.kind = FileJob::Kind::Remove;
    job.files = {{dir / "Songs" / "x.mid", dir / "Removed songs"}, {dir / "Songs" / "gone.mid", dir / "Removed songs"}};
    files.request(job);
    CHECK(files.busy());
    files.wait_idle();
    const std::optional<FileResult> r = files.take();
    REQUIRE(r.has_value());
    CHECK(r->kind == FileJob::Kind::Remove);
    CHECK(r->moved == 1);
    CHECK(r->failed == 1);
    CHECK(r->skipped == 0);
    CHECK_FALSE(r->error.empty());
    CHECK(fs::exists(dir / "Removed songs" / "x.mid"));
    CHECK_FALSE(files.take().has_value());
    CHECK_FALSE(files.busy());
    FileJob again; // a song already in the folder it is moved to is passed over, not counted as moved
    again.files = {{dir / "Songs" / "y.mid", dir / "Songs"}};
    files.request(again);
    files.wait_idle();
    const std::optional<FileResult> same = files.take();
    REQUIRE(same.has_value());
    CHECK(same->moved == 0);
    CHECK(same->skipped == 1);
}

TEST_CASE("song key: a rename or a move is the same song, edited bytes are another") {
    const std::string key = song_key(0x1234abcdULL, "Songs/for-elise.mid");
    CHECK(key == "h:000000001234abcd");
    CHECK(song_key(0x1234abcdULL, "Songs/elsewhere/elise.mid") == key);
    CHECK(song_key(0x1234abceULL, "Songs/for-elise.mid") != key);
    CHECK(song_key(~0ULL, "x") == "h:ffffffffffffffff"); // all 64 bits count
}

TEST_CASE("restore: the saved song is the copy whose content hash matches, else it is reported changed") {
    TempDir t;
    const fs::path dir = t.path;
    fs::create_directories(dir / "library");
    fs::create_directories(dir / "elsewhere");
    const std::string bach = std::string(ETUDE_TEST_DATA_DIR) + "/bach-bwv846-prelude.mid";
    const std::string satie = std::string(ETUDE_TEST_DATA_DIR) + "/satie-gymnopedie-1.mid";
    const fs::path library = dir / "library" / "tune.mid";
    const fs::path absolute = dir / "elsewhere" / "tune.mid";
    const auto put = [](const std::string& from, const fs::path& to) { fs::copy_file(from, to, fs::copy_options::overwrite_existing); };
    SessionState saved;
    saved.song_path = "tune.mid";
    saved.song_abs_path = absolute.generic_string();
    saved.song_hash = load_song_file(bach).song.hash;
    SavedSongRestore::Request request;
    request.saved = saved;
    request.library = SongPath{library, {}};
    request.absolute = split_song_path(saved.song_abs_path);
    request.options = [](const SongPath&) { return SongLoadOptions{}; };
    const auto restore = [&] {
        SongOpener opener;
        SavedSongRestore r;
        r.begin(opener, request);
        for (int frame = 0; frame < 5000; ++frame) {
            if (auto result = r.poll(opener)) {
                return *result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        FAIL("the restore never answered");
        return SavedSongRestore::Result{};
    };
    put(satie, library); // the library's file changed since it was saved
    put(bach, absolute);
    SavedSongRestore::Result result = restore();
    CHECK(result.source == SongSource::Absolute);
    CHECK(result.probe.hash_matches);
    put(satie, absolute); // neither copy has the saved bytes
    result = restore();
    CHECK(result.source == SongSource::Library);
    CHECK_FALSE(result.probe.hash_matches);
    CHECK(plan_restore(saved, result.probe.found, result.probe.hash_matches, &result.load.song.song).outcome == RestoreOutcome::SongChanged);
}

TEST_CASE("library: the open song and a developer's song never move, '..' stays inside Songs, a delete keeps its folders") {
    TempDir t;
    const fs::path dir = t.path;
    const fs::path songs = dir / "Songs";
    const fs::path dev = dir / "dev";
    const fs::path data(ETUDE_TEST_DATA_DIR);
    fs::create_directories(songs);
    fs::create_directories(dev);
    fs::copy_file(data / "edge-pickup.mid", songs / "pickup.mid");
    fs::copy_file(data / "satie-gymnopedie-1.mid", songs / "satie.mid");
    fs::copy_file(data / "edge-fast-32nds.mid", dev / "fast.mid");
    SongLibrary library({songs, dev});
    library.scan();
    library.load_all_now();
    HistoryOptions options;
    options.sync_commits = false;
    options.log = [](std::string_view) {};
    HistoryStore history(dir, options);
    history.open();
    LibraryBinding binding(library, history, dir / "Removed songs");
    int frame = 0;
    // Frames of the loop until `done`; the library's rescan and the file jobs finish by themselves.
    const auto frames_until = [&](auto done) {
        do {
            const bool changed = library.poll();
            history.poll();
            binding.update(changed, 2'000'000'000);
            binding.catalogue().update_view(20000, static_cast<double>(++frame));
        } while (!done());
    };
    frames_until([&] { return !history.loading() && binding.catalogue().size() == 3; });
    const auto index = [&](std::string_view relative) {
        const std::optional<std::size_t> i = binding.catalogue().index_of(relative);
        REQUIRE(i.has_value());
        return *i;
    };
    const std::size_t fast = index("fast.mid");
    const std::size_t pickup = index("pickup.mid");
    CHECK_FALSE(binding.why_not_movable(fast).empty());
    CHECK(binding.why_not_movable(pickup).empty());
    binding.set_current(pickup);
    CHECK_FALSE(binding.why_not_movable(pickup).empty()); // the open song
    binding.set_current(std::nullopt);

    const std::size_t both[] = {pickup, fast};
    binding.move_songs(both, " Book 2/../ ");
    frames_until([&] { return binding.catalogue().index_of("Book 2/pickup.mid").has_value() && !binding.files_busy(); });
    CHECK(fs::exists(songs / "Book 2" / "pickup.mid"));
    CHECK(fs::exists(dev / "fast.mid"));
    CHECK_FALSE(fs::exists(dir / "pickup.mid"));

    const std::size_t moved = index("Book 2/pickup.mid");
    binding.remove_songs(std::span(&moved, 1));
    frames_until([&] { return binding.catalogue().size() == 2 && !binding.files_busy(); });
    CHECK(fs::exists(dir / "Removed songs" / "Book 2" / "pickup.mid"));
    CHECK_FALSE(fs::exists(songs / "Book 2" / "pickup.mid"));
}

TEST_CASE("opening: the answer of a load that was replaced while it ran is dropped") {
    std::promise<void> started;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    SongOpener opener([&](const fs::path& path, const SongLoadOptions&) {
        if (path.filename() == "old.mid") {
            started.set_value();
            released.wait();
        }
        return fake_song(path);
    });
    opener.request("old.mid", {});
    started.get_future().wait(); // the loader is inside the old load
    opener.request("new.mid", {});
    release.set_value();
    CHECK(take_answer(opener).path == "new.mid"); // never the older song
    CHECK_FALSE(opener.take().has_value());
}

TEST_CASE("opening: a load that throws is reported with the file's name, and the next one still loads") {
    SongOpener opener([](const fs::path& path, const SongLoadOptions&) -> SongFileLoad {
        if (path.filename() == "huge.mid") {
            throw std::bad_alloc();
        }
        return fake_song(path);
    });
    opener.request("/songs/huge.mid", {});
    const SongOpener::Opened failed = take_answer(opener);
    CHECK_FALSE(failed.load.ok);
    CHECK(failed.load.error.find("huge.mid") != std::string::npos);
    opener.request("b.mid", {});
    CHECK(take_answer(opener).load.ok);
}

TEST_CASE("usb import: songs from the stick land in the Inbox, names kept") {
    StickRig rig;
    write_file(rig.stick.path / "one.mid", tune_bytes("1"));
    write_file(rig.stick.path / "Lessons/two.mid", tune_bytes("2"));
    std::vector<std::string> seen;
    ImportHooks hooks;
    hooks.on_song = [&seen](std::size_t i, ui::SongMark mark) {
        seen.push_back(std::to_string(i) + (mark == ui::SongMark::None ? "start" : "done"));
    };
    const ImportResult r = import_songs(rig.job(), hooks);
    CHECK(r.end == ui::ImportEnd::Done);
    CHECK(r.copied == 2);
    CHECK(r.duplicates == 0);
    CHECK(r.failed == 0);
    CHECK(read_file(rig.songs.path / "Inbox/one.mid") == tune_bytes("1"));
    CHECK(read_file(rig.songs.path / "Inbox/two.mid") == tune_bytes("2")); // the Inbox is flat
    CHECK(seen == std::vector<std::string>{"0start", "0done", "1start", "1done"});
    CHECK(tree(rig.songs.path / "Inbox/.incoming").empty()); // no temporary file left
}

TEST_CASE("usb import: a song already in the library, by content, is skipped") {
    StickRig rig;
    write_file(rig.stick.path / "same.mid", tune_bytes("A"));
    write_file(rig.stick.path / "renamed.mid", tune_bytes("B"));
    write_file(rig.stick.path / "new.mid", tune_bytes("C"));
    // The library has A under another name in a collection, and B's size with other bytes.
    write_file(rig.songs.path / "Collection/old name.mid", tune_bytes("A"));
    write_file(rig.songs.path / "Collection/other.mid", tune_bytes("Z"));
    const ImportResult r = import_songs(rig.job(), {});
    CHECK(r.copied == 2);
    CHECK(r.duplicates == 1);
    CHECK(r.marks[2] == ui::SongMark::Duplicate); // same.mid: new.mid, renamed.mid, same.mid
    CHECK(tree(rig.songs.path / "Inbox") == ".incoming/\nnew.mid\nrenamed.mid\n");
}

TEST_CASE("usb import: a taken name becomes name (2)") {
    StickRig rig;
    write_file(rig.stick.path / "tune.mid", tune_bytes("new"));
    write_file(rig.songs.path / "Inbox/tune.mid", tune_bytes("old")); // another song, same name
    const ImportResult r = import_songs(rig.job(), {});
    CHECK(r.copied == 1);
    CHECK(read_file(rig.songs.path / "Inbox/tune.mid") == tune_bytes("old"));
    CHECK(read_file(rig.songs.path / "Inbox/tune (2).mid") == tune_bytes("new"));
}

TEST_CASE("usb import: the stick is only read") {
    StickRig rig;
    write_file(rig.stick.path / "a.mid", tune_bytes("1"));
    write_file(rig.stick.path / "sub/b.mid", tune_bytes("2"));
    const std::string before = tree(rig.stick.path);
    struct stat a{};
    struct stat sub{};
    REQUIRE(::stat((rig.stick.path / "a.mid").c_str(), &a) == 0);
    REQUIRE(::stat((rig.stick.path / "sub").c_str(), &sub) == 0);
    // A read-only stick: any attempt to write or make a file on it would fail the copy.
    fs::permissions(rig.stick.path / "sub", fs::perms::owner_read | fs::perms::owner_exec);
    fs::permissions(rig.stick.path, fs::perms::owner_read | fs::perms::owner_exec);
    fs::permissions(rig.stick.path / "a.mid", fs::perms::owner_read);
    const ImportResult r = import_songs(rig.job(), {});
    fs::permissions(rig.stick.path, fs::perms::owner_all);
    fs::permissions(rig.stick.path / "sub", fs::perms::owner_all);
    CHECK(r.copied == 2);
    CHECK(tree(rig.stick.path) == before);
    struct stat a2{};
    REQUIRE(::stat((rig.stick.path / "a.mid").c_str(), &a2) == 0);
    CHECK(a2.st_mtime == a.st_mtime);
    CHECK(read_file(rig.stick.path / "a.mid") == tune_bytes("1"));
}

TEST_CASE("usb import: a link on the stick is not followed") {
    StickRig rig;
    TempDir secret;
    write_file(secret.path / "secret.mid", tune_bytes("S"));
    fs::create_symlink(secret.path / "secret.mid", rig.stick.path / "linked.mid");
    ImportJob job;
    job.stick = rig.stick.path;
    job.songs_folder = rig.songs.path;
    job.songs = {"linked.mid"};
    const ImportResult r = import_songs(job, {});
    CHECK(r.end == ui::ImportEnd::Done); // the stick is there: the song alone failed
    CHECK(r.copied == 0);
    CHECK(r.failed == 1);
    CHECK_FALSE(fs::exists(rig.songs.path / "Inbox/linked.mid"));
}

TEST_CASE("usb import: a folder swapped for a link after the scan does not lead out of the stick") {
    StickRig rig;
    TempDir outside;
    write_file(outside.path / "secret.mid", tune_bytes("S"));
    write_file(rig.stick.path / "Lessons/secret.mid", tune_bytes("L"));
    write_file(rig.stick.path / "a/b/deep.mid", tune_bytes("D"));
    const ImportJob job = rig.job(); // scanned: Lessons/secret.mid, a/b/deep.mid
    fs::remove_all(rig.stick.path / "Lessons");
    fs::create_symlink(outside.path, rig.stick.path / "Lessons"); // a link now, same name
    fs::remove_all(rig.stick.path / "a/b");
    fs::create_symlink(outside.path, rig.stick.path / "a/b"); // a middle folder too
    write_file(outside.path / "deep.mid", tune_bytes("X"));
    const ImportResult r = import_songs(job, {});
    CHECK(r.end == ui::ImportEnd::Done);
    CHECK(r.copied == 0);
    CHECK(r.failed == 2);
    CHECK(tree(rig.songs.path / "Inbox").find(".mid") == std::string::npos);
}

TEST_CASE("usb import: a FIFO where a song was does not hold the copy") {
    StickRig rig;
    write_file(rig.stick.path / "a.mid", tune_bytes("A"));
    write_file(rig.stick.path / "b.mid", tune_bytes("B"));
    const ImportJob job = rig.job();
    fs::remove(rig.stick.path / "a.mid");
    REQUIRE(::mkfifo((rig.stick.path / "a.mid").c_str(), 0600) == 0);
    auto done = std::async(std::launch::async, [&] { return import_songs(job, {}); });
    REQUIRE(done.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    const ImportResult r = done.get();
    CHECK(r.failed == 1);
    CHECK(r.copied == 1); // b.mid still comes in
    CHECK(r.marks[0] == ui::SongMark::Failed);
}

TEST_CASE("usb import: two songs that the hash cannot tell apart are told apart by their bytes") {
    StickRig rig;
    write_file(rig.stick.path / "a.mid", tune_bytes("1234"));
    write_file(rig.songs.path / "Collection/other.mid", tune_bytes("5678")); // same size, other bytes
    write_file(rig.songs.path / "Collection/same.mid", tune_bytes("1234"));
    write_file(rig.stick.path / "b.mid", tune_bytes("5679")); // same size as both, differs from both
    ImportHooks hooks;
    hooks.hash = [](std::uint64_t, const char*, std::size_t) { return std::uint64_t{7}; };
    const ImportResult r = import_songs(rig.job(), hooks);
    CHECK(r.marks[0] == ui::SongMark::Duplicate); // a.mid is in the library
    CHECK(r.marks[1] == ui::SongMark::Copied);    // b.mid is not, though every hash matches
    CHECK(read_file(rig.songs.path / "Inbox/b.mid") == tune_bytes("5679"));
}

TEST_CASE("stick scan: hidden files and folders, system folders and links are not entered") {
    TempDir stick;
    write_file(stick.path / ".hidden.mid", midi_bytes());
    write_file(stick.path / "._a.mid", midi_bytes());
    write_file(stick.path / ".Trashes/x.mid", midi_bytes());
    write_file(stick.path / "System Volume Information/x.mid", midi_bytes());
    write_file(stick.path / "real.mid", midi_bytes());
    TempDir outside;
    write_file(outside.path / "out.mid", midi_bytes());
    fs::create_symlink(outside.path, stick.path / "link");
    fs::create_symlink(outside.path / "out.mid", stick.path / "linked.mid");
    const StickScan scan = scan_stick(stick.path);
    REQUIRE(scan.songs.size() == 1);
    CHECK(scan.songs[0].path == "real.mid");
}
