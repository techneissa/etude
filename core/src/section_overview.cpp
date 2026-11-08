// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/section_overview.hpp"

#include "etude/transport.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace etude {

namespace {

constexpr const char* kDot = " \xC2\xB7 ";   // a middle dot between parts
constexpr const char* kEnDash = "\xE2\x80\x93";

std::string bars_text(const Section& s) {
    if (s.first_measure == s.last_measure) {
        return "Bar " + std::to_string(s.first_measure);
    }
    return "Bars " + std::to_string(s.first_measure) + kEnDash + std::to_string(s.last_measure);
}

} // namespace

double OverviewTrack::time_at(float x) const {
    const float w = x1 - x0;
    if (!(w > 0.0f) || !(duration_s > 0.0)) {
        return 0.0;
    }
    const double t = static_cast<double>(std::clamp((x - x0) / w, 0.0f, 1.0f));
    return t * duration_s;
}

float OverviewTrack::x_at(double seconds) const {
    if (!(duration_s > 0.0)) {
        return x0;
    }
    const double t = std::clamp(seconds / duration_s, 0.0, 1.0);
    return x0 + static_cast<float>(t) * (x1 - x0);
}

OverviewHit overview_hit(const SongSections& sections, const OverviewTrack& track, float x,
                         float marker_half_w) {
    OverviewHit hit;
    if (sections.sections.empty() || !(track.duration_s > 0.0) || !(track.x1 > track.x0)) {
        return hit;
    }
    float best = marker_half_w;
    for (std::size_t i = 0; i + 1 < sections.sections.size(); ++i) {
        const float d = std::fabs(track.x_at(sections.sections[i].end_s) - x);
        if (d <= best) {
            best = d;
            hit.kind = OverviewHit::Kind::Marker;
            hit.marker = i;
        }
    }
    if (hit.kind == OverviewHit::Kind::Marker) {
        return hit;
    }
    if (const Section* s = sections.at(track.time_at(x))) {
        hit.kind = OverviewHit::Kind::Segment;
        hit.section = s->id;
    }
    return hit;
}

double marker_drag_time(const Song& song, const SongSections& sections, std::size_t marker,
                        double raw_s, bool snap_to_bars) {
    if (marker + 1 >= sections.sections.size()) {
        return raw_s;
    }
    return marker_drag_time(song, sections.sections[marker].start_s,
                            sections.sections[marker + 1].end_s, raw_s, snap_to_bars);
}

double marker_drag_time(const Song& song, double left_start_s, double right_end_s, double raw_s,
                        bool snap_to_bars) {
    if (std::isnan(raw_s)) {
        return raw_s;
    }
    // A hair over the minimum, so float rounding cannot leave a section a hair short.
    constexpr double kMargin = 1e-6;
    const double lo = left_start_s + kMinSectionSeconds + kMargin;
    const double hi = right_end_s - kMinSectionSeconds - kMargin;
    if (lo > hi) {
        return raw_s;
    }
    const double held = std::clamp(raw_s, lo, hi);
    if (!snap_to_bars || song.measures.empty()) {
        return held;
    }
    double snapped = snap_to_measure(song, held, SnapMode::Nearest);
    if (snapped < lo) {
        snapped = snap_to_measure(song, lo, SnapMode::Ceil);
    } else if (snapped > hi) {
        snapped = snap_to_measure(song, hi, SnapMode::Floor);
    }
    return snapped >= lo && snapped <= hi ? snapped : held;
}

std::string clock_text(double seconds) {
    const int total = static_cast<int>(std::max(0.0, std::isnan(seconds) ? 0.0 : seconds));
    char buf[24];
    std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
    return buf;
}

std::string section_label(const Section& section) {
    if (section.custom_name || !section.snapped_to_bars || section.first_measure == 0) {
        return section.name;
    }
    return bars_text(section);
}

std::string section_detail_text(const Section& section) {
    std::string out;
    if (section.custom_name && section.snapped_to_bars && section.first_measure != 0) {
        out = bars_text(section) + kDot;
    }
    return out + clock_text(section.start_s) + kEnDash + clock_text(section.end_s);
}

std::string plan_status_text(const PlanStatus& status, const Section* section) {
    if (!status.active) {
        return {};
    }
    std::string out = "Section " + std::to_string(status.item_index + 1) + "/" +
                      std::to_string(status.item_count);
    if (section != nullptr) {
        out += kDot + section_label(*section);
    }
    const int pass = status.passes_done + 1;
    out += std::string(kDot) + "pass ";
    if (status.repeats > 0) {
        out += std::to_string(std::min(pass, status.repeats)) + "/" +
               std::to_string(status.repeats);
    } else {
        out += std::to_string(pass);
    }
    return out;
}

std::string plan_item_text(const PlanItem& item) {
    std::string out = item.repeats == 0 ? "\xC3\x97\xE2\x88\x9E"
                                        : "\xC3\x97" + std::to_string(item.repeats);
    if (item.clean_passes_only) {
        out += std::string(kDot) + "clean only";
    }
    if (item.speed_up.enabled) {
        out += std::string(kDot) + std::to_string(item.speed_up.start_percent) + kEnDash +
               format_unit_string(item.speed_up.max_percent, Unit::Percent) + " +" +
               std::to_string(item.speed_up.step_percent);
    }
    return out;
}

} // namespace etude
