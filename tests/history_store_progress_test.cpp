// SPDX-License-Identifier: GPL-3.0-or-later
// The Progress page's numbers built on the history thread from every profile's runs
// (history_store_progress.hpp).
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

constexpr std::int32_t kToday = 20'500;

// A scored run of `song` on `day`, five minutes, `n` making its start second its own.
RunRecord scored_run(std::uint64_t song, std::int32_t day, int score, int n) {
    RunRecord r;
    r.song_hash = song;
    r.song_title = "Song " + std::to_string(song);
    r.start_utc_s = 1'760'000'000 + n * 100;
    r.end_utc_s = r.start_utc_s + 300;
    r.local_day = day;
    r.duration_ms = 300'000;
    r.score = score;
    r.covered_percent = 100;
    r.all = {20, 18};
    return sanitized(r);
}

// The answer to `serial`, once the history thread has it.
std::shared_ptr<const ProgressView> answer_of(HistoryStore& store, std::uint64_t serial) {
    REQUIRE(serial != 0);
    REQUIRE(store.flush());
    for (ProgressAnswer& a : store.take_progress()) {
        if (a.serial == serial) {
            return a.view;
        }
    }
    FAIL("no answer for the request");
    return nullptr;
}

std::shared_ptr<const ProgressView> ask(HistoryStore& store, std::int64_t profile = 0,
                                        int goal_minutes = 0) {
    return answer_of(store, store.request_progress({profile, kToday, goal_minutes}));
}

} // namespace

TEST_CASE("progress: two profiles' runs give the calendar, streak, bests, ranks and practise") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    REQUIRE(store.add_profile("Anna") == ProfileResult::Ok);
    RunRecord bars = scored_run(10, kToday - 1, 500, 1);
    bars.measures = {{3, 4, 3, 0}, {4, 4, 1, 0}, {8, 4, 0, 0}};
    store.add_run(bars, "a.mid", RunEvents{}, kDefaultProfileId);
    store.add_run(scored_run(11, kToday, 300, 2), "b.mid", RunEvents{}, kDefaultProfileId);
    store.add_run(scored_run(10, kToday, 700, 3), "a.mid", RunEvents{}, 2);

    const auto mine = ask(store, kDefaultProfileId, 5);
    REQUIRE(mine != nullptr);
    CHECK(mine->profile_id == kDefaultProfileId);
    CHECK(mine->household_size == 2);
    REQUIRE(mine->calendar.size() == static_cast<std::size_t>(kCalendarDays));
    CHECK(mine->calendar.back().level == 3); // 5 minutes of a 5 minute goal
    CHECK(mine->calendar[mine->calendar.size() - 2].level == 3);
    CHECK(mine->me.streak.current == 2);
    CHECK(mine->me.points == 800);
    CHECK(mine->me.rank == 1);   // 800 against Anna's 700
    CHECK(mine->me.firsts == 1); // song 11
    CHECK(mine->goal.met);
    REQUIRE(mine->bests.size() == 2);
    CHECK(mine->bests[0].song_hash == 10);
    CHECK(mine->bests[0].rank == 2); // Anna is first on it
    CHECK(mine->bests[1].song_hash == 11);
    CHECK(mine->bests[1].rank == 1);
    // The newest run (song 11) named no bar; the one before it names bar 3 and the bar next to it.
    REQUIRE(mine->practise.size() == 1);
    CHECK(mine->practise[0].song_hash == 10);
    CHECK(mine->practise[0].first_measure == 3);
    CHECK(mine->practise[0].last_measure == 4);
    CHECK(mine->last_song_hash == 11);
    REQUIRE(mine->household.size() == 2);
    CHECK(mine->household[0].profile_id == kDefaultProfileId);
    CHECK(mine->household[1].profile_id == 2);

    const auto anna = ask(store, 2);
    REQUIRE(anna != nullptr);
    CHECK(anna->profile_id == 2);
    CHECK(anna->me.points == 700);
    CHECK(anna->me.rank == 2);
    CHECK(anna->me.firsts == 1);
    CHECK(anna->me == anna->household[1]);
}

TEST_CASE("progress: profile 0 is the active profile") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    REQUIRE(store.add_profile("Anna") == ProfileResult::Ok);
    store.add_run(scored_run(10, kToday, 700, 3), "a.mid", RunEvents{}, 2);
    REQUIRE(store.switch_profile(2));
    CHECK(ask(store)->profile_id == 2);
    CHECK(ask(store)->me.points == 700);
    REQUIRE(store.switch_profile(kDefaultProfileId));
    CHECK(ask(store)->profile_id == kDefaultProfileId);
    CHECK(ask(store)->me.points == 0);
}

TEST_CASE("progress: a run just added is in the next answer, without waiting for the write") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(ask(store)->me.runs == 0);
    store.add_run(scored_run(10, kToday, 400, 1));
    const std::uint64_t serial = store.request_progress({0, kToday, 0}); // queued behind the run
    const auto one = answer_of(store, serial);
    CHECK(one->me.runs == 1);
    CHECK(one->me.points == 400);
    store.add_run(scored_run(10, kToday, 650, 2));
    store.add_run(scored_run(12, kToday - 2, 100, 3));
    const auto three = ask(store);
    CHECK(three->me.runs == 3);
    CHECK(three->me.points == 750);
    CHECK(three->me.songs == 2);
    // Today and two days ago: yesterday's miss is covered by the freeze the earlier day earned, and today earns one.
    CHECK(three->me.streak.current == 2);
    CHECK(three->me.streak.freezes == 1);
}

TEST_CASE("progress: a profile added after the load has a line, and its runs count") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    CHECK(ask(store)->household_size == 1);
    REQUIRE(store.add_profile("Anna") == ProfileResult::Ok);
    REQUIRE(store.add_profile("Ben") == ProfileResult::Ok);
    const auto before = ask(store);
    REQUIRE(before->household.size() == 3);
    CHECK(before->household[0].profile_id == kDefaultProfileId);
    CHECK(before->household[1].profile_id == 2);
    CHECK(before->household[2].profile_id == 3);
    store.add_run(scored_run(10, kToday, 500, 1), "a.mid", RunEvents{}, 3);
    const auto after = ask(store);
    CHECK(after->household[2].points == 500);
    CHECK(after->household[2].rank == 1);
    CHECK(after->household[1].rank == 0);
    CHECK(after->me.rank == 0);
}

TEST_CASE("progress: an empty database gives an empty page") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const auto v = ask(store);
    REQUIRE(v != nullptr);
    CHECK(v->household_size == 1);
    CHECK(v->me.runs == 0);
    CHECK(v->bests.empty());
    CHECK(v->practise.empty());
    CHECK(v->calendar.size() == static_cast<std::size_t>(kCalendarDays));
    CHECK(v->today == kToday);
}

TEST_CASE("progress: the history of an earlier session is read from the database") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(scored_run(10, kToday - 3, 400, 1));
        store.add_run(scored_run(10, kToday - 2, 500, 2));
        REQUIRE(store.flush());
    }
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    const auto v = ask(store);
    CHECK(v->me.runs == 2);
    REQUIRE(v->bests.size() == 1);
    CHECK(v->bests[0].score == 500);
    CHECK(v->bests[0].trend == std::vector<int>{400, 500});
}

TEST_CASE("progress: a request after close takes no work") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.close();
    CHECK(store.request_progress({0, kToday, 0}) == 0);
    CHECK(store.take_progress().empty());
}

TEST_CASE("progress: a request the database cannot serve before the store ends is answered with "
          "no view") {
    TempDir dir;
    sql::Database other;
    REQUIRE(other.open(dir.path / "history.db") == SQLITE_OK);
    REQUIRE(other.run_script("CREATE TABLE theirs (x INTEGER); INSERT INTO theirs VALUES (1)") ==
            SQLITE_OK);
    REQUIRE(other.run_script("BEGIN EXCLUSIVE") == SQLITE_OK);
    HistoryOptions options = quiet();
    options.open_busy_timeout_ms = 20;
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    open_now(store);
    CHECK(store.state() == HistoryState::Waiting);
    const std::uint64_t serial = store.request_progress({0, kToday, 0});
    REQUIRE(serial != 0);
    CHECK(store.take_progress().empty()); // nothing can be read yet
    store.close();
    const std::vector<ProgressAnswer> answers = store.take_progress();
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].serial == serial);
    CHECK(answers[0].view == nullptr);
}

TEST_CASE("progress: a store another etude holds answers with no view or takes no work") {
    TempDir dir;
    HistoryStore first(dir.path, quiet());
    REQUIRE(open_now(first).empty());
    HistoryStore second(dir.path, quiet());
    second.open();
    const std::uint64_t serial = second.request_progress({0, kToday, 0});
    REQUIRE(second.wait_until_settled(10s));
    second.close(); // the thread has answered what it still held once it has ended
    const std::vector<ProgressAnswer> answers = second.take_progress();
    if (serial == 0) {
        CHECK(answers.empty()); // the store had settled and taken no more work
    } else {
        REQUIRE(answers.size() == 1);
        CHECK(answers[0].view == nullptr);
    }
}

TEST_CASE("progress: a request is never journaled") {
    TempDir dir;
    sql::Database other;
    REQUIRE(other.open(dir.path / "history.db") == SQLITE_OK);
    REQUIRE(other.run_script("CREATE TABLE theirs (x INTEGER); INSERT INTO theirs VALUES (1)") ==
            SQLITE_OK);
    REQUIRE(other.run_script("BEGIN EXCLUSIVE") == SQLITE_OK);
    HistoryOptions options = quiet();
    options.open_busy_timeout_ms = 20;
    options.retry_interval_ms = 30;
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        REQUIRE(store.request_progress({0, kToday, 0}) != 0);
        store.close();
    }
    std::error_code ec;
    CHECK_FALSE(fs::exists(dir.path / "history.journal", ec));
}

TEST_CASE("progress: a long history is answered whole") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    for (int i = 0; i < 1000; ++i) {
        RunRecord r =
            scored_run(static_cast<std::uint64_t>(1 + i % 400), kToday - i % 200, 100 + i % 900, i);
        r.measures = {{1 + i % 7, 4, i % 4, 0}, {2 + i % 5, 4, 1, 0}};
        store.add_run(r, "a.mid", RunEvents{}, kDefaultProfileId);
    }
    REQUIRE(store.flush(60s));
    const auto v = ask(store);
    CHECK(v->me.runs == 1000);
    CHECK(v->bests.size() == static_cast<std::size_t>(kBestSongs));
}

TEST_CASE("progress: every profile's medals are worked out on the history thread, each against its own goal") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    REQUIRE(store.add_profile("Anna") == ProfileResult::Ok);
    // Three days of five-minute runs for each of the two.
    for (int d = 0; d < 3; ++d) {
        store.add_run(scored_run(10, kToday - d, 500, 1 + d), "a.mid", RunEvents{}, kDefaultProfileId);
        store.add_run(scored_run(10, kToday - d, 600, 10 + d), "a.mid", RunEvents{}, 2);
    }
    // The profile asked for has a 5 minute goal: three days meeting it is the first Goal getter tier
    // (3 days); Anna keeps the goal every profile starts with (15 minutes), which five minutes do not meet.
    const auto view = ask(store, kDefaultProfileId, 5);
    REQUIRE(view != nullptr);
    CHECK(view->me.medals.value_of(Achievement::GoalGetter) == 3);
    CHECK(view->me.medals.tier_of(Achievement::GoalGetter) == 1);
    CHECK(view->me.medals.tier_of(Achievement::Runs) == 1);
    CHECK(view->me.medals.title() >= 1);
    REQUIRE(view->household.size() == 2);
    const ProfileProgress* anna = nullptr;
    for (const ProfileProgress& p : view->household) {
        if (p.profile_id == 2) {
            anna = &p;
        }
    }
    REQUIRE(anna != nullptr);
    CHECK(anna->medals.value_of(Achievement::GoalGetter) == 0);
    CHECK(anna->medals.tier_of(Achievement::Runs) == 1);
    // The household entry of the profile asked for is the same numbers as `me`.
    for (const ProfileProgress& p : view->household) {
        if (p.profile_id == kDefaultProfileId) {
            CHECK(p.medals == view->me.medals);
        }
    }
}
