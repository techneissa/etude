// SPDX-License-Identifier: GPL-3.0-or-later
// The replay of a stored run (#78): the history thread reads the run's notes, lays them over the
// song's and sums them up, and the frame thread takes the finished answer.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;

namespace {

// The six notes of sample_events(): half a second apart, two to a bar.
ReplayRequest request_for(const RunRecord& record) {
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
    return {record, notes, measures};
}

// The answer to a replay request, once the history thread has it.
EventsRead replay_answer(HistoryStore& store, std::uint64_t serial) {
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

TEST_CASE("replay: the stored notes come back as a finished timeline and summary") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord record = sample_run(1);
    store.add_run(record, "a.mid", sample_events());
    REQUIRE(store.flush());
    Raw raw(store.path());
    const std::int64_t id = run_id_at(raw, 1);

    const EventsRead got = replay_answer(store, store.request_replay(id, request_for(record)));
    CHECK(got.kind == EventsReadKind::Replay);
    CHECK_FALSE(got.found.has_value()); // the raw events stay on the history thread
    REQUIRE(got.replay != nullptr);
    CHECK(got.song_hash == record.song_hash);
    CHECK(got.run_id == id); // the id the frame needs to pin the run
    const RunReplay& replay = *got.replay;
    CHECK(replay.timeline.notes.size() == sample_events().events.size());
    CHECK(replay.timeline.complete);
    CHECK(replay.timeline.setup.speed_percent == 80);
    CHECK(replay.summary.detail == SummaryDetail::Played);
    CHECK(replay.summary.graded == 6);
    CHECK(replay.summary.missed == 1);
    CHECK(replay.summary.wrong == 1);
    REQUIRE(replay.summary.bar_count >= 1);
    CHECK(replay.summary.bars[0].measure == 2); // the missed third note, with the wrong press
}

TEST_CASE("replay: a run whose notes were never kept is answered with no replay") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord record = sample_run(2);
    store.add_run(record); // no events
    REQUIRE(store.flush());
    Raw raw(store.path());
    const EventsRead got =
        replay_answer(store, store.request_replay(run_id_at(raw, 2), request_for(record)));
    CHECK(got.kind == EventsReadKind::Replay);
    // No notes to watch, but the summary of the record, built on the history thread.
    REQUIRE(got.replay != nullptr);
    CHECK(got.replay->timeline.empty());
    CHECK(got.replay->summary.detail == SummaryDetail::RecordOnly);
    CHECK(got.replay->summary.graded == record.all.graded);
}

TEST_CASE("replay: a run id that belongs to another song is answered with no notes") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord mine = sample_run(1, 0xabc0);
    const RunRecord other = sample_run(2, 0xdef0);
    store.add_run(mine, "a.mid", sample_events());
    store.add_run(other, "b.mid", sample_events());
    REQUIRE(store.flush());
    Raw raw(store.path());
    // The open song is `other`'s; the run id is `mine`'s (a stale id): no notes on the wrong song.
    const EventsRead stale =
        replay_answer(store, store.request_replay(run_id_at(raw, 1), request_for(other)));
    CHECK(stale.kind == EventsReadKind::Replay);
    REQUIRE(stale.replay != nullptr);
    CHECK(stale.replay->timeline.empty());
    CHECK(stale.replay->summary.detail == SummaryDetail::RecordOnly);
    // The same id with the song it was played on still answers.
    const EventsRead same =
        replay_answer(store, store.request_replay(run_id_at(raw, 1), request_for(mine)));
    REQUIRE(same.replay != nullptr);
    CHECK_FALSE(same.replay->timeline.empty());
}

TEST_CASE("replay: a run just finished is found by its record, with no id") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord record = sample_run(1);
    store.add_run(record, "a.mid", sample_events());
    // Asked right behind the write, as the frame does when the results card appears: the history
    // thread works the queue in order, so the run is there.
    const EventsRead got = replay_answer(store, store.request_replay(0, request_for(record)));
    REQUIRE(got.replay != nullptr);
    CHECK(got.replay->summary.detail == SummaryDetail::Played);
    // A record the database has no run for (another start time) finds none.
    RunRecord other = record;
    other.start_utc_s += 1;
    const EventsRead none = replay_answer(store, store.request_replay(0, request_for(other)));
    REQUIRE(none.replay != nullptr);
    CHECK(none.replay->timeline.empty());
    CHECK(none.run_id == 0); // no run to pin
    // Nor does a record of another setup (the speed is part of what names a run).
    RunRecord slower = record;
    slower.speed_percent = 60;
    const EventsRead other_setup = replay_answer(store, store.request_replay(0, request_for(slower)));
    REQUIRE(other_setup.replay != nullptr);
    CHECK(other_setup.replay->timeline.empty());
}

TEST_CASE("replay: a run that is not there is answered with its record's summary") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const EventsRead got =
        replay_answer(store, store.request_replay(999'999, request_for(sample_run(1))));
    REQUIRE(got.replay != nullptr);
    CHECK(got.replay->timeline.empty());
    CHECK(got.replay->summary.detail == SummaryDetail::RecordOnly);
}

TEST_CASE("replay: a request with no song tables is refused, not answered later") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    ReplayRequest empty;
    empty.record = sample_run(1);
    CHECK(store.request_replay(1, empty) == 0);
}

TEST_CASE("replay: the answer is shared and read only, so taking it copies no events") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord record = sample_run(1);
    store.add_run(record, "a.mid", sample_events());
    REQUIRE(store.flush());
    Raw raw(store.path());
    EventsRead got =
        replay_answer(store, store.request_replay(run_id_at(raw, 1), request_for(record)));
    const std::shared_ptr<const RunReplay> held = std::move(got.replay);
    REQUIRE(held != nullptr);
    CHECK(held.use_count() == 1); // the frame owns it alone; the history thread keeps none
}
