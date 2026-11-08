// SPDX-License-Identifier: GPL-3.0-or-later
// Ui::open_panel: the screens, sheets and pages a PanelTab names (`--panel NAME`, a bar shortcut, a
// resume), each opened through the part of the Ui that owns it.
#include "../ui.hpp"

#include "editor.hpp"
#include "host_impl.hpp"
#include "pages.hpp"
#include "sheet.hpp"

#include "etude/medals.hpp"

namespace etude::app {

namespace {

using namespace ui;

Page page_for(PanelTab tab) {
    switch (tab) {
    case PanelTab::Notes:
        return Page::Notes;
    case PanelTab::Colours:
        return Page::Colours;
    case PanelTab::View:
        return Page::View;
    case PanelTab::Practice:
        return Page::Practice;
    case PanelTab::Input:
        return Page::Input;
    case PanelTab::Configs:
    case PanelTab::ConfigsCopy:
        return Page::Configs;
    case PanelTab::AddSongs:
        return Page::AddSongs;
    case PanelTab::About:
        return Page::About;
    case PanelTab::Sections:
    case PanelTab::Plan:
    case PanelTab::Editor:
    case PanelTab::Suggest:
    case PanelTab::Library:
    case PanelTab::KeyRing:
    case PanelTab::Learn:
    case PanelTab::LearnScales:
    case PanelTab::LearnChords:
    case PanelTab::LearnEar:
    case PanelTab::LearnMelodies:
    case PanelTab::Profiles:
    case PanelTab::ProfilesEdit:
    case PanelTab::ProfileNew:
    case PanelTab::ProfileKeys:
    case PanelTab::ProfileEditSheet:
    case PanelTab::ProfileEditFirst:
    case PanelTab::ProfileDelete:
    case PanelTab::ProfileDeleteKeys:
    case PanelTab::ProfileDeleteTyped:
    case PanelTab::Progress:
    case PanelTab::Household:
    case PanelTab::Medals:
    case PanelTab::MedalsLadder:
    case PanelTab::MedalSheet:
    case PanelTab::MedalTitles:
        break;
    }
    return Page::Notes;
}

} // namespace

void Ui::open_panel(PanelTab tab) {
    impl_->bar->close_popovers();
    if (tab == PanelTab::Sections || tab == PanelTab::Plan) {
        impl_->open = false;
        impl_->want_band = true;
        if (tab == PanelTab::Sections) {
            impl_->band.set_lane(true);
        } else {
            impl_->band.open_list();
        }
        return;
    }
    if (tab == PanelTab::Editor || tab == PanelTab::Suggest) {
        impl_->want_editor = true;
        impl_->editor_suggest = tab == PanelTab::Suggest;
        return;
    }
    if (tab == PanelTab::Library) {
        open_library();
        return;
    }
    if (tab == PanelTab::KeyRing) {
        impl_->key_ring_demo.open();
        return;
    }
    if (tab == PanelTab::MedalSheet || tab == PanelTab::MedalTitles) {
        impl_->medal_demo.open(tab == PanelTab::MedalSheet ? MedalDemo::Mode::Sheet
                                                           : MedalDemo::Mode::Titles);
        return;
    }
    if (is_learn_panel_tab(tab)) {
        open_learn_panel(tab);
        return;
    }
    if (tab == PanelTab::Progress || tab == PanelTab::Household) {
        open_progress(tab == PanelTab::Household);
        return;
    }
    if (tab == PanelTab::Medals || tab == PanelTab::MedalsLadder) {
        open_medals(tab == PanelTab::MedalsLadder ? static_cast<int>(etude::Achievement::Streak)
                                                  : -1);
        return;
    }
    if (tab == PanelTab::Profiles || tab == PanelTab::ProfilesEdit) {
        impl_->profile.open_panel(tab == PanelTab::ProfilesEdit);
        return;
    }
    if (tab == PanelTab::ProfileEditSheet || tab == PanelTab::ProfileEditFirst ||
        tab == PanelTab::ProfileDelete || tab == PanelTab::ProfileDeleteKeys ||
        tab == PanelTab::ProfileDeleteTyped) {
        impl_->profile.open_demo_sheet(
            tab == PanelTab::ProfileEditFirst     ? ProfileUi::Demo::EditFirst
            : tab == PanelTab::ProfileDelete      ? ProfileUi::Demo::DeleteLast
            : tab == PanelTab::ProfileDeleteKeys  ? ProfileUi::Demo::DeleteKeys
            : tab == PanelTab::ProfileDeleteTyped ? ProfileUi::Demo::DeleteTyped
                                                  : ProfileUi::Demo::EditLast);
        return;
    }
    if (tab == PanelTab::ProfileNew || tab == PanelTab::ProfileKeys) {
        impl_->profile.open_new_profile_sheet(tab == PanelTab::ProfileKeys);
        return;
    }
    if (tab == PanelTab::ConfigsCopy) {
        configs_page_ask_last();
    }
    impl_->open = true;
    impl_->sheet.open_at(page_for(tab));
}

} // namespace etude::app
