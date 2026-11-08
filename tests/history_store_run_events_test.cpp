// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: a run's events, their pruning and pinning, the ghost, and the answer to a read.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST_CASE("run events: a run's events are saved with it and read back whole, in one call") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(store.events_kept_per_song() == 20);
    const RunEvents events = sample_events();
    RunEvents copy = events;
    store.add_run(sample_run(1), "a.mid", std::move(copy));
    REQUIRE(store.flush());
    CHECK(store.take_errors().empty());

    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM run_events") == static_cast<std::int64_t>(events.events.size()));
    const std::int64_t id = run_id_at(raw, 1);

    const auto got = answer(store, store.request_run_events(id));
    REQUIRE(got.has_value());
    CHECK(got->run_id == id);
    CHECK(got->events == events);
    CHECK(got->setup.mode == sample_run(1).mode);
    CHECK(got->setup.hands == sample_run(1).hands);
    CHECK(got->setup.speed_percent == 80);
    CHECK(got->setup.transpose_semitones == sample_run(1).transpose_semitones);
    CHECK(got->all.graded == sample_run(1).all.graded);
    CHECK_FALSE(got->pinned);

    // A wrong press has no note, hand or grade in the table.
    CHECK(raw.num("SELECT COUNT(*) FROM run_events WHERE kind = 1 AND note_index IS NULL AND "
                  "hand IS NULL AND grade IS NULL") == 1);
}

TEST_CASE("run events: a run handed over unordered is ordered by the history thread and saved whole") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    RunEventRecorder recorder(16);
    for (const std::uint32_t note : {3u, 0u, 2u, 1u}) {
        NoteResult r;
        r.note_index = note;
        r.grade = NoteGrade::Perfect;
        r.expected_pitch = 60 + static_cast<int>(note);
        r.hand = Hand::Right;
        r.song_s = 0.5 * note;
        r.velocity = 80;
        r.press_song_s = r.song_s;
        recorder.add(r);
    }
    RunRecord record = sample_run(1);
    store.add_run(record, "a.mid", recorder.hand_over(4)); // as the frame thread hands it
    REQUIRE(store.flush());
    CHECK(store.take_errors().empty());

    RunEventRecorder reference(16);
    for (const std::uint32_t note : {3u, 0u, 2u, 1u}) {
        NoteResult r;
        r.note_index = note;
        r.grade = NoteGrade::Perfect;
        r.expected_pitch = 60 + static_cast<int>(note);
        r.hand = Hand::Right;
        r.song_s = 0.5 * note;
        r.velocity = 80;
        r.press_song_s = r.song_s;
        reference.add(r);
    }
    const RunEvents expected = reference.finish(4);
    CHECK(expected.complete);

    Raw raw(store.path());
    const auto got = answer(store, store.request_run_events(run_id_at(raw, 1)));
    REQUIRE(got.has_value());
    CHECK(got->events == expected); // in song-time order, and complete decided on the way
}

TEST_CASE("run events: they survive a restart, and a run without events has none to read") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1), "a.mid", sample_events());
        store.add_run(sample_run(2)); // no events
        REQUIRE(store.flush());
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    Raw raw(again.path());
    const auto first = answer(again, again.request_run_events(run_id_at(raw, 1)));
    REQUIRE(first.has_value());
    CHECK(first->events == sample_events());
    CHECK_FALSE(answer(again, again.request_run_events(run_id_at(raw, 2))).has_value());
    CHECK_FALSE(answer(again, again.request_run_events(999'999)).has_value());
    CHECK(again.runs().size() == 2);
}

TEST_CASE("run events: a run repeated adds its events once") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1), "a.mid", sample_events());
    store.add_run(sample_run(1), "a.mid", sample_events());
    REQUIRE(store.flush());
    CHECK(store.take_errors().empty());
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM run_events") == static_cast<std::int64_t>(sample_events().events.size()));
}

TEST_CASE("run events: only the newest runs of a song keep theirs; summaries and other songs stay") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.set_events_kept_per_song(3);
    for (int n = 1; n <= 5; ++n) {
        store.add_run(one_setup(sample_run(n, 0xabc0)), "a.mid", sample_events());
    }
    for (int n = 11; n <= 12; ++n) {
        store.add_run(one_setup(sample_run(n, 0xdef0)), "b.mid", sample_events());
    }
    REQUIRE(store.flush());
    CHECK(store.take_errors().empty());
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 7); // every summary stays
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 5); // 3 of song A, 2 of song B
    // The three newest of A: runs 3, 4 and 5.
    for (int n = 1; n <= 5; ++n) {
        CAPTURE(n);
        CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE run_id = " +
                      std::to_string(run_id_at(raw, n))) == (n >= 3 ? 1 : 0));
        CHECK(raw.num("SELECT COUNT(*) FROM run_events WHERE run_id = " +
                      std::to_string(run_id_at(raw, n))) ==
              (n >= 3 ? static_cast<std::int64_t>(sample_events().events.size()) : 0));
    }
    // No events of a run that lost them left behind.
    CHECK(raw.num("SELECT COUNT(*) FROM run_events WHERE run_id NOT IN "
                  "(SELECT run_id FROM run_event_sets)") == 0);
    CHECK(store.runs().size() == 7);
}

TEST_CASE("run events: a pinned run keeps its events however many runs follow") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.set_events_kept_per_song(2);
    store.add_run(one_setup(sample_run(1)), "a.mid", sample_events());
    REQUIRE(store.flush());
    Raw raw(store.path());
    const std::int64_t first = run_id_at(raw, 1);
    store.set_run_pinned(first, true);
    for (int n = 2; n <= 6; ++n) {
        store.add_run(one_setup(sample_run(n)), "a.mid", sample_events());
    }
    REQUIRE(store.flush());
    // The pinned run and the newest two.
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 3);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE run_id = " + std::to_string(first)) == 1);
    const auto pinned = answer(store, store.request_run_events(first));
    REQUIRE(pinned.has_value());
    CHECK(pinned->pinned);
    // Let go of the pin: the next run's save makes room.
    store.set_run_pinned(first, false);
    store.add_run(one_setup(sample_run(7)), "a.mid", sample_events());
    REQUIRE(store.flush());
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 2);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE run_id = " + std::to_string(first)) == 0);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 7);
}

TEST_CASE("run events: keeping none records none and frees the old ones with the next run") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1), "a.mid", sample_events());
    REQUIRE(store.flush());
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
    store.set_events_kept_per_song(0);
    store.add_run(sample_run(2), "a.mid", sample_events());
    REQUIRE(store.flush());
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 0);
    CHECK(raw.num("SELECT COUNT(*) FROM run_events") == 0);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 2);
    // A negative or huge setting is held to its range.
    store.set_events_kept_per_song(-4);
    CHECK(store.events_kept_per_song() == 0);
    store.set_events_kept_per_song(100'000);
    CHECK(store.events_kept_per_song() == kMaxRunEventsKept);
}

TEST_CASE("run events: a song with no content hash keeps none") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1, 0), "a.mid", sample_events());
    REQUIRE(store.flush());
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 0);
    CHECK(store.request_ghost(0, GhostSetup{}) == 0);
}

TEST_CASE("run events: deleting a run deletes its events, deleting the events keeps the run") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1), "a.mid", sample_events());
        store.add_run(sample_run(2), "a.mid", sample_events());
        REQUIRE(store.flush());
    }
    Raw raw(dir.path / "history.db");
    REQUIRE(raw.db.run_script("PRAGMA foreign_keys = ON") == SQLITE_OK);
    const std::int64_t first = run_id_at(raw, 1);
    const std::int64_t second = run_id_at(raw, 2);
    REQUIRE(raw.db.run_script("DELETE FROM run_event_sets WHERE run_id = " + std::to_string(first)) ==
            SQLITE_OK);
    CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE id = " + std::to_string(first)) == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM run_events WHERE run_id = " + std::to_string(first)) == 0);
    REQUIRE(raw.db.run_script("DELETE FROM runs WHERE id = " + std::to_string(second)) == SQLITE_OK);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 0);
    CHECK(raw.num("SELECT COUNT(*) FROM run_events") == 0);
}

TEST_CASE("ghost: the best complete run at a comparable setup is read with its events") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const auto played = [&](int n, int speed, int transpose, RunMode mode, HandFilter hands,
                            int hit, bool complete, std::uint64_t song = 0xabc0) {
        RunRecord r = sample_run(n, song);
        r.speed_percent = speed;
        r.transpose_semitones = transpose;
        r.mode = mode;
        r.hands = hands;
        r.all = {100, hit};
        store.add_run(sanitized(r), "a.mid", sample_events(6, complete));
    };
    // Wanted: Flow, both hands, 100%, no transposition.
    played(1, 100, 0, RunMode::Flow, HandFilter::Both, 80, true);   // comparable
    played(2, 108, 0, RunMode::Flow, HandFilter::Both, 90, true);   // comparable, the best
    played(3, 100, 0, RunMode::Flow, HandFilter::Both, 99, false);  // incomplete
    played(4, 100, 1, RunMode::Flow, HandFilter::Both, 98, true);   // another transposition
    played(5, 120, 0, RunMode::Flow, HandFilter::Both, 97, true);   // too fast
    played(6, 100, 0, RunMode::Wait, HandFilter::Both, 96, true);   // another mode
    played(7, 100, 0, RunMode::Flow, HandFilter::Right, 95, true);  // other hands
    played(8, 100, 0, RunMode::Flow, HandFilter::Both, 100, true, 0xdef0); // another song
    REQUIRE(store.flush());
    CHECK(store.take_errors().empty());
    Raw raw(store.path());

    GhostSetup want;
    want.mode = RunMode::Flow;
    want.hands = HandFilter::Both;
    want.speed_percent = 100;
    want.transpose_semitones = 0;
    const auto ghost = answer(store, store.request_ghost(0xabc0, want));
    REQUIRE(ghost.has_value());
    CHECK(ghost->run_id == run_id_at(raw, 2));
    CHECK(ghost->events.complete);
    CHECK(ghost->events.events == sample_events().events);

    // At 118% the window is 106.2 to 129.8: the 108% and the 120% runs are in, the 100% ones out,
    // and the 120% one scored higher.
    want.speed_percent = 118;
    const auto fast = answer(store, store.request_ghost(0xabc0, want));
    REQUIRE(fast.has_value());
    CHECK(fast->run_id == run_id_at(raw, 5));
    // Nothing comparable: a different transposition nobody played.
    want = GhostSetup{};
    want.transpose_semitones = 5;
    CHECK_FALSE(answer(store, store.request_ghost(0xabc0, want)).has_value());
    // A song never played.
    CHECK_FALSE(answer(store, store.request_ghost(0x7777, GhostSetup{})).has_value());
}

TEST_CASE("run events: a version 8 database gains the tables for them and keeps its runs") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        store.add_run(sample_run(2));
        REQUIRE(store.flush());
    }
    {
        // A file as version 8 left it: the reserved, empty run_events table of version 1 and
        // nothing else for them.
        Raw raw(dir.path / "history.db");
        undo_version_13(raw);
        REQUIRE(raw.db.run_script(
                    "DROP TABLE run_events; DROP TABLE run_event_sets; "
                    "ALTER TABLE song_practice DROP COLUMN no_shift_offer; "
                    "CREATE TABLE run_events (run_id INTEGER NOT NULL REFERENCES runs (id) ON "
                    "DELETE CASCADE, seq INTEGER NOT NULL, t_ms INTEGER NOT NULL, kind INTEGER NOT "
                    "NULL, key INTEGER NOT NULL, velocity INTEGER NOT NULL, hand INTEGER, "
                    "note_index INTEGER, grade INTEGER, PRIMARY KEY (run_id, seq)) WITHOUT ROWID; "
                    "ALTER TABLE song_practice DROP COLUMN split_point; "
                    "PRAGMA user_version = 8") == SQLITE_OK);
        CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('run_events') WHERE name = 't_ms'") == 1);
    }
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(store.runs().size() == 2);
    store.add_run(sample_run(3), "a.mid", sample_events());
    REQUIRE(store.flush());
    CHECK(store.take_errors().empty());
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(kLatestVersion >= 9);
    CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('run_events') WHERE name = 'song_ms'") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('run_events') WHERE name = 't_ms'") == 0);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
    const auto read = answer(store, store.request_run_events(run_id_at(raw, 3)));
    REQUIRE(read.has_value());
    CHECK(read->events == sample_events());
}

TEST_CASE("run events: an older run's events are never in the way of a read of a newer one") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    RunEvents a = sample_events(6);
    RunEvents b = sample_events(4);
    store.add_run(sample_run(1), "a.mid", RunEvents(a));
    store.add_run(sample_run(2), "a.mid", RunEvents(b));
    REQUIRE(store.flush());
    Raw raw(store.path());
    const auto got_a = answer(store, store.request_run_events(run_id_at(raw, 1)));
    const auto got_b = answer(store, store.request_run_events(run_id_at(raw, 2)));
    REQUIRE(got_a.has_value());
    REQUIRE(got_b.has_value());
    CHECK(got_a->events == a);
    CHECK(got_b->events == b);
}

TEST_CASE("run events: a read that fails for any reason is still answered, with nothing") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1), "a.mid", sample_events());
    REQUIRE(store.flush());
    Raw raw(store.path());
    const std::int64_t id = run_id_at(raw, 1);
    REQUIRE(answer(store, store.request_run_events(id)).has_value());
    // The events' table goes under the store (an error that is neither damage, nor a busy file,
    // nor the environment).
    REQUIRE(raw.db.run_script("DROP TABLE run_events") == SQLITE_OK);
    const std::uint64_t serial = store.request_run_events(id);
    const std::uint64_t ghost = store.request_ghost(0xabc0, GhostSetup{});
    REQUIRE(store.flush());
    bool run_answered = false;
    bool ghost_answered = false;
    for (const EventsRead& r : store.take_event_reads()) {
        if (r.serial == serial) {
            run_answered = true;
            CHECK(r.kind == EventsReadKind::Run);
            CHECK_FALSE(r.found.has_value());
        }
        if (r.serial == ghost) {
            ghost_answered = true;
            CHECK(r.kind == EventsReadKind::Ghost);
            CHECK(r.song_hash == 0xabc0);
            CHECK_FALSE(r.found.has_value());
        }
    }
    CHECK(run_answered);
    CHECK(ghost_answered);
    CHECK_FALSE(store.take_errors().empty());
}

TEST_CASE("run events: an answer says which song it is about, and one for song A is not applied to "
          "song B") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord a = sample_run(1, 0xabc0);
    const RunRecord b = sample_run(2, 0xdef0);
    store.add_run(a, "a.mid", sample_events());
    store.add_run(b, "b.mid", sample_events());
    REQUIRE(store.flush());
    const auto setup_of = [](const RunRecord& r) {
        GhostSetup s;
        s.mode = r.mode;
        s.hands = r.hands;
        s.speed_percent = r.speed_percent;
        s.transpose_semitones = r.transpose_semitones;
        return s;
    };
    // The song A is open and asks; before the answer is in, song B is opened and asks too.
    const std::uint64_t for_a = store.request_ghost(0xabc0, setup_of(a));
    const std::uint64_t for_b = store.request_ghost(0xdef0, setup_of(b));
    Raw raw(store.path());
    const std::uint64_t by_run = store.request_run_events(run_id_at(raw, 1));
    REQUIRE(store.flush());
    std::optional<EventsRead> answer_a;
    std::optional<EventsRead> answer_b;
    std::optional<EventsRead> answer_run;
    for (EventsRead& r : store.take_event_reads()) {
        if (r.serial == for_a) {
            answer_a = std::move(r);
        } else if (r.serial == for_b) {
            answer_b = std::move(r);
        } else if (r.serial == by_run) {
            answer_run = std::move(r);
        }
    }
    REQUIRE(answer_a.has_value());
    REQUIRE(answer_b.has_value());
    REQUIRE(answer_run.has_value());
    REQUIRE(answer_a->found.has_value());
    CHECK(answer_a->kind == EventsReadKind::Ghost);
    CHECK(answer_a->song_hash == 0xabc0);
    CHECK(answer_b->song_hash == 0xdef0);
    CHECK(answer_a->found->song_hash == 0xabc0);

    // Song B is open now and its request is the latest.
    CHECK_FALSE(ghost_answer_applies(*answer_a, for_b, 0xdef0)); // an older request, another song
    CHECK_FALSE(ghost_answer_applies(*answer_a, for_a, 0xdef0)); // the right request, wrong song
    CHECK(ghost_answer_applies(*answer_b, for_b, 0xdef0));
    // Back on A with the old request still the one waited for.
    CHECK(ghost_answer_applies(*answer_a, for_a, 0xabc0));
    CHECK_FALSE(ghost_answer_applies(*answer_b, for_a, 0xabc0));
    // A run read is no ghost, and says which song its run belongs to; no hash has no ghost.
    CHECK(answer_run->kind == EventsReadKind::Run);
    CHECK(answer_run->song_hash == 0xabc0);
    CHECK_FALSE(ghost_answer_applies(*answer_run, by_run, 0xabc0));
    CHECK_FALSE(ghost_answer_applies(*answer_a, for_a, 0));
}

// ---- The journal: what is held when the app quits
