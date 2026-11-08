// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/run_columns.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace etude {

namespace {

int to_int(std::int64_t v) {
    return static_cast<int>(std::clamp<std::int64_t>(v, std::numeric_limits<int>::min(),
                                                     std::numeric_limits<int>::max()));
}

} // namespace

std::int64_t hash_to_column(std::uint64_t hash) { return std::bit_cast<std::int64_t>(hash); }
std::uint64_t hash_from_column(std::int64_t column) { return std::bit_cast<std::uint64_t>(column); }

RunColumns to_columns(const RunRecord& record) {
    const RunRecord r = sanitized(record);
    RunColumns c;
    c[RunCol::SongHash] = hash_to_column(r.song_hash);
    c[RunCol::Mode] = static_cast<std::int64_t>(r.mode);
    c[RunCol::Hands] = static_cast<std::int64_t>(r.hands);
    c[RunCol::SpeedPercent] = r.speed_percent;
    c[RunCol::TransposeSemitones] = r.transpose_semitones;
    c[RunCol::StartUtcS] = r.start_utc_s;
    c[RunCol::EndUtcS] = r.end_utc_s;
    c[RunCol::LocalDay] = r.local_day;
    c[RunCol::DurationMs] = r.duration_ms;
    c[RunCol::Perfect] = r.counts.perfect;
    c[RunCol::Good] = r.counts.good;
    c[RunCol::Early] = r.counts.early;
    c[RunCol::Late] = r.counts.late;
    c[RunCol::Missed] = r.counts.missed;
    c[RunCol::Played] = r.counts.played;
    c[RunCol::Wrong] = r.wrong;
    c[RunCol::Unplayable] = r.unplayable;
    c[RunCol::LongestCombo] = r.longest_combo;
    c[RunCol::CleanPasses] = r.clean_passes;
    c[RunCol::LeftGraded] = r.left.graded;
    c[RunCol::LeftHit] = r.left.hit;
    c[RunCol::RightGraded] = r.right.graded;
    c[RunCol::RightHit] = r.right.hit;
    c[RunCol::UnknownGraded] = r.unknown.graded;
    c[RunCol::UnknownHit] = r.unknown.hit;
    c[RunCol::AllGraded] = r.all.graded;
    c[RunCol::AllHit] = r.all.hit;
    c[RunCol::Score] = r.score;
    c[RunCol::CoveredPercent] = r.covered_percent;
    c.song_title = r.song_title;
    c.measures = r.measures;
    return c;
}

std::optional<RunRecord> from_columns(const RunColumns& c) {
    const std::int64_t mode = c[RunCol::Mode];
    const std::int64_t hands = c[RunCol::Hands];
    if (mode < 0 || mode > static_cast<std::int64_t>(RunMode::Wait) || hands < 0 ||
        hands > static_cast<std::int64_t>(HandFilter::Right)) {
        return std::nullopt;
    }
    RunRecord r;
    r.song_hash = hash_from_column(c[RunCol::SongHash]);
    r.song_title = c.song_title;
    r.mode = static_cast<RunMode>(mode);
    r.hands = static_cast<HandFilter>(hands);
    r.speed_percent = to_int(c[RunCol::SpeedPercent]);
    r.transpose_semitones = to_int(c[RunCol::TransposeSemitones]);
    r.start_utc_s = c[RunCol::StartUtcS];
    r.end_utc_s = c[RunCol::EndUtcS];
    r.local_day = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(c[RunCol::LocalDay], -kMaxRunDay, kMaxRunDay));
    r.duration_ms = c[RunCol::DurationMs];
    r.counts.perfect = to_int(c[RunCol::Perfect]);
    r.counts.good = to_int(c[RunCol::Good]);
    r.counts.early = to_int(c[RunCol::Early]);
    r.counts.late = to_int(c[RunCol::Late]);
    r.counts.missed = to_int(c[RunCol::Missed]);
    r.counts.played = to_int(c[RunCol::Played]);
    r.wrong = to_int(c[RunCol::Wrong]);
    r.unplayable = to_int(c[RunCol::Unplayable]);
    r.longest_combo = to_int(c[RunCol::LongestCombo]);
    r.clean_passes = to_int(c[RunCol::CleanPasses]);
    r.left = {to_int(c[RunCol::LeftGraded]), to_int(c[RunCol::LeftHit])};
    r.right = {to_int(c[RunCol::RightGraded]), to_int(c[RunCol::RightHit])};
    r.unknown = {to_int(c[RunCol::UnknownGraded]), to_int(c[RunCol::UnknownHit])};
    r.all = {to_int(c[RunCol::AllGraded]), to_int(c[RunCol::AllHit])};
    r.score = to_int(c[RunCol::Score]);
    r.covered_percent = to_int(c[RunCol::CoveredPercent]);
    r.measures = c.measures;
    return sanitized(r);
}

} // namespace etude
