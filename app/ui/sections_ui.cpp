// SPDX-License-Identifier: GPL-3.0-or-later
// What the section band, its lane and the section editor share: each section's colour and the
// dialog that names a section.
#include "sections_ui.hpp"

#include "icons.hpp"
#include "osk.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace etude::app::ui {

ImVec4 band_tint(std::size_t index) {
    static constexpr ImVec4 kTints[] = {
        {0.60f, 0.52f, 1.00f, 1.0f}, // violet
        {0.30f, 0.80f, 0.72f, 1.0f}, // teal
        {1.00f, 0.58f, 0.70f, 1.0f}, // rose
        {0.72f, 0.86f, 0.35f, 1.0f}, // lime
        {0.98f, 0.80f, 0.36f, 1.0f}, // gold
        {0.86f, 0.56f, 0.96f, 1.0f}, // orchid
    };
    return kTints[index % (sizeof kTints / sizeof kTints[0])];
}

// ---- The naming dialog

namespace {

SectionId g_name_id = kNoSection;
osk::NameDialog g_dialog{osk::Owner::SectionName, osk::kSectionNameRules};

constexpr std::array<const char*, 6> kQuickSectionNames{"Intro",  "Verse",     "Chorus",
                                                        "Bridge", "Hard part", "Outro"};

} // namespace

void open_section_rename(SectionId id, const std::string& current) {
    g_name_id = id;
    g_dialog.open(current, osk::by_touch(), true);
}

void section_rename_dialog(Kit& k) {
    osk::NameCard card;
    card.title = "Name this section";
    card.quick = kQuickSectionNames;
    card.own = "Default name";
    card.fill = col::kSurfaceHi;
    const osk::NameResult result = g_dialog.draw(k, "##section-name", card);
    if (k.sections == nullptr) {
        return;
    }
    if (result == osk::NameResult::Save) {
        k.sections->rename(g_name_id, g_dialog.text());
    } else if (result == osk::NameResult::Own) {
        k.sections->rename(g_name_id, std::string());
        g_dialog.close();
    }
}

} // namespace etude::app::ui
