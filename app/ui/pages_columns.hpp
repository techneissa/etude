// SPDX-License-Identifier: GPL-3.0-or-later
// The two-column layout the settings pages share.
#pragma once

#include "kit.hpp"
#include "settings_catalogue.hpp"

#include <cmath>

namespace etude::app::ui {

// Lays rows out in one or two columns; `next()` moves to the second. `left_share` (0 to 1) is the
// first column's share of the width when a page's first column needs more than half (the Notes
// page's eight style tiles in one row); the default splits the width evenly.
class Columns {
  public:
    Columns(Kit& k, int columns, float left_share = 0.5f) : two_(columns > 1) {
        if (two_) {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {k.s(tok::kS3), 0.0f});
            const bool even = left_share == 0.5f;
            two_ = ImGui::BeginTable("##cols", 2,
                                     even ? ImGuiTableFlags_SizingStretchSame
                                          : ImGuiTableFlags_SizingStretchProp);
            ImGui::PopStyleVar();
            if (two_) {
                if (!even) {
                    ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthStretch,
                                            left_share);
                    ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthStretch,
                                            1.0f - left_share);
                }
                ImGui::TableNextColumn();
            }
        }
    }
    ~Columns() {
        if (two_) {
            ImGui::EndTable();
        }
    }
    Columns(const Columns&) = delete;
    Columns& operator=(const Columns&) = delete;
    // The next column's first row starts on a whole pixel, where every row after it does, so a wide
    // control in its first row ends up in line with the ones below (the column check holds them to one x).
    void next() {
        catalogue_sweep::next_column();
        if (two_) {
            ImGui::TableNextColumn();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos({std::floor(at.x), at.y});
        }
    }
    bool two() const { return two_; }

  private:
    bool two_ = false;
};

} // namespace etude::app::ui
