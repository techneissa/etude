// SPDX-License-Identifier: GPL-3.0-or-later
// The frame-side review and replay of a run (#78): the answer is taken only when it is the latest
// request's and about the open song, and the view's clock is its own.
#include "replay_session.hpp"

#include "history_store_test_support.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;

namespace {

FlowEvaluator::NoteTable song_notes() {
    auto notes = std::make_shared<std::vector<Note>>();
    for (int i = 0; i < 6; ++i) {
        Note n;
        n.start_s = 0.5 * i;
        n.duration_s = 0.25;
        n.pitch = static_cast<std::uint8_t>(60 + i);
        n.hand = i % 2 == 0 ? Hand::Right : Hand::Left;
        n.measure = 1 + i / 2;
        notes->push_back(n);
    }
    return notes;
}

FlowEvaluator::MeasureTable song_measures() {
    auto measures = std::make_shared<std::vector<Measure>>();
    for (int b = 0; b < 3; ++b) {
        Measure m;
        m.number = b + 1;
        m.start_s = 1.0 * b;
        measures->push_back(m);
    }
    return measures;
}

// A session that has the review of the stored run, ready.
struct Ready {
    TempDir dir;
    HistoryStore store{dir.path, quiet()};
    RunRecord record = sample_run(1);
    ReplaySession session;

    Ready() {
        REQUIRE(open_now(store).empty());
        store.add_run(record, "a.mid", sample_events());
        REQUIRE(store.flush());
        session.ask(store, record, song_notes(), song_measures());
        REQUIRE(session.review() == ReplaySession::Review::Loading);
        REQUIRE(store.flush());
        REQUIRE(session.poll(store, record.song_hash));
    }
};

} // namespace

TEST_CASE("replay session: the answer for the run asked about becomes the review") {
    Ready r;
    CHECK(r.session.review() == ReplaySession::Review::Ready);
    CHECK(r.session.watchable());
    CHECK(r.session.data()->summary.graded == 6);
}

TEST_CASE("replay session: an answer about another song is dropped") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord record = sample_run(1);
    store.add_run(record, "a.mid", sample_events());
    ReplaySession session;
    session.ask(store, record, song_notes(), song_measures());
    REQUIRE(store.flush());
    CHECK_FALSE(session.poll(store, record.song_hash + 1)); // another song is open now
    CHECK(session.review() == ReplaySession::Review::Loading);
}

TEST_CASE("replay session: the answer to an older request is dropped when a newer one was asked") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord first = sample_run(1);
    const RunRecord second = sample_run(2, 0xdef0); // a run of another song than the one open below
    store.add_run(first, "a.mid", sample_events());
    store.add_run(second, "b.mid", sample_events());
    ReplaySession session;
    session.ask(store, first, song_notes(), song_measures());
    REQUIRE(store.flush()); // the first answer is waiting
    session.ask(store, second, song_notes(), song_measures());
    REQUIRE(store.flush()); // and the second
    // The first is about the open song but is not the latest request's; the second is the latest
    // but about another song: neither is taken, whatever the order they come in.
    CHECK_FALSE(session.poll(store, first.song_hash));
    CHECK(session.review() == ReplaySession::Review::Loading);
    CHECK(session.data() == nullptr);
}

TEST_CASE("replay session: of two requests the latest answer is the review") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord with_notes = sample_run(1);
    const RunRecord without = sample_run(2);
    store.add_run(with_notes, "a.mid", sample_events());
    store.add_run(without, "a.mid"); // no events kept
    ReplaySession session;
    session.ask(store, with_notes, song_notes(), song_measures());
    session.ask(store, without, song_notes(), song_measures());
    REQUIRE(store.flush());
    REQUIRE(session.poll(store, without.song_hash));
    CHECK(session.review() == ReplaySession::Review::Ready);
    CHECK_FALSE(session.watchable()); // the second run's: nothing to watch
    CHECK(session.record().start_utc_s == without.start_utc_s);
}

TEST_CASE("replay session: the answer is dropped when the card went before it came") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord record = sample_run(1);
    store.add_run(record, "a.mid", sample_events());
    ReplaySession session;
    session.ask(store, record, song_notes(), song_measures());
    session.forget_review();
    REQUIRE(store.flush());
    CHECK_FALSE(session.poll(store, record.song_hash));
    CHECK(session.review() == ReplaySession::Review::None);
}

TEST_CASE("replay session: the keys light as the replay passes them and go dark on a seek or a close") {
    Ready r;
    ReplaySession& s = r.session;
    REQUIRE(s.open_view());
    CHECK(s.lit_count() == 0);
    s.play_pause();
    s.update(0.3); // the first notes pass
    CHECK(s.lit_count() > 0);
    // A seek far from any note darkens what was lit and lights only what is mid-note there.
    s.seek_ms(60'000);
    s.update(0.0);
    CHECK(s.lit_count() == 0);
    s.seek_ms(0);
    s.play_pause();
    s.update(0.0);
    s.play_pause();
    s.update(0.3);
    REQUIRE(s.lit_count() > 0);
    s.close_view();
    CHECK(s.lit_count() == 0);
}

TEST_CASE("replay session: a jump to a mistake and a change of speed") {
    Ready r;
    ReplaySession& s = r.session;
    REQUIRE(s.open_view());
    const int first = s.speed_percent();
    s.next_speed();
    CHECK(s.speed_percent() != first);
    s.seek_ms(3000);
    CHECK_FALSE(s.jump_mistake(1)); // the sample's mistakes are all behind
    CHECK(s.jump_mistake(-1));      // two seconds ahead of one, or the start
    CHECK(s.position_ms() < 3000);
    std::vector<ReplayBarMark> marks;
    s.marks(4.0, 1.0, marks);
    CHECK_FALSE(marks.empty());
}

TEST_CASE("replay session: a run with notes is pinned and unpinned by the history thread's answers") {
    Ready r;
    ReplaySession& s = r.session;
    REQUIRE(s.pin_available());
    CHECK_FALSE(s.pinned());
    s.toggle_pin(r.store);
    CHECK(s.pin_pending());
    CHECK_FALSE(s.pinned()); // not until the answer says it took
    s.toggle_pin(r.store);   // a second tap while one waits changes nothing
    REQUIRE(r.store.flush());
    CHECK(s.poll(r.store, r.record.song_hash));
    CHECK_FALSE(s.pin_pending());
    CHECK(s.pinned());
    CHECK_FALSE(s.take_pin_refused());
    s.toggle_pin(r.store);
    REQUIRE(r.store.flush());
    CHECK(s.poll(r.store, r.record.song_hash));
    CHECK_FALSE(s.pinned());
    CHECK(s.pin_available());
}

TEST_CASE("replay session: a pinned run reopens pinned, an unpinned one reopens unpinned") {
    Ready r;
    ReplaySession& s = r.session;
    s.toggle_pin(r.store);
    REQUIRE(r.store.flush());
    REQUIRE(s.poll(r.store, r.record.song_hash));
    REQUIRE(s.pinned());
    // The review asked for again (the card back on screen): the database says pinned.
    ReplaySession again;
    again.ask(r.store, r.record, song_notes(), song_measures());
    REQUIRE(r.store.flush());
    REQUIRE(again.poll(r.store, r.record.song_hash));
    CHECK(again.pin_available());
    CHECK(again.pinned());
    // Let go of it, and the next reopening is unpinned.
    again.toggle_pin(r.store);
    REQUIRE(r.store.flush());
    REQUIRE(again.poll(r.store, r.record.song_hash));
    REQUIRE_FALSE(again.pinned());
    ReplaySession third;
    third.ask(r.store, r.record, song_notes(), song_measures());
    REQUIRE(r.store.flush());
    REQUIRE(third.poll(r.store, r.record.song_hash));
    CHECK_FALSE(third.pinned());
}

TEST_CASE("replay session: a run whose notes are gone cannot be pinned") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const RunRecord record = sample_run(1);
    store.add_run(record, "a.mid"); // no events kept
    ReplaySession session;
    session.ask(store, record, song_notes(), song_measures());
    REQUIRE(store.flush());
    REQUIRE(session.poll(store, record.song_hash));
    CHECK_FALSE(session.pin_available());
    session.toggle_pin(store);
    CHECK_FALSE(session.pin_pending());
    CHECK_FALSE(session.pinned());
}
