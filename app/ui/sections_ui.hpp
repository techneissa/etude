// SPDX-License-Identifier: GPL-3.0-or-later
// Pieces the section band, its lane and the section editor share: the soft colour each section
// wears and the dialog that names a section.
#pragma once

#include "kit.hpp"
#include "sections_port.hpp"

#include <cstddef>
#include <string>

namespace etude::app::ui {

// The soft colour of the nth section; the same section wears it on the band, the lane and the
// editor.
ImVec4 band_tint(std::size_t index);

// The dialog that renames a section: the shared name card (osk.hpp) with its quick names, a
// Default name button, Cancel and Save, and the on-screen keyboard under it when a finger asks.
// open_section_rename() asks for it; section_rename_dialog() draws it, and is called once every
// frame, by the host.
void open_section_rename(SectionId id, const std::string& current);
void section_rename_dialog(Kit& k);

} // namespace etude::app::ui
