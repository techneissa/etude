// SPDX-License-Identifier: GPL-3.0-or-later
// The settings pages as plain ids, with no ImGui in them, so the settings catalogue and its tests
// (and `--panel`) can name a page without the kit. pages.hpp has the pages' drawing.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace etude::app::ui {

enum class Page : std::uint8_t {
    Notes,   // note style, block layout, note names, C markers
    Colours, // "Effects": the colours, then the hit effects, glow, trail and background
    View,
    Practice,
    Input,
    Configs,
    AddSongs, // the phone upload page, the network folder and the USB stick
    About,    // the version, the links and the updates
};

inline constexpr int kRailPages = 8; // the pages in the sheet's rail (kRail)

// The pages in the sheet's rail, in order. The song's sections and the practice list are not
// settings: they live on the section band on the practice screen.
inline constexpr std::array<Page, kRailPages> kRail{Page::Notes,    Page::Colours, Page::View,
                                                    Page::Practice, Page::Input,   Page::Configs,
                                                    Page::AddSongs, Page::About};

const char* page_title(Page p);
// The rail's caption under an icon, when the rail is too narrow for the title.
const char* page_caption(Page p);
// The page a sub-page's back button returns to; a rail page returns itself (every page is one
// today).
Page parent_of(Page p);
inline bool is_subpage(Page p) { return parent_of(p) != p; }

// The page a name given to `--panel` opens: the current names ("notes", "effects", "view",
// "practice", "input", "configs", "add-songs", "about") and the names of the pages that were folded
// into others, so scripts written for the old sheet keep working ("quick" and "look" and "names"
// open Notes, "colours" (the Effects page's old name) opens Effects, "keyboard" and "microphone"
// open Input, "metronome" opens Practice). Empty for any other text.
std::optional<Page> parse_page_name(std::string_view name);

} // namespace etude::app::ui
