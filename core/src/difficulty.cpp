// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/difficulty.hpp"

#include "etude/pitch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace etude {

namespace {

using namespace difficulty_tok;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

// The jump that counts as a big leap: a seventh.
constexpr int kBigLeapSemitones = 10;
// A leap after a rest longer than this is not counted: the hand has time to move.
constexpr double kLeapMaxGapS = 1.0;
// The run of onsets the reach is measured over, and the longest it may take.
constexpr std::size_t kReachOnsets = 5;
constexpr double kReachMaxS = 2.5;
// Where the colour part is full: this share of black keys, this share outside the key.
constexpr double kFullBlackShare = 0.5;
constexpr double kFullAccidentalShare = 0.3;
// The tempo part counts fully when the beat is divided in this many onsets (sixteenth notes):
// none for notes of a beat or longer.
constexpr double kFullOnsetsPerBeat = 4.0;
// The independence part counts fully when the sparser hand plays this many onsets in the bar.
constexpr double kFullIndependenceOnsets = 4.0;

struct Onset {
    double t = 0.0;
    int low = 127;
    int high = 0;
    int count = 0;
    std::size_t bar = 0;
};

struct BarSpan {
    std::int64_t start_tick = 0;
    double start_s = 0.0;
    double length_s = 0.0;
};

std::vector<BarSpan> bar_spans(const Song& song) {
    std::vector<BarSpan> out;
    if (song.measures.empty()) {
        out.push_back({0, 0.0, std::max(song.duration_s, kMinBarSeconds)});
        return out;
    }
    out.reserve(song.measures.size());
    for (std::size_t i = 0; i < song.measures.size(); ++i) {
        const Measure& m = song.measures[i];
        const double end = i + 1 < song.measures.size() ? song.measures[i + 1].start_s
                                                        : std::max(song.duration_s, m.start_s);
        out.push_back({m.start_tick, m.start_s, end - m.start_s});
    }
    return out;
}

std::size_t bar_of(const std::vector<BarSpan>& bars, std::int64_t tick) {
    const auto it =
        std::upper_bound(bars.begin(), bars.end(), tick,
                         [](std::int64_t t, const BarSpan& b) { return t < b.start_tick; });
    return it == bars.begin() ? 0 : static_cast<std::size_t>(it - bars.begin()) - 1;
}

// The tempo in force at `tick`: the last change at or before it (the changes are sorted by tick).
double bpm_at(const Song& song, std::int64_t tick) {
    const std::vector<TempoChange>& changes = song.tempo.changes;
    std::uint32_t us = 500'000u;
    if (!changes.empty()) {
        const auto it =
            std::upper_bound(changes.begin(), changes.end(), tick,
                             [](std::int64_t t, const TempoChange& c) { return t < c.tick; });
        us = (it == changes.begin() ? changes.front() : *(it - 1)).us_per_quarter;
    }
    return us == 0 ? 0.0 : 60'000'000.0 / static_cast<double>(us);
}

// The pitch classes of the key's major scale; C major with no key.
std::array<bool, 12> scale_of(const KeyChange* key) {
    std::array<bool, 12> in{};
    const int fifths = key != nullptr ? key->fifths : 0;
    const int tonic = ((fifths * 7) % 12 + 12) % 12;
    for (const int step : {0, 2, 4, 5, 7, 9, 11}) {
        in[static_cast<std::size_t>((tonic + step) % 12)] = true;
    }
    return in;
}

} // namespace

BarParts bar_parts(const BarFeatures& f) {
    BarParts p;
    if (f.notes <= 0) {
        return p;
    }
    p.speed = clamp01(f.onsets_per_second / kFullOnsetsPerSecond);
    p.chords =
        0.5 * clamp01((f.mean_chord_notes - 1.0) / 2.5) +
        0.5 * clamp01((static_cast<double>(f.max_chord_notes) - 1.0) / (kFullChordNotes - 1.0));
    p.span = clamp01((f.max_span - kFreeSpan) / (kFullSpan - kFreeSpan));
    p.reach = clamp01((f.max_reach - kFreeReach) / (kFullReach - kFreeReach));
    p.leaps = 0.65 * clamp01((f.max_leap - kFreeLeap) / (kFullLeap - kFreeLeap)) +
              0.35 * clamp01(static_cast<double>(f.big_leaps) / 4.0);
    p.independence = clamp01(f.independence);
    p.colour = 0.5 * clamp01(f.black_share / kFullBlackShare) +
               0.5 * clamp01(f.accidental_share / kFullAccidentalShare);
    const double per_beat = f.bpm > 0.0 ? f.onsets_per_second * 60.0 / f.bpm : 0.0;
    p.tempo = clamp01((f.bpm - kFreeBpm) / (kFullBpm - kFreeBpm)) *
              clamp01((per_beat - 1.0) / (kFullOnsetsPerBeat - 1.0));
    return p;
}

double bar_sum(const BarParts& p) {
    return kSpeed * p.speed + kChords * p.chords + kSpan * p.span + kReach * p.reach +
           kLeaps * p.leaps + kIndependence * p.independence + kColour * p.colour +
           kTempo * p.tempo;
}

float stars_from_sum(double sum) {
    const double s = std::pow(clamp01(sum / kFullSum), kCurve);
    return static_cast<float>(kMinStars + (kMaxStars - kMinStars) * s);
}

float bar_stars(const BarFeatures& f) {
    return f.notes <= 0 ? kMinStars : stars_from_sum(bar_sum(bar_parts(f)));
}

float song_stars(std::span<const float> bars) {
    if (bars.empty()) {
        return kMinStars;
    }
    std::vector<float> stretches;
    const std::size_t width = std::min<std::size_t>(kStretchBars, bars.size());
    stretches.reserve(bars.size() - width + 1);
    for (std::size_t i = 0; i + width <= bars.size(); ++i) {
        float sum = 0.0f;
        for (std::size_t j = 0; j < width; ++j) {
            sum += bars[i + j];
        }
        stretches.push_back(sum / static_cast<float>(width));
    }
    std::sort(stretches.begin(), stretches.end(), std::greater<float>());
    const auto take = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::ceil(kHardestShare * static_cast<double>(stretches.size()))));
    float sum = 0.0f;
    for (std::size_t i = 0; i < take; ++i) {
        sum += stretches[i];
    }
    return std::clamp(sum / static_cast<float>(take), kMinStars, kMaxStars);
}

int half_stars_of(float stars) {
    return std::clamp(static_cast<int>(std::lround(2.0 * static_cast<double>(stars))),
                      kMinHalfStars, kMaxHalfStars);
}

int level_of_half_stars(int half_stars) { return std::clamp((half_stars + 1) / 2, 1, 5); }

float SongDifficulty::bar_at(int measure_number) const {
    const auto it = std::lower_bound(numbers.begin(), numbers.end(), measure_number);
    if (it == numbers.end() || *it != measure_number) {
        return 0.0f;
    }
    return bars[static_cast<std::size_t>(it - numbers.begin())];
}

SongDifficulty rate_song(const Song& song) {
    SongDifficulty out;
    const std::vector<BarSpan> spans = bar_spans(song);
    const std::size_t n = spans.size();
    out.features.assign(n, BarFeatures{});
    out.numbers.reserve(n);
    if (song.measures.empty()) {
        out.numbers.push_back(1);
    } else {
        for (const Measure& m : song.measures) {
            out.numbers.push_back(m.number);
        }
    }

    // Onsets of each hand (an unknown hand reads as the right) and of both together.
    std::array<std::vector<Onset>, 2> hands; // 0 left, 1 right
    std::vector<Onset> all;
    std::vector<int> bar_notes(n, 0);
    std::vector<int> bar_black(n, 0);
    std::vector<int> bar_outside(n, 0);
    const KeyChange* key = nullptr;
    std::array<bool, 12> scale = scale_of(nullptr);
    std::size_t scale_for = static_cast<std::size_t>(-1);
    for (const Note& note : song.notes) {
        if (note.drum || note.accompaniment) {
            continue;
        }
        const std::size_t bar = bar_of(spans, note.start_tick);
        ++bar_notes[bar];
        if (is_black_key(note.pitch)) {
            ++bar_black[bar];
        }
        const KeyChange* now = key_at_seconds(song.keys, note.start_s);
        if (now != key || scale_for == static_cast<std::size_t>(-1)) {
            key = now;
            scale = scale_of(key);
            scale_for = 0;
        }
        if (!scale[static_cast<std::size_t>(pitch_class(note.pitch))]) {
            ++bar_outside[bar];
        }
        const auto add = [&](std::vector<Onset>& list) {
            if (list.empty() || note.start_s - list.back().t > kOnsetWindowS) {
                Onset o;
                o.t = note.start_s;
                o.bar = bar;
                list.push_back(o);
            }
            Onset& o = list.back();
            o.low = std::min<int>(o.low, note.pitch);
            o.high = std::max<int>(o.high, note.pitch);
            ++o.count;
        };
        add(hands[note.hand == Hand::Left ? 0 : 1]);
        add(all);
    }

    std::vector<int> onset_count(n, 0);
    for (const Onset& o : all) {
        ++onset_count[o.bar];
    }
    std::vector<double> chord_sum(n, 0.0);
    std::vector<int> chord_n(n, 0);
    for (const std::vector<Onset>& list : hands) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const Onset& o = list[i];
            BarFeatures& f = out.features[o.bar];
            chord_sum[o.bar] += o.count;
            ++chord_n[o.bar];
            f.max_chord_notes = std::max(f.max_chord_notes, o.count);
            if (o.count >= 2) {
                f.max_span = std::max(f.max_span, static_cast<double>(o.high - o.low));
            }
            if (i > 0) {
                const Onset& before = list[i - 1];
                if (o.t - before.t <= kLeapMaxGapS) {
                    const double d =
                        std::abs((o.low + o.high) * 0.5 - (before.low + before.high) * 0.5);
                    f.max_leap = std::max(f.max_leap, d);
                    if (d >= kBigLeapSemitones) {
                        ++f.big_leaps;
                    }
                }
            }
            if (i + 1 >= kReachOnsets) {
                const Onset& first = list[i + 1 - kReachOnsets];
                if (o.t - first.t <= kReachMaxS) {
                    int low = 127;
                    int high = 0;
                    for (std::size_t j = i + 1 - kReachOnsets; j <= i; ++j) {
                        low = std::min(low, list[j].low);
                        high = std::max(high, list[j].high);
                    }
                    f.max_reach = std::max(f.max_reach, static_cast<double>(high - low));
                }
            }
        }
    }

    // Independence: the sparser hand's onsets that the other hand does not play with.
    std::vector<int> left_n(n, 0);
    std::vector<int> right_n(n, 0);
    for (const Onset& o : hands[0]) {
        ++left_n[o.bar];
    }
    for (const Onset& o : hands[1]) {
        ++right_n[o.bar];
    }
    std::vector<int> unmatched_left(n, 0);
    std::vector<int> unmatched_right(n, 0);
    const auto count_unmatched = [&](const std::vector<Onset>& from,
                                     const std::vector<Onset>& other, std::vector<int>& into) {
        std::size_t j = 0;
        for (const Onset& o : from) {
            while (j < other.size() && other[j].t < o.t - kOnsetWindowS) {
                ++j;
            }
            const bool matched = j < other.size() && other[j].t <= o.t + kOnsetWindowS;
            if (!matched) {
                ++into[o.bar];
            }
        }
    };
    count_unmatched(hands[0], hands[1], unmatched_left);
    count_unmatched(hands[1], hands[0], unmatched_right);

    out.bars.assign(n, kMinStars);
    for (std::size_t b = 0; b < n; ++b) {
        BarFeatures& f = out.features[b];
        f.notes = bar_notes[b];
        if (f.notes <= 0) {
            continue;
        }
        f.onsets_per_second =
            static_cast<double>(onset_count[b]) / std::max(spans[b].length_s, kMinBarSeconds);
        f.mean_chord_notes = chord_n[b] > 0 ? chord_sum[b] / chord_n[b] : 1.0;
        if (left_n[b] > 0 && right_n[b] > 0) {
            const bool left_sparse = left_n[b] <= right_n[b];
            const int sparse = left_sparse ? left_n[b] : right_n[b];
            const int unmatched = left_sparse ? unmatched_left[b] : unmatched_right[b];
            f.independence = static_cast<double>(unmatched) / sparse *
                             clamp01(static_cast<double>(sparse) / kFullIndependenceOnsets);
        }
        f.black_share = static_cast<double>(bar_black[b]) / f.notes;
        f.accidental_share = static_cast<double>(bar_outside[b]) / f.notes;
        f.bpm = bpm_at(song, spans[b].start_tick);
        out.bars[b] = bar_stars(f);
    }
    out.stars = song_stars(out.bars);
    out.half_stars = half_stars_of(out.stars);
    return out;
}

std::vector<int> hardest_bars(const SongDifficulty& d, int max, float min_stars) {
    std::vector<int> out;
    if (max <= 0 || d.bars.empty()) {
        return out;
    }
    const float peak = *std::max_element(d.bars.begin(), d.bars.end());
    const float cutoff = std::max(min_stars, peak - kSuggestBelowPeak);
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < d.bars.size(); ++i) {
        if (d.bars[i] >= cutoff) {
            order.push_back(i);
        }
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return d.bars[a] > d.bars[b]; });
    for (std::size_t i = 0; i < order.size() && static_cast<int>(out.size()) < max; ++i) {
        out.push_back(d.numbers[order[i]]);
    }
    return out;
}

} // namespace etude
