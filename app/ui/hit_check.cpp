// SPDX-License-Identifier: GPL-3.0-or-later
#include "hit_check.hpp"

#include "etude/hit_area.hpp"
#include "etude/panel_size.hpp"

#include <algorithm>
#include <cmath>

namespace etude::app::ui {
namespace {

// Float noise: a hair under the rule is not a finding.
constexpr float kSlackMm = 0.05f;

// The part two boxes share, or the strip between them where they are apart: on each axis the span
// from the one's far edge to the other's near edge, whichever way round.
RectF between(const RectF& a, const RectF& b) {
    const float x0 = std::max(a.x, b.x);
    const float x1 = std::min(a.right(), b.right());
    const float y0 = std::max(a.y, b.y);
    const float y1 = std::min(a.bottom(), b.bottom());
    return {std::min(x0, x1), std::min(y0, y1), std::fabs(x1 - x0), std::fabs(y1 - y0)};
}

bool pressable(const HitItem& i) { return !i.surface && !i.covered && !i.visible.empty(); }

} // namespace

const char* hit_fault_name(HitFault fault) {
    switch (fault) {
    case HitFault::TooSmall:
        return "small";
    case HitFault::Overlap:
        return "overlap";
    case HitFault::TooClose:
        return "gap";
    }
    return "small";
}

HitReport check_hit_areas(std::span<const HitItem> items, float pt_per_mm, float round_pt) {
    HitReport report;
    if (!(pt_per_mm > 0.0f)) {
        return report;
    }
    const float slack_mm = kSlackMm + std::max(round_pt, 0.0f) / pt_per_mm;
    for (const HitItem& i : items) {
        if (i.cut) {
            continue;
        }
        ++report.sized;
        const float mm = std::min(i.box.w, i.box.h) / pt_per_mm;
        if (mm < kMinHitMm - slack_mm) {
            report.findings.push_back({HitFault::TooSmall, i.name, "", i.box, mm});
        }
    }
    for (std::size_t a = 0; a < items.size(); ++a) {
        if (!pressable(items[a])) {
            continue;
        }
        for (std::size_t b = a + 1; b < items.size(); ++b) {
            if (!pressable(items[b])) {
                continue;
            }
            ++report.pairs;
            const float gap = hit_area_gap(items[a].visible, items[b].visible) / pt_per_mm;
            if (gap < -slack_mm) {
                report.findings.push_back({HitFault::Overlap, items[a].name, items[b].name,
                                           between(items[a].visible, items[b].visible), -gap});
            } else if (gap < kMinHitGapMm - slack_mm) {
                report.findings.push_back({HitFault::TooClose, items[a].name, items[b].name,
                                           between(items[a].visible, items[b].visible),
                                           std::max(gap, 0.0f)});
            }
        }
    }
    return report;
}

} // namespace etude::app::ui
