// SPDX-License-Identifier: GPL-3.0-or-later
// RunRecord <-> the flat columns a database row holds.
#include "etude/run_columns.hpp"

#include <doctest/doctest.h>

#include <set>
#include <string>

using namespace etude;

namespace {

RunRecord full_record() {
    RunRecord r;
    r.song_hash = 0xfedcba9876543210ULL; // high bit set: negative as a column
    r.song_title = "Gymnopédie No. 1";
    r.mode = RunMode::Wait;
    r.hands = HandFilter::Left;
    r.speed_percent = 75;
    r.transpose_semitones = -3;
    r.start_utc_s = 1'760'000'000;
    r.end_utc_s = 1'760'000'090;
    r.local_day = 20373;
    r.duration_ms = 88'000;
    r.counts = {10, 20, 3, 4, 5, 6};
    r.wrong = 7;
    r.unplayable = 8;
    r.longest_combo = 9;
    r.clean_passes = 2;
    r.left = {30, 25};
    r.right = {12, 11};
    r.unknown = {3, 1};
    r.all = {45, 37};
    r.measures = {{0, 2, 1, 0}, {1, 8, 2, 1}, {5, 6, 0, 3}};
    return r;
}

} // namespace

TEST_CASE("run columns: every field of a record survives the round trip") {
    const RunRecord r = full_record();
    REQUIRE(sanitized(r) == r); // the fixture is in range
    const std::optional<RunRecord> back = from_columns(to_columns(r));
    REQUIRE(back.has_value());
    CHECK(*back == r);
}

TEST_CASE("run columns: the column names are the SQL identifiers, each once, in enum order") {
    std::set<std::string_view> seen;
    for (std::string_view name : kRunColumnNames) {
        CHECK_FALSE(name.empty());
        CHECK(seen.insert(name).second);
        for (char c : name) {
            CHECK(((c >= 'a' && c <= 'z') || c == '_'));
        }
    }
    CHECK(kRunColumnNames[static_cast<std::size_t>(RunCol::SongHash)] == "song_hash");
    CHECK(kRunColumnNames[static_cast<std::size_t>(RunCol::Mode)] == "mode");
    CHECK(kRunColumnNames[static_cast<std::size_t>(RunCol::TransposeSemitones)] ==
          "transpose_semitones");
    CHECK(kRunColumnNames[static_cast<std::size_t>(RunCol::LongestCombo)] == "longest_combo");
    CHECK(kRunColumnNames[static_cast<std::size_t>(RunCol::AllHit)] == "all_hit");
}

TEST_CASE("run columns: enums are their numbers and the hash is a bit pattern") {
    RunRecord r = full_record();
    RunColumns c = to_columns(r);
    CHECK(c[RunCol::Mode] == 1);
    CHECK(c[RunCol::Hands] == 1);
    CHECK(c[RunCol::SongHash] < 0);
    CHECK(hash_from_column(c[RunCol::SongHash]) == r.song_hash);
    r.mode = RunMode::Flow;
    r.hands = HandFilter::Right;
    c = to_columns(r);
    CHECK(c[RunCol::Mode] == 0);
    CHECK(c[RunCol::Hands] == 2);
    r.hands = HandFilter::Both;
    CHECK(to_columns(r)[RunCol::Hands] == 0);
    CHECK(hash_from_column(hash_to_column(0)) == 0);
    CHECK(hash_from_column(hash_to_column(~0ULL)) == ~0ULL);
}

TEST_CASE("run columns: a row with an invalid enum value is refused") {
    RunColumns c = to_columns(full_record());
    c[RunCol::Mode] = 2;
    CHECK_FALSE(from_columns(c).has_value());
    c = to_columns(full_record());
    c[RunCol::Hands] = 3;
    CHECK_FALSE(from_columns(c).has_value());
    c[RunCol::Hands] = -1;
    CHECK_FALSE(from_columns(c).has_value());
}

TEST_CASE("run columns: numbers out of range are clamped like sanitized() does") {
    RunColumns c = to_columns(full_record());
    c[RunCol::SpeedPercent] = 5000;
    c[RunCol::TransposeSemitones] = -99;
    c[RunCol::Perfect] = -4;
    c[RunCol::AllGraded] = 10;
    c[RunCol::AllHit] = 50; // more hits than graded notes
    c[RunCol::LocalDay] = 1LL << 40;
    c[RunCol::DurationMs] = -5;
    const std::optional<RunRecord> r = from_columns(c);
    REQUIRE(r.has_value());
    CHECK(r->speed_percent == 200);
    CHECK(r->transpose_semitones == -12);
    CHECK(r->counts.perfect == 0);
    CHECK(r->all.hit == 10);
    CHECK(r->local_day == kMaxRunDay);
    CHECK(r->duration_ms == 0);
    CHECK(*r == sanitized(*r));
}

TEST_CASE("run columns: to_columns sanitizes first, so a row is always in range") {
    RunRecord r = full_record();
    r.song_title = std::string("bad\x01 title");
    r.right = {5, 9};
    r.end_utc_s = r.start_utc_s - 10;
    const RunColumns c = to_columns(r);
    CHECK(c.song_title == "bad title");
    CHECK(c[RunCol::RightHit] == 5);
    CHECK(c[RunCol::EndUtcS] == c[RunCol::StartUtcS]);
}

TEST_CASE("run columns: the bars come through sorted and merged") {
    RunRecord r = full_record();
    r.measures = {{4, 2, 1, 0}, {2, 3, 0, 1}, {4, 1, 1, 1}};
    const RunColumns c = to_columns(r);
    REQUIRE(c.measures.size() == 2);
    CHECK(c.measures[0].measure == 2);
    CHECK(c.measures[1].measure == 4);
    CHECK(c.measures[1].graded == 3);
    CHECK(c.measures[1].missed == 2);
}

TEST_CASE("run columns: a hand that graded nothing still has no accuracy after the round trip") {
    RunRecord r = full_record();
    r.left = {0, 0};
    r.right = {0, 0};
    r.unknown = {0, 0};
    r.all = {0, 0};
    const std::optional<RunRecord> back = from_columns(to_columns(r));
    REQUIRE(back.has_value());
    CHECK(*back == sanitized(r));
    CHECK_FALSE(back->accuracy(HandFilter::Both).has_value());
    CHECK_FALSE(back->accuracy(HandFilter::Left).has_value());
    CHECK_FALSE(back->accuracy(HandFilter::Right).has_value());

    r.left = {4, 3};
    r.all = {4, 3};
    const std::optional<RunRecord> some = from_columns(to_columns(r));
    REQUIRE(some.has_value());
    CHECK(some->accuracy(HandFilter::Left).value() == doctest::Approx(0.75));
    CHECK_FALSE(some->accuracy(HandFilter::Right).has_value());
}
