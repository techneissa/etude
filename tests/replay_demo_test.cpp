// SPDX-License-Identifier: GPL-3.0-or-later
// The developer's invented run (--summary-demo, --replay-demo): a replay and a summary that agree
// with their record, over a song of a few bars.
#include "replay_demo.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;

namespace {

Song small_song() {
    Song s;
    for (int i = 0; i < 80; ++i) {
        Note n;
        n.start_s = 0.25 * i;
        n.duration_s = 0.2;
        n.pitch = static_cast<std::uint8_t>(60 + i % 12);
        n.hand = i % 2 == 0 ? Hand::Right : Hand::Left;
        n.measure = 1 + i / 4;
        s.notes.push_back(n);
    }
    for (int b = 0; b < 20; ++b) {
        Measure m;
        m.number = b + 1;
        m.start_s = static_cast<double>(b);
        s.measures.push_back(m);
    }
    s.duration_s = 20.0;
    return s;
}

} // namespace

TEST_CASE("demo run: the record counts what the replay holds") {
    const Song song = small_song();
    const DemoRun run = make_demo_run(song, Transposition{}, 77);
    REQUIRE(run.replay != nullptr);
    CHECK(run.record.song_hash == 77);
    CHECK(run.record.all.graded == 80);
    CHECK(run.record.counts.missed > 0);
    CHECK(run.record.wrong > 0);
    const RunSummary& s = run.replay->summary;
    CHECK(s.detail == SummaryDetail::Played);
    CHECK(s.graded == run.record.all.graded);
    CHECK(s.missed == run.record.counts.missed);
    CHECK(s.wrong == run.record.wrong);
    CHECK(s.bar_count > 0);
    CHECK(s.timing.valid());
    CHECK_FALSE(run.replay->timeline.notes.empty());
    CHECK(run.replay->timeline.complete);
}

TEST_CASE("demo run: the same song gives the same run") {
    const Song song = small_song();
    const DemoRun a = make_demo_run(song, Transposition{}, 1);
    const DemoRun b = make_demo_run(song, Transposition{}, 1);
    CHECK(a.replay->timeline.notes == b.replay->timeline.notes);
    CHECK(a.record == b.record);
}

TEST_CASE("demo run: at most the notes asked for") {
    const Song song = small_song();
    const DemoRun run = make_demo_run(song, Transposition{}, 1, 10);
    CHECK(run.record.all.graded == 10);
}
