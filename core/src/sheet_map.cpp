// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_map.hpp"

#include "etude/evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace etude {
namespace {

constexpr double kOffsetUnits = 960.0; // per quarter note, for matching offsets

long offset_key(double offset_q) { return std::lround(offset_q * kOffsetUnits); }

bool practised(const Note& n) { return !n.drum && !n.accompaniment; }

// Compressed rows from (row, value) pairs; `rows` rows. Values keep the pairs' order in each row.
void compress(std::size_t rows, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& pairs,
              std::vector<std::uint32_t>& first, std::vector<std::uint32_t>& values) {
    first.assign(rows + 1, 0);
    for (const auto& [row, value] : pairs) {
        (void)value;
        ++first[row + 1];
    }
    for (std::size_t r = 0; r < rows; ++r) {
        first[r + 1] += first[r];
    }
    values.assign(pairs.size(), 0);
    std::vector<std::uint32_t> at(first.begin(), first.end() - 1);
    for (const auto& [row, value] : pairs) {
        values[at[row]++] = value;
    }
}

} // namespace

SheetMap build_sheet_map(const SheetLayout& layout, const Song& song) {
    SheetMap map;
    const std::size_t note_count = song.notes.size();
    const std::size_t mark_count = layout.marks.size();
    if (song.measures.empty()) {
        compress(note_count, {}, map.note_first, map.note_marks);
        compress(mark_count, {}, map.mark_first, map.mark_notes);
        return map;
    }
    const int first_number = song.measures.front().number;
    const double tpq = static_cast<double>(std::max(1, song.tempo.ticks_per_quarter));
    const std::size_t bar_count = song.measures.size();

    // Played bars: start ticks, written index, reading runs.
    map.bar_start.resize(bar_count);
    map.bar_run.resize(bar_count);
    std::vector<int> written(bar_count);
    int run = 0;
    for (std::size_t k = 0; k < bar_count; ++k) {
        map.bar_start[k] = song.measures[k].start_tick;
        written[k] = song.measures[k].written_number() - first_number;
        if (k > 0 && written[k] <= written[k - 1]) {
            ++run;
        }
        map.bar_run[k] = run;
    }
    map.bar_written = written;
    const auto bar_end = [&](std::size_t k) {
        return k + 1 < bar_count ? song.measures[k + 1].start_tick
                                 : std::max(song.end_tick, song.measures[k].start_tick + 1);
    };
    const auto played_bar_of = [&](const Note& n) {
        const long long k = static_cast<long long>(n.measure) - first_number;
        return static_cast<std::size_t>(std::clamp<long long>(k, 0, static_cast<long long>(bar_count) - 1));
    };

    // Note marks by what is written: bar, pitch, offset.
    std::multimap<std::tuple<int, int, long>, std::uint32_t> by_key;
    for (std::uint32_t m = 0; m < mark_count; ++m) {
        const SheetMark& mk = layout.marks[m];
        if (mk.kind == SheetMarkKind::Note) {
            by_key.emplace(std::make_tuple(mk.bar, mk.pitch, offset_key(mk.offset_q)), m);
        }
    }
    // A note may sit a tick from its mark (a MIDI song passes the grid check one tick off, and the
    // writer puts the mark on the grid): the slack is one tick, at least one unit.
    const long slack = std::max(1L, static_cast<long>(std::ceil(kOffsetUnits / tpq)));
    std::vector<std::pair<std::uint32_t, std::uint32_t>> note_mark; // (note, mark)
    std::vector<bool> mark_used(mark_count, false);
    for (std::uint32_t i = 0; i < note_count; ++i) {
        const Note& n = song.notes[i];
        if (!practised(n)) {
            continue;
        }
        ++map.practised;
        const std::size_t k = played_bar_of(n);
        const int w = n.written_measure() - first_number;
        const double offset_q = static_cast<double>(n.start_tick - song.measures[k].start_tick) / tpq;
        const long key = offset_key(offset_q);
        // The marks at the nearest offset within the slack (the earlier one on a tie).
        const int pitch = static_cast<int>(n.pitch);
        const auto lo = by_key.lower_bound(std::make_tuple(w, pitch, key - slack));
        const auto hi = by_key.upper_bound(std::make_tuple(w, pitch, key + slack));
        long best = slack + 1;
        for (auto it = lo; it != hi; ++it) {
            best = std::min(best, std::abs(std::get<2>(it->first) - key));
        }
        if (best > slack) {
            continue;
        }
        const long at = by_key.count(std::make_tuple(w, pitch, key - best)) > 0 ? key - best : key + best;
        const auto range = by_key.equal_range(std::make_tuple(w, pitch, at));
        for (auto it = range.first; it != range.second; ++it) {
            note_mark.emplace_back(i, it->second);
            mark_used[it->second] = true;
        }
        ++map.matched;
    }

    // Tied heads: a tie's end left over shows the matched note of its pitch that still sounds
    // where it sits, in every played bar that reads its written bar. Any other mark left over (a
    // grace note the song lacks) shows nothing.
    std::vector<std::vector<std::size_t>> plays_of(1);
    {
        int max_written = 0;
        for (const int w : written) {
            max_written = std::max(max_written, w);
        }
        plays_of.assign(static_cast<std::size_t>(std::max(0, max_written)) + 1, {});
        for (std::size_t k = 0; k < bar_count; ++k) {
            if (written[k] >= 0) {
                plays_of[static_cast<std::size_t>(written[k])].push_back(k);
            }
        }
    }
    std::vector<bool> note_matched(note_count, false);
    for (const auto& [note, mark] : note_mark) {
        (void)mark;
        note_matched[note] = true;
    }
    // Matched notes by pitch, sorted by start, to find the one sounding at a tick.
    std::vector<std::vector<std::uint32_t>> by_pitch(128);
    for (std::uint32_t i = 0; i < note_count; ++i) {
        if (note_matched[i]) {
            by_pitch[song.notes[i].pitch & 127].push_back(i);
        }
    }
    for (auto& list : by_pitch) {
        std::sort(list.begin(), list.end(), [&](std::uint32_t a, std::uint32_t b) {
            return song.notes[a].start_tick < song.notes[b].start_tick;
        });
    }
    for (std::uint32_t m = 0; m < mark_count; ++m) {
        const SheetMark& mk = layout.marks[m];
        if (mk.kind != SheetMarkKind::Note || !mk.tied || mark_used[m] || mk.pitch < 0 || mk.pitch > 127 ||
            mk.bar < 0 || static_cast<std::size_t>(mk.bar) >= plays_of.size()) {
            continue;
        }
        const auto& list = by_pitch[static_cast<std::size_t>(mk.pitch)];
        for (const std::size_t k : plays_of[static_cast<std::size_t>(mk.bar)]) {
            const double at = static_cast<double>(song.measures[k].start_tick) + mk.offset_q * tpq;
            // The latest matched note of this pitch starting before `at`.
            auto it = std::upper_bound(list.begin(), list.end(), at, [&](double t, std::uint32_t i) {
                return t <= static_cast<double>(song.notes[i].start_tick);
            });
            if (it == list.begin()) {
                continue;
            }
            const Note& n = song.notes[*(it - 1)];
            if (static_cast<double>(n.end_tick) > at + 0.5) {
                note_mark.emplace_back(*(it - 1), m);
            }
        }
    }

    // Mark -> notes in the order they play.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> mark_note;
    mark_note.reserve(note_mark.size());
    for (const auto& [note, mark] : note_mark) {
        mark_note.emplace_back(mark, note);
    }
    std::stable_sort(mark_note.begin(), mark_note.end(), [&](const auto& a, const auto& b) {
        if (a.first != b.first) {
            return a.first < b.first;
        }
        if (song.notes[a.second].start_tick != song.notes[b.second].start_tick) {
            return song.notes[a.second].start_tick < song.notes[b.second].start_tick;
        }
        return a.second < b.second;
    });
    std::sort(note_mark.begin(), note_mark.end());
    note_mark.erase(std::unique(note_mark.begin(), note_mark.end()), note_mark.end());
    mark_note.erase(std::unique(mark_note.begin(), mark_note.end()), mark_note.end());
    compress(note_count, note_mark, map.note_first, map.note_marks);
    compress(mark_count, mark_note, map.mark_first, map.mark_notes);

    // Chords.
    int chords = 0;
    for (const SheetMark& mk : layout.marks) {
        chords = std::max(chords, mk.chord + 1);
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> chord_mark;
    for (std::uint32_t m = 0; m < mark_count; ++m) {
        const SheetMark& mk = layout.marks[m];
        if (mk.kind == SheetMarkKind::Note && mk.chord >= 0) {
            chord_mark.emplace_back(static_cast<std::uint32_t>(mk.chord), m);
        }
    }
    compress(static_cast<std::size_t>(chords), chord_mark, map.chord_first, map.chord_marks);

    // Onset columns of each written bar: offset -> (system, head xs).
    struct Column {
        long key = 0;
        double offset_q = 0.0;
        int system = 0;
        std::vector<float> xs;
    };
    std::vector<std::vector<Column>> columns(plays_of.size());
    for (const SheetMark& mk : layout.marks) {
        if (mk.kind == SheetMarkKind::Stem || mk.bar < 0 ||
            static_cast<std::size_t>(mk.bar) >= columns.size()) {
            continue;
        }
        auto& cols = columns[static_cast<std::size_t>(mk.bar)];
        const long key = offset_key(mk.offset_q);
        auto it = std::find_if(cols.begin(), cols.end(), [&](const Column& c) { return c.key == key; });
        if (it == cols.end()) {
            cols.push_back({key, mk.offset_q, mk.system, {}});
            it = cols.end() - 1;
        }
        if (mk.system == it->system) {
            it->xs.push_back(mk.head_x);
        }
    }
    for (auto& cols : columns) {
        std::sort(cols.begin(), cols.end(), [](const Column& a, const Column& b) { return a.key < b.key; });
        for (Column& c : cols) {
            std::sort(c.xs.begin(), c.xs.end());
        }
    }
    std::vector<const SheetBar*> box_of(plays_of.size(), nullptr);
    for (const SheetBar& b : layout.bars) {
        if (b.bar >= 0 && static_cast<std::size_t>(b.bar) < box_of.size() &&
            box_of[static_cast<std::size_t>(b.bar)] == nullptr) {
            box_of[static_cast<std::size_t>(b.bar)] = &b;
        }
    }
    for (std::size_t k = 0; k < bar_count; ++k) {
        const int w = written[k];
        if (w < 0 || static_cast<std::size_t>(w) >= columns.size()) {
            continue;
        }
        const double start = static_cast<double>(song.measures[k].start_tick);
        const double end = static_cast<double>(bar_end(k));
        const auto& cols = columns[static_cast<std::size_t>(w)];
        const SheetBar* box = box_of[static_cast<std::size_t>(w)];
        if (box != nullptr && (cols.empty() || cols.front().key > 0)) {
            map.path.push_back({start, box->system, box->x0});
        }
        for (const Column& c : cols) {
            const double t = start + c.offset_q * tpq;
            if (c.xs.empty() || t >= end) {
                continue;
            }
            map.path.push_back({t, c.system, c.xs[c.xs.size() / 2]});
        }
        if (box != nullptr) {
            map.path.push_back({end, box->system, box->x1});
        } else if (!cols.empty() && !cols.back().xs.empty()) {
            map.path.push_back({end, cols.back().system, cols.back().xs.back()});
        }
    }
    return map;
}

SheetCursor sheet_cursor_at(const SheetMap& map, double tick) {
    SheetCursor c;
    if (map.path.empty()) {
        return c;
    }
    // The last point at or before `tick`.
    const auto it = std::upper_bound(map.path.begin(), map.path.end(), tick,
                                     [](double t, const SheetCursorPoint& p) { return t < p.tick; });
    if (it == map.path.begin()) {
        c.system = map.path.front().system;
        c.x = map.path.front().x;
        return c;
    }
    const SheetCursorPoint& a = *(it - 1);
    c.system = a.system;
    c.x = a.x;
    if (it == map.path.end()) {
        return c;
    }
    const SheetCursorPoint& b = *it;
    if (b.system == a.system && b.x >= a.x && b.tick > a.tick) {
        const double f = std::clamp((tick - a.tick) / (b.tick - a.tick), 0.0, 1.0);
        c.x = static_cast<float>(static_cast<double>(a.x) + f * static_cast<double>(b.x - a.x));
    }
    return c;
}

int sheet_mark_note(const SheetMap& map, const Song& song, std::uint32_t mark, double tick) {
    if (static_cast<std::size_t>(mark) + 1 >= map.mark_first.size()) {
        return -1;
    }
    const std::uint32_t* first = map.mark_notes.data() + map.mark_first[mark];
    const std::uint32_t* last = map.mark_notes.data() + map.mark_first[mark + 1];
    if (first == last) {
        return -1;
    }
    // The first note starting after `tick`.
    const std::uint32_t* next = std::upper_bound(first, last, tick, [&](double t, std::uint32_t i) {
        return t < static_cast<double>(song.notes[i].start_tick);
    });
    if (next != last && !map.bar_start.empty()) {
        const auto run_at = [&](double t) {
            const auto b = std::upper_bound(map.bar_start.begin(), map.bar_start.end(), t,
                                            [](double v, std::int64_t s) { return v < static_cast<double>(s); });
            const std::size_t k = b == map.bar_start.begin()
                                      ? 0
                                      : static_cast<std::size_t>(b - map.bar_start.begin()) - 1;
            return map.bar_run[k];
        };
        if (run_at(static_cast<double>(song.notes[*next].start_tick)) == run_at(tick)) {
            return -1;
        }
    }
    if (next == first) {
        return -1;
    }
    return static_cast<int>(*(next - 1));
}

SheetMarkState sheet_state_for_grade(int grade) {
    if (grade < 0) {
        return SheetMarkState::Ahead;
    }
    return static_cast<NoteGrade>(grade) == NoteGrade::Missed ? SheetMarkState::Missed
                                                               : SheetMarkState::Played;
}

SheetMarkState sheet_stem_state(int notes, int ahead, int missed) {
    if (notes <= 0 || ahead > 0) {
        return SheetMarkState::Ahead;
    }
    return missed >= notes ? SheetMarkState::Missed : SheetMarkState::Played;
}

} // namespace etude
