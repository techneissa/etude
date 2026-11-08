// SPDX-License-Identifier: GPL-3.0-or-later
// The pin at the end of the bar and the grab handle that shows the bar is there while it is hidden.
#include "bar.hpp"

namespace etude::app::ui {

namespace {

constexpr float kHandleWidth = 40.0f; // design points
constexpr float kHandleHeight = 4.0f;
constexpr float kHandleInset = 6.0f; // from the screen's top edge
constexpr float kHandleAlpha = 0.30f;

} // namespace

bool pin_button(Kit& k, UiModel& model, ImVec2 centre, ImVec2 hit, float drawn, bool live,
                const char* caption) {
    const bool pinned = model.app.bar_pinned;
    const MorphGlyph glyph = pinned ? MorphGlyph::Pinned : MorphGlyph::Unpinned;
    const bool tap =
        icon_toggle(k, "##pin", glyph, caption != nullptr ? nullptr : "Keep the bar shown", centre,
                    hit, drawn, pinned, live, caption);
    if (tap) {
        model.app.bar_pinned = !pinned;
        k.say(pinned ? "The bar hides after a moment" : "The bar stays shown");
    }
    return tap;
}

void reveal_handle(const Kit& k, ImVec2 top_centre, float fade) {
    if (fade <= 0.0f) {
        return;
    }
    const float w = k.s(kHandleWidth);
    const float h = k.s(kHandleHeight);
    const float y = top_centre.y + k.s(kHandleInset);
    ImGui::GetForegroundDrawList()->AddRectFilled(
        {top_centre.x - w * 0.5f, y}, {top_centre.x + w * 0.5f, y + h},
        ImGui::GetColorU32(with_alpha(col::kText, kHandleAlpha * fade)), h * 0.5f);
}

} // namespace etude::app::ui
