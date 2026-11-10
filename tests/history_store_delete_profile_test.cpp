// SPDX-License-Identifier: GPL-3.0-or-later
// Deleting a profile (#392): the refusals, who takes over from the one in use, every per-profile table
// emptied for that profile only, one transaction that rolls back whole, and the journal.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace etude;
using namespace etude::app;
using namespace history_test;
namespace fs = std::filesystem;

namespace {

struct Opened {
    HistoryStore store;
    explicit Opened(const fs::path& dir, std::int64_t profile = kDefaultProfileId)
        : store(dir, options_for(profile)) {
        REQUIRE(open_now(store).empty());
    }
    static HistoryOptions options_for(std::int64_t profile) {
        HistoryOptions options = quiet();
        options.profile_id = profile;
        return options;
    }
};

// Seconds the runs of these tests start after, later than any profile made now: a profile is last
// used by its runs once they are newer than when it was made.
const std::int64_t kBase = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count() + 10'000;

// A run of the n-th kind of song, `at` seconds after the base.
RunRecord run_at(int n, std::int64_t at) {
    RunRecord r = sample_run(n);
    r.start_utc_s = kBase + at;
    r.end_utc_s = r.start_utc_s + 60;
    r.score = 500 + n;
    r.covered_percent = 100;
    return r;
}

// Every table of the schema that keeps rows by profile, found from the file itself: a table a later
// migration adds with a profile_id column must be added to the deletion, and fails here until it is.
std::vector<std::string> tables_with_profile_id(Raw& raw) {
    std::vector<std::string> out;
    for (const std::string& table : raw.column("SELECT name FROM sqlite_master WHERE type = 'table' "
                                               "AND name NOT LIKE 'sqlite_%' ORDER BY name")) {
        if (raw.num("SELECT COUNT(*) FROM pragma_table_info('" + table + "') WHERE name = 'profile_id'") > 0) {
            out.push_back(table);
        }
    }
    return out;
}

// One row for `id` in each table the store does not fill for other profiles (the library's own
// lists are the first profile's today), so the deletion has something to remove from every table.
void seed_other_tables(Raw& raw, std::int64_t id) {
    const std::string p = std::to_string(id);
    REQUIRE(raw.db.run_script(
                "INSERT INTO song_settings (profile_id, content_hash, title) VALUES (" + p + ", 77, 'x');"
                "INSERT INTO song_groups (profile_id, group_id, name, kind, source, position, created_utc_s) "
                "VALUES (" + p + ", 5, 'g', 0, '', 0, 1);"
                "INSERT INTO song_group_members (profile_id, group_id, content_hash, added_utc_s) "
                "VALUES (" + p + ", 5, 77, 1);"
                "INSERT INTO version_overrides (profile_id, hash_a, hash_b) VALUES (" + p + ", 77, 78)") ==
            SQLITE_OK);
}

std::int64_t rows_of(Raw& raw, const std::string& table, std::int64_t id) {
    return raw.num("SELECT COUNT(*) FROM " + table + " WHERE profile_id = " + std::to_string(id));
}

// Queues the deletion, waits for the history thread, and returns what poll() reports of it.
ProfileDeleted delete_and_poll(HistoryStore& store, std::int64_t id) {
    REQUIRE(store.delete_profile(id) == ProfileResult::Ok);
    REQUIRE(store.flush());
    for (const ProfileDeleted& d : store.poll().deleted) {
        if (d.id == id) {
            return d;
        }
    }
    FAIL("no report of the deletion");
    return {};
}

} // namespace

TEST_CASE("delete profile: the only profile, the first one, an unknown id and a history not yet "
          "loaded are refused, and nothing changes") {
    TempDir dir;
    HistoryStore early(dir.path, quiet());
    CHECK(early.delete_profile(2) == ProfileResult::NotReady);
    Opened o(dir.path);
    CHECK(o.store.delete_profile(kDefaultProfileId) == ProfileResult::LastProfile);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    CHECK(o.store.delete_profile(kDefaultProfileId) == ProfileResult::FirstProfile);
    CHECK(o.store.delete_profile(99) == ProfileResult::NoSuchProfile);
    CHECK(o.store.profiles().size() == 2);
    CHECK(o.store.active_profile() == kDefaultProfileId);
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 2);
}

TEST_CASE("delete profile: the one in use passes to the most recently used of the others (the later of its "
          "newest run and when it was last left)") {
    TempDir dir;
    {
        Opened seed(dir.path);
        for (const char* name : {"Anna", "Ben", "Cy"}) {
            REQUIRE(seed.store.add_profile(name) == ProfileResult::Ok); // ids 2, 3, 4
        }
        seed.store.add_run(run_at(1, 100), "a.mid", RunEvents{}, 2);
        seed.store.add_run(run_at(2, 900), "a.mid", RunEvents{}, 3); // Ben played last
        seed.store.add_run(run_at(3, 500), "a.mid", RunEvents{}, kDefaultProfileId);
        REQUIRE(seed.store.flush());
    }
    Opened o(dir.path, 4); // a start in Cy's profile: nobody was left in this session
    CHECK(o.store.active_profile() == 4);
    REQUIRE(o.store.delete_profile(4) == ProfileResult::Ok);
    CHECK(o.store.active_profile() == 4); // until the history thread has made the deletion
    REQUIRE(o.store.flush());
    const std::vector<ProfileDeleted> reported = o.store.poll().deleted;
    REQUIRE(reported.size() == 1);
    const ProfileDeleted& done = reported[0];
    CHECK(done.done);
    CHECK(done.name == "Cy");
    CHECK(done.switched);
    CHECK(done.active == 3);
    CHECK(o.store.active_profile() == 3);
    REQUIRE(o.store.runs().size() == 1); // Ben's own list is the active one now
    CHECK(o.store.runs()[0].start_utc_s == kBase + 900);
    CHECK_FALSE(o.store.profiles().contains(4));

    // The one in use is left alone when a different profile goes.
    const ProfileDeleted other = delete_and_poll(o.store, 2);
    CHECK(other.done);
    CHECK_FALSE(other.switched);
    CHECK(other.active == 3);
    CHECK(o.store.profiles().size() == 2);
}

TEST_CASE("delete profile: with no runs to go by, the profile left most recently takes over, else "
          "the one made last") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok); // 2
    REQUIRE(o.store.add_profile("Ben") == ProfileResult::Ok);  // 3
    REQUIRE(o.store.add_profile("Cy") == ProfileResult::Ok);   // 4
    REQUIRE(o.store.switch_profile(2));                         // leaves the first, now
    REQUIRE(o.store.switch_profile(4));                         // leaves Anna
    // The first profile and Anna were each left; Anna last (or in the same second: the earlier
    // one of equals, the first profile, stays).
    const ProfileDeleted done = delete_and_poll(o.store, 4);
    CHECK(done.switched);
    CHECK((done.active == 2 || done.active == kDefaultProfileId));
}

TEST_CASE("delete profile: every table with a profile is emptied for that profile and for no other") {
    TempDir dir;
    {
        Opened o(dir.path);
        REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok); // 2
        REQUIRE(o.store.add_profile("Ben") == ProfileResult::Ok);  // 3
        for (const std::int64_t id : {std::int64_t{1}, std::int64_t{2}, std::int64_t{3}}) {
            RunRecord r = run_at(static_cast<int>(id), id * 100);
            o.store.add_run(r, "a.mid", sample_events(4), id);
            o.store.add_run(run_at(static_cast<int>(id) + 10, id * 100 + 7), "b.mid", RunEvents{}, id);
            o.store.set_profile_settings(id, "[app]\nbar_style = dock\n");
        }
        REQUIRE(o.store.switch_profile(2));
        o.store.set_song_practice(0x77, busy_practice()); // practice choices of Anna
        REQUIRE(o.store.switch_profile(3));
        o.store.set_song_practice(0x77, busy_practice());
        REQUIRE(o.store.switch_profile(kDefaultProfileId));
        o.store.set_song_practice(0x77, busy_practice());
        REQUIRE(o.store.flush());
        Raw raw(o.store.path());
        for (const std::int64_t id : {std::int64_t{2}, std::int64_t{3}}) {
            seed_other_tables(raw, id);
        }
        seed_other_tables(raw, 1);
        // Every table that keeps rows by profile, so that a table added later is noticed.
        CHECK(tables_with_profile_id(raw) ==
              std::vector<std::string>{"profile_settings", "runs", "song_group_members", "song_groups",
                                       "song_practice", "song_practice_parts", "song_settings",
                                       "version_overrides"});
        std::vector<std::int64_t> before;
        for (const std::string& t : tables_with_profile_id(raw)) {
            for (const std::int64_t id : {1, 2, 3}) {
                before.push_back(rows_of(raw, t, id));
            }
        }
        const std::int64_t measures_of_two = raw.num(
            "SELECT COUNT(*) FROM run_measures WHERE run_id IN (SELECT id FROM runs WHERE profile_id = 2)");
        const std::int64_t events_of_two = raw.num(
            "SELECT COUNT(*) FROM run_event_sets WHERE run_id IN (SELECT id FROM runs WHERE profile_id = 2)");
        REQUIRE(measures_of_two > 0);
        REQUIRE(events_of_two > 0);
        CHECK(delete_and_poll(o.store, 2).done);
        std::size_t at = 0;
        for (const std::string& t : tables_with_profile_id(raw)) {
            for (const std::int64_t id : {1, 2, 3}) {
                INFO(t << " of profile " << id);
                if (id == 2) {
                    CHECK(rows_of(raw, t, id) == 0);
                } else {
                    CHECK(rows_of(raw, t, id) == before[at]);
                }
                ++at;
            }
        }
        CHECK(raw.num("SELECT COUNT(*) FROM profiles WHERE id = 2") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 2);
        CHECK(raw.num("SELECT COUNT(*) FROM run_measures WHERE run_id NOT IN (SELECT id FROM runs)") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE run_id NOT IN (SELECT id FROM runs)") == 0);
        // The device's own rows stay.
        CHECK(raw.num("SELECT COUNT(*) FROM songs") > 0);
    }
    // A start later finds the household without her, and her runs gone from every list.
    Opened later(dir.path);
    CHECK(later.store.profiles().size() == 2);
    CHECK_FALSE(later.store.profiles().contains(2));
    CHECK(later.store.household_runs().size() == 2);
    CHECK(later.store.run_count_all() == 4);
}

TEST_CASE("delete profile: the lists change when the history thread says it is done, and a run of the "
          "deleted profile that ends after the deletion is nobody's") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    o.store.add_run(run_at(1, 100), "a.mid", RunEvents{}, 2);
    o.store.set_profile_settings(2, "[app]\nbar_style = dock\n");
    CHECK(o.store.run_count_all() == 1);
    REQUIRE(o.store.delete_profile(2) == ProfileResult::Ok);
    // The run that was in progress ends after the deletion was queued, before it was made.
    o.store.add_run(run_at(2, 200), "a.mid", RunEvents{}, 2);
    REQUIRE(o.store.flush());
    CHECK(o.store.profiles().contains(2)); // nothing changed in memory before poll()
    CHECK(o.store.poll().deleted.size() == 1);
    CHECK(o.store.run_count_all() == 0);
    CHECK(o.store.profile_settings(2).empty());
    CHECK_FALSE(o.store.profiles().contains(2));
    o.store.add_run(run_at(3, 300), "a.mid", RunEvents{}, 2); // and one after it
    CHECK(o.store.run_count_all() == 0);
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 0); // not saved under the first profile either
}

TEST_CASE("delete profile: a failure part-way puts every row back and leaves the profile, its runs and "
          "settings in memory as they were") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    o.store.add_run(run_at(1, 100), "a.mid", sample_events(4), 2);
    o.store.set_profile_settings(2, "[app]\nbar_style = dock\n");
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    seed_other_tables(raw, 2);
    // The deletion gets as far as the runs, the practice choices and the groups before this one
    // refuses: they must come back with it.
    REQUIRE(raw.db.run_script("CREATE TRIGGER refuse BEFORE DELETE ON profile_settings "
                              "BEGIN SELECT RAISE(ABORT, 'no'); END") == SQLITE_OK);
    const std::int64_t runs = rows_of(raw, "runs", 2);
    const std::int64_t groups = rows_of(raw, "song_groups", 2);
    const std::int64_t versions = rows_of(raw, "version_overrides", 2);
    REQUIRE(runs == 1);
    REQUIRE(o.store.delete_profile(2) == ProfileResult::Ok);
    REQUIRE(o.store.flush());
    CHECK(rows_of(raw, "runs", 2) == runs);
    CHECK(rows_of(raw, "song_groups", 2) == groups);
    CHECK(rows_of(raw, "version_overrides", 2) == versions);
    CHECK(rows_of(raw, "profile_settings", 2) == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM profiles WHERE id = 2") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM run_measures WHERE run_id IN (SELECT id FROM runs WHERE profile_id = 2)") > 0);
    // In memory too: the profile, its run and its settings are all still there, and it is reported.
    const std::vector<ProfileDeleted> reported = o.store.poll().deleted;
    REQUIRE(reported.size() == 1);
    CHECK_FALSE(reported[0].done);
    CHECK(reported[0].name == "Anna");
    CHECK(o.store.profiles().contains(2));
    CHECK(o.store.run_count_all() == 1);
    CHECK(o.store.profile_settings(2) == "[app]\nbar_style = dock\n");
    const std::vector<std::string> errors = o.store.take_errors();
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("Could not save the profile") == 0);
    // And once the cause is gone, the same deletion goes through.
    REQUIRE(raw.db.run_script("DROP TRIGGER refuse") == SQLITE_OK);
    CHECK(delete_and_poll(o.store, 2).done);
    CHECK_FALSE(o.store.profiles().contains(2));
}

TEST_CASE("delete profile: refused, with nothing queued, when another etude holds the history") {
    TempDir dir;
    {
        Opened seed(dir.path);
        REQUIRE(seed.store.add_profile("Anna") == ProfileResult::Ok);
        REQUIRE(seed.store.flush());
        HistoryStore second(dir.path, quiet());
        second.open();
        REQUIRE(second.wait_until_settled(10s));
        REQUIRE(second.poll().loaded); // it reads what is there and does not save
        REQUIRE(second.profiles().contains(2));
        CHECK(second.delete_profile(2) == ProfileResult::NotSaved);
        CHECK(second.profiles().contains(2));
        CHECK(second.poll().deleted.empty());
    }
    Opened later(dir.path);
    CHECK(later.store.profiles().contains(2)); // and it is still there at the next start
}

#if !defined(_WIN32)
TEST_CASE("delete profile: a deletion held at quit goes to the journal and the next start makes it") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    {
        Opened seed(dir.path);
        REQUIRE(seed.store.add_profile("Anna") == ProfileResult::Ok);
        seed.store.add_run(run_at(1, 100), "a.mid", RunEvents{}, 2);
        REQUIRE(seed.store.flush());
    }
    make_writable(dir.path, false);
    {
        HistoryOptions options = quiet();
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        open_now(store);
        REQUIRE(store.state() == HistoryState::Ready);
        REQUIRE(store.delete_profile(2) == ProfileResult::Ok);
        CHECK_FALSE(flush_fails(store)); // the file is read-only
        store.close();
        CHECK(store.take_errors().empty());
    }
    const JournalDecode kept = decode_journal(read_all(dir.path / kJournal));
    REQUIRE(kept.entries.size() == 1);
    CHECK(std::get<JournalProfileDelete>(kept.entries[0]).profile_id == 2);
    const std::string journal_bytes = read_all(dir.path / kJournal);
    make_writable(dir.path, true);
    for (int pass = 0; pass < 2; ++pass) { // a copy that came back deletes nothing more
        if (pass == 1) {
            write_all(dir.path / kJournal, journal_bytes);
        }
        Opened o(dir.path);
        CHECK(o.store.profiles().size() == 1);
        CHECK(o.store.run_count_all() == 0);
        Raw raw(o.store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 0);
    }
}
#endif

// A journal that holds a run of a profile and then its deletion, replayed twice (the file could not
// be removed, or the app died mid-replay): the second pass finds the profile gone, and its run is
// dropped, never written under the first profile.
TEST_CASE("delete profile: a journal's run of a deleted profile is dropped on every replay, never given to "
          "the first profile") {
    TempDir dir;
    {
        Opened seed(dir.path);
        REQUIRE(seed.store.add_profile("Anna") == ProfileResult::Ok);
        REQUIRE(seed.store.flush());
    }
    JournalRun run;
    run.record = run_at(1, 100);
    run.profile_id = 2;
    JournalPractice practice;
    practice.hash = 0x77;
    practice.practice = busy_practice();
    practice.profile_id = 2;
    const std::string journal = encode_journal_header() + encode_journal_entry(run) +
                                encode_journal_entry(practice) +
                                encode_journal_entry(JournalProfileDelete{2});
    for (int pass = 0; pass < 3; ++pass) {
        write_all(dir.path / kJournal, journal);
        Opened o(dir.path);
        INFO("replay " << pass);
        CHECK_FALSE(o.store.profiles().contains(2));
        CHECK(o.store.run_count_all() == 0);
        CHECK(o.store.song_practice().empty());
        Raw raw(o.store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 1);
    }
}

TEST_CASE("delete profile: the first profile is never deleted, by a journal or anything else") {
    // A record that names it is damage when the journal is read ...
    const JournalDecode back = decode_journal(encode_journal_header() +
                                              encode_journal_entry(JournalProfileDelete{kDefaultProfileId}) +
                                              encode_journal_entry(JournalProfileDelete{7}));
    CHECK(back.damaged == 1);
    REQUIRE(back.entries.size() == 1);
    CHECK(std::get<JournalProfileDelete>(back.entries[0]).profile_id == 7);
    CHECK(decode_journal(encode_journal_header() + encode_journal_entry(JournalProfileDelete{0})).damaged == 1);
    // ... and a deletion of one the database lacks, or of the only profile, changes nothing.
    TempDir dir;
    {
        Opened seed(dir.path);
        seed.store.add_run(run_at(1, 100), "a.mid", RunEvents{}, kDefaultProfileId);
        REQUIRE(seed.store.flush());
    }
    write_all(dir.path / kJournal, encode_journal_header() + encode_journal_entry(JournalProfileDelete{2}));
    Opened o(dir.path);
    CHECK(o.store.profiles().size() == 1);
    CHECK(o.store.run_count_all() == 1);
}

TEST_CASE("delete profile: the journal record round-trips") {
    const JournalDecode back = decode_journal(encode_journal_header() +
                                              encode_journal_entry(JournalProfileDelete{7}));
    REQUIRE(back.entries.size() == 1);
    CHECK(back.damaged == 0);
    CHECK(std::get<JournalProfileDelete>(back.entries[0]).profile_id == 7);
}
