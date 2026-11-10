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
