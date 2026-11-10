// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history keyed by profile (#79): the default profile, adding and switching, a
// profile's own runs, practice choices and settings, the upgrade of older files, and the journal.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "etude/leaderboard.hpp"
#include "etude/profile_settings.hpp"
#include "etude/run_score.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// A store on `dir` that starts on `profile`, loaded.
struct Opened {
    HistoryStore store;
    Opened(const fs::path& dir, std::int64_t profile = kDefaultProfileId)
        : store(dir, with_profile(profile)) {
        REQUIRE(open_now(store).empty());
    }
    static HistoryOptions with_profile(std::int64_t profile) {
        HistoryOptions options = quiet();
        options.profile_id = profile;
        return options;
    }
};

std::int64_t profile_of_run(Raw& raw, int nth) {
    return raw.num("SELECT profile_id FROM runs ORDER BY id LIMIT 1 OFFSET " +
                   std::to_string(nth));
}

// The six notes of sample_events() as the song a replay is laid over.
ReplayRequest replay_request(const RunRecord& record, std::int64_t profile_id = 0) {
    auto notes = std::make_shared<std::vector<Note>>();
    auto measures = std::make_shared<std::vector<Measure>>();
    for (int i = 0; i < 6; ++i) {
        Note n;
        n.start_s = 0.5 * i;
        n.duration_s = 0.25;
        n.pitch = static_cast<std::uint8_t>(60 + i);
        n.hand = i % 2 == 0 ? Hand::Right : Hand::Left;
        n.measure = 1 + i / 2;
        notes->push_back(n);
    }
    for (int b = 0; b < 3; ++b) {
        Measure m;
        m.number = b + 1;
        m.start_s = 1.0 * b;
        measures->push_back(m);
    }
    ReplayRequest request{record, notes, measures};
    request.profile_id = profile_id;
    return request;
}

EventsRead replay_answer_for(HistoryStore& store, std::uint64_t serial) {
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

} // namespace

TEST_CASE("profiles: everyone starts with the default profile and no setup") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.store.profiles().size() == 1);
    CHECK(o.store.active_profile() == kDefaultProfileId);
    CHECK(o.store.profiles().default_profile().colour == kProfileColours[0]);
    CHECK(o.store.profile_settings(kDefaultProfileId).empty());
    Raw raw(o.store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 1);
    CHECK(raw.num("SELECT colour FROM profiles WHERE id = 1") == kProfileColours[0]);
    CHECK(raw.num("SELECT COUNT(*) FROM profile_settings") == 0);
}

TEST_CASE("profiles: adding, renaming and recolouring are kept for the next start") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        CHECK(store.add_profile("Too early") == ProfileResult::NotReady); // not loaded yet
        REQUIRE(open_now(store).empty());
        Profile anna;
        REQUIRE(store.add_profile("  Anna ", &anna) == ProfileResult::Ok);
        CHECK(anna.id == 2);
        CHECK(anna.name == "Anna");
        CHECK(store.add_profile("anna") == ProfileResult::NameTaken);
        CHECK(store.add_profile("") == ProfileResult::EmptyName);
        REQUIRE(store.add_profile("Ben") == ProfileResult::Ok);
        CHECK(store.rename_profile(kDefaultProfileId, "Mum") == ProfileResult::Ok);
        CHECK(store.recolour_profile(2, 0x123456) == ProfileResult::Ok);
        CHECK(store.rename_profile(9, "Nobody") == ProfileResult::NoSuchProfile);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 3);
        CHECK(raw.text("SELECT name FROM profiles WHERE id = 1") == "Mum");
        CHECK(raw.num("SELECT is_default FROM profiles WHERE id = 1") == 1);
        CHECK(raw.num("SELECT colour FROM profiles WHERE id = 2") == 0x123456);
        CHECK(raw.num("SELECT is_default FROM profiles WHERE id = 3") == 0);
    }
    Opened again(dir.path);
    REQUIRE(again.store.profiles().size() == 3);
    CHECK(again.store.profiles().find(1)->name == "Mum");
    CHECK(again.store.profiles().find(2)->colour == 0x123456);
    CHECK(again.store.profiles().find(3)->name == "Ben");
    // The next profile takes the next id and colour after the ones read back.
    Profile cleo;
    REQUIRE(again.store.add_profile("Cleo", &cleo) == ProfileResult::Ok);
    CHECK(cleo.id == 4);
}

TEST_CASE("profiles: each profile keeps its own runs, and a switch changes them at once") {
    TempDir dir;
    {
        Opened o(dir.path);
        o.store.add_run(sample_run(1));
        REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
        CHECK_FALSE(o.store.switch_profile(kDefaultProfileId)); // already the active one
        CHECK_FALSE(o.store.switch_profile(9));                 // not a profile
        REQUIRE(o.store.switch_profile(2));
        CHECK(o.store.active_profile() == 2);
        CHECK(o.store.runs().empty());
        o.store.add_run(sample_run(2));
        o.store.add_run(sample_run(3));
        REQUIRE(o.store.runs().size() == 2);
        // Back: the first profile's one run, and Anna's two are kept for the boards.
        REQUIRE(o.store.switch_profile(kDefaultProfileId));
        REQUIRE(o.store.runs().size() == 1);
        CHECK(o.store.runs()[0] == sample_run(1));
        const std::vector<ProfileRuns> household = o.store.household_runs();
        REQUIRE(household.size() == 2);
        CHECK(household[0].profile_id == kDefaultProfileId);
        CHECK(household[0].runs.size() == 1);
        CHECK(household[1].profile_id == 2);
        CHECK(household[1].runs.size() == 2);
        REQUIRE(o.store.flush());
        Raw raw(o.store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE profile_id = 1") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE profile_id = 2") == 2);
    }
    // The next start brings the active profile it is given, and the others' runs with it.
    Opened anna(dir.path, 2);
    CHECK(anna.store.active_profile() == 2);
    REQUIRE(anna.store.runs().size() == 2);
    CHECK(anna.store.runs()[0] == sample_run(2));
    CHECK(anna.store.runs()[1] == sample_run(3));
    REQUIRE(anna.store.switch_profile(kDefaultProfileId));
    REQUIRE(anna.store.runs().size() == 1);
    CHECK(anna.store.runs()[0] == sample_run(1));
}

TEST_CASE("profiles: a run finished just before a switch is saved under the profile it was played on") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    o.store.add_run(sample_run(1)); // played as the default profile
    REQUIRE(o.store.switch_profile(2));
    o.store.add_run(sample_run(2)); // the next, as Anna
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    CHECK(profile_of_run(raw, 0) == 1);
    CHECK(profile_of_run(raw, 1) == 2);
}

TEST_CASE("profiles: a song's practice choices belong to the profile") {
    TempDir dir;
    SongPractice slow;
    slow.speed_percent = 50;
    slow.hands = HandFilter::Left;
    slow.muted_parts = {0x102};
    slow.parts_set = true;
    slow.split_point = 62;
    SongPractice fast;
    fast.speed_percent = 120;
    fast.hands = HandFilter::Right;
    {
        Opened o(dir.path);
        o.store.set_song_practice(0x77, slow);
        REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
        REQUIRE(o.store.switch_profile(2));
        CHECK(o.store.song_practice().empty()); // Anna starts on the defaults for every song
        o.store.set_song_practice(0x77, fast);
        o.store.set_song_practice(0x88, slow);
        REQUIRE(o.store.flush());
        Raw raw(o.store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice WHERE profile_id = 1") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice WHERE profile_id = 2") == 2);
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice_parts WHERE profile_id = 1") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice_parts WHERE profile_id = 2") == 1);
    }
    Opened o(dir.path);
    REQUIRE(o.store.song_practice().size() == 1);
    CHECK(o.store.song_practice().at(0x77).speed_percent == 50);
    CHECK(o.store.song_practice().at(0x77).muted_parts == std::vector<std::uint32_t>{0x102});
    CHECK(o.store.song_practice().at(0x77).split_point == 62);
    REQUIRE(o.store.switch_profile(2));
    REQUIRE(o.store.song_practice().size() == 2);
    CHECK(o.store.song_practice().at(0x77).speed_percent == 120);
    CHECK(o.store.song_practice().at(0x77).hands == HandFilter::Right);
    CHECK(o.store.song_practice().at(0x77).muted_parts.empty());
    CHECK(o.store.song_practice().at(0x88).muted_parts == std::vector<std::uint32_t>{0x102});
}

TEST_CASE("profiles: a profile's settings are saved and come back with it") {
    TempDir dir;
    const std::string anna_text = "[app]\nbar_style = dock\n\n[session]\nspeed_percent = 60\n";
    {
        Opened o(dir.path);
        REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
        o.store.set_profile_settings(2, anna_text);
        o.store.set_profile_settings(2, anna_text); // the same text again: nothing more to write
        o.store.set_profile_settings(9, "[app]\nx = 1\n"); // not a profile: ignored
        CHECK(o.store.profile_settings(2) == anna_text);
        CHECK(o.store.profile_settings(9).empty());
        REQUIRE(o.store.flush());
        Raw raw(o.store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM profile_settings") == 1);
        CHECK(raw.text("SELECT settings FROM profile_settings WHERE profile_id = 2") == anna_text);
        o.store.set_profile_settings(2, "[app]\nbar_style = strip\n"); // replaced, not added
        REQUIRE(o.store.flush());
        CHECK(raw.num("SELECT COUNT(*) FROM profile_settings") == 1);
        o.store.set_profile_settings(2, anna_text);
        REQUIRE(o.store.flush());
    }
    Opened o(dir.path);
    CHECK(o.store.profile_settings(2) == anna_text);
    CHECK(o.store.profile_settings(kDefaultProfileId).empty());
    // It goes into the settings document with the profile's own keys, the device's untouched.
    SettingsDocument device = SettingsDocument::parse("[app]\nrenderer = gl\nbar_style = groups\n");
    apply_profile_settings(device, o.store.profile_settings(2));
    CHECK(device.section_text("app").find("renderer = gl") != std::string::npos);
    CHECK(device.section_text("app").find("bar_style = dock") != std::string::npos);
}

TEST_CASE("profiles: a profile the database lacks falls back to the default one") {
    TempDir dir;
    HistoryOptions options = quiet();
    options.profile_id = 9;
    HistoryStore store(dir.path, options);
    store.open();
    store.add_run(sample_run(1)); // stamped 9 before anyone knew the database had no such profile
    REQUIRE(store.wait_until_settled(10s));
    const HistoryPoll poll = store.poll();
    CHECK(poll.loaded);
    CHECK(poll.profile_changed);
    CHECK(store.active_profile() == kDefaultProfileId);
    REQUIRE(store.runs().size() == 1);
    REQUIRE(store.flush());
    Raw raw(store.path());
    CHECK(profile_of_run(raw, 0) == 1);
    CHECK(store.take_errors().empty());
}

TEST_CASE("profiles: the library belongs to the device, whoever is playing") {
    TempDir dir;
    Opened o(dir.path);
    SongPrefs star;
    star.favourite = true;
    star.title = "Mine";
    o.store.set_song_prefs(0x55, star);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    REQUIRE(o.store.switch_profile(2));
    CHECK(o.store.song_prefs().at(0x55).favourite);
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM song_settings WHERE profile_id = 1") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM song_settings WHERE profile_id <> 1") == 0);
}

TEST_CASE("profiles: ghosts and run notes are read for the profile playing") {
    TempDir dir;
    Opened o(dir.path);
    RunRecord a = sample_run(1);
    a.mode = RunMode::Flow;
    o.store.add_run(a, "a.mid", sample_events());
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    const std::int64_t run_id = raw.num("SELECT id FROM runs LIMIT 1");
    GhostSetup setup;
    setup.mode = RunMode::Flow;
    setup.hands = a.hands;
    setup.speed_percent = a.speed_percent;
    setup.transpose_semitones = a.transpose_semitones;
    CHECK(answer(o.store, o.store.request_run_events(run_id)).has_value());
    CHECK(answer(o.store, o.store.request_ghost(a.song_hash, setup)).has_value());
    REQUIRE(o.store.switch_profile(2));
    // Anna has no such run: another profile's notes are not hers to read.
    CHECK_FALSE(answer(o.store, o.store.request_run_events(run_id)).has_value());
    CHECK_FALSE(answer(o.store, o.store.request_ghost(a.song_hash, setup)).has_value());
}

// ---- The upgrade: every older file goes to the latest with its rows with the default profile.

TEST_CASE("profiles: a file of any older version upgrades, every run and choice with the default "
          "profile") {
    for (const int version : {1, 2, 5, 7, 9, 10, 11, 12}) {
        CAPTURE(version);
        TempDir dir;
        build_at_version(dir.path / "history.db", version);
        {
            Raw raw(dir.path / "history.db");
            REQUIRE(raw.db.run_script(
                        "INSERT INTO songs (id, song_key, content_hash, path, title, "
                        "first_seen_utc_s, last_seen_utc_s) VALUES (1, 'k', 4660, '/a.mid', "
                        "'Etude', 1, 2); "
                        "INSERT INTO runs (id, profile_id, song_id, song_title, song_hash, mode, "
                        "hands, speed_percent, transpose_semitones, start_utc_s, end_utc_s, "
                        "local_day, duration_ms, perfect, good, early, late, missed, played, "
                        "wrong, unplayable, longest_combo, clean_passes, left_graded, left_hit, "
                        "right_graded, right_hit, unknown_graded, unknown_hit, all_graded, "
                        "all_hit) VALUES (1, 1, 1, 'Etude', 4660, 0, 2, 80, -2, 1000, 1060, "
                        "20000, 60000, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, "
                        "25, 26, 27, 28)") == SQLITE_OK);
            if (version >= 7) {
                REQUIRE(raw.db.run_script(
                            "INSERT INTO song_practice (profile_id, content_hash, speed_percent, "
                            "count_in_bars, hands, transpose_semitones, updated_utc_s) "
                            "VALUES (1, 4660, 65, 2, 2, -2, 5)") == SQLITE_OK);
            }
        }
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.active_profile() == kDefaultProfileId);
        REQUIRE(store.profiles().size() == 1);
        CHECK(store.profiles().default_profile().name == "Player");
        CHECK(store.profiles().default_profile().colour == kProfileColours[0]);
        REQUIRE(store.runs().size() == 1);
        const RunRecord& run = store.runs()[0];
        CHECK(run.song_hash == 4660);
        CHECK(run.counts.perfect == 11);
        CHECK(run.all.graded == 27);
        CHECK(run.score == 0);           // from before scores
        CHECK(run.covered_percent == 0); // and before coverage
        CHECK_FALSE(is_scored(run));     // so it is on no board, and earns no badge
        if (version >= 7) {
            REQUIRE(store.song_practice().count(4660) == 1);
            CHECK(store.song_practice().at(4660).speed_percent == 65);
        }
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE profile_id = 1") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM profile_settings") == 0);
        // A new profile and a new run work on the upgraded file.
        REQUIRE(store.add_profile("Anna") == ProfileResult::Ok);
        REQUIRE(store.switch_profile(2));
        store.add_run(sample_run(5));
        REQUIRE(store.flush());
        CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE profile_id = 2") == 1);
        CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    }
}

TEST_CASE("profiles: a copy of a version 12 file made with sqlite3 .dump upgrades too") {
    TempDir dir;
    const fs::path original = dir.path / "orig.db";
    build_at_version(original, 12);
    {
        Raw raw(original);
        REQUIRE(raw.db.run_script(
                    "INSERT INTO song_practice (profile_id, content_hash, speed_percent) "
                    "VALUES (1, 4660, 65)") == SQLITE_OK);
    }
    const fs::path copy = dir.path / "history.db";
    dump_restore(original, copy);
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(store.state() == HistoryState::Ready);
    REQUIRE(store.song_practice().count(4660) == 1);
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('profiles') WHERE name = 'colour'") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('runs') WHERE name = 'score'") == 1);
}

TEST_CASE("profiles: opening an upgraded file again changes nothing") {
    TempDir dir;
    build_at_version(dir.path / "history.db", 12);
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
    }
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.profiles().size() == 1);
    }
    Raw raw(dir.path / "history.db");
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 1);
    CHECK(raw.text("SELECT name FROM profiles WHERE id = 1") == "Player");
}

TEST_CASE("profiles: a run finished under one profile is saved for it after a switch") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    REQUIRE(o.store.switch_profile(2)); // switched, and then the song is opened: the old run ends
    o.store.add_run(sample_run(1), "a.mid", RunEvents{}, kDefaultProfileId);
    CHECK(o.store.runs().empty()); // Anna's list is not the default profile's run
    o.store.add_run(sample_run(2), "a.mid", RunEvents{}, 2);
    REQUIRE(o.store.runs().size() == 1);
    CHECK(o.store.runs()[0] == sample_run(2));
    const std::vector<ProfileRuns> household = o.store.household_runs();
    REQUIRE(household.size() == 2);
    REQUIRE(household[0].runs.size() == 1);
    CHECK(household[0].runs[0] == sample_run(1));
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    CHECK(profile_of_run(raw, 0) == 1);
    CHECK(profile_of_run(raw, 1) == 2);
    REQUIRE(o.store.switch_profile(kDefaultProfileId));
    REQUIRE(o.store.runs().size() == 1);
    CHECK(o.store.runs()[0] == sample_run(1));
}

TEST_CASE("profiles: a replay finds a just finished run by its record under its own profile") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    const RunRecord record = sample_run(1); // both played it: the same record, two profiles' runs
    o.store.add_run(record, "a.mid", sample_events(), kDefaultProfileId);
    o.store.add_run(record, "a.mid", sample_events(), 2);
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    const std::int64_t first = raw.num("SELECT id FROM runs WHERE profile_id = 1");
    const std::int64_t anna = raw.num("SELECT id FROM runs WHERE profile_id = 2");
    REQUIRE(first != anna);
    // The default profile is active: the record names its run, and Anna's when she is named.
    CHECK(replay_answer_for(o.store, o.store.request_replay(0, replay_request(record))).run_id ==
          first);
    CHECK(replay_answer_for(o.store, o.store.request_replay(0, replay_request(record, 2))).run_id ==
          anna);
    // After a switch the active profile is Anna: no profile named is hers.
    REQUIRE(o.store.switch_profile(2));
    CHECK(replay_answer_for(o.store, o.store.request_replay(0, replay_request(record))).run_id ==
          anna);
    CHECK(replay_answer_for(o.store, o.store.request_replay(0, replay_request(record, 1))).run_id ==
          first);
    // A run only the default profile played is not found by Anna.
    RunRecord mine = sample_run(2);
    o.store.add_run(mine, "a.mid", sample_events(), kDefaultProfileId);
    const EventsRead lost = replay_answer_for(o.store, o.store.request_replay(0, replay_request(mine)));
    CHECK(lost.run_id == 0); // no run to pin, and the review is the record's own summary
    REQUIRE(lost.replay != nullptr);
    CHECK(lost.replay->summary.detail == SummaryDetail::RecordOnly);
}

TEST_CASE("profiles: a profile pins only its own runs") {
    TempDir dir;
    Opened o(dir.path);
    REQUIRE(o.store.add_profile("Anna") == ProfileResult::Ok);
    o.store.add_run(sample_run(1), "a.mid", sample_events(), kDefaultProfileId);
    o.store.add_run(sample_run(2), "a.mid", sample_events(), 2);
    REQUIRE(o.store.flush());
    Raw raw(o.store.path());
    const std::int64_t mine = raw.num("SELECT id FROM runs WHERE profile_id = 1");
    const std::int64_t hers = raw.num("SELECT id FROM runs WHERE profile_id = 2");
    REQUIRE(o.store.switch_profile(2));
    const auto took = [&](std::int64_t id, bool pin) {
        const std::uint64_t serial = o.store.set_run_pinned(id, pin);
        REQUIRE(o.store.flush());
        for (const EventsRead& r : o.store.take_event_reads()) {
            if (r.serial == serial) {
                return r.pin_took;
            }
        }
        return false;
    };
    CHECK_FALSE(took(mine, true)); // the default profile's run is not Anna's to pin
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE pinned = 1") == 0);
    CHECK(took(hers, true));
    CHECK(raw.num("SELECT pinned FROM run_event_sets WHERE run_id = " + std::to_string(hers)) == 1);
    REQUIRE(o.store.switch_profile(kDefaultProfileId));
    CHECK_FALSE(took(hers, false)); // nor hers the default profile's
    CHECK(raw.num("SELECT pinned FROM run_event_sets WHERE run_id = " + std::to_string(hers)) == 1);
}

TEST_CASE("profiles: a pin in the journal carries its profile, an older one is the default's") {
    JournalPin pin{42, true, 5};
    const JournalDecode back =
        decode_journal(encode_journal_header() + encode_journal_entry(JournalEntry{pin}));
    REQUIRE(back.entries.size() == 1);
    const JournalPin& got = std::get<JournalPin>(back.entries[0]);
    CHECK(got.run_id == 42);
    CHECK(got.pinned);
    CHECK(got.profile_id == 5);
    const std::string payload = encode_journal_entry(JournalEntry{pin}).substr(8);
    const JournalDecode old = decode_journal(encode_journal_header() +
                                             framed_record(payload.substr(0, payload.size() - 1)));
    REQUIRE(old.entries.size() == 1);
    CHECK(std::get<JournalPin>(old.entries[0]).profile_id == kDefaultProfileId);
}

TEST_CASE("profiles: a run's score and coverage are stored and read back") {
    TempDir dir;
    RunRecord r = sample_run(1);
    r.score = 731;
    r.covered_percent = 42;
    {
        Opened o(dir.path);
        o.store.add_run(r);
        REQUIRE(o.store.flush());
        Raw raw(o.store.path());
        CHECK(raw.num("SELECT score FROM runs") == 731);
        CHECK(raw.num("SELECT covered_percent FROM runs") == 42);
    }
    Opened o(dir.path);
    REQUIRE(o.store.runs().size() == 1);
    CHECK(o.store.runs()[0].score == 731);
    CHECK(o.store.runs()[0].covered_percent == 42);
    CHECK(o.store.runs()[0] == r);
}

// ---- The journal: what is held when the app quits carries the profile.

#if !defined(_WIN32)
TEST_CASE("profiles: runs, choices, settings and profiles held at quit go to the journal with "
          "their profile, and the next start saves them") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    {
        Opened seed(dir.path);
        REQUIRE(seed.store.add_profile("Anna") == ProfileResult::Ok);
        seed.store.add_run(sample_run(1));
        REQUIRE(seed.store.flush());
    }
    make_writable(dir.path, false);
    {
        HistoryOptions options = Opened::with_profile(2);
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        open_now(store);
        REQUIRE(store.state() == HistoryState::Ready);
        RunRecord scored_run = sample_run(2);
        scored_run.score = 640;
        scored_run.covered_percent = 55;
        store.add_run(scored_run, "/songs/a.mid", sample_events(4));
        store.set_song_practice(0x77, busy_practice());
        store.set_profile_settings(2, "[app]\nbar_style = dock\n");
        REQUIRE(store.add_profile("Ben") == ProfileResult::Ok);
        CHECK_FALSE(flush_fails(store)); // the writes find the file read-only
        store.close();
        CHECK(store.take_errors().empty()); // kept, so nothing to tell the player
    }
    const JournalDecode kept = decode_journal(read_all(dir.path / kJournal));
    REQUIRE(kept.entries.size() == 4);
    const JournalRun& run = std::get<JournalRun>(kept.entries[0]);
    CHECK(run.profile_id == 2);
    CHECK(run.record.score == 640);
    CHECK(run.record.covered_percent == 55);
    const JournalPractice& practice = std::get<JournalPractice>(kept.entries[1]);
    CHECK(practice.profile_id == 2);
    CHECK(practice.practice == busy_practice());
    const JournalProfileSettings& settings = std::get<JournalProfileSettings>(kept.entries[2]);
    CHECK(settings.profile_id == 2);
    CHECK(settings.text == "[app]\nbar_style = dock\n");
    const JournalProfiles& made = std::get<JournalProfiles>(kept.entries[3]);
    REQUIRE(made.rows.size() == 1);
    CHECK(made.rows[0].name == "Ben");
    CHECK(made.rows[0].id == 3);

    const std::string journal_bytes = read_all(dir.path / kJournal);
    make_writable(dir.path, true);
    for (int pass = 0; pass < 2; ++pass) { // and a copy that came back adds nothing
        if (pass == 1) {
            write_all(dir.path / kJournal, journal_bytes);
        }
        Opened o(dir.path, 2);
        CHECK(o.store.profiles().size() == 3);
        REQUIRE(o.store.runs().size() == 1);
        CHECK(o.store.runs()[0] == [&] {
            RunRecord r = sample_run(2);
            r.score = 640;
            r.covered_percent = 55;
            return r;
        }());
        REQUIRE(o.store.song_practice().count(0x77) == 1);
        CHECK(o.store.profile_settings(2) == "[app]\nbar_style = dock\n");
        Raw raw(o.store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE profile_id = 2") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE profile_id = 1") == 1);
    }
}
#endif

TEST_CASE("profiles: journal records round-trip with their profile, and an older record is the "
          "default profile's") {
    JournalRun run;
    run.record = sample_run(3);
    run.record.score = 512;
    run.record.covered_percent = 100;
    run.profile_id = 7;
    JournalPractice practice;
    practice.hash = 0x99;
    practice.practice = busy_practice();
    practice.at_utc_s = 123;
    practice.profile_id = 5;
    JournalProfiles profiles;
    profiles.rows = {{4, "Dani", 0xABCDEF, 99}, {5, "Eve", 0x010203, 100}};
    JournalProfileSettings settings{6, "[session]\nsong = a.mid\n", 55};
    std::string bytes = encode_journal_header();
    for (const JournalEntry& e :
         {JournalEntry{run}, JournalEntry{practice}, JournalEntry{profiles}, JournalEntry{settings}}) {
        bytes += encode_journal_entry(e);
    }
    const JournalDecode back = decode_journal(bytes);
    REQUIRE(back.entries.size() == 4);
    CHECK(back.damaged == 0);
    const JournalRun& r0 = std::get<JournalRun>(back.entries[0]);
    CHECK(r0.profile_id == 7);
    CHECK(r0.record == run.record);
    const JournalPractice& p1 = std::get<JournalPractice>(back.entries[1]);
    CHECK(p1.profile_id == 5);
    CHECK(p1.at_utc_s == 123);
    CHECK(std::get<JournalProfiles>(back.entries[2]).rows == profiles.rows);
    const JournalProfileSettings& kept_settings = std::get<JournalProfileSettings>(back.entries[3]);
    CHECK(kept_settings.profile_id == 6);
    CHECK(kept_settings.text == settings.text);
    CHECK(kept_settings.updated_utc_s == 55);

    // The record as the build before profiles wrote it: no profile, score or coverage on a run, no
    // profile on a practice record.
    JournalRun plain;
    plain.record = sample_run(4);
    const std::string run_payload = encode_journal_entry(plain).substr(8);
    REQUIRE(run_payload.size() > 3);
    JournalPractice dated;
    dated.hash = 0x98;
    dated.practice = busy_practice();
    dated.at_utc_s = 77;
    const std::string practice_payload = encode_journal_entry(dated).substr(8);
    const JournalDecode old = decode_journal(
        encode_journal_header() + framed_record(run_payload.substr(0, run_payload.size() - 3)) +
        framed_record(practice_payload.substr(0, practice_payload.size() - 1)));
    REQUIRE(old.entries.size() == 2);
    CHECK(old.damaged == 0);
    CHECK(std::get<JournalRun>(old.entries[0]).profile_id == kDefaultProfileId);
    CHECK(std::get<JournalRun>(old.entries[0]).record == plain.record);
    CHECK(std::get<JournalPractice>(old.entries[1]).profile_id == kDefaultProfileId);
    CHECK(std::get<JournalPractice>(old.entries[1]).at_utc_s == 77);

    // A profile record with no rows, or a settings record for no profile, is damage.
    CHECK(decode_journal(encode_journal_header() + encode_journal_entry(JournalProfiles{})).damaged ==
          1);
    CHECK(decode_journal(encode_journal_header() +
                         encode_journal_entry(JournalProfileSettings{0, "x", 0}))
              .damaged == 1);
}
