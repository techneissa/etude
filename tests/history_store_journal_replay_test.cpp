// SPDX-License-Identifier: GPL-3.0-or-later
// The history journal: replay order and dates, damaged records told to the player, the size cap, and the loss sentence.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "sqlite_db.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <cstring>
#include <doctest/doctest.h>

#include <chrono>
#include <csignal>
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

namespace {

JournalPractice practice_at(std::uint64_t hash, int speed, std::int64_t at_utc_s) {
    JournalPractice entry;
    entry.hash = hash;
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

} // namespace

TEST_CASE("journal: two practice records of one song replay to the later one") {
    TempDir dir;
    const std::int64_t held_at = now_s() - 100; // both held at one quit
    const std::string journal =
        journal_of({practice_at(0x77, 60, held_at), practice_at(0x77, 80, held_at)});
    for (int again = 0; again < 2; ++again) { // and the same journal once more
        write_all(dir.path / kJournal, journal);
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.song_practice().at(0x77).speed_percent == 80);
    }
}

TEST_CASE("journal: a song's practice choices are dated when they were made, not when the app quit") {
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
    HistoryOptions options = quiet();
    options.retry_interval_ms = 30;
    const std::int64_t before = now_s();
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        SongPractice p;
        p.speed_percent = 60;
        store.set_song_practice(0x77, p);
        p.speed_percent = 80;
        store.set_song_practice(0x77, p);
        CHECK_FALSE(flush_fails(store));
        store.close();
        CHECK(store.take_errors().empty());
    }
    const std::int64_t after = now_s();
    const JournalDecode kept = decode_journal(read_all(dir.path / kJournal));
    REQUIRE(kept.entries.size() == 2);
    for (const JournalEntry& e : kept.entries) {
        const std::int64_t at = std::get<JournalPractice>(e).at_utc_s;
        CHECK(at >= before);
        CHECK(at <= after);
    }
    CHECK(std::get<JournalPractice>(kept.entries[0]).at_utc_s <=
          std::get<JournalPractice>(kept.entries[1]).at_utc_s);
    make_writable(dir.path, true);
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(store.song_practice().at(0x77).speed_percent == 80);
    Raw raw(store.path());
    // The row is dated by the record that made it, not by the replay.
    CHECK(raw.num("SELECT updated_utc_s FROM song_practice") <= after);
}

TEST_CASE("journal: a record the quit could not replay never lands over what the session saved after it") {
    TempDir dir;
    {
        HistoryStore seed(dir.path, quiet()); // the schema, so another connection can hold it
        REQUIRE(open_now(seed).empty());
    }
    write_all(dir.path / kJournal, journal_of({prefs_of(0x9, "Old")}));
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
        SongPrefs renamed;
        renamed.title = "New";
        store.set_song_prefs(0x9, renamed);
        REQUIRE(other.run_script("ROLLBACK") == SQLITE_OK); // free now; closing makes the last try
    }
    HistoryStore store(dir.path, quiet()); // the next start
    REQUIRE(open_now(store).empty());
    CHECK(store.song_prefs().at(0x9).title == "New");
}

TEST_CASE("journal: a journal that cannot be removed or renamed is emptied, so a library change is not replayed over later ones") {
    TempDir dir;
    write_all(dir.path / kJournal, journal_of({prefs_of(0x9, "Old")}));
    HistoryOptions stuck = quiet();
    stuck.remove_file = [](const fs::path&) { return false; };
    stuck.rename_file = [](const fs::path&, const fs::path&) { return false; };
    {
        HistoryStore store(dir.path, stuck);
        REQUIRE(open_now(store).empty()); // emptied, so nothing to tell
        CHECK(store.song_prefs().at(0x9).title == "Old");
        SongPrefs renamed;
        renamed.title = "New";
        store.set_song_prefs(0x9, renamed);
        REQUIRE(store.flush());
    }
    CHECK(read_all(dir.path / kJournal).empty());
    HistoryStore store(dir.path, stuck);
    REQUIRE(open_now(store).empty());
    CHECK(store.song_prefs().at(0x9).title == "New");
}

TEST_CASE("journal: a journal that cannot be removed, renamed or emptied says what the next start does") {
    TempDir dir;
    write_all(dir.path / kJournal, journal_of({prefs_of(0x9, "Old")}));
    Log log;
    HistoryOptions stuck = log.options();
    stuck.remove_file = [](const fs::path&) { return false; };
    stuck.rename_file = [](const fs::path&, const fs::path&) { return false; };
    stuck.truncate_file = [](const fs::path&) { return false; };
    HistoryStore store(dir.path, stuck);
    CHECK(open_now(store).find("could not be removed") != std::string::npos);
    bool honest = false;
    for (const std::string& line : log.get()) {
        CHECK(line.find("leaves a song's row changed since as it is") == std::string::npos);
        honest = honest || line.find("may replace") != std::string::npos;
    }
    CHECK(honest);
    // Within this session it is not read again.
    SongPrefs renamed;
    renamed.title = "New";
    store.set_song_prefs(0x9, renamed);
    REQUIRE(store.flush());
    CHECK(store.song_prefs().at(0x9).title == "New");
}

TEST_CASE("journal: a damaged record is told to the player at the next start, with how many") {
    TempDir dir;
    std::string bytes = journal_of({prefs_of(0x1, "One"), prefs_of(0x2, "Two"), prefs_of(0x3, "Three")});
    const std::size_t second = encode_journal_header().size() + encode_journal_entry(prefs_of(0x1, "One")).size();
    bytes[second + 12] = static_cast<char>(bytes[second + 12] ^ 0x01);
    write_all(dir.path / kJournal, bytes);
    HistoryStore store(dir.path, quiet());
    const std::string said = open_now(store);
    CHECK(said.find("history.journal") != std::string::npos);
    CHECK(said.find("1 record") != std::string::npos);
    CHECK(store.song_prefs().count(0x1) == 1);
    CHECK(store.song_prefs().count(0x2) == 0);
    CHECK(store.song_prefs().count(0x3) == 1);
}

TEST_CASE("journal: a pin of a run the database does not have is logged and counted as dropped") {
    TempDir dir;
    {
        HistoryStore seed(dir.path, quiet());
        REQUIRE(open_now(seed).empty());
    }
    write_all(dir.path / kJournal, journal_of({JournalPin{987654, true}}));
    Log log;
    HistoryStore store(dir.path, log.options());
    REQUIRE(open_now(store).empty());
    bool named = false;
    bool counted = false;
    for (const std::string& line : log.get()) {
        named = named || (line.find("987654") != std::string::npos &&
                          line.find("no run") != std::string::npos);
        counted = counted || line.find("1 dropped") != std::string::npos;
    }
    CHECK(named);
    CHECK(counted);
    log.clear();
    store.set_run_pinned(424242, true); // and a live one
    REQUIRE(store.flush());
    bool live = false;
    for (const std::string& line : log.get()) {
        live = live || line.find("424242") != std::string::npos;
    }
    CHECK(live);
}

#if !defined(_WIN32)
TEST_CASE("journal: without the instance lock the journal is left alone") {
    TempDir dir;
    {
        HistoryStore seed(dir.path, quiet());
        REQUIRE(open_now(seed).empty());
    }
    JournalRun run;
    run.record = sample_run(2);
    const std::string bytes = journal_of({run});
    write_all(dir.path / kJournal, bytes);
    fs::remove(dir.path / "history.lock");
    fs::create_directory(dir.path / "history.lock"); // the lock file cannot be opened
    {
        HistoryStore store(dir.path, quiet());
        CHECK(open_now(store).find("history.journal") != std::string::npos);
        CHECK(store.state() == HistoryState::Ready);
        CHECK(store.runs().empty());
    }
    CHECK(read_all(dir.path / kJournal) == bytes);
    fs::remove(dir.path / "history.lock");
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(store.runs().size() == 1);
    CHECK_FALSE(fs::exists(dir.path / kJournal));
}
#endif

TEST_CASE("journal: a record over the size cap is not written, and does not damage the ones after it") {
    JournalRun huge;
    huge.record = sample_run(2);
    huge.song_path = std::string(kMaxJournalRecordBytes + 10, 'x');
    CHECK(encode_journal_entry(huge).empty());

    // Origin text is cut like the other text, so a job of sources always fits.
    JournalOrigins origins;
    SongOrigin o = sample_origin();
    o.url = std::string(9u << 20, 'u');
    o.title = std::string(5000, 'T');
    origins.items.emplace_back(0x1234, o);
    const std::string encoded = encode_journal_entry(origins);
    REQUIRE_FALSE(encoded.empty());
    const JournalDecode back = decode_journal(encode_journal_header() + encoded);
    REQUIRE(back.entries.size() == 1);
    const SongOrigin& got = std::get<JournalOrigins>(back.entries[0]).items.at(0).second;
    CHECK(got.url.size() == 4096);
    CHECK(got.title.size() == 4096);
    CHECK(got.licence == o.licence);

    TempDir dir;
    JournalRun fine;
    fine.record = sample_run(3);
    const std::vector<JournalEntry> entries{JournalEntry{fine}, JournalEntry{huge},
                                            JournalEntry{prefs_of(0x9, "After")}};
    const JournalAppend result = append_journal(dir.path / kJournal, entries);
    CHECK(result.saved == 2);
    REQUIRE(result.kept.size() == 3);
    CHECK(result.kept[0]);
    CHECK_FALSE(result.kept[1]);
    CHECK(result.kept[2]);
    CHECK_FALSE(result.why.empty());
    const JournalDecode file = decode_journal(read_all(dir.path / kJournal));
    CHECK(file.entries.size() == 2);
    CHECK_FALSE(file.framing_damaged);
}

#if !defined(_WIN32)
namespace {

// Lets the process write no more than `bytes` to any one file, for the life of the object.
class FileSizeLimit {
  public:
    explicit FileSizeLimit(std::size_t bytes) {
        previous_signal_ = std::signal(SIGXFSZ, SIG_IGN);
        getrlimit(RLIMIT_FSIZE, &previous_);
        rlimit now = previous_;
        now.rlim_cur = static_cast<rlim_t>(bytes);
        setrlimit(RLIMIT_FSIZE, &now);
    }
    ~FileSizeLimit() {
        setrlimit(RLIMIT_FSIZE, &previous_);
        std::signal(SIGXFSZ, previous_signal_);
    }

  private:
    rlimit previous_{};
    void (*previous_signal_)(int) = SIG_DFL;
};

} // namespace

TEST_CASE("journal: records written before a write failed are kept and counted") {
    TempDir dir;
    JournalRun a;
    a.record = sample_run(2);
    JournalRun b;
    b.record = sample_run(3);
    JournalRun c;
    c.record = sample_run(4);
    const std::vector<JournalEntry> entries{JournalEntry{a}, JournalEntry{b}, JournalEntry{c}};
    const std::size_t room = encode_journal_header().size() + encode_journal_entry(a).size() +
                             encode_journal_entry(b).size() + 5; // the third is cut inside
    JournalAppend result;
    {
        FileSizeLimit limit(room);
        result = append_journal(dir.path / kJournal, entries);
    }
    CHECK(result.saved == 2);
    REQUIRE(result.kept.size() == 3);
    CHECK(result.kept[0]);
    CHECK(result.kept[1]);
    CHECK_FALSE(result.kept[2]);
    CHECK_FALSE(result.why.empty());
    const JournalDecode file = decode_journal(read_all(dir.path / kJournal));
    CHECK(file.entries.size() == 2);
    CHECK(file.truncated);
}

TEST_CASE("journal: the loss sentence counts only the records the journal could not take") {
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
    HistoryOptions options = quiet();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    open_now(store);
    JournalRun first; // as the store will write them
    first.record = sample_run(1);
    store.add_run(sample_run(1));
    store.add_run(sample_run(2));
    store.add_run(sample_run(3));
    SongPrefs p;
    p.title = "Three";
    store.set_song_prefs(0x9, p);
    CHECK_FALSE(flush_fails(store));
    const std::size_t room =
        encode_journal_header().size() + encode_journal_entry(first).size() * 2 + 5;
    {
        FileSizeLimit limit(room); // two of the three runs fit, a bit of the third
        store.close();
    }
    const std::vector<std::string> errors = store.take_errors();
    REQUIRE(errors.size() == 1);
    CHECK(errors[0] == "1 practice run and 1 library change could not be saved");
}
#endif

// ---- Hardening (#257)
