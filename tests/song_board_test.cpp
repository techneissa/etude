// SPDX-License-Identifier: GPL-3.0-or-later
// The Board tab's data: the household's best run of a song per period, and the active profile's
// best runs chart, personal best and comparison (song_board.hpp).
#include "song_board_cache.hpp"

#include "etude/song_board.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace etude;

namespace {

constexpr std::int32_t kToday = 200;
constexpr std::uint64_t kSong = 7;

// A counting run of `song` on `day` with a stored score, `hit` of 20 graded notes at `speed`.
RunRecord run(std::int32_t day, int score, int hit = 18, int speed = 100, std::uint64_t song = kSong) {
    static std::int64_t serial = 0;
    RunRecord r;
    r.song_hash = song;
    r.local_day = day;
    r.speed_percent = speed;
    r.start_utc_s = day * 86'400LL + ++serial;
    r.end_utc_s = r.start_utc_s + 60;
    r.duration_ms = 60'000;
    r.score = score;
    r.covered_percent = 100;
    r.longest_combo = hit;
    r.all = {20, hit};
    return sanitized(r);
}

RunRecord before_scores(std::int32_t day) {
    RunRecord r = run(day, 0);
    r.covered_percent = 0;
    return r;
}

ProfileRuns of(std::int64_t id, const std::vector<RunRecord>& runs) { return {id, runs}; }

// A household of just profile 1.
std::vector<ProfileRuns> one(const std::vector<RunRecord>& runs) { return {of(1, runs)}; }

} // namespace

TEST_CASE("song board: rows are ranked best first with the best run's accuracy, speed and day") {
    const std::vector<RunRecord> anna{run(190, 700, 16, 100), run(200, 933, 17, 100)};
    const std::vector<RunRecord> ben{run(197, 1012, 18, 100)};
    const std::vector<RunRecord> mum{run(192, 780, 16, 75)};
    const std::vector<ProfileRuns> house{of(1, anna), of(2, ben), of(3, mum)};
    const SongBoard board = build_song_board(house, kSong, Period::Month, kToday, 1);
    REQUIRE(board.rows.size() == 3);
    CHECK(board.rows[0].profile_id == 2);
    CHECK(board.rows[0].rank == 1);
    CHECK(board.rows[0].score == 1012);
    CHECK(board.rows[0].accuracy_pct == 90);
    CHECK(board.rows[0].day == 197);
    CHECK(board.rows[1].profile_id == 1);
    CHECK(board.rows[1].score == 933);
    CHECK(board.rows[1].accuracy_pct == 85); // the best run's, not the latest
    CHECK(board.rows[1].runs == 2);
    CHECK(board.rows[2].profile_id == 3);
    CHECK(board.rows[2].speed_percent == 75);
    CHECK(board.rows[2].rank == 3);
}

TEST_CASE("song board: equal points share a rank, the earlier day first") {
    const std::vector<RunRecord> a{run(195, 800)};
    const std::vector<RunRecord> b{run(190, 800)};
    const std::vector<RunRecord> c{run(196, 500)};
    const std::vector<ProfileRuns> house{of(1, a), of(2, b), of(3, c)};
    const SongBoard board = build_song_board(house, kSong, Period::AllTime, kToday, 1);
    REQUIRE(board.rows.size() == 3);
    CHECK(board.rows[0].profile_id == 2);
    CHECK(board.rows[0].rank == 1);
    CHECK(board.rows[1].profile_id == 1);
    CHECK(board.rows[1].rank == 1);
    CHECK(board.rows[2].rank == 3);
}

TEST_CASE("song board: the period leaves old runs out, and nothing in it lists a profile last") {
    const std::vector<RunRecord> anna{run(200, 600)};
    const std::vector<RunRecord> ben{run(120, 900)};
    const std::vector<ProfileRuns> house{of(1, anna), of(2, ben)};
    const SongBoard week = build_song_board(house, kSong, Period::Week, kToday, 1);
    REQUIRE(week.rows.size() == 2);
    CHECK(week.rows[0].profile_id == 1);
    CHECK(week.rows[1].profile_id == 2);
    CHECK(week.rows[1].score == 0);
    CHECK(week.rows[1].accuracy_pct == -1);
    CHECK(week.rows[1].speed_percent == 0);
    CHECK(week.rows[1].runs == 0);
    const SongBoard all = build_song_board(house, kSong, Period::AllTime, kToday, 1);
    CHECK(all.rows[0].profile_id == 2);
    CHECK(all.rows[0].score == 900);
}

TEST_CASE("song board: a period with no run at all lists everyone with nothing") {
    const std::vector<RunRecord> anna{run(10, 600)};
    const std::vector<ProfileRuns> house{of(1, anna), of(2, {})};
    const SongBoard board = build_song_board(house, kSong, Period::Week, kToday, 1);
    REQUIRE(board.rows.size() == 2);
    for (const SongBoardRow& row : board.rows) {
        CHECK(row.score == 0);
        CHECK(row.accuracy_pct == -1);
        CHECK(row.runs == 0);
    }
    CHECK(board.chart.runs.size() == 1); // the chart ignores the period
}

TEST_CASE("song board: a profile with no runs is listed last") {
    const std::vector<RunRecord> anna{run(199, 400)};
    const std::vector<ProfileRuns> house{of(2, {}), of(1, anna)};
    const SongBoard board = build_song_board(house, kSong, Period::Week, kToday, 1);
    REQUIRE(board.rows.size() == 2);
    CHECK(board.rows[0].profile_id == 1);
    CHECK(board.rows[1].profile_id == 2);
    CHECK(board.rows[1].rank == 2);
}

TEST_CASE("song board: runs from before scores and other songs are not on the board") {
    const std::vector<RunRecord> anna{before_scores(190), run(195, 500, 18, 100, kSong + 1)};
    const SongBoard board = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1);
    REQUIRE(board.rows.size() == 1);
    CHECK(board.rows[0].score == 0);
    CHECK(board.rows[0].runs == 0);
    CHECK(board.chart.runs.empty());
    CHECK_FALSE(board.has_run);
    CHECK(board.vs == ScoreVs::NotCounted);
}

TEST_CASE("song board: the chart is the best five scored runs of the song, oldest first") {
    std::vector<RunRecord> anna;
    for (int i = 0; i < 9; ++i) {
        anna.push_back(run(180 + i, 100 + 10 * i));
        anna.push_back(before_scores(180 + i)); // never in the chart
        anna.push_back(before_scores(180 + i)); // never in the chart
    }
    const SongBoard board = build_song_board(one(anna), kSong, Period::Week, kToday, 1);
    REQUIRE(board.chart.runs.size() == 5);
    CHECK(board.chart.runs.front().score == 140);
    CHECK(board.chart.runs.back().score == 180);
    CHECK(board.chart.runs.back().day == 188);
    CHECK(board.chart.place == RunPlace::Best);
    CHECK(board.chart.rank == 1);
    CHECK(board.run_score == 180);
    CHECK(board.personal_best == 180);
}

TEST_CASE("song board: an old best run stays in the chart however many runs follow it") {
    std::vector<RunRecord> anna{run(150, 990)};
    for (int i = 0; i < 12; ++i) {
        anna.push_back(run(160 + i, 300 + i));
    }
    const SongBoard board = build_song_board(one(anna), kSong, Period::Week, kToday, 1);
    REQUIRE(board.chart.runs.size() == 5);
    CHECK(board.chart.runs.front().score == 990);
    CHECK(board.chart.runs.front().day == 150);
    CHECK(board.chart.star == 0);
    // The latest run (311) is in the best five, behind the 990.
    CHECK(board.chart.place == RunPlace::Best);
    CHECK(board.chart.rank == 2);
}

TEST_CASE("song board: the page's run below the best five gets its distance from the fifth") {
    std::vector<RunRecord> anna;
    for (const int score : {900, 880, 860, 840, 820}) {
        anna.push_back(run(kToday - 10, score));
    }
    anna.push_back(run(kToday, 790));
    const SongBoard board = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1);
    CHECK(board.chart.place == RunPlace::Below);
    CHECK(board.chart.behind == 30);
    CHECK(board.chart.run_score == 790);
    REQUIRE(board.chart.runs.size() == 5);
    CHECK(board.chart.runs.front().score == 900);
}

TEST_CASE("song board: the delta against the last run and the personal best") {
    const std::vector<RunRecord> beat{run(190, 885), run(200, 933)};
    SongBoard board = build_song_board(one(beat), kSong, Period::AllTime, kToday, 1);
    CHECK(board.has_run);
    CHECK(board.vs == ScoreVs::Beat);
    CHECK(board.delta == 48);
    CHECK(board.last_score == 885);
    CHECK(board.is_personal_best);
    CHECK(board.personal_best == 933);

    const std::vector<RunRecord> behind{run(190, 900), run(200, 850)};
    board = build_song_board(one(behind), kSong, Period::AllTime, kToday, 1);
    CHECK(board.vs == ScoreVs::Behind);
    CHECK(board.delta == -50);
    CHECK_FALSE(board.is_personal_best);
    CHECK(board.personal_best == 900);

    const std::vector<RunRecord> first{run(200, 400)};
    board = build_song_board(one(first), kSong, Period::AllTime, kToday, 1);
    CHECK(board.vs == ScoreVs::First);
    CHECK(board.is_personal_best);
    CHECK(board.last_score == 0);
}

TEST_CASE("song board: a run dated after today is left out") {
    const std::vector<RunRecord> anna{run(190, 500), run(kToday + 3, 999)};
    const SongBoard board = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1);
    CHECK(board.rows[0].score == 500);
    CHECK(board.run_score == 500);
    REQUIRE(board.chart.runs.size() == 1);
    CHECK(board.chart.runs[0].score == 500);
}

TEST_CASE("song board: an active profile the household does not hold gets no chart") {
    const std::vector<RunRecord> anna{run(190, 500)};
    const SongBoard board = build_song_board(one(anna), kSong, Period::AllTime, kToday, 99);
    CHECK(board.rows.size() == 1);
    CHECK(board.chart.runs.empty());
    CHECK_FALSE(board.has_run);
}

TEST_CASE("song board cache: built once for a card, rebuilt when the runs, the day or the card change") {
    app::SongBoardCache cache;
    CHECK(cache.board(Period::Week) == nullptr);
    std::vector<RunRecord> anna{run(190, 500)};
    std::vector<ProfileRuns> house{of(1, anna)};
    cache.refresh(house, kSong, kToday, 1, 5, 1);
    CHECK_FALSE(cache.stale(kSong, kToday, 1, 5, 1));
    CHECK(cache.stale(kSong, kToday, 1, 5, 2));
    CHECK(cache.stale(kSong, kToday + 1, 1, 5, 1));
    REQUIRE(cache.board(Period::Month) != nullptr);
    CHECK(cache.builds() == 1);
    CHECK(cache.board(Period::AllTime)->rows[0].score == 500);
    cache.refresh(house, kSong, kToday, 1, 5, 1);
    CHECK(cache.builds() == 1);
    anna.push_back(run(kToday, 640));
    house = {of(1, anna)};
    cache.refresh(house, kSong, kToday, 1, 5, 2);
    CHECK(cache.builds() == 2);
    CHECK(cache.board(Period::AllTime)->run_score == 640);
    cache.refresh(house, kSong, kToday + 1, 1, 5, 2);
    CHECK(cache.builds() == 3);
    cache.refresh(house, kSong, kToday + 1, 1, 6, 2);
    CHECK(cache.builds() == 4);
    cache.clear();
    CHECK(cache.board(Period::Week) == nullptr);
}

TEST_CASE("song board: the head is the card's run, not the profile's latest run of the song") {
    // An 800-point run, then a run of 5 notes: too few to score. The card of the short run says so,
    // and does not show the 800 and its comparison.
    RunRecord short_run = run(kToday, 40, 4);
    short_run.all = {5, 4};
    const std::vector<RunRecord> anna{run(kToday - 1, 800), short_run};
    const SongBoard latest = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1);
    CHECK(latest.has_run);
    CHECK(latest.run_score == 800);
    const SongBoard card = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1, &short_run);
    CHECK_FALSE(card.has_run);
    CHECK(card.vs == ScoreVs::NotCounted);
    CHECK(card.rows[0].score == 800); // the table is the household's, whatever the card
    // The personal best is the real best, 800, not 0; and the short run is not a bar of 0.
    CHECK(card.personal_best == 800);
    REQUIRE(card.chart.runs.size() == 1);
    CHECK(card.chart.runs[0].score == 800);
    CHECK_FALSE(card.chart.runs[0].current);
    CHECK(card.chart.place == RunPlace::TooShort);
}

TEST_CASE("song board: a short run with nothing before it has no bars and no personal best to show") {
    RunRecord short_run = run(kToday, 40, 4);
    short_run.all = {5, 4};
    const std::vector<RunRecord> anna{short_run};
    const SongBoard card = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1, &short_run);
    CHECK_FALSE(card.has_run);
    CHECK(card.chart.runs.empty());
    CHECK(card.chart.place == RunPlace::TooShort);
    CHECK(card.personal_best == 0);
}

TEST_CASE("song board: a run given is the head, with its own comparison and place in the chart") {
    const std::vector<RunRecord> anna{run(kToday - 3, 500), run(kToday - 2, 700), run(kToday - 1, 650)};
    // The card is about the middle run, though a later one is in the history: the chart holds all
    // three, and the middle run is the best.
    const SongBoard middle = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1, &anna[1]);
    CHECK(middle.has_run);
    CHECK(middle.run_score == 700);
    CHECK(middle.vs == ScoreVs::Beat);
    CHECK(middle.delta == 200);
    CHECK(middle.is_personal_best);
    REQUIRE(middle.chart.runs.size() == 3);
    CHECK(middle.chart.runs[1].current);
    CHECK(middle.chart.place == RunPlace::Best);
    CHECK(middle.chart.rank == 1);
    // A run not in the history yet ends the chart and is compared with the last one in it.
    const RunRecord fresh = run(kToday, 900);
    const SongBoard next = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1, &fresh);
    CHECK(next.run_score == 900);
    CHECK(next.delta == 250);
    REQUIRE(next.chart.runs.size() == 4);
    CHECK(next.chart.runs.back().score == 900);
    CHECK(next.chart.runs.back().current);
    CHECK(next.personal_best == 900);
}

TEST_CASE("song board: versions of a piece are separate files, each with its own chart") {
    // The board counts a file by its content hash: a second version of the piece has its own board.
    const std::vector<RunRecord> anna{run(190, 500), run(195, 900, 18, 100, kSong + 1)};
    const SongBoard board = build_song_board(one(anna), kSong, Period::AllTime, kToday, 1);
    REQUIRE(board.chart.runs.size() == 1);
    CHECK(board.chart.runs[0].score == 500);
    const SongBoard other = build_song_board(one(anna), kSong + 1, Period::AllTime, kToday, 1);
    REQUIRE(other.chart.runs.size() == 1);
    CHECK(other.chart.runs[0].score == 900);
}

TEST_CASE("song board: the three periods in one pass are the three single builds") {
    const std::vector<RunRecord> anna{run(kToday - 100, 500), run(kToday - 20, 700), run(kToday - 2, 650)};
    const std::vector<RunRecord> ben{run(kToday - 1, 900), run(kToday - 40, 300, 18, 100, 99)};
    const std::vector<ProfileRuns> house{of(1, anna), of(2, ben)};
    const std::array<SongBoard, 3> all = build_song_boards(house, kSong, kToday, 1);
    for (const Period period : {Period::Week, Period::Month, Period::AllTime}) {
        CHECK(all[static_cast<std::size_t>(period)] == build_song_board(house, kSong, period, kToday, 1));
    }
    // The chart is the same in all three: it ignores the period and holds every run of the song.
    CHECK(all[0].chart == all[2].chart);
    CHECK(all[0].chart.runs.size() == 3);
}
