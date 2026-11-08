// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/run_record.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// A RunRecord as the flat columns a database keeps: every number a signed 64-bit integer, in a
// fixed order, the title, and the per-bar rows. The app binds and reads these by index, so the
// column list below is the one place that says which fields a run row has; the schema must list
// the same names in the same order (a test compares them).
//
// Enums are their numbers (RunMode: 0 Flow, 1 Wait; HandFilter: 0 Both, 1 Left, 2 Right). The song
// hash is the bit pattern of the unsigned hash, so the high bit makes the integer negative.

enum class RunCol : std::size_t {
    SongHash,
    Mode,
    Hands,
    SpeedPercent,
    TransposeSemitones,
    StartUtcS,
    EndUtcS,
    LocalDay,
    DurationMs,
    Perfect,
    Good,
    Early,
    Late,
    Missed,
    Played,
    Wrong,
    Unplayable,
    LongestCombo,
    CleanPasses,
    LeftGraded,
    LeftHit,
    RightGraded,
    RightHit,
    UnknownGraded,
    UnknownHit,
    AllGraded,
    AllHit,
    Score,
    CoveredPercent,
    Count,
};

inline constexpr std::size_t kRunColumnCount = static_cast<std::size_t>(RunCol::Count);

// SQL column names, indexed by RunCol.
inline constexpr std::array<std::string_view, kRunColumnCount> kRunColumnNames{
    "song_hash",   "mode",         "hands",         "speed_percent",  "transpose_semitones",
    "start_utc_s", "end_utc_s",    "local_day",     "duration_ms",    "perfect",
    "good",        "early",        "late",          "missed",         "played",
    "wrong",       "unplayable",   "longest_combo", "clean_passes",   "left_graded",
    "left_hit",    "right_graded", "right_hit",     "unknown_graded", "unknown_hit",
    "all_graded",  "all_hit",      "score",         "covered_percent",
};

struct RunColumns {
    std::array<std::int64_t, kRunColumnCount> ints{};
    std::string song_title;
    std::vector<RunBar> measures; // sorted by bar, one row per bar

    std::int64_t& operator[](RunCol c) { return ints[static_cast<std::size_t>(c)]; }
    std::int64_t operator[](RunCol c) const { return ints[static_cast<std::size_t>(c)]; }

    friend bool operator==(const RunColumns&, const RunColumns&) = default;
};

// The columns of a record (sanitized first, so a row never holds an out-of-range value).
RunColumns to_columns(const RunRecord& record);

// The record of a row. Numbers outside their range are clamped like sanitized() does; a row whose
// mode or hands is not a valid enum value is damaged and gives nothing.
std::optional<RunRecord> from_columns(const RunColumns& columns);

// The hash <-> integer a column holds.
std::int64_t hash_to_column(std::uint64_t hash);
std::uint64_t hash_from_column(std::int64_t column);

} // namespace etude
