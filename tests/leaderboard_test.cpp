// SPDX-License-Identifier: GPL-3.0-or-later
// The household leaderboard, personal bests and "beat your last run" (leaderboard.hpp).
#include "etude/leaderboard.hpp"

#include "etude/run_score.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using namespace etude;

namespace {

// A counting run of `song` on `day` with a stored score; every call is a different run.
RunRecord run(std::uint64_t song, std::int32_t day, int score, int graded = 20) {
    static std::int64_t serial = 0;
    RunRecord r;
    r.song_hash = song;
    r.local_day = day;
    r.start_utc_s = day * 86'400LL + ++serial;
    r.end_utc_s = r.start_utc_s + 60;
    r.duration_ms = 60'000;
    r.score = score;
    r.covered_percent = 100; // scored: a whole pass
    r.counts.perfect = graded;
    r.longest_combo = graded;
    r.all = {graded, graded};
    return sanitized(r);
}

ProfileRuns of(std::int64_t id, const std::vector<RunRecord>& runs) { return {id, runs}; }

constexpr std::int32_t kToday = 200;

} // namespace

TEST_CASE("leaderboard: a song's board ranks each profile's best run") {
    const std::vector<RunRecord> anna{run(1, 190, 500), run(1, 195, 700), run(1, 198, 650),
                                      run(2, 198, 900)};
    const std::vector<RunRecord> ben{run(1, 196, 800), run(1, 197, 100)};
    const std::vector<ProfileRuns> both{of(1, anna), of(2, ben)};
    const std::vector<BoardRow> rows = song_leaderboard(both, 1, Period::AllTime, kToday);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].profile_id == 2);
    CHECK(rows[0].score == 800);
    CHECK(rows[0].runs == 2);
    CHECK(rows[0].day == 196);
    CHECK(rows[0].rank == 1);
    CHECK(rows[1].profile_id == 1);
    CHECK(rows[1].score == 700); // song 2's run is not song 1's
    CHECK(rows[1].runs == 3);
    CHECK(rows[1].rank == 2);
}

TEST_CASE("leaderboard: a run counts only with enough notes, a song, and once") {
    const RunRecord twice = run(1, 190, 500);
    const std::vector<RunRecord> anna{run(1, 190, 900, 3),      // two notes short of a best
                                      run(0, 190, 900),        // no song
                                      twice, twice,            // saved twice
                                      run(1, kToday + 5, 900)}; // dated after today
    const auto rows = song_leaderboard(std::vector<ProfileRuns>{of(1, anna)}, 1, Period::AllTime,
                                       kToday);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].score == 500);
    CHECK(rows[0].runs == 1);
    const auto overall = overall_leaderboard(std::vector<ProfileRuns>{of(1, anna)},
                                             Period::AllTime, kToday);
    CHECK(overall[0].score == 500);
    CHECK(overall[0].songs == 1);
}

TEST_CASE("leaderboard: the first run, a short run and the run itself in the history") {
    const std::vector<RunRecord> none;
    const ScoreChange first = compare_score(none, run(1, 100, 300));
    CHECK(first.vs == ScoreVs::First);
    CHECK(first.personal_best);
    CHECK(first.last_score == 0);
    CHECK(compare_score(none, run(1, 100, 300, 3)).vs == ScoreVs::NotCounted);
    CHECK(compare_score(none, run(0, 100, 300)).vs == ScoreVs::NotCounted);
    // The history may already hold the run just finished: it is not its own last run.
    const RunRecord now = run(1, 103, 650);
    const std::vector<RunRecord> with_it{run(1, 100, 500), now};
    const ScoreChange c = compare_score(with_it, now);
    CHECK(c.vs == ScoreVs::Beat);
    CHECK(c.last_score == 500);
}
