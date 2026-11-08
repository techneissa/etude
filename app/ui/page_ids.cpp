// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_ids.hpp"

namespace etude::app::ui {

const char* page_title(Page p) {
    switch (p) {
    case Page::Notes:
        return "Notes";
    case Page::Colours:
        return "Effects";
    case Page::View:
        return "View";
    case Page::Practice:
        return "Practice";
    case Page::Input:
        return "Input";
    case Page::Configs:
        return "Configs";
    case Page::AddSongs:
        return "Add songs";
    case Page::About:
        return "About";
    }
    return "";
}

const char* page_caption(Page p) { return p == Page::AddSongs ? "Songs" : page_title(p); }

Page parent_of(Page p) { return p; }

std::optional<Page> parse_page_name(std::string_view name) {
    struct Entry {
        std::string_view name;
        Page page;
    };
    static constexpr Entry kNames[] = {
        {"notes", Page::Notes},
        {"effects", Page::Colours},
        {"colours", Page::Colours}, // the page's old name
        {"view", Page::View},
        {"practice", Page::Practice},
        {"input", Page::Input},
        {"configs", Page::Configs},
        {"presets", Page::Configs}, // the page's old name
        {"add-songs", Page::AddSongs},
        {"addsongs", Page::AddSongs},
        {"upload", Page::AddSongs},
        {"about", Page::About},
        {"updates", Page::About},
        // Folded into other pages.
        {"quick", Page::Notes},
        {"look", Page::Notes},
        {"names", Page::Notes},
        {"keyboard", Page::Input},
        {"metronome", Page::Practice},
        {"microphone", Page::Input},
    };
    for (const Entry& e : kNames) {
        if (e.name == name) {
            return e.page;
        }
    }
    return std::nullopt;
}

} // namespace etude::app::ui
