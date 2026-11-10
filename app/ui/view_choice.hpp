// SPDX-License-Identifier: GPL-3.0-or-later
// What the note area shows with a song open, as the side button right of the score names it, and
// the one rule for it. The side button, Settings > View's "Show" row and the I key are the same
// setting (`AppSettings::view_mode`) and all go through what is here, so they always agree:
//
//   Off   falling notes only            (ViewMode::Falling)
//   On    the sheet above the notes     (ViewMode::Both)
//   Full  the sheet only                (ViewMode::Sheet)
//
// A song with no sheet music refuses On and Full (and Sheet and Both on the row); a tap on one
// says why and changes nothing. No ImGui here.
#pragma once

#include "morph_glyphs.hpp"
#include "tokens.hpp"

#include "etude/screen_layout.hpp"
#include "etude/session_state.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace etude::app::ui {

enum class ViewChoice : std::uint8_t { Off, On, Full };
inline constexpr int kViewChoiceCount = 3;
inline constexpr ViewChoice kViewChoices[kViewChoiceCount] = {ViewChoice::Off, ViewChoice::On,
                                                              ViewChoice::Full};

inline constexpr ViewMode view_mode_of(ViewChoice c) {
    switch (c) {
    case ViewChoice::Off:
        return ViewMode::Falling;
    case ViewChoice::On:
        return ViewMode::Both;
    case ViewChoice::Full:
        break;
    }
    return ViewMode::Sheet;
}

inline constexpr ViewChoice view_choice_of(ViewMode m) {
    switch (m) {
    case ViewMode::Falling:
        return ViewChoice::Off;
    case ViewMode::Both:
        return ViewChoice::On;
    case ViewMode::Sheet:
        break;
    }
    return ViewChoice::Full;
}

// The side button's picture for a choice: Off a staff crossed out, On a staff over falling notes,
// Full a staff with a note on it, filling the box.
inline constexpr MorphGlyph view_glyph(ViewChoice c) {
    switch (c) {
    case ViewChoice::Off:
        return MorphGlyph::ViewOff;
    case ViewChoice::On:
        return MorphGlyph::ViewOn;
    case ViewChoice::Full:
        break;
    }
    return MorphGlyph::ViewFull;
}

// The sheet-music side button waits behind the bar's popovers and the band's list and menu: they
// open over the corner of the notes where it stands.
inline constexpr bool sheet_button_waits(bool bar_popover, bool band_list, bool band_menu) {
    return bar_popover || band_list || band_menu;
}

// What lies over the corner of the notes where the three side buttons stand (the sheet-music button
// and the two reading switches). They show and hide together, on this one condition: behind a modal, the
// results card, its summary, a run's cards or the replay (`covered`), the bar's popovers and the
// band's list and menu, and the band hanging under the revealed bar (`band_over`).
struct SideCover {
    bool covered = false;
    bool bar_popover = false;
    bool band_list = false;
    bool band_menu = false;
    bool band_over = false;
};
inline constexpr bool sheet_side_waits(const SideCover& c) {
    return c.covered || c.band_over || sheet_button_waits(c.bar_popover, c.band_list, c.band_menu);
}

// The box the band's cover is tested against: the three side buttons together (the button and the
// two switches' cells; the switches' is empty while no score shows).
inline RectF sheet_side_box(const RectF& button, const RectF& toggles) {
    if (button.empty() || toggles.empty()) {
        return button.empty() ? toggles : button;
    }
    const float x = std::min(button.x, toggles.x);
    const float y = std::min(button.y, toggles.y);
    return RectF{x, y, std::max(button.right(), toggles.right()) - x,
                 std::max(button.bottom(), toggles.bottom()) - y};
}

// The side button's one tap: Off, then On, then Full, then Off again.
inline constexpr ViewChoice next_view_choice(ViewChoice c) {
    switch (c) {
    case ViewChoice::Off:
        return ViewChoice::On;
    case ViewChoice::On:
        return ViewChoice::Full;
    case ViewChoice::Full:
        break;
    }
    return ViewChoice::Off;
}

// What the side button's toast says for the state a tap went to.
inline constexpr const char* view_choice_toast(ViewChoice c) {
    constexpr const char* kWords[kViewChoiceCount] = {"Falling notes only",
                                                      "Sheet and falling notes", "Sheet only"};
    return kWords[static_cast<int>(c)];
}

// A song with no sheet music (`no_sheet`: Kit::sheet.refused()) can show only the falling notes.
inline constexpr bool view_refused(ViewMode m, bool no_sheet) {
    return no_sheet && m != ViewMode::Falling;
}

// What the note area draws, and so what the side button shows: a song with no sheet music draws the
// falling notes whatever the setting says. The setting itself stays, so the next song with sheet
// music gets it back.
inline constexpr ViewMode drawn_view_mode(ViewMode setting, bool no_sheet) {
    return view_refused(setting, no_sheet) ? ViewMode::Falling : setting;
}

// Settings > View's "Show" row: its cells in order, the mode each writes and its label. The row
// writes `view_mode_of_cell(i)` for the cell i tapped; the side button shows `view_choice_of` of
// it.
inline constexpr int kViewCells = 3;
inline constexpr ViewMode view_mode_of_cell(int cell) {
    constexpr ViewMode kModes[kViewCells] = {ViewMode::Falling, ViewMode::Sheet, ViewMode::Both};
    return kModes[cell < 0 ? 0 : (cell >= kViewCells ? kViewCells - 1 : cell)];
}
inline constexpr int view_cell_of(ViewMode m) {
    for (int i = 0; i < kViewCells; ++i) {
        if (view_mode_of_cell(i) == m) {
            return i;
        }
    }
    return 0;
}
inline constexpr const char* view_cell_label(int cell) {
    constexpr const char* kLabels[kViewCells] = {"Falling", "Sheet", "Both"};
    return kLabels[cell < 0 ? 0 : (cell >= kViewCells ? kViewCells - 1 : cell)];
}

// The cells of Settings > View's row that are refused.
inline constexpr std::uint32_t view_refused_mask(bool no_sheet) {
    std::uint32_t mask = 0;
    for (int i = 0; i < kViewCells; ++i) {
        if (view_refused(view_mode_of_cell(i), no_sheet)) {
            mask |= 1u << static_cast<unsigned>(i);
        }
    }
    return mask;
}

// What the toast says for a refused tap: the same words on the side button's toast and the row.
inline std::string view_refusal(std::string_view reason) {
    return "No sheet music for this song: " + std::string(reason);
}

enum class ViewTap : std::uint8_t {
    Set,     // the setting changed
    Same,    // it already was that choice
    Refused, // the song has no sheet music: nothing changed
};

// A tap on `c`: moves `mode` there unless the song refuses it.
inline constexpr ViewTap tap_view_choice(ViewMode& mode, ViewChoice c, bool no_sheet) {
    const ViewMode want = view_mode_of(c);
    if (view_refused(want, no_sheet)) {
        return ViewTap::Refused;
    }
    if (want == mode) {
        return ViewTap::Same;
    }
    mode = want;
    return ViewTap::Set;
}

} // namespace etude::app::ui
