// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: the first start, runs written and read back, the move from runs.dat, and a damaged file set aside.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <cstring>
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

// ---- Create

TEST_CASE("history: a first start creates the database at the current schema in WAL mode") {
    TempDir dir;
    Log log;
    {
        HistoryStore store(dir.path, log.options());
        CHECK(open_now(store).empty());
        CHECK(store.state() == HistoryState::Ready);
        CHECK(store.runs().empty());
        CHECK(store.path() == dir.path / "history.db");
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.text("PRAGMA journal_mode") == "wal");
        CHECK(raw.column("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE "
                         "'sqlite_%' ORDER BY name") ==
              std::vector<std::string>{"profile_settings", "profiles", "run_event_sets", "run_events",
                                       "run_measures", "runs", "song_group_members", "song_groups", "song_practice",
                                       "song_practice_parts", "song_settings", "song_sources", "songs",
                                       "version_overrides"});
        // One default profile, ready for profiles to be managed.
        CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 1);
        CHECK(raw.num("SELECT id FROM profiles WHERE is_default = 1") == kDefaultProfileId);
        CHECK(raw.num("SELECT COUNT(*) FROM run_events") == 0);
    }
    CHECK(log.get().empty());
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
}

TEST_CASE("history: the runs table lists core's run columns, in order") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    Raw raw(store.path());
    const std::vector<std::string> names = raw.column("SELECT name FROM pragma_table_info('runs')");
    REQUIRE(names.size() == 4 + kRunColumnCount);
    CHECK(names[0] == "id");
    CHECK(names[1] == "profile_id");
    CHECK(names[2] == "song_id");
    CHECK(names[3] == "song_title");
    for (std::size_t i = 0; i < kRunColumnCount; ++i) {
        CHECK(names[4 + i] == kRunColumnNames[i]);
    }
}

// ---- Write and read back

TEST_CASE("history: runs written come back equal and in order after a restart") {
    TempDir dir;
    std::vector<RunRecord> written;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        for (int i = 0; i < 7; ++i) {
            written.push_back(
                sample_run(i, i % 2 == 0 ? 0xabc0 : ~0ULL - 5)); // a hash with the top bit
            store.add_run(written.back(), "Songs/x.mid");
            CHECK(store.runs().size() == static_cast<std::size_t>(i + 1)); // visible at once
        }
        store.flush();
        CHECK(store.stats().writes == 7);
        CHECK(store.stats().failures == 0);
        CHECK(store.take_errors().empty());
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    REQUIRE(again.runs().size() == written.size());
    for (std::size_t i = 0; i < written.size(); ++i) {
        CHECK(again.runs()[i] == written[i]);
    }
    Raw raw(again.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 7);
    CHECK(raw.num("SELECT COUNT(*) FROM run_measures") == 7 * 6);
    CHECK(raw.num("SELECT COUNT(DISTINCT profile_id) FROM runs") == 1);
    CHECK(raw.num("SELECT MIN(profile_id) FROM runs") == kDefaultProfileId);
    CHECK(raw.num("SELECT COUNT(*) FROM songs") == 2);
}

TEST_CASE(
    "history: queued runs are saved when the store is destroyed, and nothing blocks the caller") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        for (int i = 0; i < 40; ++i) {
            store.add_run(sample_run(i));
            (void)store.take_errors(); // the frame thread polls while the writer works
            (void)store.stats();
        }
    } // no flush()
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    CHECK(again.runs().size() == 40);
}

TEST_CASE("history: a run repeated is stored once") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        store.add_run(sample_run(1)); // the same song, second, mode, hands, speed and transposition
        store.add_run(sample_run(2));
        store.flush();
        CHECK(store.take_errors().empty());
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 2);
        CHECK(raw.num("SELECT COUNT(*) FROM run_measures") == 12);
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    CHECK(again.runs().size() == 2);
}

TEST_CASE("history: streaks and trouble spots from the stored runs match the ones from the runs") {
    TempDir dir;
    std::vector<RunRecord> runs;
    for (std::int32_t day = 90; day < 100; ++day) {
        if (day == 94) {
            continue; // a gap
        }
        runs.push_back(testing::make_bars(7, 8, {3, 4}, 3, day, 1));
        runs.push_back(testing::make_bars(7, 8, {4}, 2, day, 0));
        runs.push_back(testing::make_bars(9, 5, {}, 0, day, 0));
    }
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        for (const RunRecord& r : runs) {
            store.add_run(r);
        }
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    REQUIRE(again.runs().size() == runs.size());

    const Streak want = practice_streak(runs, 99);
    const Streak got = practice_streak(again.runs(), 99);
    CHECK(got.current == want.current);
    CHECK(got.longest == want.longest);
    CHECK(got.today_done == want.today_done);
    CHECK(want.longest == 5); // days 95..99; the gap on 94 ended the earlier run of 4

    const TroublePlan plan_want = trouble_plan(runs, 7);
    const TroublePlan plan = trouble_plan(again.runs(), 7);
    REQUIRE_FALSE(plan_want.spots.empty());
    REQUIRE(plan.spots.size() == plan_want.spots.size());
    for (std::size_t i = 0; i < plan.spots.size(); ++i) {
        CHECK(plan.spots[i].first_measure == plan_want.spots[i].first_measure);
        CHECK(plan.spots[i].last_measure == plan_want.spots[i].last_measure);
        CHECK(plan.spots[i].heat == doctest::Approx(plan_want.spots[i].heat));
    }
    CHECK(plan.runs_read == plan_want.runs_read);
    CHECK(song_bests(again.runs(), 9).size() == song_bests(runs, 9).size());
}

// ---- Songs

TEST_CASE("history: a renamed song file keeps its history under one song row") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1, 0x5151), "Songs/elise.mid");
    store.add_run(sample_run(2, 0x5151), "Songs/Beethoven/elise-renamed.mid");
    store.add_run(sample_run(3, 0x5152), "Songs/Beethoven/elise-renamed.mid"); // edited: new bytes
    store.flush();
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM songs") == 2);
    CHECK(raw.text("SELECT path FROM songs WHERE song_key = 'h:" + content_hash_hex(0x5151) +
                   "'") == "Songs/Beethoven/elise-renamed.mid");
    CHECK(raw.num("SELECT COUNT(DISTINCT song_id) FROM runs WHERE song_hash = " +
                  std::to_string(0x5151)) == 1);
    CHECK(raw.num("SELECT last_seen_utc_s - first_seen_utc_s FROM songs WHERE song_key = 'h:" +
                  content_hash_hex(0x5151) + "'") == 100);
    CHECK(raw.text("SELECT title FROM songs WHERE song_key = 'h:" + content_hash_hex(0x5152) +
                   "'") == "Song " + std::to_string(0x5152));
}

TEST_CASE("history: a run whose song has no hash is keyed by its path, or by nothing") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1, 0), "Songs\\generated.mid");
    store.add_run(sample_run(2, 0), "./Songs/generated.mid"); // the same path, written another way
    store.add_run(sample_run(3, 0), "");
    store.flush();
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM songs") == 1);
    CHECK(raw.text("SELECT song_key FROM songs") == "p:Songs/generated.mid");
    CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE song_id IS NULL") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);
}

// ---- Moving runs.dat in

TEST_CASE(
    "history: the first start moves runs.dat into the database and keeps it as runs.dat.bak") {
    TempDir dir;
    std::vector<RunRecord> old_runs;
    for (int i = 0; i < 4; ++i) {
        old_runs.push_back(sample_run(i, 0x70 + static_cast<std::uint64_t>(i % 2)));
    }
    std::string newer = serialize_run_record(sample_run(9));
    newer[2] = 9; // a record a newer build wrote, between the second and third run
    std::vector<std::string> raw_records;
    for (std::size_t i = 0; i < old_runs.size(); ++i) {
        if (i == 2) {
            raw_records.push_back(newer);
        }
        raw_records.push_back(serialize_run_record(old_runs[i]));
    }
    std::string bytes = encode_runs(raw_records);
    write_all(dir.path / "runs.dat", bytes);

    Log log;
    {
        HistoryStore store(dir.path, log.options());
        CHECK(open_now(store).empty()); // nothing the player needs to be told
        REQUIRE(store.runs().size() == 4);
        for (std::size_t i = 0; i < 4; ++i) {
            CHECK(store.runs()[i] == old_runs[i]);
        }
    }
    const std::vector<std::string> lines = log.get();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("4 runs moved from runs.dat") != std::string::npos);
    CHECK(lines[0].find("1 from a newer version") != std::string::npos);
    CHECK(lines[0].find("runs.dat.bak") != std::string::npos);
    CHECK_FALSE(fs::exists(dir.path / "runs.dat"));
    CHECK(read_all(dir.path / "runs.dat.bak") == bytes);

    // The second start reads the database only.
    log.clear();
    {
        HistoryStore store(dir.path, log.options());
        CHECK(open_now(store).empty());
        CHECK(store.runs().size() == 4);
    }
    CHECK(log.get().empty());
    CHECK(read_all(dir.path / "runs.dat.bak") == bytes);
    CHECK_FALSE(fs::exists(dir.path / "runs.dat.bak.1"));
}

TEST_CASE("history: moving runs.dat in twice adds nothing (an interrupted move, or an old build)") {
    TempDir dir;
    std::vector<RunRecord> old_runs{sample_run(0), sample_run(1), sample_run(2)};
    const std::string bytes = raw_runs_file(old_runs);
    write_all(dir.path / "runs.dat", bytes);
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 3);
    }
    // The rename was lost, say: the same file is there again.
    write_all(dir.path / "runs.dat", bytes);
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 3);
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);
        CHECK(raw.num("SELECT COUNT(*) FROM run_measures") == 18);
    }
    CHECK_FALSE(fs::exists(dir.path / "runs.dat"));
    CHECK(read_all(dir.path / "runs.dat.bak") == bytes);
    CHECK(read_all(dir.path / "runs.dat.bak.1") == bytes); // never overwritten

    // An older build ran in between and added one run: only that one is new.
    old_runs.push_back(sample_run(3));
    write_all(dir.path / "runs.dat", raw_runs_file(old_runs));
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    REQUIRE(store.runs().size() == 4);
    CHECK(store.runs()[3] == sample_run(3));
}

TEST_CASE("history: a runs.dat that is not a runs file is kept as .bak and adds no runs") {
    TempDir dir;
    write_all(dir.path / "runs.dat", "garbage, not runs");
    Log log;
    HistoryStore store(dir.path, log.options());
    CHECK(open_now(store).empty());
    CHECK(store.runs().empty());
    CHECK(read_all(dir.path / "runs.dat.bak") == "garbage, not runs");
    CHECK_FALSE(fs::exists(dir.path / "runs.dat"));
    const std::vector<std::string> lines = log.get();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("not a runs file") != std::string::npos);
}

TEST_CASE("history: new runs after the move are saved beside the old ones") {
    TempDir dir;
    write_all(dir.path / "runs.dat", raw_runs_file({sample_run(0), sample_run(1)}));
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(2));
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    REQUIRE(again.runs().size() == 3);
    CHECK(again.runs()[2] == sample_run(2));
}

// ---- A damaged or locked database

TEST_CASE(
    "history: a file that is not a database is set aside with one log line, and a new one starts") {
    TempDir dir;
    const std::string junk =
        "this is not a sqlite file, it is long enough to be read as a header....";
    write_all(dir.path / "history.db", junk);
    Log log;
    {
        HistoryStore store(dir.path, log.options());
        const std::string message = open_now(store);
        CHECK(message.find("damaged") != std::string::npos);
        CHECK(message.find("history.db.damaged") != std::string::npos);
        CHECK(store.state() == HistoryState::Ready);
        CHECK(store.runs().empty());
        store.add_run(sample_run(1));
        store.flush();
        CHECK(store.take_errors().empty());
    }
    const std::vector<std::string> lines = log.get();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("damaged") != std::string::npos);
    CHECK(read_all(dir.path / "history.db.damaged") == junk);
    HistoryStore again(dir.path, log.options());
    CHECK(open_now(again).empty());
    CHECK(again.runs().size() == 1);
    CHECK(log.get().size() == 1); // nothing more
}

TEST_CASE(
    "history: pages of a real database overwritten, or the file cut short, count as damaged") {
    for (const bool cut : {false, true}) {
        TempDir dir;
        {
            HistoryStore store(dir.path, quiet());
            REQUIRE(open_now(store).empty());
            for (int i = 0; i < 60; ++i) {
                store.add_run(sample_run(i));
            }
        }
        const fs::path db = dir.path / "history.db";
        REQUIRE_FALSE(fs::exists(dir.path / "history.db-wal")); // a clean close left one file
        std::string bytes = read_all(db);
        REQUIRE(bytes.size() > 40'000);
        if (cut) {
            bytes.resize(bytes.size() / 3);
        } else {
            std::fill(bytes.begin() + 4096, bytes.begin() + 4096 + 20'000, '\xAB');
        }
        write_all(db, bytes);

        Log log;
        HistoryStore store(dir.path, log.options());
        const std::string message = open_now(store);
        INFO((cut ? "cut short" : "overwritten"));
        CHECK_FALSE(message.empty());
        CHECK(log.get().size() == 1);
        CHECK(read_all(dir.path / "history.db.damaged") == bytes);
        CHECK(store.state() == HistoryState::Ready);
        CHECK(store.runs().empty());
        store.add_run(sample_run(1));
        store.flush();
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
    }
}

TEST_CASE("history: a second damaged file keeps the first copy and takes the next name") {
    TempDir dir;
    write_all(dir.path / "history.db",
              "first junk, long enough to be a bad header, not a database");
    {
        HistoryStore store(dir.path, quiet());
        CHECK_FALSE(open_now(store).empty());
    }
    write_all(dir.path / "history.db", "second junk, long enough to be a bad header, no database");
    {
        HistoryStore store(dir.path, quiet());
        CHECK_FALSE(open_now(store).empty());
    }
    CHECK(read_all(dir.path / "history.db.damaged").rfind("first", 0) == 0);
    CHECK(read_all(dir.path / "history.db.damaged.1").rfind("second", 0) == 0);
}

TEST_CASE("history: stale WAL files beside a damaged database do not leak into the new one") {
    TempDir dir;
    write_all(dir.path / "history.db",
              "junk that is not a database but is long enough to read....");
    write_all(dir.path / "history.db-wal", std::string(4096, '\x01'));
    write_all(dir.path / "history.db-shm", std::string(32768, '\x02'));
    {
        HistoryStore store(dir.path, quiet());
        CHECK_FALSE(open_now(store).empty());
        CHECK(store.state() == HistoryState::Ready);
        store.add_run(sample_run(1));
        store.flush();
        CHECK(store.take_errors().empty());
    }
    HistoryStore again(dir.path, quiet());
    CHECK(open_now(again).empty());
    CHECK(again.runs().size() == 1);
}
