// SPDX-License-Identifier: GPL-3.0-or-later
// Drawing the achievements: a medal (the atlas's disc with the badge's icon embossed on its face, or
// a ghost for one not yet earned), the ring of a person's medals split by category (the Household's
// summary) and the colours and icons of the categories.
#pragma once

#include "face_layout.hpp"
#include "kit.hpp"

#include "etude/medal_eval.hpp"
#include "etude/medals.hpp"

#include <string>
#include <string_view>

namespace etude::app::ui {

// The UTF-8 glyph of a Lucide icon by name ("flame"), "" for a name the table lacks.
const char* lucide_glyph(std::string_view name);

ImVec4 category_colour(etude::MedalCategory category);
const char* category_glyph(etude::MedalCategory category);

// The medal of `achievement` at `tier` (0: not earned yet: a ghost of the first one), the disc `d`
// across centred on `centre`; `large` draws it with its ribbon above (the ribbon reaches above the
// disc by about 0.54 d). Nothing is rendered here: the atlas holds the pixels.
void draw_medal(const Kit& k, ImDrawList* dl, etude::Achievement achievement, int tier, ImVec2 centre,
                float d, bool large);

// The same for a bare material and icon (the design sheet, the tier ladder's rungs).
// `dim` (0 .. 1) fades a medal not yet held (the ladder's rungs above the tier reached).
void draw_medal_of(const Kit& k, ImDrawList* dl, etude::Material material, const char* icon_glyph,
                   ImVec2 centre, float d, bool large, bool ghost, float dim = 1.0f);

// The name in bold and, after it, the title in the quieter regular: "Anna \xC2\xB7 Soloist", from `slot`'s
// left edge. The title is the first to go when the two do not fit the slot.
void name_and_title(const Kit& k, ImDrawList* dl, const Box& slot, float size, const char* name,
                    const char* title, ImVec4 ink);

// The name in bold, then the title's small medal (the piano emblem of its tier) and the title in the quieter
// regular: "Anna [medal] Soloist". The medal and the title are the first to go when they do not fit the slot.
void name_title_medal(const Kit& k, ImDrawList* dl, const Box& slot, float size, const char* name, int title,
                      ImVec4 ink);

// The streak freezes banked, a small snowflake and the count ("[snowflake] 3"), the icon's centre at `left`
// (the line's middle) and the count after it; dimmed at 0. Returns the width taken. `size` is the line's
// type size. `width_only` measures without drawing, so a line can leave room for it.
float draw_freezes(const Kit& k, ImDrawList* dl, ImVec2 left, float size, int freezes, bool width_only = false);

// The ring: one arc for each category, as filled as the share of the category's medal points, the
// colour of the category, with `label` (the points) inside. `d` across.
void draw_medal_ring(Kit& k, ImDrawList* dl, ImVec2 centre, float d, const etude::MedalSet& set,
                     const char* label);

} // namespace etude::app::ui
