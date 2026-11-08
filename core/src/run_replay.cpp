// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/run_replay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace etude {

namespace {

constexpr std::int32_t kMaxMs = std::numeric_limits<std::int32_t>::max();

std::int32_t to_ms(double seconds) {
    const double ms = std::round(seconds * 1000.0);
    if (!(ms > -static_cast<double>(kMaxMs))) { // also NaN
        return ms < 0 ? -kMaxMs : 0;
    }
    return ms >= static_cast<double>(kMaxMs) ? kMaxMs : static_cast<std::int32_t>(ms);
}

ReplayMark mark_of(const RunEvent& e) {
    switch (e.kind) {
    case RunEventKind::Wrong:
        return ReplayMark::Wrong;
    case RunEventKind::Missed:
        return ReplayMark::Missed;
    case RunEventKind::Press:
        break;
    }
    switch (e.grade) {
    case NoteGrade::Perfect:
        return ReplayMark::Perfect;
    case NoteGrade::Good:
        return ReplayMark::Good;
    case NoteGrade::Early:
        return ReplayMark::Early;
    case NoteGrade::Late:
        return ReplayMark::Late;
    case NoteGrade::Played:
    case NoteGrade::Missed: // a press never carries it after sanitizing
        break;
    }
    return ReplayMark::Played;
}

bool by_time(const ReplayNote& a, const ReplayNote& b) {
    if (a.time_ms != b.time_ms) {
        return a.time_ms < b.time_ms;
    }
    if (a.note_index != b.note_index) {
        return a.note_index < b.note_index;
    }
    if (a.key != b.key) {
        return a.key < b.key;
    }
    return a.mark < b.mark;
}

ReplayNote make_note(const RunEvent& e, const ReplaySong& song, bool& matched) {
    ReplayNote n;
    n.time_ms = e.song_ms;
    n.error_ms = e.error_ms;
    n.note_index = e.note_index;
    n.key = e.key;
    n.velocity = e.velocity;
    n.hand = e.hand;
    n.mark = mark_of(e);
    matched = false;
    if (e.note_index >= 0 && static_cast<std::size_t>(e.note_index) < song.notes.size()) {
        const Note& note = song.notes[static_cast<std::size_t>(e.note_index)];
        n.expected_start_ms = to_ms(note.start_s);
        n.expected_end_ms = std::max(to_ms(note.start_s + note.duration_s), n.expected_start_ms);
        n.measure = note.measure;
        if (n.hand == Hand::Unknown) {
            n.hand = note.hand;
        }
        if (e.kind == RunEventKind::Press) {
            n.offset_ms = e.song_ms - n.expected_start_ms;
        }
        matched = true;
        return n;
    }
    n.measure = measure_number_at(song.measures, static_cast<double>(e.song_ms) / 1000.0);
    return n;
}

} // namespace

Grade replay_grade(ReplayMark mark) {
    switch (mark) {
    case ReplayMark::Perfect:
    case ReplayMark::Played:
        return Grade::Perfect;
    case ReplayMark::Good:
        return Grade::Good;
    case ReplayMark::Early:
        return Grade::Early;
    case ReplayMark::Late:
        return Grade::Late;
    case ReplayMark::Missed:
        return Grade::Miss;
    case ReplayMark::Wrong:
        return Grade::Wrong;
    }
    return Grade::Miss;
}

std::int32_t replay_length_ms(const ReplayNote& note) {
    if (!note.has_note()) {
        return kReplayWrongLengthMs;
    }
    return std::max(note.expected_end_ms - note.expected_start_ms, kMinReplayLengthMs);
}

int measure_number_at(std::span<const Measure> measures, double seconds) {
    if (measures.empty()) {
        return 1;
    }
    // The last bar whose start is not after `seconds`.
    const auto after = std::upper_bound(
        measures.begin(), measures.end(), seconds,
        [](double t, const Measure& m) { return t < m.start_s; });
    return after == measures.begin() ? measures.front().number : std::prev(after)->number;
}

ReplayTimeline build_replay_timeline(const RunEvents& run, const GhostSetup& setup,
                                     const ReplaySong& song) {
    ReplayTimeline out;
    out.setup = setup;
    out.expected_notes = std::max(run.expected_notes, 0);
    out.dropped = std::max(run.dropped, 0);
    out.complete = run.complete && !run.unordered;
    out.notes.reserve(run.events.size());

    std::vector<std::int32_t> judged; // the note of each press or miss, to count and to spot repeats
    judged.reserve(run.events.size());
    for (const RunEvent& raw : run.events) {
        const RunEvent e = sanitized(raw);
        bool matched = false;
        out.notes.push_back(make_note(e, song, matched));
        if (e.note_index >= 0) {
            judged.push_back(e.note_index);
            if (!matched) {
                ++out.unmatched;
            }
        }
    }
    std::stable_sort(out.notes.begin(), out.notes.end(), by_time);

    std::sort(judged.begin(), judged.end());
    // How many times each note was judged: a loop judges each note once a lap, so a last lap cut
    // short leaves its later notes one judgement behind the earlier ones. (Counted before the
    // duplicates are squeezed out below.)
    std::int32_t fewest = std::numeric_limits<std::int32_t>::max();
    std::int32_t most = 0;
    for (std::size_t i = 0; i < judged.size();) {
        std::size_t j = i;
        while (j < judged.size() && judged[j] == judged[i]) {
            ++j;
        }
        fewest = std::min(fewest, static_cast<std::int32_t>(j - i));
        most = std::max(most, static_cast<std::int32_t>(j - i));
        i = j;
    }
    const auto distinct = std::unique(judged.begin(), judged.end());
    out.looped = distinct != judged.end();
    out.graded_notes = static_cast<std::int32_t>(distinct - judged.begin());
    // `complete` is decided by the store when it writes the run; an older set may not carry it, and
    // the number of notes it was to cover may be 0.
    const bool short_of_notes = out.expected_notes > 0 && out.graded_notes < out.expected_notes;
    const bool unfinished_lap = out.looped && fewest < most;
    out.stopped_early = !out.complete && (out.looped ? unfinished_lap : short_of_notes);

    for (const ReplayNote& n : out.notes) {
        if (n.mark == ReplayMark::Missed || n.mark == ReplayMark::Wrong) {
            out.mistakes.push_back(n.time_ms); // notes are in time order, so this is too
        }
        out.reached_ms = std::max(out.reached_ms, n.time_ms);
        out.max_length_ms = std::max(out.max_length_ms, replay_length_ms(n));
        const std::int64_t end = static_cast<std::int64_t>(n.time_ms) +
                                 std::max(replay_length_ms(n), kMinReplayLengthMs);
        out.duration_ms = std::max<std::int32_t>(
            out.duration_ms, static_cast<std::int32_t>(std::min<std::int64_t>(end, kMaxMs)));
        if (n.has_note()) {
            out.duration_ms = std::max(out.duration_ms, n.expected_end_ms);
        }
    }
    return out;
}

std::optional<std::int32_t> mistake_position(const ReplayTimeline& timeline,
                                             std::int32_t position_ms, int direction) {
    const std::vector<std::int32_t>& m = timeline.mistakes;
    const std::int64_t pos = position_ms;
    if (direction > 0) {
        // Each mistake ahead in turn: its lead point when that is ahead of the playhead, the
        // mistake itself when the playhead is inside the lead (or the lead would fall before the
        // start), and over a mistake the playhead stands exactly at the lead point of: it was just
        // reached, the next one is the one after.
        for (auto it = std::upper_bound(m.begin(), m.end(), pos); it != m.end(); ++it) {
            const std::int64_t lead_at = static_cast<std::int64_t>(*it) - kMistakeLeadMs;
            if (lead_at > pos) {
                return static_cast<std::int32_t>(lead_at);
            }
            if (lead_at != pos) {
                return *it;
            }
        }
        return std::nullopt;
    }
    // The last mistake whose lead point (not before the start) is behind the playhead.
    const auto end = std::lower_bound(m.begin(), m.end(), pos + kMistakeLeadMs);
    if (end == m.begin() || pos <= 0) {
        return std::nullopt;
    }
    return std::max<std::int32_t>(*(end - 1) - kMistakeLeadMs, 0);
}

std::span<const ReplayNote> replay_window(const ReplayTimeline& timeline, std::int32_t from_ms,
                                          std::int32_t to_ms_) {
    if (to_ms_ <= from_ms || timeline.notes.empty()) {
        return {};
    }
    const auto first = std::lower_bound(
        timeline.notes.begin(), timeline.notes.end(), from_ms,
        [](const ReplayNote& n, std::int32_t t) { return n.time_ms < t; });
    const auto last = std::lower_bound(
        first, timeline.notes.end(), to_ms_,
        [](const ReplayNote& n, std::int32_t t) { return n.time_ms < t; });
    return {timeline.notes.data() + (first - timeline.notes.begin()),
            static_cast<std::size_t>(last - first)};
}

// ---- The player -----------------------------------------------------------------------------

int clamp_replay_speed(int percent) {
    return std::clamp(percent, kMinReplaySpeedPercent, kMaxReplaySpeedPercent);
}

int next_replay_speed(int current, int run_speed_percent) {
    std::array<int, 7> speeds{25, 50, 75, 100, 150, 200, clamp_replay_speed(run_speed_percent)};
    std::sort(speeds.begin(), speeds.end());
    for (const int s : speeds) {
        if (s > current) {
            return s;
        }
    }
    return speeds.front();
}

void ReplayPlayer::open(const ReplayTimeline& timeline) {
    position_ = 0.0;
    playing_ = false;
    finished_ = false;
    seeked_ = true;
    speed_ = clamp_replay_speed(timeline.setup.speed_percent);
}

void ReplayPlayer::play(const ReplayTimeline& timeline) {
    if (timeline.duration_ms <= 0) {
        return;
    }
    if (finished_ || position_ >= static_cast<double>(timeline.duration_ms)) {
        position_ = 0.0;
        finished_ = false;
        seeked_ = true;
    }
    playing_ = true;
}

std::int32_t ReplayPlayer::position_ms() const {
    return static_cast<std::int32_t>(std::min(position_, static_cast<double>(kMaxMs)));
}

double ReplayPlayer::fraction(const ReplayTimeline& timeline) const {
    if (timeline.duration_ms <= 0) {
        return 0.0;
    }
    return std::clamp(position_ / static_cast<double>(timeline.duration_ms), 0.0, 1.0);
}

void ReplayPlayer::seek_ms(const ReplayTimeline& timeline, std::int32_t ms) {
    position_ = std::clamp(static_cast<double>(ms), 0.0,
                           static_cast<double>(std::max(timeline.duration_ms, 0)));
    finished_ = false;
    seeked_ = true;
}

void ReplayPlayer::seek_fraction(const ReplayTimeline& timeline, double share) {
    const double f = std::isnan(share) ? 0.0 : std::clamp(share, 0.0, 1.0);
    seek_ms(timeline, static_cast<std::int32_t>(std::lround(f * timeline.duration_ms)));
}

bool ReplayPlayer::take_seeked() { return std::exchange(seeked_, false); }

std::span<const ReplayNote> ReplayPlayer::advance(const ReplayTimeline& timeline, double dt_s) {
    if (!playing_ || !(dt_s > 0.0) || !std::isfinite(dt_s)) {
        return {};
    }
    const double end = static_cast<double>(timeline.duration_ms);
    const double from = position_;
    position_ = std::min(position_ + dt_s * 1000.0 * speed_ / 100.0, end);
    if (position_ >= end) {
        playing_ = false;
        finished_ = true;
    }
    // Whole milliseconds: a note at 1000 is passed by the step that reaches 1000 or later, once.
    const auto lo = static_cast<std::int32_t>(std::ceil(from));
    const auto hi = static_cast<std::int32_t>(std::ceil(position_));
    return replay_window(timeline, lo, hi);
}

} // namespace etude
