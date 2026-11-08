// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/staff_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

namespace etude {

namespace {

double mean_pitch(const StaffTrack& t) {
    return static_cast<double>(t.pitch_sum) / static_cast<double>(t.notes);
}

} // namespace

StaffPlan plan_staves(std::span<const StaffTrack> tracks, const StaffOptions& options) {
    StaffPlan plan;
    plan.staff.assign(tracks.size(), Staff::Treble);
    long long total = 0;
    for (const StaffTrack& t : tracks) {
        if (!t.accompaniment && t.notes > 0) {
            total += t.notes;
        }
    }
    // The tracks that decide: practised, with notes, and (for hands) not an unnamed stray.
    std::vector<bool> counts(tracks.size(), false);
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const StaffTrack& t = tracks[i];
        const bool stray = options.for_hands && t.hand == Hand::Unknown &&
                           static_cast<double>(t.notes) < kStrayTrackShare * static_cast<double>(total);
        counts[i] = !t.accompaniment && t.notes > 0 && !stray;
        plan.used += counts[i] ? 1 : 0;
    }
    std::vector<bool> low(tracks.size(), false);
    double lowest_unnamed = 0.0;
    double highest_unnamed = 0.0;
    int unnamed = 0;
    bool named = false;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (!counts[i]) {
            continue;
        }
        const StaffTrack& t = tracks[i];
        const double mean = mean_pitch(t);
        low[i] = t.hand == Hand::Left ||
                 (t.hand == Hand::Unknown && plan.used >= 2 && mean < kBassTrackMeanPitch);
        if (t.hand == Hand::Unknown) {
            lowest_unnamed = unnamed == 0 ? mean : std::min(lowest_unnamed, mean);
            highest_unnamed = unnamed == 0 ? mean : std::max(highest_unnamed, mean);
            ++unnamed;
        } else {
            named = true;
        }
    }
    // Unnamed tracks all on one side of middle C but well apart: the lower ones are the left hand.
    if (options.for_hands && !named && unnamed >= 2 &&
        highest_unnamed - lowest_unnamed >= kTrackSeparation) {
        const double middle = (lowest_unnamed + highest_unnamed) / 2.0;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (counts[i]) {
                low[i] = mean_pitch(tracks[i]) < middle;
            }
        }
    }
    bool any_low = false;
    bool any_high = false;
    long long sum = 0;
    long long count = 0;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (!counts[i]) {
            continue;
        }
        plan.staff[i] = low[i] ? Staff::Bass : Staff::Treble;
        (low[i] ? any_low : any_high) = true;
        sum += tracks[i].pitch_sum;
        count += tracks[i].notes;
    }
    plan.staves = any_low && any_high ? 2 : 1;
    plan.decides = plan.staves == 2 && plan.used >= 2;
    // A stray track's notes go to the staff of the deciding track nearest in pitch (a song can have
    // thousands of parts, so the deciding tracks are searched by their mean pitch, sorted).
    std::vector<std::pair<double, std::size_t>> by_mean;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (counts[i]) {
            by_mean.emplace_back(mean_pitch(tracks[i]), i);
        }
    }
    std::sort(by_mean.begin(), by_mean.end());
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const StaffTrack& t = tracks[i];
        if (counts[i] || t.accompaniment || t.notes <= 0) {
            continue;
        }
        if (!by_mean.empty()) {
            const double mean = mean_pitch(t);
            const auto above = std::lower_bound(by_mean.begin(), by_mean.end(),
                                                std::pair<double, std::size_t>{mean, 0});
            const auto below = above == by_mean.begin() ? above : std::prev(above);
            const auto nearest =
                above == by_mean.end() ||
                        (above != below && mean - below->first <= above->first - mean)
                    ? below
                    : above;
            plan.staff[i] = plan.staff[nearest->second];
        }
        sum += t.pitch_sum;
        count += t.notes;
    }
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].accompaniment) {
            plan.staff[i] = Staff::None;
        }
    }
    if (plan.staves == 1) {
        plan.single_bass = count > 0 && static_cast<double>(sum) / static_cast<double>(count) <
                                            kSingleBassMeanPitch;
        // A single staff's tracks are all on it: the staff the clef says.
        for (Staff& s : plan.staff) {
            if (s != Staff::None) {
                s = plan.single_bass ? Staff::Bass : Staff::Treble;
            }
        }
    }
    return plan;
}

} // namespace etude
