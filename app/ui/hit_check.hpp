// SPDX-License-Identifier: GPL-3.0-or-later
// The audit's touch rules, from the hit areas a screen drew (icon_audit.cpp collects them from
// ImGui's item hooks): every area at least `kMinHitMm` wide and tall, no two overlapping, no two
// closer than `kMinHitGapMm`. No ImGui here, so a test runs it on made-up areas.
#pragma once

#include "etude/screen_layout.hpp"

#include <span>
#include <string>
#include <vector>

namespace etude::app::ui {

// One pressable item, in the caller's unit (ImGui points).
struct HitItem {
    std::string name;
    RectF box;          // the whole item
    RectF visible;      // the part its window shows; empty when none is on screen
    bool cut = false;   // its window cuts it (a row scrolled half out): not held to the size rule
    bool surface = false; // a drag surface (the scrubber, the roll): measured for size only
    bool covered = false; // not pressable now: a card or window above it takes the touch
};

enum class HitFault : unsigned char { TooSmall, Overlap, TooClose };
const char* hit_fault_name(HitFault fault);

struct HitFinding {
    HitFault fault = HitFault::TooSmall;
    std::string a;
    std::string b; // empty for TooSmall
    RectF where;   // the item for TooSmall; the strip between (or the part shared by) the two
    float mm = 0.0f; // the smaller side, the overlap depth (positive), or the gap
};

struct HitReport {
    std::vector<HitFinding> findings;
    int sized = 0; // items held to the size rule
    int pairs = 0; // pairs of pressable items looked at
};

// Checks `items` at `pt_per_mm` points to the millimetre. Items that no window shows, covered ones
// and drag surfaces take no part in a pair; a pair is a finding when its areas overlap, or are
// less than the gap apart. ImGui puts every item on a whole pixel, so a distance may come out up to
// `round_pt` points short of what the layout asked for: that much is not a finding.
HitReport check_hit_areas(std::span<const HitItem> items, float pt_per_mm, float round_pt = 1.0f);

} // namespace etude::app::ui
