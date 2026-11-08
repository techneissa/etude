// SPDX-License-Identifier: GPL-3.0-or-later
#include "row_layout.hpp"

#include <imgui.h>
#include <imgui_internal.h> // the window's id

#include <array>
#include <cmath>
#include <map>
#include <string>

namespace etude::app::ui {

namespace {

int g_scope = 0;
LabelZones g_zones;

std::uint32_t column_slot() {
    const std::uint32_t scope = static_cast<std::uint32_t>(g_scope);
    const auto column = static_cast<std::uint32_t>(ImGui::TableGetColumnIndex());
    // The window, not the ID stack: a row may have pushed its own id before it asks.
    const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
    return static_cast<std::uint32_t>(window != nullptr ? window->ID : 0u) ^
           (scope * 0x9E3779B1u) ^ (column * 0x85EBCA6Bu);
}

std::array<bool, 64> g_measured{};
bool g_hidden = false; // the frame being drawn is a page's measuring frame

int g_column_offset = 0;
bool g_probing = false;
std::vector<column_probe::Row> g_rows;

} // namespace

void row_scope(int scope) { g_scope = scope; }
void row_column_offset(int offset) { g_column_offset = offset; }
int current_row_scope() { return g_scope; }

RowFrame row_frame(float left, float width, float u) {
    RowFrame f;
    f.slot = column_slot();
    const float need = g_zones.need(f.slot, ImGui::GetFrameCount());
    f.measured = need > 0.0f;
    f.plan = plan_column(left, left + width, need, u,
                         g_zones.reset(f.slot, ImGui::GetFrameCount()));
    return f;
}

void row_report(const RowFrame& f, float label_end_from_left) {
    g_zones.report(f.slot, ImGui::GetFrameCount(), label_end_from_left);
}

void row_report_reset(const RowFrame& f) { g_zones.report_reset(f.slot, ImGui::GetFrameCount()); }

RowMeasure::RowMeasure(int scope)
    : scope_(scope),
      hidden_(scope > 0 && static_cast<std::size_t>(scope) < g_measured.size() &&
              !g_measured[static_cast<std::size_t>(scope)]) {
    if (hidden_) {
        g_hidden = true;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
        ImGui::BeginDisabled(true);
    }
}

RowMeasure::~RowMeasure() {
    if (hidden_) {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        g_hidden = false;
        g_measured[static_cast<std::size_t>(scope_)] = true;
    }
}

namespace column_probe {

bool on() { return g_probing; }

void start() {
    g_probing = true;
    g_rows.clear();
}

std::vector<Row> stop() {
    g_probing = false;
    return std::move(g_rows);
}

void note(const RowFrame& f, RowKind kind, Span span, bool stacked, bool radio) {
    if (!g_probing) {
        return;
    }
    Row r;
    r.scope = g_scope;
    r.column = column_of(ImGui::TableGetColumnIndex(), g_column_offset);
    r.kind = kind;
    r.stacked = stacked;
    r.radio = radio;
    r.span = span;
    r.visible = !g_hidden;
    r.zone = f.plan.control_x - f.plan.left;
    r.zone_measured = f.measured;
    g_rows.push_back(r);
}

} // namespace column_probe

} // namespace etude::app::ui
