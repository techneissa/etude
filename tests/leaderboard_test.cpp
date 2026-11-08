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

std::vector<RunRecord> none_of() { return {}; }

ProfileRuns of(std::int64_t id, const std::vector<RunRecord>& runs) { return {id, runs}; }

constexpr std::int32_t kToday = 200;

} // namespace

TEST_CASE("leaderboard: a period reaches back a week, a month or for ever") {
    CHECK(period_first_day(Period::Week, kToday) == 194);
    CHECK(period_first_day(Period::Month, kToday) == 171);
    CHECK(in_period(194, Period::Week, kToday));
    CHECK_FALSE(in_period(193, Period::Week, kToday));
    CHECK(in_period(kToday, Period::Week, kToday));
    CHECK_FALSE(in_period(kToday + 1, Period::Week, kToday)); // a clock that ran ahead
    CHECK(in_period(171, Period::Month, kToday));
    CHECK_FALSE(in_period(170, Period::Month, kToday));
    CHECK(in_period(-50'000, Period::AllTime, kToday));
    CHECK_FALSE(in_period(kToday + 1, Period::AllTime, kToday));
}

// A 4-bar loop played 20 times before scores existed: 400 notes graded, all perfect, no score and
// no coverage recorded.
RunRecord old_loop_run(std::int32_t day, std::uint64_t song = 1) {
    RunRecord r = run(song, day, 0, 400);
    r.covered_percent = 0;
    r.clean_passes = 20;
    for (int bar = 1; bar <= 4; ++bar) {
        r.measures.push_back({bar, 100, 0, 0});
    }
    return r;
}

TEST_CASE("leaderboard: a run from before scores is on no board and no best") {
    const RunRecord old = old_loop_run(190);
    CHECK_FALSE(is_scored(old));
    CHECK(is_scored(run(1, 100, 640)));
    CHECK(is_scored(run(1, 100, 0))); // scored 0 (all missed) is still scored
    const std::vector<RunRecord> anna{old, run(1, 195, 300)};
    const std::vector<RunRecord> ben{old_loop_run(191)};
    const std::vector<ProfileRuns> both{of(1, anna), of(2, ben)};
    const auto rows = song_leaderboard(both, 1, Period::AllTime, kToday);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].profile_id == 1);
    CHECK(rows[0].score == 300); // not the old loop's
    CHECK(rows[0].runs == 1);
    CHECK(rows[1].profile_id == 2);
    CHECK(rows[1].runs == 0);
    CHECK(rows[1].score == 0);
    const auto overall = overall_leaderboard(both, Period::AllTime, kToday);
    CHECK(overall[0].score == 300);
    CHECK(overall[1].songs == 0);
    CHECK_FALSE(personal_best(ben, 1, kToday).found);
    // Nor is it the "last run" to beat.
    const ScoreChange c = compare_score(std::vector<RunRecord>{old}, run(1, 196, 200));
    CHECK(c.vs == ScoreVs::First);
    CHECK(c.personal_best);
    CHECK(compare_score(none_of(), old).vs == ScoreVs::NotCounted);
}

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

TEST_CASE("leaderboard: the period leaves out older runs, and a profile with none is last") {
    const std::vector<RunRecord> anna{run(1, 150, 900), run(1, 199, 300)};
    const std::vector<RunRecord> ben{run(1, 190, 400)};
    const std::vector<RunRecord> cleo;
    const std::vector<ProfileRuns> all{of(1, anna), of(2, ben), of(3, cleo)};
    const auto week = song_leaderboard(all, 1, Period::Week, kToday);
    REQUIRE(week.size() == 3);
    CHECK(week[0].profile_id == 1);
    CHECK(week[0].score == 300);
    CHECK(week[1].profile_id == 2); // 190 is outside the week: no runs, no score
    CHECK(week[1].score == 0);
    CHECK(week[1].runs == 0);
    CHECK(week[2].profile_id == 3);
    const auto month = song_leaderboard(all, 1, Period::Month, kToday);
    CHECK(month[0].profile_id == 2);
    CHECK(month[0].score == 400);
    CHECK(month[1].profile_id == 1);
    CHECK(month[1].score == 300);
    const auto ever = song_leaderboard(all, 1, Period::AllTime, kToday);
    CHECK(ever[0].profile_id == 1);
    CHECK(ever[0].score == 900);
    CHECK(ever[0].day == 150);
}

TEST_CASE("leaderboard: equal points share a rank and the earlier day goes first") {
    const std::vector<RunRecord> anna{run(1, 190, 600)};
    const std::vector<RunRecord> ben{run(1, 180, 600)};
    const std::vector<RunRecord> cleo{run(1, 185, 400)};
    const std::vector<ProfileRuns> all{of(1, anna), of(2, ben), of(3, cleo)};
    const auto rows = song_leaderboard(all, 1, Period::AllTime, kToday);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].profile_id == 2);
    CHECK(rows[1].profile_id == 1);
    CHECK(rows[0].rank == 1);
    CHECK(rows[1].rank == 1);
    CHECK(rows[2].rank == 3);
    // Empty profiles all share the last place.
    const std::vector<RunRecord> none;
    const std::vector<ProfileRuns> two_empty{of(4, none), of(2, none)};
    const auto empty = song_leaderboard(two_empty, 1, Period::AllTime, kToday);
    CHECK(empty[0].rank == 1);
    CHECK(empty[1].rank == 1);
    CHECK(empty[0].profile_id == 4); // a stable sort keeps the given order
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

TEST_CASE("leaderboard: overall is the sum of each song's best in the period") {
    const std::vector<RunRecord> anna{run(1, 190, 500), run(1, 195, 700), run(2, 196, 300),
                                      run(3, 100, 900)};
    const std::vector<RunRecord> ben{run(1, 196, 1000)};
    const std::vector<ProfileRuns> both{of(1, anna), of(2, ben)};
    const auto ever = overall_leaderboard(both, Period::AllTime, kToday);
    REQUIRE(ever.size() == 2);
    CHECK(ever[0].profile_id == 1);
    CHECK(ever[0].score == 700 + 300 + 900);
    CHECK(ever[0].songs == 3);
    CHECK(ever[0].runs == 4);
    CHECK(ever[1].score == 1000);
    // Inside the week Anna has 700 + 300 and Ben 1000: a tie on points, one rank, and the order
    // of a tie (the day the total was reached, both 196, then the lower profile id).
    const auto week = overall_leaderboard(both, Period::Week, kToday);
    CHECK(week[0].score == 1000);
    CHECK(week[1].score == 1000);
    CHECK(week[0].rank == 1);
    CHECK(week[1].rank == 1);
    CHECK(week[0].profile_id == 1);
    CHECK(week[1].profile_id == 2);
}

TEST_CASE("leaderboard: personal best of a song, a tie going to the earlier run") {
    const std::vector<RunRecord> runs{run(1, 100, 500), run(1, 110, 800), run(2, 120, 999),
                                      run(1, 130, 800), run(1, 190, 650)};
    const PersonalBest best = personal_best(runs, 1, kToday);
    CHECK(best.found);
    CHECK(best.score == 800);
    CHECK(best.day == 110);
    CHECK(best.index == 1);
    CHECK(best.runs == 4);
    const PersonalBest recent = personal_best(runs, 1, kToday, Period::Month);
    CHECK(recent.score == 650);
    CHECK(recent.index == 4);
    CHECK(recent.runs == 1);
    CHECK_FALSE(personal_best(runs, 3, kToday).found);
    CHECK_FALSE(personal_best(runs, 1, kToday, Period::Week).found);
}

TEST_CASE("leaderboard: a run is compared with the last run of the song") {
    const std::vector<RunRecord> history{run(1, 100, 500), run(2, 101, 900), run(1, 102, 600)};
    const ScoreChange up = compare_score(history, run(1, 103, 650));
    CHECK(up.vs == ScoreVs::Beat);
    CHECK(up.last_score == 600);
    CHECK(up.delta == 50);
    CHECK(up.score == 650);
    CHECK(up.personal_best); // above every earlier run of the song
    const ScoreChange same = compare_score(history, run(1, 103, 600));
    CHECK(same.vs == ScoreVs::Tied);
    CHECK(same.delta == 0);
    CHECK_FALSE(same.personal_best);
    const ScoreChange down = compare_score(history, run(1, 103, 520));
    CHECK(down.vs == ScoreVs::Behind);
    CHECK(down.delta == -80);
    CHECK_FALSE(down.personal_best);
}

TEST_CASE("leaderboard: beating the last run is not the same as a personal best") {
    const std::vector<RunRecord> history{run(1, 100, 900), run(1, 102, 400)};
    const ScoreChange c = compare_score(history, run(1, 103, 500));
    CHECK(c.vs == ScoreVs::Beat);
    CHECK_FALSE(c.personal_best);
    const ScoreChange top = compare_score(history, run(1, 103, 901));
    CHECK(top.personal_best);
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
