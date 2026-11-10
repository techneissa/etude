// SPDX-License-Identifier: GPL-3.0-or-later
// The colours a player can give the four Learn sections (Settings > View). Seven shades, each a
// card colour: its tab, the top of its cards and the keys it lights (`learn_key_colours`). Teal,
// pink, coral and violet are the sections' own colours, the defaults. Every shade keeps the
// contrast rules: clear of both default hand colours, of the unlit keys
// and of the Ear card's answers, readable on the dark card, and apart from the other shades. Two
// sections may share a shade. The choice is a `LearnPicks` in the look (`LookSettings::learn_picks`),
// so it is kept per profile with the rest of the look; everything that draws a section's colour asks
// `learn_section_colour` (learn_colours.hpp) and none names a shade.
#pragma once

#include "etude/color.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace etude {

enum class LearnSection : std::uint8_t { Scales, Chords, Ear, Melodies };
inline constexpr std::size_t kLearnSectionCount = 4;

// In the order the settings row shows them: around the colour wheel from the warm end.
enum class LearnShade : std::uint8_t { Coral, Lime, Jade, Teal, Violet, Magenta, Pink };
inline constexpr std::size_t kLearnShadeCount = 7;

struct LearnShadeInfo {
    std::string_view key;  // the stored value
    std::string_view name; // the words on the settings row
    RGBA colour;
};

inline constexpr std::array<LearnShadeInfo, kLearnShadeCount> kLearnShades{{
    {"coral", "Coral", {242, 119, 106, 255}},
    {"lime", "Lime", {150, 210, 90, 255}},
    {"jade", "Jade", {50, 190, 120, 255}},
    {"teal", "Teal", {46, 196, 182, 255}},
    {"violet", "Violet", {167, 139, 250, 255}},
    {"magenta", "Magenta", {220, 100, 215, 255}},
    {"pink", "Pink", {242, 122, 181, 255}},
}};

inline constexpr const LearnShadeInfo& learn_shade_info(LearnShade shade) {
    // A value outside the enum (never from a file: the parser takes only shade names) reads the first.
    const auto i = static_cast<std::size_t>(shade);
    return kLearnShades[i < kLearnShades.size() ? i : 0];
}

// A shade for each section, in `LearnSection` order.
using LearnPicks = std::array<LearnShade, kLearnSectionCount>;

// Scales teal, Chords pink, Ear coral, Melodies violet.
inline constexpr LearnPicks kLearnDefaultPicks{LearnShade::Teal, LearnShade::Pink, LearnShade::Coral,
                                               LearnShade::Violet};

// The Learn colours tabs (Settings > View) are the four sections in `LearnSection` order, one set of
// seven shades under the picked tab. A tab index outside the four reads the first.
inline constexpr std::size_t learn_tab_section(int tab) {
    return tab >= 0 && tab < static_cast<int>(kLearnSectionCount) ? static_cast<std::size_t>(tab) : 0;
}

// The shade picked for the section a tab shows.
inline constexpr LearnShade learn_tab_pick(const LearnPicks& picks, int tab) {
    return picks[learn_tab_section(tab)];
}

// `picks` after a tap on `shade` under `tab`: only that tab's section changes, the other three keep
// theirs.
inline constexpr LearnPicks learn_pick_on_tab(LearnPicks picks, int tab, LearnShade shade) {
    picks[learn_tab_section(tab)] = shade;
    return picks;
}

// Reset to default for one tab: its section goes back to its own default shade and the other three
// keep theirs. Returns the shade it had, for the toast's Undo.
inline constexpr LearnShade reset_learn_tab(LearnPicks& picks, int tab) {
    const std::size_t section = learn_tab_section(tab);
    const LearnShade prior = picks[section];
    picks[section] = kLearnDefaultPicks[section];
    return prior;
}

} // namespace etude
