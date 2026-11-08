// SPDX-License-Identifier: GPL-3.0-or-later
#include "kit.hpp"

#include "kit_internal.hpp"

#include "clef_icon.hpp"
#include "font_ink.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "pressed_fade.hpp"
#include "row_layout.hpp"
#include "sections_port.hpp"
#include "etude/accidental_text.hpp"
#include "etude/note_style.hpp"
#include "transport.hpp"

#include <imgui_internal.h> // ClearActiveID

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace etude::app::ui {



namespace {

// What a segment shows inside its cell `c` (`cell` wide): its icon and label centred as a group, or
// the label alone (a label too wide for the icon beside it goes: a long press names it). `weight` is
// how chosen the cell reads, fading with the pill's slide: the ink goes from the second text colour
// to the first and the icon to the accent. `bold` is the one face of the words, changed once as the
// pill passes.
void segment_content(Kit& k, ImDrawList* dl, const Seg& s, const Box& c, float cell, float csize,
                     float gap, bool bold, float weight, const State& st) {
    const ImVec4 ink_off = state_ink(st, col::kText2);
    const ImVec4 ink_on = state_ink(st, col::kText);
    float cw = 0.0f;
    const bool show_label =
        s.label != nullptr &&
        (s.icon == nullptr ||
         text_size(k.regular, csize, s.label).x + k.s(tok::kIconSm) + gap + k.s(tok::kS2) <=
             cell);
    if (s.icon != nullptr) {
        cw += k.s(tok::kIconSm);
    }
    if (show_label) {
        cw += (cw > 0 ? gap : 0.0f) + text_size(k.regular, csize, s.label).x;
    }
    float x = c.x0 + (cell - cw) * 0.5f;
    const float cy = c.cy();
    if (s.icon != nullptr) {
        glyph(k, dl, s.icon, {x + k.s(tok::kIconSm) * 0.5f, cy}, k.s(tok::kIconSm),
              mix(ink_off, state_ink(st, col::kAccent), weight));
        x += k.s(tok::kIconSm) + gap;
    }
    if (show_label) {
        const FitText shown = fit(k.regular, csize, s.label, cell - k.s(tok::kS1));
        const float shown_w = text_size(bold ? k.bold : k.regular, csize, shown.c_str()).x;
        if (s.icon == nullptr) {
            x = c.x0 + (cell - shown_w) * 0.5f;
        }
        fade_label(k, dl, csize, {x, c.y0, x + shown_w, c.y1}, LabelAlign::Centre, shown.c_str(),
                   bold, weight, ink_off, ink_on);
    }
    if (s.label != nullptr && !show_label) {
        long_press_tip(k, s.label);
    }
}

// Each cell's content: its icon and label. `min_cell` is the width that reads comfortably (with
// padding), which decides whether the label goes above the track; `need` the least a label can be
// read at, under which the cells wrap. Every cell is at least `kHit` wide on top of that
// (segment_layout.hpp).
void segment_needs(const Kit& k, std::span<const Seg> items, float csize, float gap, float* min_cell,
                   float* need) {
    for (const Seg& s : items) {
        float cw = 0.0f;
        if (s.icon != nullptr) {
            cw += k.s(tok::kIconSm);
        }
        if (s.label != nullptr) {
            cw += (cw > 0 ? gap : 0.0f) + text_size(k.regular, csize, s.label).x;
        }
        *min_cell = std::max(*min_cell, cw + k.s(tok::kS4));
        *need = std::max(*need, cw + k.s(tok::kS1)); // the room a label is cut to (cell - kS1)
    }
}

// The cell of a row of `count` equal cells of width `cell` from `x0` (the first is cell `first` of the
// control) whose words are bold while the pill is at `pill`: the one nearest its centre.
int segment_bold_cell(const Box& pill, float x0, float cell, int first, int count) {
    std::array<float, 32> centres{};
    const int n = std::min<int>(count, static_cast<int>(centres.size()));
    for (int i = 0; i < n; ++i) {
        centres[static_cast<std::size_t>(i)] = x0 + cell * (static_cast<float>(i) + 0.5f);
    }
    return first + pill_bold_cell((pill.x0 + pill.x1) * 0.5f,
                                  std::span<const float>(centres.data(), static_cast<std::size_t>(n)));
}

// The icon and label left of the track (above it when `stacked`); returns how far the row moves down.
struct LeadSize {
    float w, h, text_pt;
};
float segment_lead(Kit& k, ImDrawList* dl, const char* id, Icon icon, const char* label,
                   const char* info, ImVec2 pos, const LeadSize& size, bool stacked,
                   float track_x0) {
    const bool has_icon = icon.glyph != nullptr || icon.morph != MorphGlyph::Count;
    // An (i) button's hit square sits in the label's line, so a stacked line holding one is taller.
    const float lh = stacked ? k.s(info != nullptr ? kStackedInfoLabelH : kStackedLabelH) : size.h;
    const float cy = stacked ? stacked_label_cy(k, pos.y, info != nullptr) : pos.y + lh * 0.5f;
    const float info_w = info != nullptr && label != nullptr ? k.s(kInfoReserve) : 0.0f;
    float x = pos.x + k.s(tok::kS3);
    const ImVec2 at{x + k.s(tok::kIcon) * 0.5f, cy};
    if (icon.glyph != nullptr) {
        glyph(k, dl, icon.glyph, at, k.s(tok::kIcon), col::kText2);
    } else if (has_icon) {
        // A drawn icon: it morphs into the next when the choice changes.
        morph_glyph(k, dl, ImGui::GetID(id) + 1, icon.morph, at, k.s(tok::kIcon), col::kText2,
                    kSpringSmooth);
    }
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    if (label != nullptr) {
        const float room =
            label_room(k, stacked ? pos.x + size.w - k.s(tok::kS3) : track_x0, x) - info_w;
        const RowLabel shown = row_label(k.regular, size.text_pt, label, room);
        draw_row_label(k, dl, k.regular, size.text_pt, {x, cy - lh * 0.5f, pos.x + size.w, cy + lh * 0.5f},
                       col::kText, shown);
        if (info != nullptr) {
            info_button(k, info, {x + shown.width + k.s(kInfoOffset), cy}, nullptr, nullptr, 0.0f,
                    stacked ? (k.s(tok::kHit) - k.s(tok::kChip)) * 0.5f : 0.0f);
        }
    }
    return stacked ? lh : 0.0f;
}

// Where a segmented track's cells sit: from `x0`, `cell` wide, in rows that wrap when the cells
// cannot all keep `kHit` (`rows`), the first row's top at `y`.
struct SegmentTrack {
    float u = 1.0f;
    float x0 = 0.0f;
    float y = 0.0f;
    float cell = 0.0f;
    float h = 0.0f;     // a row's hit height
    float pitch = 0.0f; // from one wrapped row to the next: a hit and a dead gap
    float ch = 0.0f;    // the face's height
    SegmentRows rows;
    int row_of(int i) const { return i / rows.per_row; }
    // A cell's pill (the thumb, and each cell's hover and press) in the track `c`.
    Box pill_in(const Box& c) const {
        const SegmentCellBoxes b = segment_cell_boxes(c.x0, c.x1, u);
        return Box{b.pill_x0, c.y0 + b.pill_y0, b.pill_x1, c.y0 + b.pill_y1};
    }
    // The cell at index `at` (fractional while it moves).
    Box cell_box(float at) const {
        const int r = row_of(static_cast<int>(std::floor(at + 1e-4f)));
        const float col_at = at - static_cast<float>(r * rows.per_row);
        const float ty = y + static_cast<float>(r) * pitch + (h - ch) * 0.5f;
        return Box{x0 + cell * col_at, ty, x0 + cell * (col_at + 1.0f), ty + ch};
    }
};

SegmentTrack segment_track(const Kit& k, const Span& track, int n, float need, float y, float h) {
    SegmentTrack t;
    t.u = k.u;
    t.x0 = track.x0;
    t.y = y;
    t.h = h;
    // Cells that cannot all keep `kHit` in one row wrap into rows of their own track.
    t.rows = segment_rows(track.w(), n, need, k.u);
    t.cell = t.rows.cell_w;
    // A cell's hit area is the cell less the dead gap to its neighbour; wrapped rows are a hit and a
    // gap apart.
    t.pitch = h + k.s(tok::kHitGap);
    t.ch = k.s(tok::kFace);
    return t;
}

// What the cells show and answer to: the choices, the chosen one, the faint ones and where a tap on
// one is reported, the label sizes, and the pill as it slides.
struct SegmentCells {
    std::span<const Seg> items;
    int* current;
    std::uint32_t disabled;
    int* refused;
    float csize;
    float gap;
    ImGuiID thumb_id;
    bool has_pill;
    Box pill;
    int bold_cell;
};

// Each cell's hit area, hover, press, divider and words; true when a tap chose another cell.
bool segment_cells(Kit& k, ImDrawList* dl, const SegmentTrack& t, const SegmentCells& cs) {
    const int n = static_cast<int>(cs.items.size());
    const float inner_r = k.s(tok::kRadiusInner);
    int* const current = cs.current;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const Seg& s = cs.items[static_cast<std::size_t>(i)];
        const Box c = t.cell_box(static_cast<float>(i));
        const float row_y = t.y + static_cast<float>(t.row_of(i)) * t.pitch;
        const SegmentCellBoxes cb = segment_cell_boxes(c.x0, c.x1, t.u);
        const Box hit{cb.hit_x0, row_y, cb.hit_x1, row_y + t.h};
        ImGui::SetCursorScreenPos({hit.x0, hit.y0});
        ImGui::PushID(i);
        const bool pressed = ImGui::InvisibleButton("##seg", {hit.w(), hit.h()});
        const bool on = i == *current;
        const bool off = i < 32 && ((cs.disabled >> i) & 1u) != 0;
        // The thumb is the selected state; each cell draws its own hover and press.
        const State st = item_state(k, false, true, off);
        ImGui::PopID();
        draw_state(k, dl, "##seg", t.pill_in(c), {hit.x0, hit.y0, hit.x1, hit.y1}, st, Idle::None,
                   inner_r);
        const bool row_start_cell = i % t.rows.per_row == 0;
        if (!row_start_cell) {
            // A divider shows where no pill is: it fades out as the pill's edge comes up to it.
            float shade = 1.0f;
            if (cs.has_pill && t.row_of(*current) == t.row_of(i)) {
                const float dist = std::max(cs.pill.x0 - c.x0, c.x0 - cs.pill.x1);
                const float edge = segment_pill_inset(t.u);
                shade = std::clamp((dist - edge) / (2.0f * edge), 0.0f, 1.0f);
            }
            if (shade > 0.0f) {
                dl->AddLine({c.x0, c.y0 + t.ch * 0.28f}, {c.x0, c.y1 - t.ch * 0.28f},
                            u32(with_alpha(col::kHairline, shade)), 1.0f);
            }
        }
        // The content lies inside the hit area (the cell less the dead gap), as the face does.
        segment_content(k, dl, s, {hit.x0, c.y0, hit.x1, c.y1}, hit.w(), cs.csize, cs.gap,
                        i == cs.bold_cell, cell_weight(k, cs.thumb_id, i, on), st);
        if (tapped(pressed) && off) {
            if (cs.refused != nullptr) {
                *cs.refused = i;
            }
        } else if (tapped(pressed) && !on) {
            *current = i;
            changed = true;
        }
    }
    return changed;
}

// Whether the track goes under the label, and where it runs: beside the label when its cells share
// the room there, else under it (a row with no label keeps the block's span). Tells the column's
// check what the row drew.
struct SegmentPlan {
    bool stacked = false;
    Span track;
};
SegmentPlan segment_plan(const Kit& k, const RowFrame& frame, bool has_label, bool has_icon, int n,
                         float min_cell, float label_x) {
    const bool labelled = has_label || has_icon;
    const Span beside = wide_span(frame.plan, false, k.u);
    const float least_cell = std::max(min_cell, k.s(tok::kCell));
    const bool stacked = has_label && (frame.plan.stacked ||
                                       least_cell * static_cast<float>(n) > beside.w());
    // An icon alone has no label to put the track under: it keeps its row.
    const bool icon_only = !has_label && has_icon;
    RowKind kind = RowKind::Wide;
    Span track = wide_span(frame.plan, stacked, k.u);
    if (!labelled) {
        kind = RowKind::Block;
        track = block_span(frame.plan, k.u);
    } else if (icon_only && frame.plan.stacked) {
        kind = RowKind::Inline;
        track = {label_x, frame.plan.control_right};
    }
    column_probe::note(frame, kind, track, stacked);
    return {stacked, track};
}

} // namespace

bool segmented(Kit& k, const char* id, Icon icon, const char* label, std::span<const Seg> items,
               int* current, const char* info) {
    return segmented(k, id, icon, label, items, current, 0u, nullptr, info);
}

bool segmented(Kit& k, const char* id, Icon icon, const char* label, std::span<const Seg> items,
               int* current, std::uint32_t disabled, int* refused, const char* info) {
    const bool has_icon = icon.glyph != nullptr || icon.morph != MorphGlyph::Count;
    float w = 0;
    ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit);
    const int n = static_cast<int>(items.size());
    const float lsize = k.s(tok::kBody);
    const float csize = k.s(tok::kLabel);
    const float gap = k.s(tok::kS2); // between a cell's icon and its label
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float min_cell = 0.0f;
    float need = 0.0f;
    segment_needs(k, items, csize, gap, &min_cell, &need);
    // The label, or an icon alone, sits left of the track; text lines up with the other rows. The
    // track starts where the column's controls start; only cells that cannot share the room beside
    // the label put the track under it.
    const bool labelled = label != nullptr || has_icon;
    const RowFrame frame = row_frame(pos.x, w, k.u);
    const float label_x = pos.x + k.s(tok::kS3) + k.s(tok::kIcon) + k.s(tok::kS3);
    const ImVec2 ls = label != nullptr ? text_size(k.regular, lsize, label) : ImVec2{0.0f, 0.0f};
    const float info_w = info != nullptr && label != nullptr ? k.s(kInfoReserve) : 0.0f;
    if (labelled) {
        row_report(frame, label_x + ls.x + info_w - pos.x);
    }
    const SegmentPlan plan =
        segment_plan(k, frame, label != nullptr, has_icon, n, min_cell, label_x);
    const bool stacked = plan.stacked;
    const Span track = plan.track;
    const ImVec2 start = pos;
    if (labelled) {
        pos.y += segment_lead(k, dl, id, icon, label, info, pos, {w, h, lsize}, stacked, track.x0);
    }
    const SegmentTrack t = segment_track(k, track, n, need, pos.y, h);
    const float x0 = t.x0;
    const float cell = t.cell;
    const SegmentRows& rows = t.rows;
    const float gap_hit = k.s(tok::kHitGap);
    const float pitch = t.pitch;
    const float inner_r = k.s(tok::kRadiusInner);
    for (int r = 0; r < rows.rows; ++r) {
        const int count = std::min(rows.per_row, n - r * rows.per_row);
        const float ty = pos.y + static_cast<float>(r) * pitch + (h - t.ch) * 0.5f;
        dl->AddRectFilled({x0, ty}, {x0 + cell * static_cast<float>(count), ty + t.ch},
                          u32(col::kFill), k.s(tok::kRadiusControl));
    }

    ImGui::PushID(id);
    const ImGuiID thumb_id = ImGui::GetID("##thumb");
    Box pill{};
    int bold_cell = -1;
    const bool has_pill = *current >= 0 && *current < n;
    if (has_pill) {
        // The pill slides from the old cell to the new one; to another row it moves at once.
        const Box c = t.cell_box(static_cast<float>(*current));
        pill = slide_pill(k, thumb_id, *current, t.row_of(*current), t.pill_in(c));
        State on;
        on.selected = 1.0f;
        draw_state(k, dl, "##thumb", pill, c, on, Idle::None, inner_r);
        const int first = t.row_of(*current) * rows.per_row;
        bold_cell = segment_bold_cell(pill, x0, cell, first, std::min(rows.per_row, n - first));
    }
    const SegmentCells cells{items, current, disabled, refused, csize, gap, thumb_id, has_pill, pill,
                             bold_cell};
    const bool changed = segment_cells(k, dl, t, cells);
    ImGui::PopID();
    end_row(start, {w, pos.y - start.y + pitch * static_cast<float>(rows.rows) - gap_hit});
    return changed;
}

namespace {

// The pill of a cell whose hit row is `cell`: the track is `kFace` tall, centred on the row.
Box tab_pill(const Kit& k, const Box& cell) {
    const SegmentCellBoxes b = segment_cell_boxes(cell.x0, cell.x1, k.u);
    const float top = cell.cy() - b.track_h * 0.5f;
    return {b.pill_x0, top + b.pill_y0, b.pill_x1, top + b.pill_y1};
}

} // namespace

Box slide_pill(Kit& k, ImGuiID key, int cell, int row, const Box& pill) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    // Kept in the window's storage next to the control's own ID, as `tween` keeps its values.
    const ImGuiID f0 = key + 11;
    const ImGuiID f1 = key + 12;
    const ImGuiID f2 = key + 13;
    const ImGuiID f3 = key + 14;
    const ImGuiID ft = key + 15;
    const ImGuiID ic = key + 16;
    const ImGuiID ir = key + 17;
    const ImGuiID iseen = key + 18;
    const int frame = ImGui::GetFrameCount();
    PillSlide s;
    s.from_x0 = st->GetFloat(f0, pill.x0);
    s.from_x1 = st->GetFloat(f1, pill.x1);
    s.to_x0 = st->GetFloat(f2, pill.x0);
    s.to_x1 = st->GetFloat(f3, pill.x1);
    s.t = st->GetFloat(ft, 1.0f);
    s.cell = st->GetInt(ic, cell);
    s.row = st->GetInt(ir, row);
    const int last = st->GetInt(iseen, -10);
    s.seen = last >= 0;
    // Not drawn on the frame before (a page just opened, a control that came back): no slide.
    const bool fresh = frame - last > 1;
    pill_slide_step(s, cell, row, pill.x0, pill.x1, k.dt, fresh, k.reduced_motion);
    st->SetFloat(f0, s.from_x0);
    st->SetFloat(f1, s.from_x1);
    st->SetFloat(f2, s.to_x0);
    st->SetFloat(f3, s.to_x1);
    st->SetFloat(ft, s.t);
    st->SetInt(ic, s.cell);
    st->SetInt(ir, s.row);
    st->SetInt(iseen, frame);
    Box out = pill;
    pill_slide_at(s, &out.x0, &out.x1);
    return out;
}

float cell_weight(Kit& k, ImGuiID key, int cell, bool on) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID fw = key + 21 + static_cast<ImGuiID>(cell) * 2;
    const ImGuiID fl = fw + 1;
    const int frame = ImGui::GetFrameCount();
    float weight = st->GetFloat(fw, on ? 1.0f : 0.0f);
    const bool fresh = frame - st->GetInt(fl, -10) > 1;
    const float shown = cell_weight_step(&weight, on, k.dt, fresh, k.reduced_motion);
    st->SetFloat(fw, weight);
    st->SetInt(fl, frame);
    return shown;
}

void fade_label(Kit& k, ImDrawList* dl, float size, const Box& slot, LabelAlign align,
                const char* text, bool bold, float weight, ImVec4 ink_off, ImVec4 ink_on) {
    label_at(k, dl, bold ? k.bold : k.regular, size, slot, align, mix(ink_off, ink_on, weight), text);
}

int tab_track(Kit& k, ImDrawList* dl, const char* id, std::span<const Box> cells, int chosen) {
    if (cells.empty()) {
        return -1;
    }
    const float track_h = k.s(tok::kFace);
    const float cy = cells.front().cy();
    dl->AddRectFilled({cells.front().x0, cy - track_h * 0.5f}, {cells.back().x1, cy + track_h * 0.5f},
                      u32(col::kFill), k.s(tok::kRadiusControl));
    if (chosen < 0 || chosen >= static_cast<int>(cells.size())) {
        return -1;
    }
    ImGui::PushID(id);
    const Box target = tab_pill(k, cells[static_cast<std::size_t>(chosen)]);
    // The pill slides to the chosen cell: its x and width ease.
    const Box pill = slide_pill(k, ImGui::GetID("##thumb"), chosen, 0, target);
    State on;
    on.selected = 1.0f;
    draw_state(k, dl, "##thumb", pill, cells[static_cast<std::size_t>(chosen)], on, Idle::None,
               k.s(tok::kRadiusInner));
    ImGui::PopID();
    std::array<float, 16> centres{};
    const std::size_t count = std::min<std::size_t>(cells.size(), centres.size());
    for (std::size_t i = 0; i < count; ++i) {
        centres[i] = cells[i].cx();
    }
    return pill_bold_cell((pill.x0 + pill.x1) * 0.5f, std::span<const float>(centres.data(), count));
}

TabHit tab_cell(Kit& k, ImDrawList* dl, const char* id, const Box& cell, bool on) {
    const SegmentCellBoxes b = segment_cell_boxes(cell.x0, cell.x1, k.u);
    const Box hit{b.hit_x0, cell.y0, b.hit_x1, cell.y1};
    TabHit out;
    const bool pressed = press_area(k, id, hit);
    // The chosen pill is the track's thumb; each cell draws only its own hover and press.
    out.state = item_state(k, false);
    out.pill = tab_pill(k, cell);
    out.weight = cell_weight(k, ImGui::GetID(id), 0, on);
    draw_state(k, dl, id, out.pill, hit, out.state, Idle::None, k.s(tok::kRadiusInner));
    audit::note(id, {out.pill.x0, out.pill.y0}, {out.pill.x1, out.pill.y1}, InkKind::Target);
    out.tapped = tapped(pressed);
    return out;
}

} // namespace etude::app::ui
