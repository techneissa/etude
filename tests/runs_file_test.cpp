// SPDX-License-Identifier: GPL-3.0-or-later
// The old runs.dat format, which only the one-time move into history.db reads: framing, damage and
// records from a newer build.
#include "runs_file.hpp"

#include <doctest/doctest.h>

#include <random>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

RunRecord sample_run(int n) {
    RunRecord r;
    r.song_hash = 0xabc0 + static_cast<std::uint64_t>(n);
    r.song_title = "Song " + std::to_string(n);
    r.mode = n % 2 == 0 ? RunMode::Flow : RunMode::Wait;
    r.hands = HandFilter::Right;
    r.speed_percent = 80;
    r.start_utc_s = 1'760'000'000 + n * 100;
    r.end_utc_s = r.start_utc_s + 60;
    r.local_day = 20373;
    r.duration_ms = 55'000;
    r.counts.perfect = 10 + n;
    r.counts.missed = 2;
    r.right = {12 + n, 10 + n};
    r.all = r.right;
    r.longest_combo = 7;
    r.measures = {{1, 4, 1, 0}, {2, 8, 1, 1}};
    return sanitized(r);
}

} // namespace

TEST_CASE("runs file: a record's check catches a flipped byte, the rest are read") {
    std::vector<std::string> raw;
    for (int i = 0; i < 3; ++i) {
        raw.push_back(serialize_run_record(sample_run(i)));
    }
    std::string bytes = encode_runs(raw);
    const std::size_t second = kRunsHeader.size() + 8 + raw[0].size() + 8 + 5;
    bytes[second] = static_cast<char>(bytes[second] ^ 0x5A);
    const RunsDecode d = decode_runs(bytes);
    CHECK(d.damaged == 1);
    REQUIRE(d.runs.size() == 2);
    CHECK(d.runs[0] == sample_run(0));
    CHECK(d.runs[1] == sample_run(2));
}

TEST_CASE("runs file: input that is not a runs file is flagged and gives no runs") {
    const RunsDecode d = decode_runs("garbage, not runs");
    CHECK(d.bad_header);
    CHECK(d.runs.empty());
    CHECK_FALSE(decode_runs("").bad_header); // nothing at all is an empty history
}

TEST_CASE("runs file: a file cut off inside a record keeps every whole record before it") {
    std::vector<std::string> raw;
    for (int i = 0; i < 4; ++i) {
        raw.push_back(serialize_run_record(sample_run(i)));
    }
    const std::string bytes = encode_runs(raw);
    for (std::size_t cut = kRunsHeader.size(); cut < bytes.size(); ++cut) {
        const RunsDecode d = decode_runs(std::string_view(bytes).substr(0, cut));
        CHECK_FALSE(d.bad_header);
        CHECK(d.runs.size() <= 4);
        for (std::size_t i = 0; i < d.runs.size(); ++i) {
            CHECK(d.runs[i] == sample_run(static_cast<int>(i)));
        }
        // Exactly at a record boundary nothing is cut; anywhere else the tail is.
        std::size_t boundary = kRunsHeader.size();
        bool at_boundary = cut == boundary;
        for (const std::string& r : raw) {
            boundary += 8 + r.size();
            at_boundary = at_boundary || cut == boundary;
        }
        CHECK(d.truncated == !at_boundary);
    }
}

TEST_CASE("runs file: records from a newer build are kept byte for byte") {
    std::string future = serialize_run_record(sample_run(0));
    future[2] = 9; // version byte
    const std::vector<std::string> raw{serialize_run_record(sample_run(1)), future};
    const RunsDecode d = decode_runs(encode_runs(raw));
    CHECK(d.runs.size() == 1);
    CHECK(d.newer == 1);
    CHECK(d.damaged == 0);
    CHECK(d.kept == raw);
}

TEST_CASE("runs file: random damage never crashes and never yields a record that was not saved") {
    std::vector<std::string> raw;
    for (int i = 0; i < 6; ++i) {
        raw.push_back(serialize_run_record(sample_run(i)));
    }
    const std::string good = encode_runs(raw);
    std::mt19937 rng(7);
    for (int trial = 0; trial < 2000; ++trial) {
        std::string bytes = good;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int f = 0; f < flips; ++f) {
            const std::size_t at = rng() % bytes.size();
            bytes[at] = static_cast<char>(rng() & 0xFF);
        }
        if (rng() % 3 == 0) {
            bytes.resize(rng() % bytes.size());
        }
        const RunsDecode d = decode_runs(bytes);
        for (const RunRecord& r : d.runs) {
            bool known = false;
            for (int i = 0; i < 6; ++i) {
                known = known || r == sample_run(i);
            }
            CHECK(known);
        }
    }
}
