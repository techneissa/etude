// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_view.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

ViewMode next_view_mode(ViewMode mode) {
    switch (mode) {
    case ViewMode::Falling:
        return ViewMode::Sheet;
    case ViewMode::Sheet:
        return ViewMode::Both;
    case ViewMode::Both:
        break;
    }
    return ViewMode::Falling;
}

const char* view_mode_label(ViewMode mode) {
    switch (mode) {
    case ViewMode::Falling:
        return "Falling notes";
    case ViewMode::Sheet:
        return "Sheet music";
    case ViewMode::Both:
        break;
    }
    return "Sheet music and falling notes";
}

std::optional<ViewMode> parse_view_mode(std::string_view text) {
    if (text == "falling") {
        return ViewMode::Falling;
    }
    if (text == "sheet") {
        return ViewMode::Sheet;
    }
    if (text == "both") {
        return ViewMode::Both;
    }
    return std::nullopt;
}

RectF score_area(const ScreenLayout& layout) {
    RectF r = layout.note_area;
    if (!layout.chord_label.empty() && layout.chord_label.bottom() <= r.y + 0.5f) {
        r.h += r.y - layout.chord_label.y;
        r.y = layout.chord_label.y;
    }
    return r;
}

RectF sheet_view_button(const RectF& area, float corner_px) {
    if (area.empty() || !std::isfinite(corner_px) || !(corner_px > 0.0f)) {
        return {};
    }
    const float inset = std::round(corner_px * kSheetColumnInsetShare);
    return RectF{area.right() - inset - corner_px, area.y + inset, corner_px, corner_px};
}

SheetSideButtons sheet_side_buttons(const RectF& area, float corner_px, float bottom) {
    SheetSideButtons b;
    const RectF view = sheet_view_button(area, corner_px);
    if (view.empty()) {
        return b;
    }
    b.column = view.y + 3.0f * corner_px <= bottom;
    const auto cell = [&](int i) {
        // Column: the button, names, colours down; row: names, colours, the button left to right.
        const float at = static_cast<float>(i) * corner_px;
        return b.column ? RectF{view.x, view.y + at, corner_px, corner_px}
                        : RectF{view.x - (2.0f * corner_px - at), view.y, corner_px, corner_px};
    };
    b.view = cell(b.column ? 0 : 2);
    b.names = cell(b.column ? 1 : 0);
    b.colours = cell(b.column ? 2 : 1);
    const float x = std::min({b.view.x, b.names.x, b.colours.x});
    const float y = std::min({b.view.y, b.names.y, b.colours.y});
    const float r = std::max({b.view.right(), b.names.right(), b.colours.right()});
    const float d = std::max({b.view.bottom(), b.names.bottom(), b.colours.bottom()});
    b.block = RectF{x, y, r - x, d - y};
    const float tx = std::min(b.names.x, b.colours.x);
    const float ty = std::min(b.names.y, b.colours.y);
    b.toggles = RectF{tx, ty, std::max(b.names.right(), b.colours.right()) - tx,
                      std::max(b.names.bottom(), b.colours.bottom()) - ty};
    return b;
}

namespace {

// Beside the paper's top right, on the screen's own background: the three side buttons, in a
// column or a row as sheet_side_buttons decides; the paper gives up their width and a gap.
void reserve_corner(SheetPlan& p, const RectF& note_area, float corner_px, float gap, float margin) {
    p.side = sheet_side_buttons(note_area, corner_px, p.sheet.bottom());
    p.corner = p.side.toggles;
    p.paper.w = std::max(0.0f, std::min(p.paper.w, p.side.block.x - gap - p.paper.x));
    p.system_width_px = std::floor(std::max(0.0f, p.paper.w - 2.0f * margin));
}

} // namespace

SheetPlan plan_sheet(const RectF& note_area, ViewMode mode, bool has_song, bool two_staves,
                     float px_per_mm, float corner_px, const SheetNameRoom& names_asked) {
    SheetPlan p;
    p.notes = note_area;
    if (!has_song || mode == ViewMode::Falling || note_area.empty()) {
        return p;
    }
    if (!(px_per_mm > 0.0f) || !std::isfinite(px_per_mm)) {
        px_per_mm = kDefaultPxPerMm;
    }
    const float min_space = kSheetMinSpaceMm * px_per_mm;
    // Only the Sheet view makes room for names; Both keeps its strip as it is.
    const SheetNameRoom names = mode == ViewMode::Sheet ? names_asked : SheetNameRoom{};
    const float max_space = kSheetMaxSpaceMm * px_per_mm;
    // A system's height at a staff space: its staves and their marks, and with names their lanes,
    // which hold the tallest stack of names at the names' size (never smaller than `line_px`).
    const int stacks = std::max(0, names.above) + std::max(0, names.below);
    const float spaces = (two_staves ? kGrandStaffSpaces : kSingleStaffSpaces) +
                         (stacks > 0 ? 2.0f * kSheetNameLaneSpaces : 0.0f);
    const float line_max = names.line_max_px > 0.0f ? std::max(names.line_px, names.line_max_px) : 1e9f;
    const auto line_at = [&](float space) {
        return std::clamp(names.line_spaces * space, names.line_px, line_max);
    };
    const auto system_h = [&](float space) {
        return space * spaces + static_cast<float>(stacks) * line_at(space);
    };
    const float frame = 2.0f * (kSheetMarginSpaces + kSheetGapSpaces);
    // Room each staff space takes across: the system, the margins and the gaps.
    const float across = kSheetWidthSpaces + 2.0f * (kSheetMarginSpaces + kSheetGapSpaces);
    corner_px = std::isfinite(corner_px) ? std::max(0.0f, corner_px) : 0.0f;
    const float by_width = std::max(0.0f, note_area.w - corner_px) / across;
    // Down: `n` systems, the paper's margins and the gaps.
    const auto by_height = [&](float height, int n) {
        const auto fn = static_cast<float>(n);
        const auto fs = static_cast<float>(stacks);
        // The names at their smallest or largest size, if that is what they take at the space
        // that gives; else in proportion to it.
        const auto fixed_at = [&](float line) { return (height - fn * fs * line) / (fn * spaces + frame); };
        const float smallest = fixed_at(names.line_px);
        if (stacks == 0 || names.line_spaces * smallest <= names.line_px) {
            return std::max(0.0f, smallest);
        }
        const float largest = fixed_at(line_max);
        if (names.line_spaces * largest >= line_max) {
            return std::max(0.0f, largest);
        }
        return height / (fn * (spaces + fs * names.line_spaces) + frame);
    };
    float space = 0.0f;
    if (mode == ViewMode::Sheet) {
        p.sheet = note_area;
        p.notes = RectF{note_area.x, note_area.bottom(), note_area.w, 0.0f};
        const float two = std::min({by_width, by_height(note_area.h, 2), max_space});
        if (two >= min_space) {
            p.systems = 2;
            space = two;
        } else {
            p.systems = 1;
            space = std::min({by_width, by_height(note_area.h, 1), max_space});
        }
    } else {
        p.systems = 1;
        space = std::min(by_width, max_space);
        space = std::min(space, by_height(note_area.h * kSheetStripMaxShare, 1));
        const float strip = std::round(system_h(space) + space * frame);
        p.sheet = RectF{note_area.x, note_area.y, note_area.w, std::min(strip, note_area.h)};
        p.notes = RectF{note_area.x, p.sheet.bottom(), note_area.w, note_area.h - p.sheet.h};
    }
    space = std::max(space, 1.0f);
    p.staff_space_px = space;
    const float gap = std::round(space * kSheetGapSpaces);
    p.paper = RectF{p.sheet.x + gap, p.sheet.y + gap, std::max(0.0f, p.sheet.w - 2.0f * gap),
                    std::max(0.0f, p.sheet.h - 2.0f * gap)};
    const float margin = space * kSheetMarginSpaces;
    p.system_width_px = std::floor(std::max(0.0f, p.paper.w - 2.0f * margin));
    // A slot is never much taller than its system: the paper ends under the last one.
    p.slot_height_px = std::max(0.0f, std::min((p.paper.h - 2.0f * margin) / static_cast<float>(p.systems),
                                               system_h(space) * kSheetSlotSlack));
    p.paper.h = std::min(p.paper.h, std::round(p.slot_height_px * static_cast<float>(p.systems) + 2.0f * margin));
    if (corner_px > 0.0f) {
        reserve_corner(p, note_area, corner_px, gap, margin);
    }
    p.shown = !p.sheet.empty() && p.system_width_px > 0.0f && p.slot_height_px > 0.0f;
    return p;
}

float sheet_turn_at(float from, float to, float elapsed_s, bool reduced) {
    if (reduced || std::fabs(to - from) > 1.0f || !(elapsed_s >= 0.0f) || elapsed_s >= kSheetTurnS) {
        return to;
    }
    const float t = elapsed_s / kSheetTurnS;
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    return from + (to - from) * eased;
}

} // namespace etude
