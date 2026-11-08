// SPDX-License-Identifier: GPL-3.0-or-later
// Which text fields are open, known the moment one opens (Dear ImGui's own `WantTextInput` comes
// a frame later), so the frame that opens a field already keeps the piano's typing keys and the
// shortcuts out of it, and a screen that reads raw finger events leaves a naming dialog's touches
// alone. No ImGui here, so tests/library_layout_test.cpp checks it.
#pragma once

#include <array>
#include <cstddef>

namespace etude::app::ui::osk {

enum class Owner : unsigned char {
    SectionName, // a name dialog (modal over the whole window)
    LookName,    // a name dialog
    Library,     // the library's search or name card
    Tempo,       // the typed tempo's field
    ProfileName, // the profile sheet's name field (modal over the whole window)
};

inline std::array<bool, 5>& open_fields() {
    static std::array<bool, 5> fields{};
    return fields;
}

inline void set_field_open(Owner who, bool open) {
    open_fields()[static_cast<std::size_t>(who)] = open;
}

// Some field takes typing.
inline bool text_field_open() {
    for (const bool open : open_fields()) {
        if (open) {
            return true;
        }
    }
    return false;
}

// A name dialog is up or asked for: it is modal, so the screen under it takes no touch.
inline bool dialog_open() {
    return open_fields()[static_cast<std::size_t>(Owner::SectionName)] ||
           open_fields()[static_cast<std::size_t>(Owner::LookName)] ||
           open_fields()[static_cast<std::size_t>(Owner::ProfileName)];
}

// Whether the UI keeps a key or text event for the open field: ImGui says it wants text (a frame
// late), or a field is open now.
inline bool field_takes_event(bool key_or_text_event, bool imgui_wants_text, bool field_open) {
    return key_or_text_event && (imgui_wants_text || field_open);
}

} // namespace etude::app::ui::osk
