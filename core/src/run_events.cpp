// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/run_events.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

std::int32_t ns_to_ms(std::int64_t ns) {
    // Round half away from zero without a double, so an exact millisecond stays exact.
    const std::int64_t half = ns >= 0 ? 500'000 : -500'000;
    const std::int64_t ms = (ns + half) / 1'000'000;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(ms, -kMaxMs, kMaxMs));
}

std::uint8_t byte_in(int v, int low, int high) {
    return static_cast<std::uint8_t>(std::clamp(v, low, high));
}

// Song-time order; the rest only makes the order the same on every machine.
bool before(const RunEvent& a, const RunEvent& b) {
    if (a.song_ms != b.song_ms) {
        return a.song_ms < b.song_ms;
    }
    if (a.note_index != b.note_index) {
        return a.note_index < b.note_index;
    }
    if (a.key != b.key) {
        return a.key < b.key;
    }
    return a.kind < b.kind;
}

bool by_note(const RunEvent& a, const RunEvent& b) {
    return a.note_index != b.note_index ? a.note_index < b.note_index : before(a, b);
}

} // namespace

RunEvent make_run_event(const NoteResult& r) {
    RunEvent e;
    e.note_index = static_cast<std::int32_t>(
        std::min<std::uint32_t>(r.note_index, static_cast<std::uint32_t>(kMaxMs)));
    e.hand = r.hand;
    e.grade = r.grade;
    e.key = byte_in(r.expected_pitch, 0, 127);
    if (r.grade == NoteGrade::Missed) {
        e.kind = RunEventKind::Missed;
        e.song_ms = to_ms(r.song_s);
        return e;
    }
    e.kind = RunEventKind::Press;
    e.velocity = byte_in(r.velocity, 1, 127);
    e.song_ms = to_ms(r.press_song_s);
    e.error_ms = ns_to_ms(r.timing_error_ns);
    return e;
}

RunEvent make_run_event(const WrongPress& p) {
    RunEvent e;
    e.kind = RunEventKind::Wrong;
    e.key = byte_in(p.key, 0, 127);
    e.velocity = byte_in(p.velocity, 1, 127);
    e.song_ms = to_ms(p.song_s);
    return e;
}

RunEvent sanitized(const RunEvent& in) {
    RunEvent e = in;
    e.key = std::min<std::uint8_t>(e.key, 127);
    e.song_ms = std::max(e.song_ms, 0);
    e.error_ms = std::clamp<std::int32_t>(e.error_ms, -kMaxMs, kMaxMs);
    if (static_cast<std::uint8_t>(e.kind) > static_cast<std::uint8_t>(RunEventKind::Missed)) {
        e.kind = RunEventKind::Wrong;
    }
    if (static_cast<std::uint8_t>(e.hand) > static_cast<std::uint8_t>(Hand::Right)) {
        e.hand = Hand::Unknown;
    }
    if (static_cast<std::uint8_t>(e.grade) > static_cast<std::uint8_t>(NoteGrade::Played)) {
        e.grade = NoteGrade::Missed;
    }
    switch (e.kind) {
    case RunEventKind::Press:
        e.velocity = std::clamp<std::uint8_t>(e.velocity, 1, 127);
        e.note_index = std::max(e.note_index, 0);
        if (e.grade == NoteGrade::Missed) {
            e.grade = NoteGrade::Played;
        }
        break;
    case RunEventKind::Wrong:
        e.velocity = std::clamp<std::uint8_t>(e.velocity, 1, 127);
        e.note_index = -1;
        e.error_ms = 0;
        e.hand = Hand::Unknown;
        e.grade = NoteGrade::Missed;
        break;
    case RunEventKind::Missed:
        e.velocity = 0;
        e.note_index = std::max(e.note_index, 0);
        e.error_ms = 0;
        e.grade = NoteGrade::Missed;
        break;
    }
    return e;
}

RunEventRecorder::RunEventRecorder(std::size_t capacity) : capacity_(capacity) {
    events_.reserve(capacity_);
}

void RunEventRecorder::put(const RunEvent& event) {
    if (events_.size() >= capacity_) {
        dropped_ = dropped_ < kMaxMs ? dropped_ + 1 : dropped_;
        return;
    }
    events_.push_back(event); // within the reserved capacity: no allocation
}

void RunEventRecorder::add(const NoteResult& result) { put(make_run_event(result)); }

void RunEventRecorder::add_wrong(const WrongPress& press) { put(make_run_event(press)); }

void RunEventRecorder::add_dropped(int count) {
    if (count > 0) {
        dropped_ = count > kMaxMs - dropped_ ? kMaxMs : dropped_ + count;
    }
}

void RunEventRecorder::clear() {
    events_.clear();
    dropped_ = 0;
}

RunEvents RunEventRecorder::hand_over(std::size_t practised_notes) {
    RunEvents out;
    out.dropped = dropped_;
    out.expected_notes = static_cast<std::int32_t>(std::min<std::size_t>(practised_notes, kMaxMs));
    out.events.assign(events_.begin(), events_.end()); // the one allocation of a run, here at its end
    out.unordered = true;
    clear();
    return out;
}

RunEvents RunEventRecorder::finish(std::size_t practised_notes) {
    RunEvents out = hand_over(practised_notes);
    finish_ordering(out);
    return out;
}

void finish_ordering(RunEvents& run) {
    if (!run.unordered) {
        return;
    }
    run.unordered = false;
    // Every practised note once and nothing else: sorted by note, the notes are 0.. with no
    // repeats only if each one's neighbour differs.
    std::sort(run.events.begin(), run.events.end(), by_note);
    std::size_t noted = 0;
    bool repeated = false;
    std::int32_t last = -1;
    for (const RunEvent& e : run.events) {
        if (e.note_index < 0) {
            continue;
        }
        repeated = repeated || e.note_index == last;
        last = e.note_index;
        ++noted;
    }
    const std::size_t practised = static_cast<std::size_t>(std::max(run.expected_notes, 0));
    run.complete = !run.setup_changed && run.dropped == 0 && !repeated && practised > 0 &&
                   noted == practised;
    std::sort(run.events.begin(), run.events.end(), before);
}

} // namespace etude
