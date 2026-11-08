// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the history_store_*_test.cpp files: a temporary folder, a log sink, a second connection
// to look into the database file, and a sample run.
#pragma once

#include "history_store.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "sqlite_db.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <algorithm>
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace history_test {

using namespace etude;
using namespace etude::app;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
               ("etude-history-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

inline std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void write_all(const fs::path& p, const std::string& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// For stores whose log lines nobody checks: keeps the test output clean.
inline HistoryOptions quiet() {
    HistoryOptions o;
    o.log = [](std::string_view) {};
    return o;
}

// The lines a store logs (the callback runs on the history thread).
struct Log {
    std::mutex mutex;
    std::vector<std::string> lines_;
    HistoryOptions options() {
        HistoryOptions o;
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
    void clear() {
        std::lock_guard lock(mutex);
        lines_.clear();
    }
};

// Starts the store and waits for the history thread to settle: loaded, or found the database
// busy. Returns the sentence the player would be told.
inline std::string open_now(HistoryStore& store) {
    store.open();
    REQUIRE(store.wait_until_settled(10s));
    return store.poll().message;
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
};

// Takes what migration 13 (profiles' colour and settings, a run's score) added back out of a
// database of the latest shape, for the tests that make an older file by dropping what a later
// migration made. Call it first, with the older version's own drops after it.
inline void undo_version_13(Raw& raw) {
    REQUIRE(raw.db.run_script("DROP TABLE profile_settings; "
                              "ALTER TABLE profiles DROP COLUMN colour; "
                              "ALTER TABLE runs DROP COLUMN score; "
                              "ALTER TABLE runs DROP COLUMN covered_percent") == SQLITE_OK);
}

// A finished run of song `song`, the n-th one: its own start second, some bars.
inline RunRecord sample_run(int n, std::uint64_t song = 0xabc0, int bars = 6) {
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
    for (int b = 0; b < bars; ++b) {
        r.measures.push_back({b, 4, b % 3, b % 2});
    }
    return sanitized(r);
}

inline std::string raw_runs_file(const std::vector<RunRecord>& runs) {
    std::vector<std::string> raw;
    for (const RunRecord& r : runs) {
        raw.push_back(serialize_run_record(r));
    }
    return encode_runs(raw);
}

inline const std::size_t kLatestVersion = static_cast<std::size_t>(history_migrations().back().version);


inline constexpr std::string_view kVersionNext =
    "ALTER TABLE profiles ADD COLUMN color INTEGER NOT NULL DEFAULT 0;"
    "CREATE TABLE badges (id INTEGER PRIMARY KEY, profile_id INTEGER NOT NULL, name TEXT NOT NULL)";

// Every migration this build has, then one more step.
inline std::vector<Migration> with_next(std::string_view sql) {
    std::vector<Migration> out(history_migrations().begin(), history_migrations().end());
    out.push_back({static_cast<int>(kLatestVersion) + 1, sql});
    return out;
}

inline GroupEdit group_edit(GroupEdit::Kind kind, std::uint64_t id, std::string name = {},
                     std::uint64_t hash = 0) {
    GroupEdit e;
    e.kind = kind;
    e.group.id = id;
    e.group.name = std::move(name);
    e.hash = hash;
    e.at_utc_s = 50;
    return e;
}

// Events of a run over `notes` notes of a song, one press each 500 ms apart, the third missed, and
// one wrong press: every kind there is.
inline RunEvents sample_events(int notes = 6, bool complete = true) {
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

// A run at one setup (the prune keeps the best run of each setup besides the newest ones, and
// sample_run varies the setup with n): the n-th run, whose accuracy rises with n, so the best is
// always the newest.
inline RunRecord one_setup(RunRecord r) {
    r.mode = RunMode::Flow;
    r.hands = HandFilter::Both;
    r.transpose_semitones = 0;
    return sanitized(r);
}

inline std::int64_t run_id_at(Raw& raw, int n) {
    return raw.num("SELECT id FROM runs WHERE start_utc_s = " +
                   std::to_string(sample_run(n).start_utc_s));
}

// The answer to a request, once the history thread has it.
inline std::optional<StoredRunEvents> answer(HistoryStore& store, std::uint64_t serial) {
    REQUIRE(serial != 0);
    REQUIRE(store.flush());
    for (EventsRead& r : store.take_event_reads()) {
        if (r.serial == serial) {
            return std::move(r.found);
        }
    }
    FAIL("no answer for the request");
    return std::nullopt;
}

inline const fs::path kJournal = "history.journal";

// A practice choice with every field set, to see each one come back.
inline SongPractice busy_practice() {
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
    p.split_point = 60; // a placed split
    return p;
}

#if !defined(_WIN32)
inline void make_writable(const fs::path& dir, bool writable) {
    for (const char* name : {"history.db", "history.db-wal", "history.db-shm"}) {
        std::error_code ec; // the -wal and -shm files are there only while a store has it open
        fs::permissions(dir / name,
                        writable ? fs::perms::owner_read | fs::perms::owner_write
                                 : fs::perms::owner_read,
                        fs::perm_options::replace, ec);
    }
}

// The database has run 1 and then goes read-only. A store opens it (reading works), is given two
// runs (the second with its played notes), a song's practice choices and, when `with_library`, a
// song's star, none of which it can write, and is closed with them held. Returns what it reported.
inline std::vector<std::string> quit_with_held_jobs(const fs::path& dir, bool with_library = false,
                                             bool journal_is_a_folder = false) {
    {
        HistoryStore seed(dir, quiet());
        REQUIRE(open_now(seed).empty());
        seed.add_run(sample_run(1));
    }
    make_writable(dir, false);
    if (journal_is_a_folder) {
        fs::create_directory(dir / kJournal); // a folder where the file should be
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
    CHECK_FALSE(store.flush(300ms)); // the writes find the file read-only
    CHECK(store.state() == HistoryState::Waiting);
    store.close();
    return store.take_errors();
}

// Opens the folder as the next start does.
inline void check_landed(const fs::path& dir, std::size_t runs_expected, bool quiet_start = true) {
    HistoryStore store(dir, quiet());
    const std::string said = open_now(store);
    CHECK(said.empty() == quiet_start);
    CHECK(store.state() == HistoryState::Ready);
    CHECK(store.runs().size() == runs_expected);
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == static_cast<std::int64_t>(runs_expected));
}
#endif

// A record frame around `payload`, as the writer frames one.
inline std::string framed_record(const std::string& payload) {
    std::string out;
    for (const std::uint32_t v : {static_cast<std::uint32_t>(payload.size()),
                                  static_cast<std::uint32_t>(content_hash(payload) & 0xFFFFFFFFu)}) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
        }
    }
    return out + payload;
}

inline SongOrigin sample_origin() {
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

// A journal as the build before #269 wrote it, field by field from the layout in
// history_journal.hpp and not through the encoder: a practice record of the first layout (no split,
// no time), which every build since has read.
inline std::string first_layout_practice_record() {
    std::string p = "P";
    for (int i = 0; i < 8; ++i) {
        p.push_back(i == 0 ? 0x77 : 0); // content hash 0x77
    }
    p.push_back(static_cast<char>(40 << 1)); // speed_percent 40 (zigzag)
    p.push_back(static_cast<char>(1 << 1));  // count_in_bars 1
    p.push_back(1);                          // hands: Left
    p.push_back(static_cast<char>(2 << 1));  // transpose 2
    p.push_back(1);                          // parts_set
    p.push_back(1);                          // one muted part
    p.push_back(0x05);                       // key 5
    p.push_back(0);                          // loop start
    p.push_back(0);                          // loop end
    p.push_back(0);                          // loop_enabled
    p.push_back(0);                          // section_id
    p.push_back(0);                          // plan_item
    p.push_back(0);                          // no_shift_offer
    return framed_record(p);
}

// A practice record of song 0x77 without its last `dropped` bytes (the zigzag of 60 and of a small
// time are one byte each, the time here is 0: not known).
// The practice record as an older build wrote it: without its last `dropped` fields (1: the time,
// 2: the split too), and always without the profile that came after the time.
inline std::string practice_record_without(std::size_t dropped) {
    JournalPractice practice;
    practice.hash = 0x77;
    practice.practice = busy_practice();
    const std::string full = encode_journal_entry(practice);
    return framed_record(full.substr(8, full.size() - 8 - dropped - 1));
}

inline std::int64_t now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// A database file as version `version` left it: the first `version` migrations, each in a
// transaction with the version set, and nothing dropped by hand afterwards.
inline void build_at_version(const fs::path& file, int version) {
    sql::Database db;
    REQUIRE(db.open(file) == SQLITE_OK);
    for (const Migration& m : history_migrations()) {
        if (m.version > version) {
            break;
        }
        REQUIRE(db.run_script("BEGIN IMMEDIATE") == SQLITE_OK);
        REQUIRE(db.run_script(m.sql) == SQLITE_OK);
        REQUIRE(db.run_script("PRAGMA user_version = " + std::to_string(m.version)) == SQLITE_OK);
        REQUIRE(db.run_script("COMMIT") == SQLITE_OK);
    }
}

// What `sqlite3 src .dump | sqlite3 dst` makes: every table written out as stored, its rows, then
// the indexes, and no `user_version` (a dump has no PRAGMA for it).
inline void dump_restore(const fs::path& src, const fs::path& dst) {
    std::vector<std::string> tables;
    std::vector<std::string> names;
    std::vector<std::string> indexes;
    {
        Raw from(src);
        tables = from.column("SELECT sql FROM sqlite_master WHERE type = 'table' AND sql IS NOT NULL "
                             "AND name NOT LIKE 'sqlite_%' ORDER BY rowid");
        names = from.column("SELECT name FROM sqlite_master WHERE type = 'table' AND sql IS NOT NULL "
                            "AND name NOT LIKE 'sqlite_%' ORDER BY rowid");
        indexes = from.column("SELECT sql FROM sqlite_master WHERE type = 'index' AND sql IS NOT NULL "
                              "ORDER BY rowid");
    }
    sql::Database to;
    REQUIRE(to.open(dst) == SQLITE_OK);
    REQUIRE(to.run_script("ATTACH '" + src.string() + "' AS old") == SQLITE_OK);
    REQUIRE(to.run_script("PRAGMA foreign_keys = OFF; BEGIN") == SQLITE_OK);
    for (const std::string& t : tables) {
        REQUIRE(to.run_script(t) == SQLITE_OK);
    }
    for (const std::string& n : names) {
        REQUIRE(to.run_script("INSERT INTO main.\"" + n + "\" SELECT * FROM old.\"" + n + "\"") ==
                SQLITE_OK);
    }
    for (const std::string& i : indexes) {
        REQUIRE(to.run_script(i) == SQLITE_OK);
    }
    REQUIRE(to.run_script("COMMIT") == SQLITE_OK);
    std::int64_t version = -1;
    REQUIRE(to.query_int("PRAGMA user_version", version) == SQLITE_OK);
    REQUIRE(version == 0);
}

// The answer to a pin: whether it took.
inline bool pin_took(HistoryStore& store, std::uint64_t serial) {
    REQUIRE(serial != 0);
    REQUIRE(store.flush());
    for (const EventsRead& r : store.take_event_reads()) {
        if (r.serial == serial) {
            CHECK(r.kind == EventsReadKind::Pin);
            CHECK_FALSE(r.found.has_value());
            return r.pin_took;
        }
    }
    FAIL("no answer for the pin");
    return false;
}

inline constexpr std::string_view kThreeIndexes =
    "SELECT COUNT(*) FROM sqlite_master WHERE name IN "
    "('songs_by_content_hash', 'runs_by_song', 'song_group_members_by_song')";

inline bool logged(Log& log, std::string_view needle) {
    for (const std::string& line : log.get()) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace history_test
