// SPDX-License-Identifier: GPL-3.0-or-later
// The names the --panel option takes, and the panel each opens.
#include "../ui.hpp"

#include "page_ids.hpp"

#include <cstring>
#include <optional>
#include <utility>

namespace etude::app {

bool parse_panel_tab(const char* text, PanelTab* out) {
    static const std::pair<const char*, PanelTab> named_tabs[] = {
        {"editor", PanelTab::Editor},
        {"sections", PanelTab::Sections},
        {"plan", PanelTab::Plan},
        {"suggest", PanelTab::Suggest},
        {"library", PanelTab::Library},
        {"key-ring", PanelTab::KeyRing},
        {"learn", PanelTab::Learn},
        {"learn-scales", PanelTab::LearnScales},
        {"learn-chords", PanelTab::LearnChords},
        {"learn-ear", PanelTab::LearnEar},
        {"learn-melodies", PanelTab::LearnMelodies},
        {"configs-copy", PanelTab::ConfigsCopy},
        {"profiles", PanelTab::Profiles},
        {"profiles-edit", PanelTab::ProfilesEdit},
        {"profile-new", PanelTab::ProfileNew},
        {"profile-keys", PanelTab::ProfileKeys},
        {"profile-edit-sheet", PanelTab::ProfileEditSheet},
        {"profile-edit-first", PanelTab::ProfileEditFirst},
        {"profile-delete", PanelTab::ProfileDelete},
        {"profile-delete-keys", PanelTab::ProfileDeleteKeys},
        {"profile-delete-typed", PanelTab::ProfileDeleteTyped},
        {"progress", PanelTab::Progress},
        {"household", PanelTab::Household},
        {"medals", PanelTab::Medals},
        {"medals-ladder", PanelTab::MedalsLadder},
        {"medal-sheet", PanelTab::MedalSheet},
        {"medal-titles", PanelTab::MedalTitles},
    };
    for (const auto& [name, tab] : named_tabs) {
        if (std::strcmp(text, name) == 0) {
            *out = tab;
            return true;
        }
    }
    const std::optional<ui::Page> page = ui::parse_page_name(text);
    if (!page) {
        return false;
    }
    switch (*page) {
    case ui::Page::Notes:
        *out = PanelTab::Notes;
        break;
    case ui::Page::Colours:
        *out = PanelTab::Colours;
        break;
    case ui::Page::View:
        *out = PanelTab::View;
        break;
    case ui::Page::Practice:
        *out = PanelTab::Practice;
        break;
    case ui::Page::Input:
        *out = PanelTab::Input;
        break;
    case ui::Page::Configs:
        *out = PanelTab::Configs;
        break;
    case ui::Page::AddSongs:
        *out = PanelTab::AddSongs;
        break;
    case ui::Page::About:
        *out = PanelTab::About;
        break;
    }
    return true;
}

} // namespace etude::app
