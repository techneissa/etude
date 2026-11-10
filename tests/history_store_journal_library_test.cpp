// SPDX-License-Identifier: GPL-3.0-or-later
// The history journal: the library's changes held at quit, a journal that cannot be removed, and a later quit adding to an earlier one.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "sqlite_db.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>

#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

#if !defined(_WIN32)

namespace {

// The song 0x77 and the group 5 the seed has, and the run (with its played notes) it pins.
constexpr std::uint64_t kSong = 0x77;
constexpr std::uint64_t kOther = 0x88;

void seed_library(const fs::path& dir) {
    HistoryStore seed(dir, quiet());
    REQUIRE(open_now(seed).empty());
    seed.add_run(sample_run(1), "/songs/a.mid", sample_events(4));
    SongPrefs old;
    old.title = "Old name";
    seed.set_song_prefs(kSong, old);
    seed.edit_groups(group_edit(GroupEdit::Kind::Create, 5, "Exam"));
    seed.edit_groups(group_edit(GroupEdit::Kind::Add, 5, {}, kSong));
    seed.edit_version_overrides({VersionEdit::Kind::Split, 0xAA, 0});
    REQUIRE(seed.flush());
}

// The library's changes of a quit, one of each kind, on top of what seed_library made.
void make_library_changes(HistoryStore& store, std::int64_t run_id) {
    SongPrefs renamed;
    renamed.title = "New name";
    renamed.composer = "Chopin";
    renamed.favourite = true;
    store.set_song_prefs(kSong, renamed);
    SongPrefs fresh;
    fresh.title = "Fresh";
    store.set_song_prefs(kOther, fresh);
    store.edit_groups(group_edit(GroupEdit::Kind::Create, 6, "Warm-ups"));
    store.edit_groups(group_edit(GroupEdit::Kind::Add, 6, {}, kOther));
    store.edit_groups(group_edit(GroupEdit::Kind::Add, 6, {}, kSong));
    store.edit_groups(group_edit(GroupEdit::Kind::Drop, 5, {}, kSong));
    store.edit_groups(group_edit(GroupEdit::Kind::Rename, 5, "Exams"));
    store.edit_groups(group_edit(GroupEdit::Kind::Remove, 5));
    store.set_song_origin(kOther, sample_origin());
    store.edit_version_overrides({VersionEdit::Kind::Join, kSong, kOther});
    store.edit_version_overrides({VersionEdit::Kind::Clear, 0xAA, 0});
    store.set_run_pinned(run_id, true);
}

// What make_library_changes leaves in the database, whichever way it got there.
void check_library_changes(const fs::path& dir) {
    Raw raw(dir / "history.db");
    const auto col = [](std::uint64_t h) { return std::to_string(hash_to_column(h)); };
    CHECK(raw.num("SELECT COUNT(*) FROM song_settings") == 2);
    CHECK(raw.text("SELECT title FROM song_settings WHERE content_hash = " + col(kSong)) ==
          "New name");
    CHECK(raw.num("SELECT favourite FROM song_settings WHERE content_hash = " + col(kSong)) == 1);
    CHECK(raw.text("SELECT title FROM song_settings WHERE content_hash = " + col(kOther)) ==
          "Fresh");
    CHECK(raw.num("SELECT COUNT(*) FROM song_groups") == 1); // group 5 is gone, 6 is new
    CHECK(raw.text("SELECT name FROM song_groups") == "Warm-ups");
    CHECK(raw.num("SELECT COUNT(*) FROM song_group_members") == 2);
    CHECK(raw.num("SELECT COUNT(*) FROM song_sources") == 1);
    CHECK(raw.text("SELECT credit FROM song_sources") == "Bernd Krueger");
    CHECK(raw.num("SELECT COUNT(*) FROM version_overrides") == 1); // the split went, the join is in
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE pinned = 1") == 1);
}

} // namespace

TEST_CASE("journal: song settings, groups, sources, overrides and pins held at quit are saved at the next start, once") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    seed_library(dir.path);
    std::int64_t run_id = 0;
    {
        Raw raw(dir.path / "history.db");
        run_id = run_id_at(raw, 1);
    }
    make_writable(dir.path, false);
    HistoryOptions options = quiet();
    options.retry_interval_ms = 30;
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        REQUIRE(store.state() == HistoryState::Ready);
        make_library_changes(store, run_id);
        CHECK_FALSE(flush_fails(store)); // every write finds the file read-only
        store.close();
        CHECK(store.take_errors().empty()); // all of it is in the journal
    }
    const fs::path file = dir.path / kJournal;
    const std::string journal = read_all(file);
    const JournalDecode kept = decode_journal(journal);
    CHECK(kept.damaged == 0);
    // In the order they were made: 2 settings, 6 group edits, 1 source, 2 override rows, 1 pin.
    REQUIRE(kept.entries.size() == 12);
    CHECK(std::holds_alternative<JournalPrefs>(kept.entries[0]));
    CHECK(std::holds_alternative<JournalPrefs>(kept.entries[1]));
    for (std::size_t i = 2; i < 8; ++i) {
        CHECK(std::holds_alternative<JournalGroups>(kept.entries[i]));
    }
    CHECK(std::holds_alternative<JournalOrigins>(kept.entries[8]));
    CHECK(std::holds_alternative<JournalVersions>(kept.entries[9]));
    CHECK(std::holds_alternative<JournalVersions>(kept.entries[10]));
    REQUIRE(std::holds_alternative<JournalPin>(kept.entries[11]));
    CHECK(std::get<JournalPin>(kept.entries[11]).run_id == run_id);
    CHECK(std::get<JournalOrigins>(kept.entries[8]).items.at(0).second == sample_origin());
    {
        Raw raw(dir.path / "history.db"); // the database is as the seed left it
        CHECK(raw.num("SELECT COUNT(*) FROM song_settings") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE pinned = 1") == 0);
    }

    make_writable(dir.path, true);
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.song_prefs().at(kSong).title == "New name"); // loaded after the replay
        CHECK(store.groups().groups().size() == 1);
        CHECK(store.song_origins().size() == 1);
        CHECK(store.runs().size() == 1);
    }
    CHECK_FALSE(fs::exists(file));
    check_library_changes(dir.path);

    // The file could not be deleted last time, or a copy came back: the same records again.
    for (int again = 0; again < 2; ++again) {
        write_all(file, journal);
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK_FALSE(fs::exists(file));
        check_library_changes(dir.path);
    }
}

TEST_CASE("journal: a long job of song settings is cut into records and saved whole") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    {
        HistoryStore seed(dir.path, quiet());
        REQUIRE(open_now(seed).empty());
    }
    make_writable(dir.path, false);
    std::vector<std::pair<std::uint64_t, SongPrefs>> many;
    for (std::uint64_t n = 1; n <= 450; ++n) {
        SongPrefs p;
        p.title = "Song " + std::to_string(n);
        many.emplace_back(n, p);
    }
    HistoryOptions options = quiet();
    options.retry_interval_ms = 30;
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        store.set_song_prefs(many);
        CHECK_FALSE(flush_fails(store));
        store.close();
        CHECK(store.take_errors().empty());
    }
    const JournalDecode kept = decode_journal(read_all(dir.path / kJournal));
    CHECK(kept.damaged == 0);
    CHECK(kept.entries.size() == 3); // 200, 200 and 50
    make_writable(dir.path, true);
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(store.song_prefs().size() == 450);
    CHECK(store.song_prefs().at(450).title == "Song 450");
}

TEST_CASE("journal: when it cannot be written, the library's changes are in the loss sentence, and only then") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    seed_library(dir.path);
    std::int64_t run_id = 0;
    {
        Raw raw(dir.path / "history.db");
        run_id = run_id_at(raw, 1);
    }
    make_writable(dir.path, false);
    fs::create_directory(dir.path / kJournal); // a folder where the file should be
    HistoryOptions options = quiet();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    open_now(store);
    make_library_changes(store, run_id);
    store.add_run(sample_run(2));
    store.set_song_practice(kSong, busy_practice());
    CHECK_FALSE(flush_fails(store));
    store.close();
    const std::vector<std::string> errors = store.take_errors();
    REQUIRE(errors.size() == 1);
    // One run, one practice record, and the 12 jobs of the library: 2 settings, 6 group edits (one
    // job each), a source, 2 override edits and a pin.
    CHECK(errors[0] == "1 practice run, 1 practice setting and 12 library changes could not be saved");
}

TEST_CASE("journal: a journal that is not ours or from a newer etude is left as it is, and said") {
    TempDir dir;
    const std::string newer = "etude journal 2\nrecords this build cannot read";
    write_all(dir.path / kJournal, newer);
    {
        HistoryStore store(dir.path, quiet());
        store.open();
        REQUIRE(store.wait_until_settled(10s));
        const HistoryPoll poll = store.poll();
        CHECK(poll.message.find("history.journal") != std::string::npos);
        CHECK(poll.message.find("newer Etude") != std::string::npos);
        CHECK(store.state() == HistoryState::Ready);
        store.add_run(sample_run(1)); // the history works as usual
        REQUIRE(store.flush());
    }
    CHECK(read_all(dir.path / kJournal) == newer);
    check_landed(dir.path, 1, false);
    CHECK(read_all(dir.path / kJournal) == newer);

    write_all(dir.path / kJournal, "my shopping list\n");
    Log log;
    HistoryStore store(dir.path, log.options());
    REQUIRE(open_now(store).find("history.journal") != std::string::npos);
    CHECK(read_all(dir.path / kJournal) == "my shopping list\n");
}

TEST_CASE("journal: a later quit adds to the journal an earlier one left") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    REQUIRE(quit_with_held_jobs(dir.path).empty()); // three records, the database still read-only
    {
        HistoryOptions options = quiet();
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        open_now(store); // the journal could not be replayed into a file that cannot be written
        store.add_run(sample_run(4));
        store.close();
        CHECK(store.take_errors().empty());
    }
    CHECK(decode_journal(read_all(dir.path / kJournal)).entries.size() == 4);
    make_writable(dir.path, true);
    check_landed(dir.path, 4);
}
#endif


TEST_CASE("journal: a practice record written before the split or the time was added replays with defaults") {
    SongPractice expected = busy_practice();

    // Before the time was added: the split is kept.
    JournalDecode back = decode_journal(encode_journal_header() + practice_record_without(1));
    REQUIRE(back.entries.size() == 1);
    CHECK(back.damaged == 0);
    CHECK(std::get<JournalPractice>(back.entries[0]).practice == expected);
    CHECK(std::get<JournalPractice>(back.entries[0]).at_utc_s == 0);

    // Before the split too: Auto.
    const std::string bytes = encode_journal_header() + practice_record_without(2);
    back = decode_journal(bytes);
    REQUIRE(back.entries.size() == 1);
    expected.split_point = kAutoSplit;
    CHECK(std::get<JournalPractice>(back.entries[0]).practice == expected);

    // And the store takes it in: the split is Auto, the rest as written.
    TempDir dir;
    write_all(dir.path / kJournal, bytes);
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    REQUIRE(store.song_practice().count(0x77) == 1);
    CHECK(store.song_practice().at(0x77) == expected);
    CHECK_FALSE(fs::exists(dir.path / kJournal));
}

TEST_CASE("journal: a placed split round-trips through the bytes and the replay") {
    JournalPractice practice;
    practice.hash = 0x78;
    practice.practice = busy_practice();
    practice.at_utc_s = now_s();
    TempDir dir;
    write_all(dir.path / kJournal, encode_journal_header() + encode_journal_entry(practice));
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    REQUIRE(store.song_practice().count(0x78) == 1);
    CHECK(store.song_practice().at(0x78).split_point == 60);
    CHECK(store.song_practice().at(0x78) == busy_practice());
}

TEST_CASE("journal: a header cut short is an empty journal, not a foreign file") {
    for (const std::string_view cut : {std::string_view("etude journal "),
                                       std::string_view("etude journal 1"),
                                       std::string_view("etude jo")}) {
        const JournalDecode d = decode_journal(cut);
        CHECK(d.truncated);
        CHECK_FALSE(d.bad_header);
        CHECK(d.valid_end == 0);
        CHECK(d.entries.empty());
    }
    CHECK(decode_journal("etude journal x\n").bad_header);
    CHECK(decode_journal("etude journal \n").bad_header);
#if !defined(_WIN32)
    TempDir dir;
    const fs::path file = dir.path / kJournal;
    write_all(file, "etude journal "); // 16 bytes
    JournalRun run;
    run.record = sample_run(1);
    CHECK(append_journal(file, std::vector<JournalEntry>{run}).saved == 1);
    const JournalDecode d = decode_journal(read_all(file));
    CHECK(d.entries.size() == 1);
    CHECK_FALSE(d.truncated);
#endif
}

TEST_CASE("journal: a damaged length in record 2 of 5 keeps the file whole, and the first record lands") {
    std::string bytes = encode_journal_header();
    std::size_t second = 0;
    for (int n = 1; n <= 5; ++n) {
        JournalRun run;
        run.record = sample_run(n);
        if (n == 2) {
            second = bytes.size();
        }
        bytes += encode_journal_entry(run);
    }
    std::string damaged = bytes;
    for (int i = 0; i < 4; ++i) {
        damaged[second + static_cast<std::size_t>(i)] = '\xFF'; // an impossible length
    }
    const JournalDecode d = decode_journal(damaged);
    CHECK(d.entries.size() == 1);
    CHECK(d.framing_damaged);
    CHECK_FALSE(d.truncated);
    CHECK(d.valid_end == second);

    TempDir dir;
    write_all(dir.path / kJournal, damaged);
    Log log;
    {
        HistoryStore store(dir.path, log.options());
        CHECK(open_now(store).find("history.journal") != std::string::npos); // the damage is told
        CHECK(store.runs().size() == 1);
    }
    // Not deleted: the bytes after the damage are in the copy, whole.
    CHECK_FALSE(fs::exists(dir.path / kJournal));
    CHECK(read_all(dir.path / "history.journal.rejected") == damaged);
    bool said = false;
    for (const std::string& line : log.get()) {
        said = said || (line.find("after the damage were not read") != std::string::npos);
    }
    CHECK(said);
#if !defined(_WIN32)
    // A quit that adds to such a file moves it aside the same way and starts the new one from what
    // could be read.
    TempDir other;
    write_all(other.path / kJournal, damaged);
    JournalRun extra;
    extra.record = sample_run(9);
    CHECK(append_journal(other.path / kJournal, std::vector<JournalEntry>{extra}).saved == 1);
    CHECK(read_all(other.path / "history.journal.rejected") == damaged);
    const JournalDecode fresh = decode_journal(read_all(other.path / kJournal));
    CHECK_FALSE(fresh.framing_damaged);
    REQUIRE(fresh.entries.size() == 2);
    CHECK(std::get<JournalRun>(fresh.entries[0]).record == sample_run(1));
    CHECK(std::get<JournalRun>(fresh.entries[1]).record == sample_run(9));
#endif
}

TEST_CASE("journal: a journal that cannot be removed is not replayed over what changed since") {
    TempDir dir;
    JournalPractice practice;
    practice.hash = 0x77;
    practice.practice = busy_practice();
    practice.at_utc_s = now_s();
    JournalRun run;
    run.record = sample_run(2);
    const std::string journal =
        encode_journal_header() + encode_journal_entry(run) + encode_journal_entry(practice);
    write_all(dir.path / kJournal, journal);

    // Nothing can remove or rename it.
    HistoryOptions stuck = quiet();
    stuck.remove_file = [](const fs::path&) { return false; };
    stuck.rename_file = [](const fs::path&, const fs::path&) { return false; };
    stuck.truncate_file = [](const fs::path&) { return false; };
    {
        HistoryStore store(dir.path, stuck);
        const std::string said = open_now(store);
        CHECK(said.find("could not be removed") != std::string::npos);
        CHECK(store.runs().size() == 1);
        CHECK(store.song_practice().at(0x77).speed_percent == 65);
    }
    CHECK(read_all(dir.path / kJournal) == journal);

    // The player changes the song afterwards (the row is dated after the journal).
    {
        Raw raw(dir.path / "history.db");
        REQUIRE(raw.db.run_script("UPDATE song_practice SET speed_percent = 90, updated_utc_s = "
                                  "updated_utc_s + 1000") == SQLITE_OK);
    }
    {
        HistoryStore store(dir.path, quiet()); // the next start reads the journal again
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 1);                                // the run is not added twice
        CHECK(store.song_practice().at(0x77).speed_percent == 90);      // the newer row stays
    }
    CHECK_FALSE(fs::exists(dir.path / kJournal));
}

TEST_CASE("journal: a journal that cannot be removed is renamed out of the way") {
    TempDir dir;
    JournalPractice practice;
    practice.hash = 0x77;
    practice.practice = busy_practice();
    practice.at_utc_s = now_s();
    write_all(dir.path / kJournal, encode_journal_header() + encode_journal_entry(practice));
    HistoryOptions options = quiet();
    options.remove_file = [](const fs::path&) { return false; }; // the rename is the real one
    {
        HistoryStore store(dir.path, options);
        REQUIRE(open_now(store).empty());
        CHECK(store.song_practice().at(0x77).speed_percent == 65);
    }
    CHECK_FALSE(fs::exists(dir.path / kJournal));
    CHECK(fs::exists(dir.path / "history.journal.done"));
    {
        Raw raw(dir.path / "history.db");
        REQUIRE(raw.db.run_script("UPDATE song_practice SET speed_percent = 90") == SQLITE_OK);
    }
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(store.song_practice().at(0x77).speed_percent == 90); // not replayed
}

TEST_CASE("journal: a record is applied unless the database has a newer row for that song") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        SongPractice mine;
        mine.speed_percent = 90;
        store.set_song_practice(0x77, mine);
        store.set_song_practice(0x78, mine);
        REQUIRE(store.flush());
    }
    JournalPractice old_one; // written long before the row
    old_one.hash = 0x77;
    old_one.practice = busy_practice();
    old_one.at_utc_s = 1000;
    JournalPractice later; // written after it
    later.hash = 0x78;
    later.practice = busy_practice();
    later.at_utc_s = now_s() + 10'000;
    JournalPractice unknown_time; // from a writer that left the time off: applied
    unknown_time.hash = 0x79;
    unknown_time.practice = busy_practice();
    write_all(dir.path / kJournal, encode_journal_header() + encode_journal_entry(old_one) +
                                       encode_journal_entry(later) +
                                       encode_journal_entry(unknown_time));
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(store.song_practice().at(0x77).speed_percent == 90);
    CHECK(store.song_practice().at(0x78).speed_percent == 65);
    CHECK(store.song_practice().at(0x79).speed_percent == 65);
}

TEST_CASE("journal: the attempt made at quit does not replay a journal, and the jobs held join it") {
    TempDir dir;
    {
        HistoryStore seed(dir.path, quiet()); // the schema, so another connection can hold it
        REQUIRE(open_now(seed).empty());
    }
    JournalRun kept;
    kept.record = sample_run(2);
    JournalRun kept_too;
    kept_too.record = sample_run(3);
    write_all(dir.path / kJournal,
              encode_journal_header() + encode_journal_entry(kept) + encode_journal_entry(kept_too));
    sql::Database other;
    REQUIRE(other.open(dir.path / "history.db") == SQLITE_OK);
    REQUIRE(other.run_script("BEGIN EXCLUSIVE") == SQLITE_OK);
    HistoryOptions options = quiet();
    options.open_busy_timeout_ms = 20;
    options.retry_interval_ms = 60'000;
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        CHECK(store.state() == HistoryState::Waiting);
        store.add_run(sample_run(1));
        REQUIRE(other.run_script("ROLLBACK") == SQLITE_OK); // free now; closing makes the last try
    }
    {
        Raw raw(dir.path / "history.db");
        // Not replayed, and the run held joins the journal behind the older records instead of
        // going into the database ahead of them.
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 0);
    }
    CHECK(decode_journal(read_all(dir.path / kJournal)).entries.size() == 3);
    HistoryStore store(dir.path, quiet()); // the next start does
    REQUIRE(open_now(store).empty());
    CHECK(store.runs().size() == 3);
    CHECK_FALSE(fs::exists(dir.path / kJournal));
}

// ---- Review round of #299: order, the replay's guards, and what is counted
