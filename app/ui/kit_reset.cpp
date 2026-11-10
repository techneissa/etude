// SPDX-License-Identifier: GPL-3.0-or-later
// The reset button of a settings slider or stepper row, and the toast that says what it did.
#include "kit_internal.hpp"

namespace etude::app::ui {

bool reset_button(Kit& k, const RowFrame& frame, float cy, bool active, const char* label) {
    const Span span = reset_span(frame.plan, k.u);
    const float hit = k.s(tok::kHit);
    // A whole-pixel centre, so the face and the glyph sit on it as the info button's do.
    const ImVec2 centre{std::round((span.x0 + span.x1) * 0.5f), std::round(cy)};
    if (!active) {
        // Greyed: the glyph alone, in the faint ink. Not an item, so it takes no touch.
        ImVec4 ink = col::kText2;
        ink.w *= tok::kDisabledAlpha;
        glyph(k, ImGui::GetWindowDrawList(), icon::kReset, centre, k.s(tok::kIconSm), ink);
        return false;
    }
    ImGui::SetCursorScreenPos({centre.x - hit * 0.5f, centre.y - hit * 0.5f});
    const bool pressed = ImGui::InvisibleButton("##reset", {hit, hit});
    const State st = item_state(k, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float half = k.s(tok::kChip) * 0.5f;
    const Box face{centre.x - half, centre.y - half, centre.x + half, centre.y + half};
    // Named with its row, so a check finds the button of the row it means.
    if (audit::on()) {
        audit::note((std::string("##reset ") + label).c_str(), {face.x0, face.y0},
                    {face.x1, face.y1});
    }
    draw_state(k, dl, "##reset", face,
               {centre.x - hit * 0.5f, centre.y - hit * 0.5f, centre.x + hit * 0.5f,
                centre.y + hit * 0.5f},
               st);
    glyph(k, dl, icon::kReset, centre, k.s(tok::kIconSm), state_ink(st, col::kText2));
    return tapped(pressed);
}

} // namespace etude::app::ui
