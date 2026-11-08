// SPDX-License-Identifier: GPL-3.0-or-later
// The settings pages, built from the kit and editing the UiModel: the sheet (sheet.cpp) shows the
// ones in `kRail` in its rail, Notes first. Each page lays itself out for the width it gets, in one
// column or two. Each setting is drawn on one page only (settings_catalogue.hpp).
#pragma once

#include "kit.hpp"
#include "page_ids.hpp"
#include "settings_catalogue.hpp"
#include "sheet_layout.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace etude::app::ui {

// Draws the body of `p` at the cursor, as wide as the content region (`body`, the page area the
// sheet gives it). Returns the page to open when a row drills further, else `p`.
Page draw_page(Kit& k, Page p, UiModel& model, int columns, ImVec2 body);
// What a page adds to the sheet's header row, drawn after the title (the Effects page's
// Colours | Hit effects switch).
void draw_page_header(Kit& k, Page p, ImVec2 body, int columns);

const char* page_icon(Page p);

// The MIDI input and output rows of Settings > Input (pages_midi_ports.cpp).
void midi_port_rows(Kit& k, AppSettings& app);

// The Input page (pages_input.cpp).
void page_input(Kit& k, UiModel& model, int columns);

// The Configs page (pages_configs.cpp).
void page_configs(Kit& k, UiModel& model, int columns);
// Developer (--panel configs-copy): the page opens with its question asked for the last of the
// other profiles, once they are known.
void configs_page_ask_last();
// The icon audit leaves out what the Configs page's open question covers (from here on, in
// this frame), the sheet's header included: the sheet calls it before drawing that.
void configs_page_cover_audit();

// The Add songs page (pages_add_songs.cpp).
void page_add_songs(Kit& k, UiModel& model, int columns);

// The About page (pages_about.cpp) and the sheet that replaces its body for a link
// (pages_about_link.cpp): Back, the address's name, its QR code and the address in text.
void page_about(Kit& k, UiModel& model, int columns);
void draw_link_sheet(Kit& k, AboutPort& port, ImVec2 origin, float w, float body_h);
// The row of icon colours under the About page's Updates (pages_about_icons.cpp).
void about_icon_picker(Kit& k, UiModel& model);

// The View page's Learn colours (pages_learn_colours.cpp): the four sections' shades and a reset.
void learn_colour_rows(Kit& k, UiModel& model);
// The settings sheet was opened: Learn colours shows its first tab again.
void learn_colours_sheet_opened();

// The note style tiles (Notes page).
bool style_picker(Kit& k, LookSettings& look);
// Quick names for the look-naming dialog: one tap each, before the keyboard is wanted.
inline constexpr std::array<const char*, 6> kQuickNames{"My look", "Practice", "Bright",
                                                        "Night",   "Big keys", "Soft"};

} // namespace etude::app::ui
