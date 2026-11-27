// SPDX-License-Identifier: GPL-3.0-or-later
// Icons: a font glyph or a drawn one that morphs.
#pragma once

#include "kit_core.hpp"
#include "morph.hpp"

#include <imgui.h>

namespace etude::app::ui {

// --- Drawing -----------------------------------------------------------------------------------
// A control's icon: a font glyph (a string from icons.hpp) or a drawn icon that morphs into the
// next one the control shows.
struct Icon {
    const char* glyph = nullptr;
    MorphGlyph morph = MorphGlyph::Count;
    Icon(const char* g) : glyph(g) {} // NOLINT(google-explicit-constructor): any icon string
    Icon(MorphGlyph m) : morph(m) {}  // NOLINT(google-explicit-constructor)
};
void glyph(const Kit& k, ImDrawList* dl, const char* icon, ImVec2 centre, float size,
           ImVec4 colour);

} // namespace etude::app::ui
