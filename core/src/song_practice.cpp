// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_practice.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

SongPractice sanitized(const SongPractice& practice) {
    SongPractice out = practice;
    sanitize(out);
    return out;
}

void sanitize(SongPractice& out) {
    out.speed_percent = std::clamp(out.speed_percent, kMinSpeedPercent, kMaxSpeedPercent);
    out.count_in_bars = std::clamp(out.count_in_bars, 0, kMaxCountInBars);
    if (out.hands != HandFilter::Left && out.hands != HandFilter::Right) {
        out.hands = HandFilter::Both;
    }
    out.transpose_semitones =
        std::clamp(out.transpose_semitones, -kMaxTransposeSemitones, kMaxTransposeSemitones);
    out.split_point = sanitized_split_point(out.split_point);
    if (!out.parts_set) {
        out.muted_parts.clear();
    }
    std::sort(out.muted_parts.begin(), out.muted_parts.end());
    out.muted_parts.erase(std::unique(out.muted_parts.begin(), out.muted_parts.end()),
                          out.muted_parts.end());
    if (out.muted_parts.size() > kMaxSongPracticeParts) {
        out.muted_parts.resize(kMaxSongPracticeParts);
    }
    const auto time = [](double t) {
        return std::isfinite(t) ? std::clamp(t, 0.0, kMaxSessionSeconds) : 0.0;
    };
    // Whole milliseconds, as the history keeps them.
    out.loop_start_s = std::round(time(out.loop_start_s) * 1000.0) / 1000.0;
    out.loop_end_s = std::round(time(out.loop_end_s) * 1000.0) / 1000.0;
    if (!out.has_loop()) {
        out.loop_start_s = out.loop_end_s = 0.0;
        out.loop_enabled = false;
    }
    out.section_id = std::clamp(out.section_id, 0, kMaxSessionSectionId);
    out.plan_item = std::clamp(out.plan_item, 0, kMaxSessionPlanItems);
}

PracticeChanges note_changes(PracticeChanges c, const SongPractice& at_open,
                             const SongPractice& now) {
    if (now.speed_percent != at_open.speed_percent) {
        c.speed = true;
        c.value.speed_percent = now.speed_percent;
    }
    if (now.count_in_bars != at_open.count_in_bars) {
        c.count_in = true;
        c.value.count_in_bars = now.count_in_bars;
    }
    if (now.hands != at_open.hands) {
        c.hands = true;
        c.value.hands = now.hands;
    }
    if (now.transpose_semitones != at_open.transpose_semitones) {
        c.transpose = true;
        c.value.transpose_semitones = now.transpose_semitones;
    }
    if (now.split_point != at_open.split_point) {
        c.split = true;
        c.value.split_point = now.split_point;
    }
    if (now.parts_set != at_open.parts_set || now.muted_parts != at_open.muted_parts) {
        c.parts = true;
        c.value.parts_set = now.parts_set;
        c.value.muted_parts = now.muted_parts;
    }
    if (now.loop_start_s != at_open.loop_start_s || now.loop_end_s != at_open.loop_end_s ||
        now.loop_enabled != at_open.loop_enabled || now.section_id != at_open.section_id ||
        now.plan_item != at_open.plan_item) {
        c.loop = true;
        c.value.loop_start_s = now.loop_start_s;
        c.value.loop_end_s = now.loop_end_s;
        c.value.loop_enabled = now.loop_enabled;
        c.value.section_id = now.section_id;
        c.value.plan_item = now.plan_item;
    }
    if (now.no_shift_offer != at_open.no_shift_offer) {
        c.shift_offer = true;
        c.value.no_shift_offer = now.no_shift_offer;
    }
    return c;
}

SongPractice apply_changes(const SongPractice& base, const PracticeChanges& c) {
    SongPractice out = base;
    if (c.speed) {
        out.speed_percent = c.value.speed_percent;
    }
    if (c.count_in) {
        out.count_in_bars = c.value.count_in_bars;
    }
    if (c.hands) {
        out.hands = c.value.hands;
    }
    if (c.transpose) {
        out.transpose_semitones = c.value.transpose_semitones;
    }
    if (c.split) {
        out.split_point = c.value.split_point;
    }
    if (c.parts) {
        out.parts_set = c.value.parts_set;
        out.muted_parts = c.value.muted_parts;
    }
    if (c.loop) {
        out.loop_start_s = c.value.loop_start_s;
        out.loop_end_s = c.value.loop_end_s;
        out.loop_enabled = c.value.loop_enabled;
        out.section_id = c.value.section_id;
        out.plan_item = c.value.plan_item;
    }
    if (c.shift_offer) {
        out.no_shift_offer = c.value.no_shift_offer;
    }
    return out;
}

SongPractice with_player_changes(const SongPractice& stored, const SongPractice& at_open,
                                 const SongPractice& now) {
    return apply_changes(stored, note_changes({}, at_open, now));
}

} // namespace etude
