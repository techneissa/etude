// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_store.hpp"

#include "history_store_internal.hpp"

#include "runs_file.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <cctype>
#include <map>
#include <set>
#include <span>
#include <system_error>
#include <utility>
#include <variant>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace etude::app {

namespace fs = std::filesystem;

namespace {

// The schema version in a file's own header (bytes 60 to 63, big-endian), read without SQLite: nothing
// is opened, locked or rolled back. -1 when the file does not begin like a SQLite database.
std::int64_t header_user_version(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    unsigned char head[64] = {};
    in.read(reinterpret_cast<char*>(head), sizeof head);
    static const char kMagic[] = "SQLite format 3";
    if (in.gcount() < 64 || std::memcmp(head, kMagic, sizeof kMagic) != 0) {
        return -1;
    }
    return (static_cast<std::int64_t>(head[60]) << 24) | (static_cast<std::int64_t>(head[61]) << 16) |
           (static_cast<std::int64_t>(head[62]) << 8) | static_cast<std::int64_t>(head[63]);
}

// "INSERT ... (profile_id, song_id, song_title, <the run columns>) VALUES (?1, ...)". Built from
// core's column list once, so the SQL and the mapping cannot drift.
const std::string& insert_run_sql() {
    static const std::string sql = [] {
        std::string s = "INSERT INTO runs (profile_id, song_id, song_title";
        for (std::string_view name : kRunColumnNames) {
            s += ", ";
            s += name;
        }
        s += ") VALUES (?1, ?2, ?3";
        for (std::size_t i = 0; i < kRunColumnCount; ++i) {
            s += ", ?" + std::to_string(4 + i);
        }
        // A repeat of a run (core's same_run) is skipped; any other constraint failure is an error.
        s += ") ON CONFLICT (profile_id, song_hash, start_utc_s, mode, hands, speed_percent, "
             "transpose_semitones) DO NOTHING";
        return s;
    }();
    return sql;
}
constexpr std::string_view kUpsertSong =
    "INSERT INTO songs (song_key, content_hash, path, title, first_seen_utc_s, last_seen_utc_s) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?5) "
    "ON CONFLICT (song_key) DO UPDATE SET "
    "content_hash = excluded.content_hash, "
    "path = CASE WHEN excluded.path <> '' THEN excluded.path ELSE songs.path END, "
    "title = CASE WHEN excluded.title <> '' THEN excluded.title ELSE songs.title END, "
    "first_seen_utc_s = min(songs.first_seen_utc_s, excluded.first_seen_utc_s), "
    "last_seen_utc_s = max(songs.last_seen_utc_s, excluded.last_seen_utc_s) "
    "RETURNING id";
// The whole record, except that the date added keeps the earliest one stored (0 is none).
constexpr std::string_view kUpsertPrefs =
    "INSERT INTO song_settings (profile_id, content_hash, title, composer, genre, favourite, "
    "added_utc_s, chosen_utc_s) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
    "ON CONFLICT (profile_id, content_hash) DO UPDATE SET "
    "title = excluded.title, composer = excluded.composer, genre = excluded.genre, "
    "favourite = excluded.favourite, chosen_utc_s = excluded.chosen_utc_s, "
    "added_utc_s = CASE WHEN song_settings.added_utc_s = 0 THEN excluded.added_utc_s "
    "WHEN excluded.added_utc_s = 0 THEN song_settings.added_utc_s "
    "ELSE min(song_settings.added_utc_s, excluded.added_utc_s) END";
// Where downloaded songs came from (migration 4): the latest download of a file wins.
constexpr std::string_view kUpsertSource =
    "INSERT INTO song_sources (content_hash, catalogue, catalogue_id, title, composer, licence, "
    "licence_url, credit, source_url, page_url, downloaded_utc_s) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11) "
    "ON CONFLICT (content_hash) DO UPDATE SET catalogue = excluded.catalogue, "
    "catalogue_id = excluded.catalogue_id, title = excluded.title, composer = excluded.composer, "
    "licence = excluded.licence, licence_url = excluded.licence_url, credit = excluded.credit, "
    "source_url = excluded.source_url, page_url = excluded.page_url, "
    "downloaded_utc_s = excluded.downloaded_utc_s";
// What the player settled about versions of one piece (migration 6).
constexpr std::string_view kInsertOverride =
    "INSERT INTO version_overrides (profile_id, hash_a, hash_b) VALUES (?1, ?2, ?3) "
    "ON CONFLICT (profile_id, hash_a, hash_b) DO NOTHING";
constexpr std::string_view kDeleteOverride =
    "DELETE FROM version_overrides WHERE profile_id = ?1 AND hash_a = ?2 AND hash_b = ?3";
// What the player last used on each song (migration 7).
constexpr std::string_view kUpsertPractice =
    "INSERT INTO song_practice (profile_id, content_hash, speed_percent, count_in_bars, hands, "
    "transpose_semitones, updated_utc_s) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, "
    "CASE WHEN ?7 > 0 THEN ?7 ELSE CAST(strftime('%s', 'now') AS INTEGER) END) "
    "ON CONFLICT (profile_id, content_hash) DO UPDATE SET "
    "speed_percent = excluded.speed_percent, count_in_bars = excluded.count_in_bars, "
    "hands = excluded.hands, transpose_semitones = excluded.transpose_semitones, "
    "updated_utc_s = excluded.updated_utc_s";
// With the loop and the parts (migration 8).
constexpr std::string_view kUpsertPractice8 =
    "INSERT INTO song_practice (profile_id, content_hash, speed_percent, count_in_bars, hands, "
    "transpose_semitones, updated_utc_s, loop_start_ms, loop_end_ms, loop_enabled, section_id, "
    "plan_item, parts_set) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, CASE WHEN ?13 > 0 THEN ?13 ELSE "
    "CAST(strftime('%s', 'now') AS INTEGER) END, ?7, ?8, ?9, ?10, ?11, "
    "?12) "
    "ON CONFLICT (profile_id, content_hash) DO UPDATE SET "
    "speed_percent = excluded.speed_percent, count_in_bars = excluded.count_in_bars, "
    "hands = excluded.hands, transpose_semitones = excluded.transpose_semitones, "
    "updated_utc_s = excluded.updated_utc_s, loop_start_ms = excluded.loop_start_ms, "
    "loop_end_ms = excluded.loop_end_ms, loop_enabled = excluded.loop_enabled, "
    "section_id = excluded.section_id, plan_item = excluded.plan_item, "
    "parts_set = excluded.parts_set";
// With the octave-shift offer turned off (migration 10).
constexpr std::string_view kUpsertPractice10 =
    "INSERT INTO song_practice (profile_id, content_hash, speed_percent, count_in_bars, hands, "
    "transpose_semitones, updated_utc_s, loop_start_ms, loop_end_ms, loop_enabled, section_id, "
    "plan_item, parts_set, no_shift_offer) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, CASE WHEN ?14 > 0 THEN ?14 ELSE "
    "CAST(strftime('%s', 'now') AS INTEGER) END, ?7, ?8, ?9, ?10, ?11, "
    "?12, ?13) "
    "ON CONFLICT (profile_id, content_hash) DO UPDATE SET "
    "speed_percent = excluded.speed_percent, count_in_bars = excluded.count_in_bars, "
    "hands = excluded.hands, transpose_semitones = excluded.transpose_semitones, "
    "updated_utc_s = excluded.updated_utc_s, loop_start_ms = excluded.loop_start_ms, "
    "loop_end_ms = excluded.loop_end_ms, loop_enabled = excluded.loop_enabled, "
    "section_id = excluded.section_id, plan_item = excluded.plan_item, "
    "parts_set = excluded.parts_set, no_shift_offer = excluded.no_shift_offer";
// With the hand split too (migration 11).
constexpr std::string_view kUpsertPractice11 =
    "INSERT INTO song_practice (profile_id, content_hash, speed_percent, count_in_bars, hands, "
    "transpose_semitones, updated_utc_s, loop_start_ms, loop_end_ms, loop_enabled, section_id, "
    "plan_item, parts_set, no_shift_offer, split_point) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, CASE WHEN ?15 > 0 THEN ?15 ELSE "
    "CAST(strftime('%s', 'now') AS INTEGER) END, ?7, ?8, ?9, ?10, ?11, "
    "?12, ?13, ?14) "
    "ON CONFLICT (profile_id, content_hash) DO UPDATE SET "
    "speed_percent = excluded.speed_percent, count_in_bars = excluded.count_in_bars, "
    "hands = excluded.hands, transpose_semitones = excluded.transpose_semitones, "
    "updated_utc_s = excluded.updated_utc_s, loop_start_ms = excluded.loop_start_ms, "
    "loop_end_ms = excluded.loop_end_ms, loop_enabled = excluded.loop_enabled, "
    "section_id = excluded.section_id, plan_item = excluded.plan_item, "
    "parts_set = excluded.parts_set, no_shift_offer = excluded.no_shift_offer, "
    "split_point = excluded.split_point";
constexpr std::string_view kDeleteParts =
    "DELETE FROM song_practice_parts WHERE profile_id = ?1 AND content_hash = ?2";
constexpr std::string_view kInsertPart =
    "INSERT INTO song_practice_parts (profile_id, content_hash, part_key) VALUES (?1, ?2, ?3) "
    "ON CONFLICT DO NOTHING";
// The player's groups (migration 3). A group made twice (an edit replayed) stays as it was.
constexpr std::string_view kInsertGroup =
    "INSERT INTO song_groups (profile_id, group_id, name, kind, source, position, created_utc_s) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) ON CONFLICT (profile_id, group_id) DO NOTHING";
constexpr std::string_view kRenameGroup =
    "UPDATE song_groups SET name = ?3 WHERE profile_id = ?1 AND group_id = ?2";
constexpr std::string_view kDeleteGroup =
    "DELETE FROM song_groups WHERE profile_id = ?1 AND group_id = ?2";
constexpr std::string_view kDeleteGroupSongs =
    "DELETE FROM song_group_members WHERE profile_id = ?1 AND group_id = ?2";
constexpr std::string_view kAddGroupSong =
    "INSERT INTO song_group_members (profile_id, group_id, content_hash, added_utc_s) "
    "VALUES (?1, ?2, ?3, ?4) ON CONFLICT (profile_id, group_id, content_hash) DO NOTHING";
constexpr std::string_view kDropGroupSong = "DELETE FROM song_group_members "
                                            "WHERE profile_id = ?1 AND group_id = ?2 AND "
                                            "content_hash = ?3";
constexpr std::string_view kMoveGroup =
    "UPDATE song_groups SET position = ?3 WHERE profile_id = ?1 AND group_id = ?2";
// The played notes of a run (migration 9).
constexpr std::string_view kInsertEventSet =
    "INSERT INTO run_event_sets (run_id, event_count, expected_notes, dropped, complete) "
    "VALUES (?1, ?2, ?3, ?4, ?5)";
constexpr std::string_view kInsertEvent =
    "INSERT INTO run_events (run_id, seq, kind, song_ms, error_ms, key, velocity, note_index, "
    "hand, grade) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)";
// Keeps the newest ?3 runs' events of the song, every pinned run's and, when ?4 is 1, the best run
// of each setup: the complete run with the highest accuracy (hit / graded; a tie goes to more
// graded notes, then to the later run: core's best_ghost_run) among those played at the same mode,
// hands, transposition and speed. The ghost is the best run of the song's comparable setups
// (speed within 10% of the speed now), and the best of that band is the best of its own setup, so
// keeping each setup's best keeps the ghost whatever speed is asked for. Deleting a set deletes
// its events (ON DELETE CASCADE). The runs themselves stay.
constexpr std::string_view kPruneEvents =
    "DELETE FROM run_event_sets WHERE run_id IN ("
    "SELECT s.run_id FROM run_event_sets s JOIN runs r ON r.id = s.run_id "
    "WHERE r.profile_id = ?1 AND r.song_hash = ?2 AND s.pinned = 0 "
    "ORDER BY s.run_id DESC LIMIT -1 OFFSET ?3) "
    "AND (?4 = 0 OR run_id NOT IN ("
    "SELECT b.run_id FROM (SELECT s2.run_id AS run_id, ROW_NUMBER() OVER ("
    "PARTITION BY r2.mode, r2.hands, r2.transpose_semitones, r2.speed_percent "
    "ORDER BY r2.all_hit * 1.0 / r2.all_graded DESC, r2.all_graded DESC, s2.run_id DESC) AS place "
    "FROM run_event_sets s2 JOIN runs r2 ON r2.id = s2.run_id "
    "WHERE r2.profile_id = ?1 AND r2.song_hash = ?2 AND s2.complete = 1 AND r2.all_graded > 0) b "
    "WHERE b.place = 1))";
// A pin takes a run of the profile ?3 only: run ids are the database's, but a profile pins its own.
constexpr std::string_view kPinEvents =
    "UPDATE run_event_sets SET pinned = ?2 WHERE run_id = ?1 AND "
    "run_id IN (SELECT id FROM runs WHERE profile_id = ?3)";
constexpr std::string_view kSelectEventSet =
    "SELECT r.mode, r.hands, r.speed_percent, r.transpose_semitones, r.all_graded, r.all_hit, "
    "s.dropped, s.expected_notes, s.complete, s.pinned, r.song_hash FROM run_event_sets s "
    "JOIN runs r ON r.id = s.run_id WHERE s.run_id = ?1 AND r.profile_id = ?2";
// The run a record names: the fields core's same_run() compares (the unique index runs_same_run).
constexpr std::string_view kSelectRunId =
    "SELECT id FROM runs WHERE profile_id = ?1 AND song_hash = ?2 AND start_utc_s = ?3 AND "
    "mode = ?4 AND hands = ?5 AND speed_percent = ?6 AND transpose_semitones = ?7";
constexpr std::string_view kSelectEvents =
    "SELECT kind, song_ms, error_ms, key, velocity, note_index, hand, grade FROM run_events "
    "WHERE run_id = ?1 ORDER BY seq";
// A song's runs that kept events: few (the newest N and the pinned), so the rule in core ranks them.
constexpr std::string_view kSelectCandidates =
    "SELECT r.id, r.mode, r.hands, r.speed_percent, r.transpose_semitones, s.complete, "
    "r.all_graded, r.all_hit FROM run_event_sets s JOIN runs r ON r.id = s.run_id "
    "WHERE r.profile_id = ?1 AND r.song_hash = ?2 ORDER BY r.id";
constexpr std::string_view kInsertMeasure =
    "INSERT INTO run_measures (run_id, measure, graded, missed, wrong) VALUES (?1, ?2, ?3, ?4, ?5)";
// The shape of a database's tables, not the text that made them: each table's columns (name,
// declared type, not null, primary key position) and the names of its indexes, all normalised
// (names lower case, types upper case, column order and defaults left out). Two databases of one
// schema have the same shape however they were made or by which SQLite version: the statement in
// sqlite_master keeps the spelling of whatever created it (whitespace, quoting, a default's text, an
// ALTER TABLE's rewrite), which says nothing about what the tables are.
struct ColumnShape {
    std::string type;
    bool not_null = false;
    int pk = 0;
    friend bool operator==(const ColumnShape&, const ColumnShape&) = default;
};
struct SchemaShape {
    std::map<std::string, std::map<std::string, ColumnShape>> tables; // table, then column
    std::set<std::string> indexes;
};
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}
int read_shape(sql::Database& db, SchemaShape& out) {
    out = {};
    std::vector<std::string> tables;
    {
        sql::Statement stmt;
        int rc = stmt.prepare(db.get(), "SELECT type, name FROM sqlite_master "
                                        "WHERE substr(name, 1, 7) <> 'sqlite_' "
                                        "AND type IN ('table', 'index')");
        if (rc != SQLITE_OK) {
            return rc;
        }
        while ((rc = stmt.step()) == SQLITE_ROW) {
            if (stmt.column_text(0) == "table") {
                tables.push_back(stmt.column_text(1));
            } else {
                out.indexes.insert(lower(stmt.column_text(1)));
            }
        }
        if (rc != SQLITE_DONE) {
            return rc;
        }
    }
    for (const std::string& table : tables) {
        std::string quoted = "'";
        for (const char c : table) {
            quoted += c == '\'' ? std::string("''") : std::string(1, c);
        }
        quoted += "'";
        sql::Statement stmt;
        int rc = stmt.prepare(db.get(), "SELECT name, type, \"notnull\", pk FROM pragma_table_info(" +
                                            quoted + ")");
        if (rc != SQLITE_OK) {
            return rc;
        }
        auto& columns = out.tables[lower(table)];
        while ((rc = stmt.step()) == SQLITE_ROW) {
            columns[lower(stmt.column_text(0))] = {upper(stmt.column_text(1)),
                                                   stmt.column_int(2) != 0,
                                                   static_cast<int>(stmt.column_int(3))};
        }
        if (rc != SQLITE_DONE) {
            return rc;
        }
    }
    return SQLITE_OK;
}
// Whether `have` has every table of `want` with every column of it, by name. Extra tables, columns
// and indexes in `have`, and another declared type, are none of our business.
bool shape_covers(const SchemaShape& have, const SchemaShape& want) {
    for (const auto& [table, columns] : want.tables) {
        const auto found = have.tables.find(table);
        if (found == have.tables.end()) {
            return false;
        }
        for (const auto& column : columns) {
            if (found->second.find(column.first) == found->second.end()) {
                return false;
            }
        }
    }
    return true;
}
// Every table, column and index of the one, and no others, as in the other.
bool shape_equal(const SchemaShape& a, const SchemaShape& b) {
    return a.tables == b.tables && a.indexes == b.indexes;
}
// The shape each migration leaves, built in memory: [i] is after migrations_[i].
int reference_shapes(std::span<const Migration> migrations, std::vector<SchemaShape>& out) {
    sql::Database reference;
    int rc = reference.open(":memory:");
    out.clear();
    for (const Migration& m : migrations) {
        if (rc == SQLITE_OK) {
            rc = reference.run_script(m.sql);
        }
        SchemaShape shape;
        if (rc == SQLITE_OK) {
            rc = read_shape(reference, shape);
        }
        out.push_back(std::move(shape));
    }
    return rc;
}

} // namespace

HistoryStore::Attempt HistoryStore::attempt_open(std::string& why) {
    why.clear();
    thread_runs_.clear();
    thread_profile_rows_.clear();
    thread_profile_settings_.clear();
    thread_profile_ids_.clear();
    thread_prefs_.clear();
    thread_groups_.clear();
    thread_members_.clear();
    thread_sources_.clear();
    thread_versions_.clear();
    thread_practice_.clear();
    const Attempt attempt = open_steps(why);
    if (attempt != Attempt::Ready) {
        thread_runs_.clear();
        thread_profile_rows_.clear();
        thread_profile_settings_.clear();
        thread_profile_ids_.clear();
        thread_prefs_.clear();
        thread_groups_.clear();
        thread_members_.clear();
        thread_sources_.clear();
        thread_versions_.clear();
        thread_practice_.clear();
        close_database();
    }
    return attempt;
}

// The failure of result code `rc`, with the connection's message as the reason.
HistoryStore::Attempt HistoryStore::fail(int rc, std::string& why) {
    why = db_.error_message();
    switch (sql::classify(rc)) {
    case sql::Failure::Locked:
        return Attempt::Locked;
    case sql::Failure::Damaged:
        return Attempt::Damaged;
    default:
        return unusable_for(rc);
    }
}

// A failure that is none of the others: a file the program may not write is told as that, since
// it never passes by waiting, and the player is told why runs are not saved.
HistoryStore::Attempt HistoryStore::unusable_for(int rc) {
    const int primary = rc & 0xFF;
    return primary == SQLITE_READONLY || primary == SQLITE_CANTOPEN || primary == SQLITE_PERM
               ? Attempt::Unwritable
               : Attempt::Unusable;
}

// Whether a database file that is already there belongs to a newer etude. Nothing is written: the
// version is read from the file's own header, and then through a read-only connection, so a newer
// database comes out of this byte for byte as it went in (setting WAL mode, for one, would rewrite
// its header). The one thing that can still change it is a hot rollback journal of an interrupted
// write, which only a read-write connection can roll back (`open_steps`, which reads the version
// again first thing and never goes on with a newer file).
HistoryStore::Attempt HistoryStore::check_version(std::string& why) {
    if (!path_present(path())) {
        return Attempt::Ready;
    }
    if (const std::int64_t found = header_user_version(path()); found > migrations_.back().version) {
        why = "schema version " + std::to_string(found) + ", this build knows " +
              std::to_string(migrations_.back().version);
        return Attempt::Newer;
    }
    int rc = db_.open(path(), sql::Database::Mode::ReadOnly);
    if (rc != SQLITE_OK) {
        // A read-only open that fails is not about the contents; the read-write open below says
        // more.
        db_.close();
        return Attempt::Ready;
    }
    db_.set_checkpoint_on_close(false);
    db_.set_busy_timeout(options_.open_busy_timeout_ms);
    std::int64_t version = 0;
    rc = db_.query_int("PRAGMA user_version", version);
    if (rc != SQLITE_OK) {
        if ((rc & 0xFF) == SQLITE_READONLY) {
            // An earlier start died in the middle of a transaction and left its rollback journal:
            // only a read-write connection can roll it back, so a read-only one says "attempt to
            // write a readonly database" for as long as the journal is there. That says nothing
            // about the contents; the read-write open below rolls it back and reads the version
            // itself (`migrate` refuses a newer one).
            db_.close();
            return Attempt::Ready;
        }
        return fail(rc, why);
    }
    db_.close();
    if (version > migrations_.back().version) {
        why = "schema version " + std::to_string(version) + ", this build knows " +
              std::to_string(migrations_.back().version);
        return Attempt::Newer;
    }
    return Attempt::Ready;
}

// Reads the version through the read-write connection just opened: Newer for a file this build must
// not touch, Ready otherwise.
HistoryStore::Attempt HistoryStore::open_version(std::string& why) {
    std::int64_t version = 0;
    const int rc = db_.query_int("PRAGMA user_version", version);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    if (version > migrations_.back().version) {
        why = "schema version " + std::to_string(version) + ", this build knows " +
              std::to_string(migrations_.back().version);
        return Attempt::Newer;
    }
    return Attempt::Ready;
}

HistoryStore::Attempt HistoryStore::open_steps(std::string& why) {
    if (const Attempt a = check_version(why); a != Attempt::Ready) {
        return a;
    }
    int rc = db_.open(path());
    if (rc != SQLITE_OK) {
        why = "cannot open (result code " + std::to_string(rc) + ")";
        return sql::classify(rc) == sql::Failure::Damaged ? Attempt::Damaged : unusable_for(rc);
    }
    // Until the schema is known to be ours and at a version this build can use, nothing may write
    // the file, and closing the last connection to a WAL database writes its -wal into it: a file
    // from a newer etude (or one that is not ours) keeps its bytes whichever way it was opened.
    db_.set_checkpoint_on_close(false);
    db_.set_busy_timeout(options_.open_busy_timeout_ms);
    // The version first, before anything that can end with the file set aside (an integrity check
    // that fails): the first read rolls a hot journal back (SQLite's own recovery, and the one
    // change this open may make), and a file of a newer etude is then left where it is.
    if (const Attempt a = open_version(why); a != Attempt::Ready) {
        return a;
    }
    // The first statements that read the file are where a file that is not a database, or is
    // locked, shows up. Nothing here changes the file: the journal mode is set after the schema is
    // in place, so a database that is not ours is not touched when its migration fails.
    rc = db_.run_script("PRAGMA foreign_keys = ON; PRAGMA synchronous = FULL");
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    std::string check;
    rc = db_.query_text("PRAGMA quick_check(1)", check);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    if (check != "ok") {
        why = "integrity check: " + check;
        return Attempt::Damaged;
    }
    Attempt a = migrate(why);
    if (a != Attempt::Ready) {
        return a;
    }
    db_.set_checkpoint_on_close(true);
    std::string journal;
    rc = db_.query_text("PRAGMA journal_mode = WAL", journal);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    if (!prepare_statements()) {
        why = db_.error_message();
        return Attempt::Unusable;
    }
    // The profiles the database holds, before the legacy runs and the journal are written: a run
    // of a profile it lacks is kept under the default one.
    a = load_profiles(why);
    if (a != Attempt::Ready) {
        return a;
    }
    a = import_legacy_runs(why);
    if (a != Attempt::Ready) {
        return a;
    }
    a = replay_journal(why);
    if (a != Attempt::Ready) {
        return a;
    }
    compact_if_worthwhile();
    a = load_everything(why);
    if (a != Attempt::Ready) {
        return a;
    }
    db_.set_busy_timeout(options_.write_busy_timeout_ms);
    return Attempt::Ready;
}

// Reads the profiles (again: the journal may have made some) and everything the history holds, of
// every profile.
HistoryStore::Attempt HistoryStore::load_everything(std::string& why) {
    for (Attempt (HistoryStore::*load)(std::string&) :
         {&HistoryStore::load_profiles, &HistoryStore::load_runs, &HistoryStore::load_prefs,
          &HistoryStore::load_groups, &HistoryStore::load_sources, &HistoryStore::load_versions,
          &HistoryStore::load_practice}) {
        const Attempt a = (this->*load)(why);
        if (a != Attempt::Ready) {
            return a;
        }
    }
    return Attempt::Ready;
}

// A file with tables and `user_version` 0 is what `sqlite3 h.db .dump | sqlite3 new.db` makes (the
// dump has no PRAGMA), or a restore by another tool: our tables, but no version, so migration 1
// would fail on "table profiles already exists" for ever. The tables' shape (not their text: see
// SchemaShape) is compared with what each migration builds in memory. The same shape as one version
// is that version. Failing that, the highest version whose tables and columns the file all has
// (extra ones, a missing index, are no matter): it is ours, a little different, and the
// migrations go on from there. Only when it is not even that is the file not ours to migrate or to
// move, and it is given up for this session (Foreign) with the reason. A file with only other
// tables (nothing named profiles, songs or runs) is migrated as before: ours are made beside them.
HistoryStore::Attempt HistoryStore::adopt_unversioned(std::int64_t& version, std::string& why) {
    std::int64_t ours = 0;
    int rc = db_.query_int("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND "
                           "name IN ('profiles', 'songs', 'runs')",
                           ours);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    if (ours == 0) {
        return Attempt::Ready; // a new database, or one with only tables of someone else's
    }
    SchemaShape found;
    rc = read_shape(db_, found);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    std::vector<SchemaShape> shapes;
    if (reference_shapes(migrations_, shapes) != SQLITE_OK) {
        why = "cannot build the reference schema";
        return Attempt::Unusable;
    }
    int matched = 0;
    int covered = 0;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (shape_equal(found, shapes[i])) {
            matched = migrations_[i].version;
        }
        if (shape_covers(found, shapes[i])) {
            covered = migrations_[i].version;
        }
    }
    const int chosen = matched != 0 ? matched : covered;
    if (chosen == 0) {
        why = "tables but no schema version (user_version 0), and they are not tables of any "
              "schema version this build knows";
        return Attempt::Foreign;
    }
    rc = db_.run_script("PRAGMA user_version = " + std::to_string(chosen));
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    log("history: " + path().string() + " had tables but no schema version; they are version " +
        std::to_string(chosen) + (matched != 0 ? "" : " (with differences)") + ", set so");
    version = chosen;
    return Attempt::Ready;
}

// A file at the latest version is trusted unless it clearly is not ours: a core table (profiles,
// songs, runs) missing, or without the columns we write. Anything else that differs (an extra
// table, column or index, a missing index, another spelling of the statements) opens as it is: it
// is a player's real database, and refusing it would stop it saving. The check is there so a foreign
// file that happens to carry our version number is not switched to WAL mode and then fails on its
// first statement.
HistoryStore::Attempt HistoryStore::check_tables(std::string& why) {
    SchemaShape found;
    int rc = read_shape(db_, found);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    std::vector<SchemaShape> shapes;
    if (reference_shapes(migrations_, shapes) != SQLITE_OK) {
        why = "cannot build the reference schema";
        return Attempt::Unusable;
    }
    for (const std::string table : {"profiles", "songs", "runs"}) {
        SchemaShape want;
        const auto it = shapes.back().tables.find(table);
        if (it == shapes.back().tables.end()) {
            continue; // a test's own migration list
        }
        want.tables[table] = it->second;
        if (!shape_covers(found, want)) {
            why = "schema version " + std::to_string(migrations_.back().version) +
                  " but no table " + table + " of ours";
            return Attempt::Foreign;
        }
    }
    return Attempt::Ready;
}

HistoryStore::Attempt HistoryStore::migrate(std::string& why) {
    std::int64_t version = 0;
    int rc = db_.query_int("PRAGMA user_version", version);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    const int latest = migrations_.back().version;
    if (version > latest) {
        why = "schema version " + std::to_string(version) + ", this build knows " +
              std::to_string(latest);
        return Attempt::Newer;
    }
    if (version == latest) {
        if (const Attempt a = check_tables(why); a != Attempt::Ready) {
            return a;
        }
    }
    if (version == 0) {
        if (const Attempt a = adopt_unversioned(version, why); a != Attempt::Ready) {
            return a;
        }
    }
    if (version == 0) {
        // A new file is made in incremental mode, which only works before its first table exists:
        // an old file in mode NONE changes only through a VACUUM (compact_if_worthwhile). Failing
        // to set it costs nothing but that mode.
        (void)db_.run_script("PRAGMA auto_vacuum = INCREMENTAL");
    }
    for (const Migration& m : migrations_) {
        if (m.version <= version) {
            continue;
        }
        // One transaction per step, so a failure leaves the database at the version before it.
        rc = db_.run_script("BEGIN IMMEDIATE");
        if (rc == SQLITE_OK) {
            rc = db_.run_script(m.sql);
        }
        if (rc == SQLITE_OK) {
            rc = db_.run_script("PRAGMA user_version = " + std::to_string(m.version));
        }
        if (rc == SQLITE_OK) {
            rc = db_.run_script("COMMIT");
        }
        if (rc != SQLITE_OK) {
            const Attempt a = fail(rc, why);
            (void)db_.run_script("ROLLBACK");
            // Whatever failed, only damage (corrupt, not a database) may move the file; a full
            // disk, a read-only file or a step that does not apply leaves it where it is.
            return a;
        }
    }
    return Attempt::Ready;
}

bool HistoryStore::prepare_statements() {
    sqlite3* db = db_.get();
    return begin_.prepare(db, "BEGIN IMMEDIATE") == SQLITE_OK &&
           commit_.prepare(db, "COMMIT") == SQLITE_OK &&
           rollback_.prepare(db, "ROLLBACK") == SQLITE_OK &&
           prepare_profile_statements() &&
           upsert_song_.prepare(db, kUpsertSong) == SQLITE_OK &&
           insert_run_.prepare(db, insert_run_sql()) == SQLITE_OK &&
           insert_measure_.prepare(db, kInsertMeasure) == SQLITE_OK &&
           upsert_prefs_.prepare(db, kUpsertPrefs) == SQLITE_OK &&
           (!has_groups() || (insert_group_.prepare(db, kInsertGroup) == SQLITE_OK &&
                              rename_group_.prepare(db, kRenameGroup) == SQLITE_OK &&
                              delete_group_.prepare(db, kDeleteGroup) == SQLITE_OK &&
                              delete_group_songs_.prepare(db, kDeleteGroupSongs) == SQLITE_OK &&
                              add_group_song_.prepare(db, kAddGroupSong) == SQLITE_OK &&
                              drop_group_song_.prepare(db, kDropGroupSong) == SQLITE_OK &&
                              move_group_.prepare(db, kMoveGroup) == SQLITE_OK)) &&
           (!has_sources() || upsert_source_.prepare(db, kUpsertSource) == SQLITE_OK) &&
           (!has_versions() || (insert_override_.prepare(db, kInsertOverride) == SQLITE_OK &&
                                delete_override_.prepare(db, kDeleteOverride) == SQLITE_OK)) &&
           (!has_practice() ||
            upsert_practice_.prepare(db, has_practice_split()    ? kUpsertPractice11
                                         : has_practice_offer()  ? kUpsertPractice10
                                         : has_practice_extras() ? kUpsertPractice8
                                                                 : kUpsertPractice) == SQLITE_OK) &&
           (!has_practice_extras() || (delete_parts_.prepare(db, kDeleteParts) == SQLITE_OK &&
                                       insert_part_.prepare(db, kInsertPart) == SQLITE_OK)) &&
           (!has_events() || (insert_event_set_.prepare(db, kInsertEventSet) == SQLITE_OK &&
                              insert_event_.prepare(db, kInsertEvent) == SQLITE_OK &&
                              prune_events_.prepare(db, kPruneEvents) == SQLITE_OK &&
                              pin_events_.prepare(db, kPinEvents) == SQLITE_OK &&
                              select_event_set_.prepare(db, kSelectEventSet) == SQLITE_OK &&
                              select_events_.prepare(db, kSelectEvents) == SQLITE_OK &&
                              select_candidates_.prepare(db, kSelectCandidates) == SQLITE_OK &&
                              select_run_id_.prepare(db, kSelectRunId) == SQLITE_OK));
}

// Moves the database and its WAL files out of the way, so that a new one can start in their place.
// The three files go together under one name: if any rename fails, the ones already moved are put
// back and nothing is deleted. On failure `moved` says why; on success it is the new name. A WAL
// file left beside a new database would be replayed into it, which is why they move too.
bool HistoryStore::set_aside(std::string& moved) {
    const fs::path main = path();
    const fs::path target = unused_name(main, ".damaged");
    if (target.empty()) {
        moved = "every .damaged name is taken, remove some of them";
        return false;
    }
    struct Move {
        fs::path from;
        fs::path to;
    };
    std::vector<Move> done;
    for (std::string_view suffix : {"", "-wal", "-shm"}) {
        fs::path from = main;
        from += suffix;
        fs::path to = target;
        to += suffix;
        if (!path_present(from)) {
            continue;
        }
        std::error_code ec;
        if (path_present(to)) {
            ec = std::make_error_code(std::errc::file_exists);
        } else {
            fs::rename(from, to, ec);
        }
        if (ec) {
            for (auto it = done.rbegin(); it != done.rend(); ++it) {
                std::error_code ignore;
                fs::rename(it->to, it->from, ignore);
            }
            moved = "cannot move " + from.filename().string() + " to " + to.filename().string() +
                    ": " + ec.message();
            return false;
        }
        done.push_back({from, to});
    }
    moved = target.filename().string();
    return true;
}

// A write found the open database corrupt: close it, set it aside and open a new one. False when
// that cannot be done now (the runs stay queued) or the store is closing.
bool HistoryStore::recover_from_damage(const std::string& reason) {
    close_database();
    std::string moved;
    if (set_aside(moved)) {
        log("history: " + path().string() + " is damaged (" + reason + "); kept as " + moved +
            ", starting a new one");
        say("Practice history damaged; kept as " + moved + ", starting a new one");
    } else {
        log("history: " + path().string() + " is damaged (" + reason +
            ") and could not be moved aside (" + moved +
            "); runs are kept in memory and saved when it opens");
        say("Practice history unusable for now; runs are saved when it opens");
        {
            std::lock_guard lock(mutex_);
            set_state(HistoryState::Waiting);
        }
        if (!sleep_for_retry()) {
            give_up(false);
            return false;
        }
    }
    if (open_loop(false)) {
        return true;
    }
    return false;
}

} // namespace etude::app
