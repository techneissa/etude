// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay_demo.hpp"

#include "etude/run_events.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace etude::app {

namespace {

// How the invented run plays note `i`: a grade and the error of the press in milliseconds.
struct Play {
    NoteGrade grade = NoteGrade::Perfect;
    int error_ms = 0;
};

Play play_of(std::size_t i) {
    if (i % 17 == 5) {
        return {NoteGrade::Missed, 0};
    }
    if (i % 11 == 3) {
        return {NoteGrade::Early, -72};
    }
    if (i % 9 == 4) {
        return {NoteGrade::Late, 76};
    }
    if (i % 7 == 2) {
        return {NoteGrade::Good, (i % 2 == 0 ? 38 : -34)};
    }
    return {NoteGrade::Perfect, static_cast<int>(i % 5) * 4 - 8};
}

void count(RunRecord& r, const Note& n, NoteGrade grade, std::map<int, RunBar>& bars) {
    RunHand& hand = n.hand == Hand::Left ? r.left : n.hand == Hand::Right ? r.right : r.unknown;
    RunBar& bar = bars[n.measure];
    bar.measure = n.measure;
    ++hand.graded;
    ++r.all.graded;
    ++bar.graded;
    if (grade == NoteGrade::Missed) {
        ++r.counts.missed;
        ++bar.missed;
        return;
    }
    ++hand.hit;
    ++r.all.hit;
    ++(grade == NoteGrade::Perfect ? r.counts.perfect
       : grade == NoteGrade::Good  ? r.counts.good
       : grade == NoteGrade::Early ? r.counts.early
                                   : r.counts.late);
}

} // namespace

DemoRun make_demo_run(const Song& song, const Transposition& transposition,
                      std::uint64_t song_hash, std::size_t max_notes) {
    DemoRun out;
    RunRecord& r = out.record;
    r.song_hash = song_hash;
    r.song_title = "Demo run";
    r.mode = RunMode::Flow;
    r.hands = HandFilter::Both;
    std::map<int, RunBar> bars;
    RunEvents events;
    std::size_t played = 0;
    for (std::size_t index = 0; index < song.notes.size() && played < max_notes; ++index) {
        const Note& n = song.notes[index];
        if (n.drum || n.accompaniment) {
            continue;
        }
        const Play p = play_of(played);
        const int key = transposition.expected_pitch(n).pitch;
        RunEvent e;
        e.note_index = static_cast<std::int32_t>(index);
        e.key = static_cast<std::uint8_t>(std::clamp(key, 0, 127));
        e.hand = n.hand;
        e.grade = p.grade;
        const auto start_ms = static_cast<std::int32_t>(std::lround(n.start_s * 1000.0));
        if (p.grade == NoteGrade::Missed) {
            e.kind = RunEventKind::Missed;
            e.song_ms = start_ms;
        } else {
            e.kind = RunEventKind::Press;
            e.song_ms = start_ms + p.error_ms;
            e.error_ms = p.error_ms;
            e.velocity = static_cast<std::uint8_t>(70 + played % 40);
        }
        events.events.push_back(e);
        count(r, n, p.grade, bars);
        if (played % 23 == 8) {
            // A wrong key a tone off the note, a little after it.
            RunEvent w;
            w.kind = RunEventKind::Wrong;
            w.key = static_cast<std::uint8_t>(std::clamp(key + 2, 0, 127));
            w.velocity = 66;
            w.song_ms = start_ms + 130;
            events.events.push_back(w);
            ++r.wrong;
            ++bars[n.measure].wrong;
        }
        ++played;
    }
    for (const auto& [measure, bar] : bars) {
        r.measures.push_back(bar);
    }
    r.duration_ms = static_cast<std::int64_t>(song.duration_s * 1000.0);
    r.longest_combo = 24;
    events.expected_notes = static_cast<std::int32_t>(played);
    events.unordered = true;
    finish_ordering(events);
    out.replay = std::make_shared<const RunReplay>(
        build_run_replay(r, events, ReplaySong{song.notes, song.measures}));
    return out;
}

} // namespace etude::app
