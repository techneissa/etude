// SPDX-License-Identifier: GPL-3.0-or-later
// Which screen the player is on, as the resume point names it, and opening one of those at a start
// (core resume_point.hpp lists the names).
#include "host_impl.hpp"

#include "page_ids.hpp"

#include "etude/resume_point.hpp"

namespace etude::app {

namespace {

const char* page_screen(ui::Page page) {
    switch (page) {
    case ui::Page::Notes:
        return "notes";
    case ui::Page::Colours:
        return "effects";
    case ui::Page::View:
        return "view";
    case ui::Page::Practice:
        return "practice";
    case ui::Page::Input:
        return "input";
    case ui::Page::Configs:
        return "presets";
    case ui::Page::AddSongs:
        return "add-songs";
    case ui::Page::About:
        return "about";
    }
    return "play";
}

} // namespace

const char* Ui::resume_screen() const {
    const Impl& im = *impl_;
    if (im.learn.is_open()) {
        return im.learn.resume_screen();
    }
    if (im.library.is_open()) {
        return "library";
    }
    if (im.open) {
        return page_screen(im.sheet.page());
    }
    return "play"; // the practice screen (the section editor and the cards on it are not screens of their own)
}

void Ui::open_resume_screen(std::string_view name) {
    if (!resume_screen_known(name) || name == "play") {
        return;
    }
    if (name == "library") {
        open_library();
        return;
    }
    if (name.rfind("learn", 0) == 0) {
        const std::string text(name);
        PanelTab tab = PanelTab::Learn;
        if (parse_panel_tab(text.c_str(), &tab)) {
            open_learn_panel(tab);
        }
        return;
    }
    const std::string text(name);
    PanelTab tab = PanelTab::Notes;
    if (parse_panel_tab(text.c_str(), &tab)) {
        open_panel(tab);
    }
}

} // namespace etude::app
