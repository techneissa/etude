// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: runs, songs by content hash, profiles and their deletion, a busy
// or second-instance database, damaged and newer files, migrations, the one-time VACUUM, and the
// journal a quit keeps when the database cannot be written (replayed once, never over newer data).
#include "history_store.hpp"

#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"
#include "runs_file.hpp"
#include "sqlite_db.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

using namespace etude;
using namespace etude::app;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path =
            fs::temp_directory_path() / ("etude-history-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_all(const fs::path& p, const std::string& bytes) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << bytes;
}

// The store's log lines go nowhere.
HistoryOptions quiet() {
    HistoryOptions o;
    o.log = [](std::string_view) {};
    o.sync_commits = false;
    return o;
}

// The lines a store logs (the callback runs on the history thread).
struct Log {
    std::mutex mutex;
    std::vector<std::string> lines_;
    HistoryOptions options() {
        HistoryOptions o;
        o.sync_commits = false;
        o.log = [this](std::string_view line) {
            std::lock_guard lock(mutex);
            lines_.emplace_back(line);
        };
        return o;
    }
    std::vector<std::string> get() {
        std::lock_guard lock(mutex);
        return lines_;
    }
    bool has(std::string_view needle) {
        for (const std::string& line : get()) {
            if (line.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

// Starts the store and waits for the history thread to settle; returns what the player is told.
std::string open_now(HistoryStore& store) {
    store.open();
    REQUIRE(store.wait_until_settled(10s));
    return store.poll().message;
}

// A store that is open and loaded (quiet unless given options).
struct Opened : HistoryStore {
    explicit Opened(const fs::path& dir, HistoryOptions options = quiet()) : HistoryStore(dir, std::move(options)) {
        REQUIRE(open_now(*this).empty());
    }
};

// A flush that cannot finish (the file is locked or read-only). Returns once the history thread
// has tried and failed `attempts` times (`failed_attempts()`): 1 is the queued write, 2 a retry.
bool flush_fails(HistoryStore& store, int attempts = 1) {
    const bool flushed = store.flush(1ms);
    for (int i = 0; i < 20000 && store.failed_attempts() < attempts; ++i) {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(store.failed_attempts() >= attempts); // a hang guard
    return flushed;
}

// A second connection to look at what is really in the file.
struct Raw {
    sql::Database db;
    explicit Raw(const fs::path& path) {
        REQUIRE(db.open(path) == SQLITE_OK);
        db.set_busy_timeout(2000);
    }
    std::int64_t num(std::string_view query) {
        std::int64_t v = -1;
        REQUIRE(db.query_int(query, v) == SQLITE_OK);
        return v;
    }
    std::string text(std::string_view query) {
        std::string v;
        REQUIRE(db.query_text(query, v) == SQLITE_OK);
        return v;
    }
    std::vector<std::string> column(std::string_view query) {
        sql::Statement s;
        REQUIRE(s.prepare(db.get(), query) == SQLITE_OK);
        std::vector<std::string> out;
        while (s.step() == SQLITE_ROW) {
            out.push_back(s.column_text(0));
        }
        return out;
    }
    void run(const std::string& script) { REQUIRE(db.run_script(script) == SQLITE_OK); }
};

// A finished run of song `song`, the n-th one: its own start second, some bars.
RunRecord sample_run(int n, std::uint64_t song = 0xabc0) {
    RunRecord r;
    r.song_hash = song;
    r.song_title = "Song " + std::to_string(song);
    r.mode = n % 2 == 0 ? RunMode::Flow : RunMode::Wait;
    r.hands = n % 3 == 0 ? HandFilter::Both : HandFilter::Right;
    r.speed_percent = 80;
    r.transpose_semitones = n % 5 - 2;
    r.start_utc_s = 1'760'000'000 + n * 100;
    r.end_utc_s = r.start_utc_s + 60;
    r.local_day = 20373 + n / 3;
    r.duration_ms = 55'000;
    r.counts = {10 + n, 4, 1, 2, 2, 0};
    r.wrong = 3;
    r.unplayable = 1;
    r.longest_combo = 7;
    r.clean_passes = 1;
    r.right = {12 + n, 10 + n};
    r.left = {5, 4};
    r.unknown = {0, 0};
    r.all = {17 + n, 14 + n};
    for (int b = 0; b < 6; ++b) {
        r.measures.push_back({b, 4, b % 3, b % 2});
    }
    return sanitized(r);
}

// A run at one setup: the prune keeps the best run of each setup besides the newest ones.
RunRecord one_setup(RunRecord r) {
    r.mode = RunMode::Flow;
    r.hands = HandFilter::Both;
    r.transpose_semitones = 0;
    return sanitized(r);
}

// Events of a run over `notes` notes, one press each 500 ms apart, the third missed, and one wrong
// press: every kind there is.
RunEvents sample_events(int notes = 6, bool complete = true) {
    RunEvents e;
    e.expected_notes = notes;
    e.complete = complete;
    for (int i = 0; i < notes; ++i) {
        RunEvent ev;
        ev.note_index = i;
        ev.key = static_cast<std::uint8_t>(60 + i);
        ev.hand = i % 2 == 0 ? Hand::Right : Hand::Left;
        if (i == 2) {
            ev.kind = RunEventKind::Missed;
            ev.song_ms = i * 500;
            ev.grade = NoteGrade::Missed;
        } else {
            ev.kind = RunEventKind::Press;
            ev.song_ms = i * 500 + 7;
            ev.error_ms = i == 1 ? -23 : 11;
            ev.velocity = static_cast<std::uint8_t>(70 + i);
            ev.grade = i == 1 ? NoteGrade::Good : NoteGrade::Perfect;
        }
        e.events.push_back(ev);
    }
    RunEvent wrong;
    wrong.kind = RunEventKind::Wrong;
    wrong.key = 61;
    wrong.velocity = 33;
    wrong.song_ms = 1200;
    e.events.push_back(wrong);
    std::sort(e.events.begin(), e.events.end(),
              [](const RunEvent& a, const RunEvent& b) { return a.song_ms < b.song_ms; });
    return e;
}

// A practice choice with every field set, to see each one come back.
SongPractice busy_practice() {
    SongPractice p;
    p.speed_percent = 65;
    p.count_in_bars = 2;
    p.hands = HandFilter::Left;
    p.transpose_semitones = -3;
    p.parts_set = true;
    p.muted_parts = {0x102, 0x305};
    p.loop_start_s = 12.5;
    p.loop_end_s = 20.25;
    p.loop_enabled = true;
    p.section_id = 4;
    p.plan_item = 2;
    p.no_shift_offer = true;
    p.split_point = 60;
    return p;
}

GroupEdit group_edit(GroupEdit::Kind kind, std::uint64_t id, std::string name = {},
                     std::uint64_t hash = 0) {
    GroupEdit e;
    e.kind = kind;
    e.group.id = id;
    e.group.name = std::move(name);
    e.hash = hash;
    e.at_utc_s = 50;
    return e;
}

SongOrigin sample_origin() {
    SongOrigin o;
    o.catalogue = "piano-midi";
    o.id = "midis/chopin/chpn_op10_e01.mid";
    o.title = "Etudes";
    o.composer = "Chopin";
    o.licence = "CC BY-SA 3.0 DE";
    o.licence_url = "https://creativecommons.org/licenses/by-sa/3.0/de/deed.en";
    o.credit = "Bernd Krueger";
    o.url = "https://www.piano-midi.de/midis/chopin/chpn_op10_e01.mid";
    o.page = "https://www.piano-midi.de/chopin.htm";
    o.downloaded_utc_s = 1'760'000'000;
    return o;
}

std::int64_t now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const std::size_t kLatestVersion = static_cast<std::size_t>(history_migrations().back().version);
const fs::path kJournal = "history.journal";
constexpr std::string_view kJunk =
    "junk that is not a database, but long enough to be read as one.....";

// Every migration this build has, then one more step.
std::vector<Migration> with_next(std::string_view sql) {
    std::vector<Migration> out(history_migrations().begin(), history_migrations().end());
    out.push_back({static_cast<int>(kLatestVersion) + 1, sql});
    return out;
}

std::int64_t run_id_at(Raw& raw, int n) {
    return raw.num("SELECT id FROM runs WHERE start_utc_s = " +
                   std::to_string(sample_run(n).start_utc_s));
}

std::int64_t runs_in(const fs::path& db) {
    Raw raw(db);
    return raw.num("SELECT COUNT(*) FROM runs");
}

// The answer to a request or a pin, once the history thread has it.
EventsRead read_answer(HistoryStore& store, std::uint64_t serial) {
    REQUIRE(serial != 0);
    REQUIRE(store.flush());
    for (EventsRead& r : store.take_event_reads()) {
        if (r.serial == serial) {
            return std::move(r);
        }
    }
    FAIL("no answer for the request");
    return {};
}

std::optional<StoredRunEvents> events_of(HistoryStore& store, std::uint64_t serial) {
    return read_answer(store, serial).found;
}

bool pin_took(HistoryStore& store, std::uint64_t serial) {
    const EventsRead r = read_answer(store, serial);
    CHECK(r.kind == EventsReadKind::Pin);
    CHECK_FALSE(r.found.has_value());
    return r.pin_took;
}

void make_writable(const fs::path& dir, bool writable) {
    for (const char* name : {"history.db", "history.db-wal", "history.db-shm"}) {
        std::error_code ec; // the -wal and -shm files are there only while a store has it open
        fs::permissions(dir / name,
                        writable ? fs::perms::owner_read | fs::perms::owner_write
                                 : fs::perms::owner_read,
                        fs::perm_options::replace, ec);
    }
}

// Root writes to read-only files, so the cases that rely on one are skipped there.
bool running_as_root() {
    if (::geteuid() != 0) {
        return false;
    }
    MESSAGE("skipped: root writes to read-only files");
    return true;
}

// The database has run 1 and then goes read-only. A store opens it, is given two runs (the second
// with played notes), a song's practice choices and, with `with_library`, a star, none of which it
// can write, and is closed with them held. Returns what it reported.
std::vector<std::string> quit_with_held_jobs(const fs::path& dir, bool with_library = false,
                                             bool journal_is_a_folder = false) {
    {
        Opened seed(dir);
        seed.add_run(sample_run(1));
    }
    make_writable(dir, false);
    if (journal_is_a_folder) {
        fs::create_directory(dir / kJournal);
    }
    HistoryOptions options = quiet();
    options.retry_interval_ms = 30;
    HistoryStore store(dir, options);
    open_now(store);
    REQUIRE(store.state() == HistoryState::Ready);
    store.add_run(sample_run(2));
    store.add_run(sample_run(3), "/songs/a.mid", sample_events(4));
    store.set_song_practice(0x77, busy_practice());
    if (with_library) {
        SongPrefs star;
        star.favourite = true;
        store.set_song_prefs(0x77, star);
    }
    CHECK_FALSE(flush_fails(store)); // the writes find the file read-only
    CHECK(store.state() == HistoryState::Waiting);
    store.close();
    return store.take_errors();
}

// Opens the folder as the next start does.
void check_landed(const fs::path& dir, std::size_t runs_expected, bool quiet_start = true) {
    HistoryStore store(dir, quiet());
    CHECK(open_now(store).empty() == quiet_start);
    CHECK(store.state() == HistoryState::Ready);
    CHECK(store.runs().size() == runs_expected);
    CHECK(runs_in(store.path()) == static_cast<std::int64_t>(runs_expected));
}

// A record frame around `payload`, as the writer frames one.
std::string framed_record(const std::string& payload) {
    std::string out;
    for (const std::uint32_t v :
         {static_cast<std::uint32_t>(payload.size()),
          static_cast<std::uint32_t>(content_hash(payload) & 0xFFFFFFFFu)}) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
        }
    }
    return out + payload;
}

// A practice record of the first layout (no split, no time), field by field from the layout in
// history_journal.hpp and not through the encoder: every build since has read it.
std::string first_layout_practice_record() {
    std::string p = "P";
    for (int i = 0; i < 8; ++i) {
        p.push_back(i == 0 ? 0x77 : 0); // content hash 0x77
    }
    for (const int byte : {40 << 1,    // speed_percent 40 (zigzag)
                           1 << 1,     // count_in_bars 1
                           1,          // hands: Left
                           2 << 1,     // transpose 2
                           1,          // parts_set
                           1, 0x05,    // one muted part, key 5
                           0, 0, 0,    // loop start, loop end, loop_enabled
                           0, 0, 0}) { // section_id, plan_item, no_shift_offer
        p.push_back(static_cast<char>(byte));
    }
    return framed_record(p);
}

// The practice record as an older build wrote it, without its last `dropped` fields (1: the time,
// 2: the split too).
std::string practice_record_without(std::size_t dropped) {
    JournalPractice practice;
    practice.hash = 0x77;
    practice.practice = busy_practice();
    const std::string full = encode_journal_entry(practice);
    return framed_record(full.substr(8, full.size() - 8 - dropped - 1));
}

// A database file as version `version` left it: the first `version` migrations.
void build_at_version(const fs::path& file, int version) {
    sql::Database db;
    REQUIRE(db.open(file) == SQLITE_OK);
    for (const Migration& m : history_migrations()) {
        if (m.version > version) {
            break;
        }
        for (const std::string& step :
             {std::string("BEGIN IMMEDIATE"), std::string(m.sql),
              "PRAGMA user_version = " + std::to_string(m.version), std::string("COMMIT")}) {
            REQUIRE(db.run_script(step) == SQLITE_OK);
        }
    }
}

HistoryOptions on_profile(std::int64_t profile) {
    HistoryOptions options = quiet();
    options.profile_id = profile;
    return options;
}

void write_runs(const fs::path& dir, int count) {
    Opened store(dir);
    for (int i = 0; i < count; ++i) {
        store.add_run(sample_run(i));
    }
}

// A database file that another connection holds exclusively locked, as a program mid-write does.
struct HeldLock {
    Raw other;
    explicit HeldLock(const fs::path& db) : other(db) {
        other.run("CREATE TABLE theirs (x INTEGER); BEGIN EXCLUSIVE");
    }
    void release() { other.run("ROLLBACK"); }
};

} // namespace

// ---- Runs and songs

TEST_CASE(
    "runs: written runs come back equal and in order after a restart, a repeat is stored once") {
    TempDir dir;
    std::vector<RunRecord> written;
    {
        Opened store(dir.path);
        for (int i = 0; i < 7; ++i) {
            written.push_back(
                sample_run(i, i % 2 == 0 ? 0xabc0 : ~0ULL - 5)); // a hash with the top bit
            store.add_run(written.back(), "Songs/x.mid");
        }
        store.add_run(written[3]); // the same song, second, mode, hands, speed and transposition
        // No flush: queued runs are saved when the store is destroyed.
    }
    Opened again(dir.path);
    REQUIRE(again.runs().size() == written.size());
    for (std::size_t i = 0; i < written.size(); ++i) {
        CHECK(again.runs()[i] == written[i]);
    }
    CHECK(runs_in(again.path()) == 7);
}

TEST_CASE("runs: a renamed song file keeps its history under one song row, keyed by its hash") {
    TempDir dir;
    Opened store(dir.path);
    store.add_run(sample_run(1, 0x5151), "Songs/elise.mid");
    store.add_run(sample_run(2, 0x5151), "Songs/Beethoven/elise-renamed.mid");
    store.add_run(sample_run(3, 0x5152), "Songs/Beethoven/elise-renamed.mid"); // edited: new bytes
    store.flush();
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM songs") == 2);
    CHECK(raw.num("SELECT COUNT(DISTINCT song_id) FROM runs WHERE song_hash = " +
                  std::to_string(0x5151)) == 1);
    CHECK(raw.text("SELECT path FROM songs WHERE song_key = 'h:" + content_hash_hex(0x5151) +
                   "'") == "Songs/Beethoven/elise-renamed.mid");
}

TEST_CASE("runs: a runs.dat moves in once; moving it again adds nothing; a file that is not one is "
          "kept") {
    TempDir dir;
    const std::vector<RunRecord> old_runs{sample_run(0), sample_run(1), sample_run(2)};
    std::string newer = serialize_run_record(sample_run(9));
    newer[2] = 9; // a record a newer build wrote: skipped
    const std::string bytes =
        encode_runs({serialize_run_record(old_runs[0]), newer, serialize_run_record(old_runs[1]),
                     serialize_run_record(old_runs[2])});
    for (int pass = 0; pass < 3;
         ++pass) { // 2: the rename was lost, the same file again; 3: garbage
        write_all(dir.path / "runs.dat", pass == 2 ? "garbage, not runs" : bytes);
        Opened store(dir.path);
        REQUIRE(store.runs().size() == 3);
        CHECK(store.runs()[2] == old_runs[2]);
        CHECK_FALSE(fs::exists(dir.path / "runs.dat"));
    }
    CHECK(read_all(dir.path / "runs.dat.bak") == bytes); // never overwritten
    CHECK(read_all(dir.path / "runs.dat.bak.1") == bytes);
    CHECK(read_all(dir.path / "runs.dat.bak.2") == "garbage, not runs");
}

TEST_CASE("runs: a run's played notes are saved with it, and a prune keeps pinned and the newest") {
    TempDir dir;
    {
        Opened store(dir.path);
        store.add_run(sample_run(1), "a.mid", sample_events());
        store.add_run(sample_run(2)); // no notes
        REQUIRE(store.flush());
    }
    Opened store(dir.path);
    Raw raw(store.path());
    const auto first = events_of(store, store.request_run_events(run_id_at(raw, 1)));
    REQUIRE(first.has_value());
    CHECK(first->events == sample_events());
    CHECK_FALSE(events_of(store, store.request_run_events(run_id_at(raw, 2))).has_value());

    store.set_events_kept_per_song(1);
    store.add_run(one_setup(sample_run(3)), "a.mid", sample_events());
    REQUIRE(store.flush());
    CHECK(pin_took(store, store.set_run_pinned(run_id_at(raw, 3), true)));
    store.add_run(one_setup(sample_run(4)), "a.mid", sample_events());
    store.add_run(one_setup(sample_run(5)), "a.mid", sample_events());
    REQUIRE(store.flush());
    // Run 1 is the best of its own setup; of the rest, run 3 is pinned and run 5 the newest.
    CHECK(raw.column("SELECT run_id FROM run_event_sets ORDER BY run_id") ==
          std::vector<std::string>{std::to_string(run_id_at(raw, 1)),
                                   std::to_string(run_id_at(raw, 3)),
                                   std::to_string(run_id_at(raw, 5))});
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 5); // the summaries all stay
    CHECK_FALSE(
        pin_took(store, store.set_run_pinned(run_id_at(raw, 4), true))); // pruned: did not take
}

TEST_CASE("songs: a song's settings, practice, groups, source and versions are kept by its hash") {
    TempDir dir;
    SongPrefs star;
    star.title = "Mine";
    star.favourite = true;
    {
        Opened store(dir.path);
        store.set_song_prefs(0x55, star);
        store.set_song_practice(0x55, busy_practice());
        store.edit_groups(group_edit(GroupEdit::Kind::Create, 11, "Recital"));
        store.edit_groups(group_edit(GroupEdit::Kind::Add, 11, {}, 0x55));
        store.edit_groups(group_edit(GroupEdit::Kind::Add, 11, {}, 0x56));
        store.edit_groups(group_edit(GroupEdit::Kind::Drop, 11, {}, 0x56));
        store.set_song_origin(0xFEDCBA9876543210ULL, sample_origin());
        store.edit_version_overrides({VersionEdit::Kind::Split, 0xAAULL, 0});
        store.edit_version_overrides({VersionEdit::Kind::Join, 0xB2ULL, 0xB1ULL});
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    Opened again(dir.path);
    CHECK(again.song_prefs().at(0x55).title == "Mine");
    CHECK(again.song_prefs().at(0x55).favourite);
    CHECK(again.song_practice().at(0x55) == busy_practice());
    CHECK(again.groups().contains(11, 0x55));
    CHECK_FALSE(again.groups().contains(11, 0x56));
    CHECK(again.song_origins().at(0xFEDCBA9876543210ULL) == sample_origin());
    CHECK(again.version_overrides().split(0xAA));
    CHECK(again.version_overrides().joined(0xB1));
}

// ---- Profiles

TEST_CASE("profiles: each profile keeps its own runs and practice, the library is the device's") {
    TempDir dir;
    SongPractice slow;
    slow.speed_percent = 50;
    slow.muted_parts = {0x102};
    slow.parts_set = true;
    {
        Opened o(dir.path);
        o.add_run(sample_run(1));
        o.set_song_practice(0x77, slow);
        SongPrefs star;
        star.favourite = true;
        o.set_song_prefs(0x55, star);
        REQUIRE(o.add_profile("Anna") == ProfileResult::Ok);
        REQUIRE(o.switch_profile(2));
        CHECK(o.runs().empty());
        CHECK(o.song_practice().empty()); // Anna starts on the defaults
        CHECK(o.song_prefs().at(0x55).favourite);
        o.add_run(sample_run(2));
        // A run that ended just after the switch is saved under the profile it was played on.
        o.add_run(sample_run(3), "a.mid", RunEvents{}, kDefaultProfileId);
        o.set_profile_settings(2, "[app]\nbar_style = dock\n");
        REQUIRE(o.flush());
        Raw raw(o.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE profile_id = 1") == 2);
        CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE profile_id = 2") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM song_settings WHERE profile_id <> 1") == 0);
    }
    {
        Opened anna(dir.path, on_profile(2));
        REQUIRE(anna.runs().size() == 1);
        CHECK(anna.runs()[0] == sample_run(2));
        CHECK(anna.profile_settings(2) == "[app]\nbar_style = dock\n");
        CHECK(anna.profile_settings(kDefaultProfileId).empty());
        REQUIRE(anna.switch_profile(kDefaultProfileId));
        CHECK(anna.runs().size() == 2);
        CHECK(anna.song_practice().at(0x77).muted_parts == std::vector<std::uint32_t>{0x102});
    }
    HistoryStore lost(dir.path, on_profile(9)); // a profile the database lacks
    open_now(lost);
    CHECK(lost.active_profile() == kDefaultProfileId);
}

namespace {

RunRecord run_at(int n, std::int64_t at) {
    RunRecord r = sample_run(n);
    r.start_utc_s = now_s() + 10'000 + at;
    r.end_utc_s = r.start_utc_s + 60;
    return r;
}

// One row for `id` in each table the store does not fill for other profiles (the library's own
// lists are the first profile's), so the deletion has something to remove from every table.
void seed_other_tables(Raw& raw, std::int64_t id) {
    const std::string p = std::to_string(id);
    raw.run("INSERT INTO song_settings (profile_id, content_hash, title) VALUES (" + p +
            ", 77, 'x');"
            "INSERT INTO song_groups (profile_id, group_id, name, kind, source, position, "
            "created_utc_s) "
            "VALUES (" +
            p +
            ", 5, 'g', 0, '', 0, 1);"
            "INSERT INTO song_group_members (profile_id, group_id, content_hash, added_utc_s) "
            "VALUES (" +
            p +
            ", 5, 77, 1);"
            "INSERT INTO version_overrides (profile_id, hash_a, hash_b) VALUES (" +
            p + ", 77, 78)");
}

std::int64_t rows_of(Raw& raw, const std::string& table, std::int64_t id) {
    return raw.num("SELECT COUNT(*) FROM " + table + " WHERE profile_id = " + std::to_string(id));
}

bool delete_and_poll(HistoryStore& store, std::int64_t id) {
    REQUIRE(store.delete_profile(id) == ProfileResult::Ok);
    REQUIRE(store.flush());
    for (const ProfileDeleted& d : store.poll().deleted) {
        if (d.id == id) {
            return d.done;
        }
    }
    FAIL("no report of the deletion");
    return false;
}

} // namespace

TEST_CASE("delete profile: the first and the only profile, an unknown id and an unloaded history "
          "are refused") {
    TempDir dir;
    HistoryStore early(dir.path, quiet());
    CHECK(early.delete_profile(2) == ProfileResult::NotReady);
    Opened o(dir.path);
    CHECK(o.delete_profile(kDefaultProfileId) == ProfileResult::LastProfile);
    REQUIRE(o.add_profile("Anna") == ProfileResult::Ok);
    CHECK(o.delete_profile(kDefaultProfileId) == ProfileResult::FirstProfile);
    CHECK(o.delete_profile(99) == ProfileResult::NoSuchProfile);
    CHECK(o.profiles().size() == 2);
}

TEST_CASE(
    "delete profile: every table with a profile is emptied for that profile and for no other") {
    TempDir dir;
    {
        Opened o(dir.path);
        REQUIRE(o.add_profile("Anna") == ProfileResult::Ok); // 2
        REQUIRE(o.add_profile("Ben") == ProfileResult::Ok);  // 3
        for (const std::int64_t id : {1, 2, 3}) {
            o.add_run(run_at(static_cast<int>(id), id * 100), "a.mid", sample_events(4), id);
            o.set_profile_settings(id, "[app]\nbar_style = dock\n");
        }
        for (const std::int64_t id : {2, 3, 1}) {
            REQUIRE(o.switch_profile(id));
            o.set_song_practice(0x77, busy_practice());
        }
        REQUIRE(o.flush());
        Raw raw(o.path());
        for (const std::int64_t id : {1, 2, 3}) {
            seed_other_tables(raw, id);
        }
        // Every table that keeps rows by profile, found from the file: one a later migration adds
        // must be added to the deletion, and fails here until it is.
        std::vector<std::string> tables;
        for (const std::string& table :
             raw.column("SELECT name FROM sqlite_master WHERE type = 'table' "
                        "AND name NOT LIKE 'sqlite_%' ORDER BY name")) {
            if (raw.num("SELECT COUNT(*) FROM pragma_table_info('" + table +
                        "') WHERE name = 'profile_id'") > 0) {
                tables.push_back(table);
            }
        }
        CHECK(tables == std::vector<std::string>{"profile_settings", "runs", "song_group_members",
                                                 "song_groups", "song_practice",
                                                 "song_practice_parts", "song_settings",
                                                 "version_overrides"});
        std::vector<std::int64_t> before;
        for (const std::string& t : tables) {
            for (const std::int64_t id : {1, 2, 3}) {
                before.push_back(rows_of(raw, t, id));
            }
        }
        CHECK(delete_and_poll(o, 2));
        std::size_t at = 0;
        for (const std::string& t : tables) {
            for (const std::int64_t id : {1, 2, 3}) {
                INFO(t << " of profile " << id);
                CHECK(rows_of(raw, t, id) == (id == 2 ? 0 : before[at]));
                ++at;
            }
        }
        CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 2);
        CHECK(raw.num(
                  "SELECT COUNT(*) FROM run_measures WHERE run_id NOT IN (SELECT id FROM runs)") ==
              0);
        CHECK(
            raw.num(
                "SELECT COUNT(*) FROM run_event_sets WHERE run_id NOT IN (SELECT id FROM runs)") ==
            0);
    }
    Opened later(dir.path);
    CHECK_FALSE(later.profiles().contains(2));
    CHECK(later.run_count_all() == 2);
}

TEST_CASE("delete profile: a failure part-way puts every row back, and a run ended after it is "
          "nobody's") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.add_profile("Anna") == ProfileResult::Ok);
    o.add_run(run_at(1, 100), "a.mid", sample_events(4), 2);
    o.set_profile_settings(2, "[app]\nbar_style = dock\n");
    REQUIRE(o.flush());
    Raw raw(o.path());
    seed_other_tables(raw, 2);
    // The deletion gets as far as the runs and the groups before this refuses: they come back.
    raw.run("CREATE TRIGGER refuse BEFORE DELETE ON profile_settings BEGIN SELECT RAISE(ABORT, "
            "'no'); END");
    REQUIRE(o.delete_profile(2) == ProfileResult::Ok);
    REQUIRE(o.flush());
    CHECK(rows_of(raw, "runs", 2) == 1);
    CHECK(rows_of(raw, "song_groups", 2) == 1);
    CHECK(rows_of(raw, "profile_settings", 2) == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM run_measures WHERE run_id IN (SELECT id FROM runs WHERE "
                  "profile_id = 2)") > 0);
    const std::vector<ProfileDeleted> reported = o.poll().deleted;
    REQUIRE(reported.size() == 1);
    CHECK_FALSE(reported[0].done);
    CHECK(o.profiles().contains(2));
    CHECK(o.run_count_all() == 1);
    CHECK(o.take_errors().size() == 1);
    raw.run("DROP TRIGGER refuse");
    // The run in progress ends after the deletion was queued: it is saved under nobody.
    REQUIRE(o.delete_profile(2) == ProfileResult::Ok);
    o.add_run(run_at(2, 200), "a.mid", RunEvents{}, 2);
    REQUIRE(o.flush());
    CHECK(o.poll().deleted.at(0).done);
    CHECK_FALSE(o.profiles().contains(2));
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 0);
}

TEST_CASE(
    "delete profile: a deletion held at quit is made at the next start, and a run of the deleted "
    "profile in the journal is dropped on every replay") {
    if (running_as_root()) {
        return;
    }
    TempDir dir;
    {
        Opened seed(dir.path);
        REQUIRE(seed.add_profile("Anna") == ProfileResult::Ok);
        seed.add_run(run_at(1, 100), "a.mid", RunEvents{}, 2);
        REQUIRE(seed.flush());
    }
    make_writable(dir.path, false);
    {
        HistoryOptions options = quiet();
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        open_now(store);
        REQUIRE(store.delete_profile(2) == ProfileResult::Ok);
        CHECK_FALSE(flush_fails(store)); // the file is read-only
        store.close();
        CHECK(store.take_errors().empty());
    }
    const std::string journal = read_all(dir.path / kJournal);
    CHECK(std::get<JournalProfileDelete>(decode_journal(journal).entries.at(0)).profile_id == 2);
    make_writable(dir.path, true);
    // A record that names the first profile is damage, not a deletion.
    CHECK(decode_journal(encode_journal_header() +
                         encode_journal_entry(JournalProfileDelete{kDefaultProfileId}))
              .damaged == 1);
    JournalRun late; // a run of Anna's behind her deletion
    late.record = run_at(2, 200);
    late.profile_id = 2;
    for (int pass = 0; pass < 3; ++pass) { // and a copy that came back deletes nothing more
        write_all(dir.path / kJournal, pass == 0 ? journal : journal + encode_journal_entry(late));
        Opened o(dir.path);
        CHECK(o.profiles().size() == 1);
        CHECK(o.run_count_all() == 0);
        CHECK(runs_in(o.path()) == 0);
    }
}

// ---- Busy databases and a second etude

TEST_CASE("busy: a database someone else holds is never moved; runs wait in memory and are saved "
          "when it frees") {
    TempDir dir;
    HeldLock held(dir.path / "history.db");
    Log log;
    HistoryOptions options = log.options();
    options.open_busy_timeout_ms = 20;
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    CHECK(open_now(store).find("busy") != std::string::npos);
    CHECK(store.state() == HistoryState::Waiting);
    store.add_run(sample_run(1));
    store.add_run(sample_run(2));
    CHECK(store.runs().size() == 2);    // this session still has them
    CHECK_FALSE(flush_fails(store, 2)); // they cannot be saved yet, and a retry fails too
    CHECK(log.get().size() == 1);       // told once however many retries
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    held.release();
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 2);
    CHECK(raw.num("SELECT COUNT(*) FROM theirs") == 0); // their table is untouched
}

TEST_CASE("busy: a write that finds the database locked is held and retried, once, in order") {
    TempDir dir;
    Log log;
    HistoryOptions options = log.options();
    options.write_busy_timeout_ms = 20;
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1));
    REQUIRE(store.flush(10s));
    Raw raw(store.path());
    raw.run("BEGIN IMMEDIATE"); // another connection is writing
    store.add_run(sample_run(2));
    store.add_run(sample_run(3));
    CHECK_FALSE(flush_fails(store, 2)); // the write failed, and so did a retry
    CHECK(log.get().size() == 1);
    CHECK(store.take_errors().empty()); // held back is not a failure
    raw.run("COMMIT");
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    CHECK(raw.column("SELECT start_utc_s FROM runs ORDER BY id") ==
          std::vector<std::string>{std::to_string(sample_run(1).start_utc_s),
                                   std::to_string(sample_run(2).start_utc_s),
                                   std::to_string(sample_run(3).start_utc_s)});
}

TEST_CASE("busy: a second etude on the same folder reads the history and saves nothing") {
    TempDir dir;
    Opened first(dir.path);
    first.add_run(sample_run(1));
    first.add_run(sample_run(2));
    REQUIRE(first.add_profile("Anna") == ProfileResult::Ok);
    REQUIRE(first.flush());
    {
        HistoryStore second(dir.path, quiet());
        CHECK(open_now(second).find("another Etude") != std::string::npos);
        CHECK(second.state() == HistoryState::ReadOnly);
        CHECK(second.runs().size() == 2); // it can read what the first one saved
        second.add_run(sample_run(3));
        CHECK(second.flush());
        CHECK(second.delete_profile(2) == ProfileResult::NotSaved); // and queues nothing
        CHECK(second.profiles().contains(2));
    }
    first.add_run(sample_run(4));
    REQUIRE(first.flush());
    Raw raw(first.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE start_utc_s = " +
                  std::to_string(sample_run(3).start_utc_s)) == 0);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);

    // A second etude never creates a database the first has not made.
    for (const char* name : {"history.db", "history.db-wal", "history.db-shm"}) {
        fs::remove(dir.path / name);
    }
    HistoryStore third(dir.path, quiet());
    open_now(third);
    CHECK(third.state() == HistoryState::ReadOnly);
    CHECK_FALSE(fs::exists(dir.path / "history.db"));
}

TEST_CASE("busy: a run that cannot be saved is reported and leaves no half-written run") {
    TempDir dir;
    Opened store(dir.path);
    store.add_run(sample_run(1));
    store.flush();
    Raw(store.path()).run("DROP TABLE run_measures"); // the schema broke
    store.add_run(sample_run(2));
    store.flush();
    CHECK(store.take_errors().size() == 1);
    CHECK(store.runs().size() == 2); // this session still has it
    CHECK(runs_in(store.path()) == 1);
}

// ---- Damaged, foreign and newer files

TEST_CASE(
    "damaged: a file that is not a database, overwritten pages or a cut file are set aside whole") {
    for (const int kind : {0, 1, 2}) {
        CAPTURE(kind);
        TempDir dir;
        std::string bytes(kJunk);
        if (kind > 0) {
            write_runs(dir.path, 60);
            bytes = read_all(dir.path / "history.db");
            if (kind == 1) {
                std::fill(bytes.begin() + 4096, bytes.begin() + 4096 + 20'000, '\xAB');
            } else {
                bytes.resize(bytes.size() / 3);
            }
        }
        write_all(dir.path / "history.db", bytes);
        write_all(dir.path / "history.db-wal", std::string(4096, '\x01')); // stale files beside it
        Log log;
        HistoryStore store(dir.path, log.options());
        CHECK(open_now(store).find("history.db.damaged") != std::string::npos);
        CHECK(store.state() == HistoryState::Ready);
        store.add_run(sample_run(1));
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        CHECK(log.get().size() == 1);
        CHECK(read_all(dir.path / "history.db.damaged") == bytes);
        CHECK(runs_in(store.path()) == 1);
    }
    // A second damaged file keeps the first copy and takes the next name.
    TempDir dir;
    for (const char* junk : {"first junk, long enough to be a bad header, not a database",
                             "second junk, long enough to be a bad header, no database"}) {
        write_all(dir.path / "history.db", junk);
        HistoryStore store(dir.path, quiet());
        CHECK_FALSE(open_now(store).empty());
    }
    CHECK(read_all(dir.path / "history.db.damaged").rfind("first", 0) == 0);
    CHECK(read_all(dir.path / "history.db.damaged.1").rfind("second", 0) == 0);
}

TEST_CASE("damaged: a database that goes corrupt during a write is set aside and the runs go to a "
          "new one") {
    TempDir dir;
    Log log;
    HistoryStore store(dir.path, log.options());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1));
    REQUIRE(store.flush());
    {
        // Break the schema under the open store: its next statement reads it and finds it corrupt.
        Raw raw(store.path());
        const std::int64_t cookie = raw.num("PRAGMA schema_version");
        raw.run("PRAGMA writable_schema = ON");
        raw.run("UPDATE sqlite_master SET rootpage = 99999 WHERE name = 'run_measures'");
        raw.run("PRAGMA writable_schema = OFF");
        raw.run("PRAGMA schema_version = " + std::to_string(cookie + 5));
    }
    store.add_run(sample_run(2));
    store.add_run(sample_run(3));
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    CHECK(store.take_errors().empty()); // nothing was dropped
    CHECK(fs::exists(dir.path / "history.db.damaged"));
    CHECK(log.get().size() == 1);
    CHECK(runs_in(store.path()) == 2); // the held run and the one queued behind it
}

TEST_CASE("damaged: when it cannot be moved aside, or every name is taken, nothing is deleted or "
          "overwritten") {
    TempDir dir;
    write_all(dir.path / "history.db", std::string(kJunk));
    for (int i = 0; i < 8; ++i) {
        write_all(dir.path /
                      (i == 0 ? "history.db.damaged" : "history.db.damaged." + std::to_string(i)),
                  "old " + std::to_string(i));
    }
    {
        HistoryOptions options = quiet();
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        open_now(store);
        CHECK(store.state() == HistoryState::Waiting);
    }
    CHECK(read_all(dir.path / "history.db") == kJunk);
    CHECK(read_all(dir.path / "history.db.damaged") == "old 0");
    CHECK(read_all(dir.path / "history.db.damaged.7") == "old 7");

    TempDir blocked;
    write_all(blocked.path / "history.db", std::string(kJunk));
    fs::create_directories(blocked.path / "history.db.damaged-wal" / "keep"); // cannot be replaced
    write_all(blocked.path / "history.db-wal", "wal bytes");
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    {
        HistoryStore store(blocked.path, options);
        open_now(store);
        store.add_run(sample_run(1));
        CHECK(store.runs().size() == 1);
        CHECK_FALSE(flush_fails(store));
    }
    CHECK(read_all(blocked.path / "history.db") == kJunk); // moved back, not lost
    CHECK(fs::exists(blocked.path / "history.db.damaged-wal" / "keep"));
    CHECK(log.get().at(0).find("could not be moved aside") != std::string::npos);
}

TEST_CASE(
    "foreign: a file with a table of ours by name that is not ours is never moved or retried") {
    TempDir dir;
    Raw(dir.path / "history.db").run("CREATE TABLE profiles (other_column TEXT)");
    const std::string before = read_all(dir.path / "history.db");
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    CHECK_FALSE(open_now(store).empty());
    CHECK(store.state() == HistoryState::Unavailable);
    store.add_run(sample_run(1));
    CHECK(store.flush(300ms)); // nothing is retried
    CHECK(store.runs().size() == 1);
    CHECK(log.get().size() == 1);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(read_all(dir.path / "history.db") == before);
}

TEST_CASE(
    "newer: a database from a newer etude comes out byte for byte, in rollback and in WAL mode") {
    for (const bool wal : {false, true}) {
        CAPTURE(wal);
        TempDir dir;
        TempDir made;
        {
            // The newer build's connection stays open so a WAL file still holds its work; the files
            // are copied out from under it.
            sql::Database newer;
            REQUIRE(newer.open(made.path / "history.db") == SQLITE_OK);
            if (wal) {
                std::string mode;
                REQUIRE(newer.query_text("PRAGMA journal_mode = WAL", mode) == SQLITE_OK);
                newer.set_checkpoint_on_close(false);
                REQUIRE(newer.run_script("PRAGMA wal_autocheckpoint = 0") == SQLITE_OK);
            }
            REQUIRE(
                newer.run_script("CREATE TABLE future (x INTEGER); INSERT INTO future VALUES (7); "
                                 "PRAGMA user_version = 99") == SQLITE_OK);
            for (const char* name : {"history.db", "history.db-wal"}) {
                if (fs::exists(made.path / name)) {
                    fs::copy_file(made.path / name, dir.path / name);
                }
            }
        }
        const std::string db_before = read_all(dir.path / "history.db");
        const std::string wal_before = wal ? read_all(dir.path / "history.db-wal") : std::string();
        Log log;
        {
            HistoryStore store(dir.path, log.options());
            CHECK(open_now(store).find("newer") != std::string::npos);
            CHECK(store.state() == HistoryState::Unavailable);
            store.add_run(sample_run(1)); // kept for this session, not written
            store.flush();
            CHECK(store.runs().size() == 1);
            store.close();
        }
        CHECK(log.get().size() == 1);
        CHECK(read_all(dir.path / "history.db") == db_before);
        CHECK((wal ? read_all(dir.path / "history.db-wal") == wal_before
                   : !fs::exists(dir.path / "history.db-wal")));
        CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    }
}

TEST_CASE(
    "newer: a newer database with a hot journal is told as newer; both files stay untouched") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    Raw(db).run("CREATE TABLE future (x BLOB); PRAGMA user_version = 99");
    // A child dies inside a big transaction (no close, no rollback), leaving history.db-journal.
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        sql::Database died;
        const bool ok =
            died.open(db) == SQLITE_OK &&
            died.run_script("PRAGMA journal_mode = DELETE; PRAGMA cache_size = 4; BEGIN; "
                            "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n "
                            "WHERE i < 600) INSERT INTO future SELECT zeroblob(3000) FROM n") ==
                SQLITE_OK;
        _exit(ok ? 0 : 3);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE((WIFEXITED(status) && WEXITSTATUS(status) == 0));
    REQUIRE(fs::exists(dir.path / "history.db-journal"));
    const std::string db_before = read_all(db);
    const std::string journal_before = read_all(dir.path / "history.db-journal");
    {
        HistoryStore store(dir.path, quiet());
        CHECK(open_now(store).find("newer") != std::string::npos);
        CHECK(store.state() == HistoryState::Unavailable);
        store.add_run(sample_run(1));
        store.flush();
    }
    CHECK(read_all(db) == db_before); // a rollback would have changed it
    CHECK(read_all(dir.path / "history.db-journal") == journal_before);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
}

// ---- Migrations

TEST_CASE("migrations: a new step migrates an existing database and keeps its runs; a fresh one "
          "runs every step") {
    TempDir dir;
    write_runs(dir.path, 2);
    const std::vector<Migration> steps =
        with_next("ALTER TABLE profiles ADD COLUMN color INTEGER NOT NULL DEFAULT 0;"
                  "CREATE TABLE badges (id INTEGER PRIMARY KEY, profile_id INTEGER NOT NULL, name "
                  "TEXT NOT NULL)");
    HistoryOptions options = quiet();
    options.migrations = steps;
    TempDir fresh;
    for (const fs::path& folder : {dir.path, fresh.path}) {
        HistoryStore store(folder, options);
        REQUIRE(open_now(store).empty());
        if (folder == dir.path) {
            REQUIRE(store.runs().size() == 2);
            CHECK(store.runs()[0] == sample_run(0)); // the migrated run's contents, not only the count
        } else {
            CHECK(store.runs().empty());
        }
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion) + 1);
        CHECK(raw.num("SELECT COUNT(*) FROM badges") == 0);
    }
}

TEST_CASE("migrations: a step that fails leaves the file at the version before it, and the next "
          "start completes it") {
    TempDir dir;
    write_runs(dir.path, 1);
    const int next = static_cast<int>(kLatestVersion) + 1;
    std::vector<Migration> chain =
        with_next("ALTER TABLE profiles ADD COLUMN step_one INTEGER NOT NULL DEFAULT 0");
    chain.push_back({next + 1,
                     "ALTER TABLE profiles ADD COLUMN step_two INTEGER NOT NULL DEFAULT 0; "
                     "CREATE TABLE step_two_table (x INTEGER); "
                     "ALTER TABLE no_such_table ADD COLUMN x INTEGER"});
    {
        HistoryOptions options = quiet();
        options.migrations = chain;
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        CHECK(open_now(store).find("not usable for now") != std::string::npos);
        CHECK(store.state() == HistoryState::Waiting); // held in memory, retried
        store.add_run(sample_run(2));
        CHECK_FALSE(flush_fails(store, 2));
    }
    {
        // The first step is whole; the second left nothing: not its column, not its table.
        Raw raw(dir.path / "history.db");
        CHECK(raw.num("PRAGMA user_version") == next);
        CHECK(
            raw.num("SELECT COUNT(*) FROM pragma_table_info('profiles') WHERE name = 'step_two'") ==
            0);
        CHECK(raw.num("SELECT COUNT(*) FROM sqlite_master WHERE name = 'step_two_table'") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
    }
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged")); // a bug in a step is not damage
    chain.back().sql = "ALTER TABLE profiles ADD COLUMN step_two INTEGER NOT NULL DEFAULT 0";
    HistoryOptions fixed = quiet();
    fixed.migrations = chain;
    HistoryStore store(dir.path, fixed);
    CHECK(open_now(store).empty());
    CHECK(store.runs().size() == 2); // the run held while the step failed is in the journal
    CHECK(Raw(store.path()).num("PRAGMA user_version") == next + 1);
}

TEST_CASE("migrations: a file left by an older build upgrades and keeps what it held") {
    for (const int version : {2, 9, static_cast<int>(kLatestVersion) - 1}) {
        CAPTURE(version);
        TempDir dir;
        build_at_version(dir.path / "history.db", version);
        Raw(dir.path / "history.db")
            .run("INSERT INTO songs (song_key, content_hash, path, title, first_seen_utc_s, "
                 "last_seen_utc_s) "
                 "VALUES ('k1', 77, '/a.mid', 'A', 1, 2); "
                 "INSERT INTO song_settings (profile_id, content_hash, title, favourite) "
                 "VALUES (1, 77, 'Mine', 1)");
        {
            HistoryStore store(dir.path, quiet());
            CHECK(open_now(store).empty());
            store.add_run(sample_run(1));
            REQUIRE(store.flush());
            CHECK(store.take_errors().empty());
            CHECK(store.song_prefs().at(77).title == "Mine");
            CHECK(store.song_prefs().at(77).favourite);
            Raw raw(store.path());
            CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
            CHECK(raw.num("SELECT COUNT(*) FROM songs WHERE song_key = 'k1'") == 1);
            CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
        }
        CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    }
}

// ---- The one-time VACUUM

namespace {

RunEvents bulk_events(int count) {
    RunEvents e;
    e.expected_notes = count;
    e.complete = true;
    for (int i = 0; i < count; ++i) {
        RunEvent ev;
        ev.kind = RunEventKind::Press;
        ev.note_index = i;
        ev.key = static_cast<std::uint8_t>(40 + i % 48);
        ev.velocity = 80;
        ev.hand = Hand::Right;
        ev.song_ms = i * 100;
        ev.grade = NoteGrade::Perfect;
        e.events.push_back(ev);
    }
    return e;
}

// Every column of every run row, in id order: what "every run intact" is compared against.
std::vector<std::string> dump_runs(Raw& raw) {
    const auto columns = raw.num("SELECT COUNT(*) FROM pragma_table_info('runs')");
    sql::Statement s;
    REQUIRE(s.prepare(raw.db.get(), "SELECT * FROM runs ORDER BY id") == SQLITE_OK);
    std::vector<std::string> rows;
    while (s.step() == SQLITE_ROW) {
        std::string row;
        for (int i = 0; i < columns; ++i) {
            row += (s.column_is_null(i) ? "NULL" : s.column_text(i)) + std::string("|");
        }
        rows.push_back(row);
    }
    return rows;
}

// A closed database of 20 runs with 600 events each, as a build before the VACUUM made it
// (auto_vacuum NONE), with all but the newest two event sets deleted: mostly free pages.
void mostly_free_file(const fs::path& dir) {
    {
        Opened store(dir);
        store.set_events_kept_per_song(kMaxRunEventsKept);
        for (int n = 1; n <= 20; ++n) {
            store.add_run(one_setup(sample_run(n)), "a.mid", bulk_events(600));
        }
        REQUIRE(store.flush());
    }
    Raw raw(dir / "history.db");
    raw.run("PRAGMA auto_vacuum = NONE; VACUUM; PRAGMA wal_checkpoint(TRUNCATE)");
    raw.run("DELETE FROM run_event_sets WHERE run_id NOT IN (SELECT run_id FROM run_event_sets "
            "ORDER BY "
            "run_id DESC LIMIT 2); DELETE FROM run_events WHERE run_id NOT IN (SELECT run_id FROM "
            "run_event_sets); PRAGMA wal_checkpoint(TRUNCATE)");
}

// A hook for HistoryOptions::compacting that holds the VACUUM mid-way until the test lets go.
struct VacuumGate {
    std::atomic<bool> entered{false};
    std::atomic<bool> released{false};
    std::function<void()> hook() {
        return [this] {
            entered = true;
            while (!released) {
                std::this_thread::sleep_for(1ms);
            }
        };
    }
    void wait_entered() {
        for (int i = 0; i < 10000 && !entered; ++i) {
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE(entered);
    }
};

// A store whose VACUUM is held at `gate`, on a mostly free file whose runs were dumped first.
struct HeldVacuum {
    TempDir dir;
    std::vector<std::string> rows;
    VacuumGate gate;
    Log log;
    HistoryOptions options;
    HeldVacuum() {
        mostly_free_file(dir.path);
        Raw raw(dir.path / "history.db");
        rows = dump_runs(raw);
        options = small_threshold();
        options.compacting = gate.hook();
    }
    HistoryOptions small_threshold() {
        HistoryOptions o = log.options();
        o.compact_min_free_bytes = 16 * 1024;
        return o;
    }
    // The file is still a usable mode-NONE database with its first 20 runs, and a next start
    // compacts it.
    void check_untouched_then_compacts(std::size_t runs) {
        CHECK(log.has("stopped"));
        CHECK_FALSE(log.has("compacted"));
        {
            Raw raw(dir.path / "history.db");
            CHECK(raw.num("PRAGMA auto_vacuum") == 0);
            CHECK(raw.text("PRAGMA quick_check(1)") == "ok");
            const auto now = dump_runs(raw);
            REQUIRE(now.size() == runs);
            CHECK(std::vector<std::string>(now.begin(), now.begin() + 20) == rows);
        }
        {
            HistoryStore store(dir.path, small_threshold());
            REQUIRE(open_now(store).empty());
            CHECK(store.runs().size() == runs);
        }
        CHECK(log.has("compacted"));
        CHECK(Raw(dir.path / "history.db").num("PRAGMA auto_vacuum") == 2);
    }
};

} // namespace

TEST_CASE("vacuum: quitting during the one-time VACUUM stops it, and the next start compacts") {
    HeldVacuum v;
    HistoryStore store(v.dir.path, v.options);
    store.open(); // the VACUUM is under way: the store is not settled
    v.gate.wait_entered();
    std::atomic<bool> closing{false};
    std::thread closer([&] {
        closing = true;
        store.close();
    });
    while (!closing) {
        std::this_thread::sleep_for(1ms);
    }
    std::this_thread::sleep_for(50ms); // close() has asked it to stop
    v.gate.released = true;
    closer.join();
    v.check_untouched_then_compacts(20);
}

TEST_CASE("vacuum: a run that finishes during the VACUUM stops it and is saved at once") {
    HeldVacuum v;
    {
        HistoryStore store(v.dir.path, v.options);
        store.open();
        v.gate.wait_entered();
        store.add_run(one_setup(sample_run(21)), "a.mid", bulk_events(10));
        v.gate.released = true;
        REQUIRE(store.wait_until_settled(10s));
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        CHECK(store.state() == HistoryState::Ready);
    }
    v.check_untouched_then_compacts(21);
}

// ---- The journal

namespace {

JournalPractice practice_at(std::uint64_t hash, int speed, std::int64_t at_utc_s) {
    JournalPractice entry;
    entry.hash = hash;
    entry.practice = busy_practice();
    entry.practice.speed_percent = speed;
    entry.at_utc_s = at_utc_s;
    return entry;
}

JournalPrefs prefs_of(std::uint64_t hash, const std::string& title) {
    JournalPrefs entry;
    SongPrefs p;
    p.title = title;
    entry.items.emplace_back(hash, p);
    return entry;
}

std::string journal_of(std::initializer_list<JournalEntry> entries) {
    std::string bytes = encode_journal_header();
    for (const JournalEntry& e : entries) {
        bytes += encode_journal_entry(e);
    }
    return bytes;
}

JournalRun run_entry(int n) {
    JournalRun run;
    run.record = sample_run(n);
    return run;
}

// A store on a database that was created and is now closed.
void seed_database(const fs::path& dir) {
    Opened seed(dir);
}

} // namespace

TEST_CASE("journal: every kind of record round-trips through the file's bytes") {
    JournalRun run = run_entry(7);
    run.song_path = "/songs/caf\xC3\xA9.mid";
    run.events = sample_events(5, false);
    JournalPractice practice;
    practice.hash = 0xFFFFFFFFFFFFFF01ULL; // the top bit set, as a hash can be
    practice.practice = busy_practice();
    JournalPrefs prefs = prefs_of(0x2, "Mine");
    JournalGroups groups;
    groups.edits = {group_edit(GroupEdit::Kind::Create, 0xFFFFFFFFFFFFFFF0ULL, "Caf\xC3\xA9"),
                    group_edit(GroupEdit::Kind::Add, 9, {}, 0xABCDEF0123456789ULL)};
    JournalOrigins origins;
    origins.items.emplace_back(0x1234, sample_origin());
    JournalVersions versions;
    versions.rows = {{0xAA, 0, true}, {0xB1, 0xB2, false}};
    const JournalPin pin{1234567, true};

    const std::string bytes = journal_of({run, practice, prefs, groups, origins, versions, pin});
    const JournalDecode back = decode_journal(bytes);
    REQUIRE(back.entries.size() == 7);
    CHECK(back.damaged == 0);
    CHECK(back.valid_end == bytes.size());
    CHECK(std::get<JournalRun>(back.entries[0]).record == run.record);
    CHECK(std::get<JournalRun>(back.entries[0]).song_path == run.song_path);
    CHECK(std::get<JournalRun>(back.entries[0]).events == run.events);
    CHECK(std::get<JournalPractice>(back.entries[1]).hash == practice.hash);
    CHECK(std::get<JournalPractice>(back.entries[1]).practice == practice.practice);
    CHECK(std::get<JournalPrefs>(back.entries[2]).items == prefs.items);
    CHECK(std::get<JournalGroups>(back.entries[3]).edits[0].group == groups.edits[0].group);
    CHECK(std::get<JournalGroups>(back.entries[3]).edits[1].hash == groups.edits[1].hash);
    CHECK(std::get<JournalOrigins>(back.entries[4]).items == origins.items);
    CHECK(std::get<JournalVersions>(back.entries[5]).rows.size() == 2);
    CHECK(std::get<JournalPin>(back.entries[6]).run_id == pin.run_id);

    // A later writer's extra bytes after a record's fields are left unread.
    const JournalDecode longer = decode_journal(
        encode_journal_header() + framed_record(encode_journal_entry(pin).substr(8) + "more"));
    REQUIRE(longer.entries.size() == 1);
    CHECK(longer.damaged == 0);
}

TEST_CASE("journal: damaged bytes are skipped or told apart, and the rest is kept") {
    const std::string head = encode_journal_header();
    const std::string a = encode_journal_entry(run_entry(1));
    const std::string b = encode_journal_entry(practice_at(5, 70, 0));

    // Cut inside the last record: it is not read, and the valid part ends where the first does.
    const JournalDecode torn = decode_journal((head + a + b).substr(0, head.size() + a.size() + 5));
    CHECK(torn.entries.size() == 1);
    CHECK(torn.truncated);
    CHECK(torn.valid_end == head.size() + a.size());

    // A flipped byte, a kind this build does not know, a record cut inside its fields and a pin of
    // no run are each skipped as damaged, and the record after it is read.
    std::string flipped = head + a + b;
    flipped[head.size() + 12] = static_cast<char>(flipped[head.size() + 12] ^ 0x40);
    const std::string cut =
        encode_journal_entry(JournalGroups{{group_edit(GroupEdit::Kind::Create, 1, "x")}})
            .substr(8);
    for (const std::string& bytes :
         {flipped, head + framed_record("Zrecords of a later build") + b,
          head + framed_record(cut.substr(0, cut.size() - 3)) + b,
          head + framed_record(encode_journal_entry(JournalPin{0, true}).substr(8)) + b}) {
        const JournalDecode d = decode_journal(bytes);
        CHECK(d.damaged >= 1);
        REQUIRE_FALSE(d.entries.empty());
        CHECK(std::holds_alternative<JournalPractice>(d.entries.back()));
        CHECK_FALSE(d.truncated);
    }

    CHECK(decode_journal("not a journal\n").bad_header);
    CHECK(decode_journal("etude journal x\n").bad_header);
    const JournalDecode cut_header = decode_journal("etude jour"); // an empty journal, not foreign
    CHECK(cut_header.truncated);
    CHECK_FALSE(cut_header.bad_header);
    CHECK(decode_journal("etude journal 2\nanything").newer_version == 2);
}

TEST_CASE("journal: appending cuts a torn tail, refuses a file that is not ours, skips a record "
          "over the cap") {
    TempDir dir;
    const fs::path file = dir.path / kJournal;
    CHECK(append_journal(file, std::vector<JournalEntry>{run_entry(1)}).saved == 1);
    write_all(file,
              read_all(file) + encode_journal_entry(run_entry(2)).substr(0, 9)); // a cut write
    CHECK(append_journal(file, std::vector<JournalEntry>{run_entry(3)}).saved == 1);
    const JournalDecode all = decode_journal(read_all(file));
    CHECK(all.damaged == 0);
    CHECK_FALSE(all.truncated);
    REQUIRE(all.entries.size() == 2);
    CHECK(std::get<JournalRun>(all.entries[1]).record == sample_run(3));

    // Not ours, or newer: left byte for byte, nothing written.
    for (const std::string& foreign :
         {std::string("a diary\n"), std::string("etude journal 9\nx")}) {
        write_all(file, foreign);
        CHECK(append_journal(file, std::vector<JournalEntry>{run_entry(1)}).saved == 0);
        CHECK(read_all(file) == foreign);
    }

    // A record over the size cap is not written and does not damage the ones after it.
    fs::remove(file);
    JournalRun huge = run_entry(2);
    huge.song_path = std::string(kMaxJournalRecordBytes + 10, 'x');
    const JournalAppend result =
        append_journal(file, std::vector<JournalEntry>{run_entry(3), huge, prefs_of(0x9, "After")});
    CHECK(result.saved == 2);
    CHECK_FALSE(result.kept[1]);
    const JournalDecode written = decode_journal(read_all(file));
    CHECK(written.entries.size() == 2);
    CHECK_FALSE(written.framing_damaged);
}

TEST_CASE("journal: jobs held at quit are saved once at the next start, a copy that comes back "
          "adds nothing") {
    if (running_as_root()) {
        return;
    }
    TempDir dir;
    CHECK(quit_with_held_jobs(dir.path, true).empty()); // kept, so nothing to tell the player
    const fs::path file = dir.path / kJournal;
    const std::string journal = read_all(file);
    CHECK(decode_journal(journal).entries.size() == 4); // two runs, a practice choice, a star
    make_writable(dir.path, true);
    check_landed(dir.path, 3); // run 1 from before, then the two kept
    CHECK_FALSE(fs::exists(file));
    for (int again = 0; again < 2; ++again) {
        write_all(file, journal);
        check_landed(dir.path, 3);
        CHECK_FALSE(fs::exists(file));
    }
    Opened store(dir.path);
    CHECK(store.runs()[2] == sample_run(3));
    CHECK(store.song_practice().at(0x77) == busy_practice());
    CHECK(store.song_prefs().at(0x77).favourite);
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM song_practice_parts") == 2);
}

TEST_CASE("journal: a torn last record or a bad check is skipped, the rest is saved, the player is "
          "told") {
    if (running_as_root()) {
        return;
    }
    for (const bool torn : {true, false}) {
        CAPTURE(torn);
        TempDir dir;
        REQUIRE(quit_with_held_jobs(dir.path).empty());
        std::string journal = read_all(dir.path / kJournal); // run 2, run 3, a practice choice
        if (torn) {
            journal.resize(journal.size() - 4); // the power went in the last record
        } else {
            const std::size_t third =
                encode_journal_header().size() +
                encode_journal_entry(decode_journal(journal).entries[0]).size();
            journal[third + 12] = static_cast<char>(journal[third + 12] ^ 0x01); // inside run 3
        }
        write_all(dir.path / kJournal, journal);
        make_writable(dir.path, true);
        HistoryStore store(dir.path, quiet());
        CHECK(open_now(store).find("1 record") != std::string::npos);
        CHECK(store.runs().size() ==
              (torn ? 3u : 2u)); // torn: only the practice choice is lost; bad check: run 3
        CHECK(store.song_practice().count(0x77) == (torn ? 0u : 1u));
        CHECK_FALSE(fs::exists(dir.path / kJournal));
    }
}

TEST_CASE("journal: when it cannot be written either, the loss is reported") {
    if (running_as_root()) {
        return;
    }
    TempDir dir;
    const std::vector<std::string> errors = quit_with_held_jobs(dir.path, true, true);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0] ==
          "2 practice runs, 1 practice setting and 1 library change could not be saved");
    make_writable(dir.path, true);
    check_landed(dir.path, 1, false); // only the first run: the rest is gone, and the player knows
}

TEST_CASE(
    "journal: a record with a damaged length keeps the file whole, and the first record lands") {
    std::string bytes = encode_journal_header();
    std::size_t second = 0;
    for (int n = 1; n <= 5; ++n) {
        second = n == 2 ? bytes.size() : second;
        bytes += encode_journal_entry(run_entry(n));
    }
    bytes.replace(second, 4, 4, '\xFF'); // an impossible length
    CHECK(decode_journal(bytes).framing_damaged);

    TempDir dir;
    write_all(dir.path / kJournal, bytes);
    {
        HistoryStore store(dir.path, quiet());
        CHECK(open_now(store).find("history.journal") != std::string::npos); // the damage is told
        CHECK(store.runs().size() == 1);
    }
    // Not deleted: the bytes after the damage are in the copy, whole.
    CHECK_FALSE(fs::exists(dir.path / kJournal));
    CHECK(read_all(dir.path / "history.journal.rejected") == bytes);
}

TEST_CASE("journal: a journal that is not ours or from a newer etude is left as it is, and said") {
    for (const std::string& foreign :
         {std::string("etude journal 2\nrecords this build cannot read"),
          std::string("my shopping list\n")}) {
        TempDir dir;
        write_all(dir.path / kJournal, foreign);
        HistoryStore store(dir.path, quiet());
        CHECK(open_now(store).find("history.journal") != std::string::npos);
        CHECK(store.state() == HistoryState::Ready);
        store.add_run(sample_run(1)); // the history works as usual
        REQUIRE(store.flush());
        store.close();
        CHECK(read_all(dir.path / kJournal) == foreign);
    }
}

TEST_CASE("journal: an older record layout replays with defaults, and a copy that comes back adds "
          "nothing") {
    TempDir dir;
    seed_database(dir.path);
    JournalRun run = run_entry(2);
    run.song_path = "/songs/a.mid";
    run.events = sample_events(4);
    const std::string old_journal =
        encode_journal_header() + encode_journal_entry(run) + first_layout_practice_record();
    for (int pass = 0; pass < 2; ++pass) {
        write_all(dir.path / kJournal, old_journal);
        Opened store(dir.path);
        CHECK(store.runs().size() == 1);
        CHECK(store.song_practice().at(0x77).speed_percent == 40);
        CHECK(store.song_practice().at(0x77).muted_parts == std::vector<std::uint32_t>{5});
        CHECK(Raw(store.path()).num("SELECT COUNT(*) FROM run_event_sets") == 1);
    }
    // A record from before the split or the time existed: the split is Auto, the time unknown.
    SongPractice expected = busy_practice();
    expected.split_point = kAutoSplit;
    const JournalDecode back = decode_journal(encode_journal_header() + practice_record_without(2));
    REQUIRE(back.entries.size() == 1);
    CHECK(std::get<JournalPractice>(back.entries[0]).practice == expected);
    CHECK(std::get<JournalPractice>(back.entries[0]).at_utc_s == 0);
}

TEST_CASE(
    "journal: a record is applied unless the database has a newer row; of two, the later wins") {
    TempDir dir;
    {
        Opened store(dir.path);
        SongPractice mine;
        mine.speed_percent = 90;
        store.set_song_practice(0x77, mine);
        store.set_song_practice(0x78, mine);
        REQUIRE(store.flush());
    }
    const std::int64_t held_at = now_s() - 100;
    write_all(dir.path / kJournal,
              journal_of({practice_at(0x77, 65, 1000),             // written long before the row
                          practice_at(0x78, 65, now_s() + 10'000), // written after it
                          practice_at(0x79, 65, 0), // a writer that left the time off
                          practice_at(0x7A, 60, held_at), practice_at(0x7A, 80, held_at)}));
    Opened store(dir.path);
    CHECK(store.song_practice().at(0x77).speed_percent == 90);
    CHECK(store.song_practice().at(0x78).speed_percent == 65);
    CHECK(store.song_practice().at(0x79).speed_percent == 65);
    CHECK(store.song_practice().at(0x7A).speed_percent == 80);
}

TEST_CASE("journal: a journal that cannot be removed is not replayed over later changes") {
    TempDir dir;
    seed_database(dir.path);
    const std::string journal = journal_of({run_entry(2), practice_at(0x77, 65, now_s())});
    write_all(dir.path / kJournal, journal);
    const auto no = [](const fs::path&) { return false; };
    HistoryOptions no_remove = quiet();
    no_remove.remove_file = no;
    HistoryOptions neither = no_remove; // neither the delete nor the rename works
    neither.rename_file = [](const fs::path&, const fs::path&) { return false; };
    HistoryOptions stuck = neither; // and the journal cannot be emptied either
    stuck.truncate_file = no;
    {
        HistoryStore store(dir.path, stuck);
        CHECK(open_now(store).find("could not be removed") != std::string::npos);
        CHECK(store.runs().size() == 1);
    }
    CHECK(read_all(dir.path / kJournal) == journal);
    Raw(dir.path / "history.db") // the player changes the song afterwards
        .run("UPDATE song_practice SET speed_percent = 90, updated_utc_s = updated_utc_s + 1000");
    HistoryStore store(dir.path, quiet()); // the next start reads the journal again
    REQUIRE(open_now(store).empty());
    CHECK(store.runs().size() == 1); // the run is not added twice
    CHECK(store.song_practice().at(0x77).speed_percent == 90);

    // When only the delete fails, the rename moves it out of the way; when neither works, it is
    // emptied, so a library change is not replayed over later ones.
    TempDir other;
    write_all(other.path / kJournal, journal_of({prefs_of(0x9, "Old")}));
    {
        HistoryStore s(other.path, no_remove);
        REQUIRE(open_now(s).empty());
    }
    CHECK(fs::exists(other.path / "history.journal.done"));
    write_all(other.path / kJournal, journal_of({prefs_of(0x9, "Old")}));
    {
        HistoryStore s(other.path, neither);
        REQUIRE(open_now(s).empty());
        SongPrefs renamed;
        renamed.title = "New";
        s.set_song_prefs(0x9, renamed);
        REQUIRE(s.flush());
    }
    CHECK(read_all(other.path / kJournal).empty());
    HistoryStore s(other.path, neither);
    REQUIRE(open_now(s).empty());
    CHECK(s.song_prefs().at(0x9).title == "New");
}

TEST_CASE("journal: a record the quit could not replay never lands over what the session saved "
          "after it") {
    TempDir dir;
    seed_database(dir.path);
    write_all(dir.path / kJournal, journal_of({prefs_of(0x9, "Old"), run_entry(2), run_entry(3)}));
    Raw other(dir.path / "history.db");
    other.run("BEGIN EXCLUSIVE");
    HistoryOptions options = quiet();
    options.open_busy_timeout_ms = 20;
    options.retry_interval_ms = 60'000;
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        CHECK(store.state() == HistoryState::Waiting);
        SongPrefs renamed;
        renamed.title = "New";
        store.set_song_prefs(0x9, renamed);
        store.add_run(sample_run(1));
        other.run("ROLLBACK"); // free now; closing makes the last try
    }
    // The quit did not replay the journal; the held jobs joined it behind the older records.
    CHECK(decode_journal(read_all(dir.path / kJournal)).entries.size() == 5);
    HistoryStore store(dir.path, quiet()); // the next start replays it
    REQUIRE(open_now(store).empty());
    CHECK(store.song_prefs().at(0x9).title == "New");
    CHECK(store.runs().size() == 3);
}

TEST_CASE("journal: without the instance lock the journal is left alone") {
    TempDir dir;
    seed_database(dir.path);
    const std::string bytes = journal_of({run_entry(2)});
    write_all(dir.path / kJournal, bytes);
    fs::remove(dir.path / "history.lock");
    fs::create_directory(dir.path / "history.lock"); // the lock file cannot be opened
    {
        HistoryStore store(dir.path, quiet());
        CHECK(open_now(store).find("history.journal") != std::string::npos);
        CHECK(store.runs().empty());
    }
    CHECK(read_all(dir.path / kJournal) == bytes);
    fs::remove(dir.path / "history.lock");
    Opened store(dir.path);
    CHECK(store.runs().size() == 1);
    CHECK_FALSE(fs::exists(dir.path / kJournal));
}
