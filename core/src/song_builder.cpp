// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_builder.hpp"

#include "part_index.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

constexpr std::uint8_t kDrumChannel = 9;

std::int64_t bar_ticks(const TimeSignature& t, int ppq, std::int64_t bars) {
    return bars * 4 * ppq * t.numerator / t.denominator;
}

void normalize_time_signatures(Song& song) {
    const int ppq = song.tempo.ticks_per_quarter;
    auto& sigs = song.time_signatures;
    // A bar under one tick long can't be placed (the loader drops it too).
    std::erase_if(sigs, [ppq](const TimeSignature& t) {
        return t.numerator == 0 || t.denominator == 0 ||
               std::int64_t{4} * ppq * t.numerator < t.denominator;
    });
    std::stable_sort(sigs.begin(), sigs.end(), [](const TimeSignature& a, const TimeSignature& b) {
        return a.tick < b.tick;
    });
    std::vector<TimeSignature> out{TimeSignature{}};
    for (const TimeSignature& t : sigs) {
        if (t.tick == out.back().tick) {
            out.back() = t;
        } else {
            out.push_back(t);
        }
    }
    sigs = std::move(out);
}

void normalize_tempo(TempoMap& map) {
    if (map.ticks_per_quarter <= 0) {
        map.ticks_per_quarter = 480;
    }
    std::stable_sort(map.changes.begin(), map.changes.end(),
                     [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });
    std::vector<TempoChange> out(1);
    for (const TempoChange& c : map.changes) {
        if (c.us_per_quarter == 0) {
            continue;
        }
        if (c.tick <= out.back().tick) {
            out.back().us_per_quarter = c.us_per_quarter; // the later change at a tick wins
            continue;
        }
        const TempoChange& prev = out.back();
        const double s = prev.seconds + static_cast<double>(c.tick - prev.tick) *
                                            prev.us_per_quarter / 1e6 / map.ticks_per_quarter;
        out.push_back({c.tick, s, c.us_per_quarter});
    }
    map.changes = std::move(out);
}

} // namespace

bool first_bar_is_pickup(const FirstBar& bar) {
    // A lone 1/4 bar before another signature is how exporters write a one-beat pickup.
    const bool lone_quarter =
        bar.numerator == 1 && bar.denominator == 4 && bar.next_signature_differs;
    return bar.marked_pickup || bar.length < bar.nominal || lone_quarter;
}

bool build_song_measures(Song& song) {
    const int ppq = song.tempo.ticks_per_quarter;
    const auto& sigs = song.time_signatures;
    std::vector<Measure>& bars = song.measures;
    bars.clear();
    for (std::size_t i = 0; i < sigs.size(); ++i) {
        const TimeSignature& t = sigs[i];
        const bool last = i + 1 == sigs.size();
        if (t.tick >= song.end_tick && !bars.empty()) {
            break;
        }
        const std::int64_t segment_end = last ? song.end_tick : sigs[i + 1].tick;
        for (std::int64_t k = 0;; ++k) {
            const std::int64_t start = t.tick + bar_ticks(t, ppq, k);
            if (start >= segment_end && !(bars.empty() && k == 0)) {
                break;
            }
            if (bars.size() >= kMaxSongMeasures) {
                return false;
            }
            bars.push_back({0, start, song.tempo.seconds_at(start), t.numerator, t.denominator});
        }
    }
    // Only the first bar can be a pickup, numbered 0 (see first_bar_is_pickup). Here the bar is as
    // long as the next signature event leaves it. A pickup written with no signature event at its
    // end can't be told from a full bar, and stays bar 1.
    int first = 1;
    if (bars.size() >= 2) {
        const Measure& a = bars[0];
        const Measure& b = bars[1];
        FirstBar bar;
        bar.length = b.start_tick - a.start_tick;
        bar.nominal = bar_ticks({0, a.numerator, a.denominator}, ppq, 1);
        bar.numerator = a.numerator;
        bar.denominator = a.denominator;
        bar.next_signature_differs = a.numerator != b.numerator || a.denominator != b.denominator;
        if (first_bar_is_pickup(bar)) {
            first = 0;
        }
    }
    for (std::size_t i = 0; i < bars.size(); ++i) {
        bars[i].number = first + static_cast<int>(i);
    }
    return true;
}

std::uint32_t us_per_quarter_from_bpm(double bpm) {
    constexpr std::uint32_t kSlowest = 0xFFFFFF;
    if (!(bpm > 0.0)) {
        return kSlowest;
    }
    const double us = std::round(60'000'000.0 / bpm);
    if (us < 1.0) {
        return 1;
    }
    return us >= kSlowest ? kSlowest : static_cast<std::uint32_t>(us);
}

TempoMap make_tempo_map(int ticks_per_quarter, double bpm) {
    TempoMap map;
    map.ticks_per_quarter = ticks_per_quarter;
    map.changes.assign(1, TempoChange{0, 0.0, us_per_quarter_from_bpm(bpm)});
    return map;
}

bool finish_song(Song& song) {
    normalize_tempo(song.tempo);
    normalize_time_signatures(song);

    std::erase_if(song.notes, [](const Note& n) { return n.end_tick <= n.start_tick; });
    std::sort(song.notes.begin(), song.notes.end(), [](const Note& a, const Note& b) {
        if (a.start_tick != b.start_tick) {
            return a.start_tick < b.start_tick;
        }
        if (a.pitch != b.pitch) {
            return a.pitch < b.pitch;
        }
        return a.track != b.track ? a.track < b.track : a.channel < b.channel;
    });
    song.end_tick = 0;
    for (Note& n : song.notes) {
        n.drum = n.channel == kDrumChannel;
        n.start_s = song.tempo.seconds_at(n.start_tick);
        n.duration_s = song.tempo.seconds_at(n.end_tick) - n.start_s;
        song.end_tick = std::max(song.end_tick, n.end_tick);
    }
    song.duration_s = song.tempo.seconds_at(song.end_tick);

    const bool complete = build_song_measures(song);
    for (Note& n : song.notes) {
        n.measure = song.measure_at(n.start_tick);
    }

    std::stable_sort(song.events.begin(), song.events.end(),
                     [](const ChannelEvent& a, const ChannelEvent& b) { return a.tick < b.tick; });
    for (ChannelEvent& e : song.events) {
        e.time_s = song.tempo.seconds_at(e.tick);
    }

    for (Part& p : song.parts) {
        p.note_count = 0;
    }
    PartIndex part_at(song.parts);
    for (const Note& n : song.notes) {
        std::size_t at = part_at.find(n.track, n.channel);
        if (at == PartIndex::kNone) {
            Part p;
            p.track = n.track;
            p.channel = n.channel;
            p.drum = n.drum;
            p.hand = n.hand;
            song.parts.push_back(std::move(p));
            at = song.parts.size() - 1;
            part_at.add(n.track, n.channel, at);
        }
        Part& part = song.parts[at];
        part.drum = part.drum || n.drum;
        ++part.note_count;
    }
    // Like the loader: the first program change of a part names its instrument.
    for (const ChannelEvent& e : song.events) {
        if (e.kind != ChannelEventKind::Program) {
            continue;
        }
        const std::size_t at = part_at.find(e.track, e.channel);
        if (at != PartIndex::kNone && !song.parts[at].has_program) {
            song.parts[at].program = static_cast<std::uint8_t>(e.value);
            song.parts[at].has_program = true;
        }
    }
    std::sort(song.parts.begin(), song.parts.end(), [](const Part& a, const Part& b) {
        return a.track != b.track ? a.track < b.track : a.channel < b.channel;
    });
    return complete;
}

} // namespace etude
