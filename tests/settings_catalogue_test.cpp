// SPDX-License-Identifier: GPL-3.0-or-later
// The settings catalogue (app/ui/settings_catalogue.hpp): each setting has one home, a sweep that
// draws a setting on two pages, on a page that is not its home, or on none is caught, and the
// names `--panel` takes still open a page after the pages were merged.
#include "ui/settings_catalogue.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace etude::app::ui;

namespace {

// What a sweep draws when every setting is where it lives: on its page once, the settings of the
// bar on the bar.
std::vector<Drawn> everything_at_home() {
    std::vector<Drawn> out;
    for (const CatalogueEntry& e : catalogue()) {
        if (e.home.in_library) {
            out.push_back({false, Page::Notes, e.id, true});
        } else if (e.home.on_bar) {
            out.push_back({true, Page::Notes, e.id});
        } else {
            out.push_back({false, e.home.page, e.id, false, std::max(0, fixed_column(e.id))});
        }
    }
    return out;
}

bool mentions(const std::vector<std::string>& problems, const std::string& text) {
    return std::any_of(problems.begin(), problems.end(),
                       [&](const std::string& p) { return p.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("catalogue: one entry per setting, in order, each with a name") {
    std::set<std::string> names;
    std::size_t i = 0;
    for (const CatalogueEntry& e : catalogue()) {
        CHECK(static_cast<std::size_t>(e.id) == i++);
        CHECK_FALSE(e.name.empty());
        CHECK(names.insert(std::string(e.name)).second);
    }
    CHECK(i == kSettingCount);
}

TEST_CASE("catalogue: every rail page holds settings") {
    for (const Page p : kRail) {
        if (p == Page::AddSongs) {
            continue; // its switches start a server and the file keeps no setting of them
        }
        CHECK_FALSE(settings_on(p).empty());
    }
}

TEST_CASE("catalogue: settings live where the rules put them") {
    const auto home = [](Setting s) { return entry_of(s).home; };
    // One copy each: high contrast, the saved configs and the note style are on one page only.
    CHECK(home(Setting::HighContrast).page == Page::Colours);
    CHECK(home(Setting::Configs).page == Page::Configs);
    CHECK(home(Setting::NoteStyle).page == Page::Notes);
    CHECK(home(Setting::KeepBarShown).page == Page::View);
    CHECK(home(Setting::BandDifficulty).page == Page::View);
    CHECK_FALSE(entry_of(Setting::BandDifficulty).bar_shortcut); // one row, on its page only
    // The metronome's switch and the count-in belong to the bar and the tempo popover, their
    // detailed settings to Practice.
    CHECK(home(Setting::MetronomeOn).on_bar);
    CHECK(home(Setting::CountIn).on_bar);
    CHECK(home(Setting::MetronomeVolume).page == Page::Practice);
    CHECK(home(Setting::WeightedScore).page == Page::Practice);
    CHECK_FALSE(entry_of(Setting::WeightedScore).bar_shortcut);
    // "Replays kept per song" is a history setting of the Practice page, saved as run_events_kept.
    CHECK(home(Setting::RunsKept).page == Page::Practice);
    CHECK(entry_of(Setting::RunsKept).name == "run_events_kept");
    CHECK_FALSE(entry_of(Setting::RunsKept).bar_shortcut);
    // What the folded pages held.
    CHECK(home(Setting::KeyboardProfile).page == Page::Input);
    CHECK(home(Setting::MicDevice).page == Page::Input);
    CHECK(home(Setting::TypingLetters).page == Page::Input);
    CHECK(home(Setting::Latency).page == Page::Input);
    CHECK(home(Setting::BlackKeyNames).page == Page::Notes);
}

TEST_CASE("sweep check: every setting drawn once on its home page passes") {
    CHECK(check_drawn(everything_at_home()).empty());
}

TEST_CASE("sweep check: a setting drawn on two pages fails") {
    std::vector<Drawn> drawn = everything_at_home();
    drawn.push_back({false, Page::Notes, Setting::HighContrast}); // also on Notes
    const std::vector<std::string> problems = check_drawn(drawn);
    CHECK(mentions(problems, "high_contrast is drawn on 2 pages"));
    CHECK(mentions(problems, "high_contrast is drawn on Notes, its home is Effects"));
}

TEST_CASE("sweep check: a setting drawn on a page other than its home fails") {
    std::vector<Drawn> drawn = everything_at_home();
    for (Drawn& d : drawn) {
        if (d.setting == Setting::Latency) {
            d.page = Page::Practice; // moved without updating the catalogue
        }
    }
    const std::vector<std::string> problems = check_drawn(drawn);
    CHECK(mentions(problems, "latency_correction_ms is drawn on Practice, its home is Input"));
}

TEST_CASE("sweep check: a setting drawn on no page fails") {
    std::vector<Drawn> drawn = everything_at_home();
    std::erase_if(drawn, [](const Drawn& d) { return d.setting == Setting::TrailFade; });
    const std::vector<std::string> problems = check_drawn(drawn);
    CHECK(problems.size() == 1);
    CHECK(mentions(problems, "trail_fade is drawn on no page"));
}

TEST_CASE("sweep check: a setting of the bar drawn on a page fails") {
    std::vector<Drawn> drawn = everything_at_home();
    drawn.push_back({false, Page::Practice, Setting::MetronomeOn});
    drawn.push_back({false, Page::Practice, Setting::CountIn});
    const std::vector<std::string> problems = check_drawn(drawn);
    CHECK(mentions(problems, "metronome lives on the bar but is drawn on Practice"));
    CHECK(mentions(problems, "count_in_bars lives on the bar but is drawn on Practice"));
}

TEST_CASE("sweep check: a setting of the bar that the bar never draws fails") {
    // The tempo popover's count-in row (or the metronome's switch) removed: the sweep sees nothing.
    for (const Setting gone : {Setting::CountIn, Setting::MetronomeOn}) {
        std::vector<Drawn> drawn = everything_at_home();
        std::erase_if(drawn, [&](const Drawn& d) { return d.setting == gone; });
        const std::vector<std::string> problems = check_drawn(drawn);
        CHECK(problems.size() == 1);
        CHECK(mentions(problems, "lives on the bar but the bar drew no row for it"));
    }
}

TEST_CASE("sweep check: the bar draws a page's setting only as a declared shortcut") {
    // The pin, the sheet music's two corner toggles and its Off, On, Full button flip a page's
    // setting; the look shortcuts open pages.
    std::vector<Setting> declared;
    for (const CatalogueEntry& e : catalogue()) {
        if (e.bar_shortcut) {
            declared.push_back(e.id);
            CHECK_FALSE(e.home.on_bar); // a shortcut flips a setting that has a page
        }
    }
    CHECK(declared == std::vector<Setting>{Setting::NamesOnSheet, Setting::SheetNoteColours,
                                           Setting::ViewMode, Setting::KeepBarShown,
                                           Setting::AccompanimentVolume});
    // A declared shortcut on the bar is fine, and the setting is still on its one page.
    std::vector<Drawn> drawn = everything_at_home();
    for (const Setting s : declared) {
        drawn.push_back({true, Page::Notes, s});
    }
    CHECK(check_drawn(drawn).empty());
    // A bar row for a setting that is not declared is a second copy.
    drawn.push_back({true, Page::Notes, Setting::HighContrast});
    const std::vector<std::string> problems = check_drawn(drawn);
    CHECK(problems.size() == 1);
    CHECK(
        mentions(problems, "high_contrast is drawn on the bar but is not a declared bar shortcut"));
}

TEST_CASE("sweep check: the library's order lives in the library and nowhere else") {
    for (const Setting s : {Setting::LibrarySort, Setting::LibraryGroup, Setting::LibraryFilter,
                            Setting::LibraryFacets, Setting::LibraryView}) {
        CHECK(entry_of(s).home.in_library);
    }
    // Not drawn by the library: caught.
    std::vector<Drawn> drawn = everything_at_home();
    std::erase_if(drawn, [](const Drawn& d) { return d.setting == Setting::LibraryGroup; });
    std::vector<std::string> problems = check_drawn(drawn);
    CHECK(problems.size() == 1);
    CHECK(mentions(problems, "library_group lives in the library but the library drew no row"));
    // Drawn on a page too, or a page's setting drawn in the library: caught.
    drawn = everything_at_home();
    drawn.push_back({false, Page::View, Setting::LibrarySort});
    drawn.push_back({false, Page::Notes, Setting::HighContrast, true});
    problems = check_drawn(drawn);
    CHECK(mentions(problems, "lives in the library but is drawn on View"));
    CHECK(mentions(problems, "high_contrast is drawn in the library but does not live there"));
}

TEST_CASE("sweep recording: rows are kept only while a sweep runs, on the page being drawn") {
    catalogue_sweep::set_page(Page::View);
    catalogue_sweep::drawn(Setting::KeySize); // not recording: dropped
    catalogue_sweep::start();
    catalogue_sweep::drawn(Setting::CountIn); // no page: drawn on the bar
    catalogue_sweep::set_page(Page::View);
    catalogue_sweep::drawn(Setting::KeySize);
    catalogue_sweep::set_page(Page::Input);
    catalogue_sweep::drawn(Setting::Latency);
    catalogue_sweep::clear_page();
    catalogue_sweep::drawn(Setting::MetronomeOn); // the page is done: the bar again
    catalogue_sweep::set_library(true);
    catalogue_sweep::drawn(Setting::LibraryFilter);
    catalogue_sweep::set_library(false);
    const std::vector<Drawn> drawn = catalogue_sweep::stop();
    REQUIRE(drawn.size() == 5);
    CHECK(drawn[4].in_library);
    CHECK(drawn[4].setting == Setting::LibraryFilter);
    CHECK(drawn[0].on_bar);
    CHECK(drawn[0].setting == Setting::CountIn);
    CHECK_FALSE(drawn[1].on_bar);
    CHECK(drawn[1].page == Page::View);
    CHECK(drawn[1].setting == Setting::KeySize);
    CHECK(drawn[2].page == Page::Input);
    CHECK(drawn[3].on_bar);
    CHECK(drawn[3].setting == Setting::MetronomeOn);
    CHECK_FALSE(catalogue_sweep::recording());
}

TEST_CASE("sweep check: the accompaniment rows sit in the Practice page's right column, under the "
          "metronome's") {
    CHECK(fixed_column(Setting::AccompanimentOn) == 1);
    CHECK(fixed_column(Setting::AccompanimentVolume) == 1);
    CHECK(fixed_column(Setting::MetronomePulse) == 1);
    CHECK(fixed_column(Setting::HitWindow) == -1);
    CHECK(check_drawn(everything_at_home()).empty());
    // The sweep records the column each row was drawn in: `next_column` is `Columns::next`.
    catalogue_sweep::start();
    catalogue_sweep::set_page(Page::Practice);
    catalogue_sweep::drawn(Setting::HitWindow);
    catalogue_sweep::next_column();
    catalogue_sweep::drawn(Setting::MetronomePulse);
    catalogue_sweep::drawn(Setting::AccompanimentOn);
    catalogue_sweep::set_page(Page::Input); // a new page starts at its first column
    catalogue_sweep::drawn(Setting::Latency);
    const std::vector<Drawn> drawn = catalogue_sweep::stop();
    REQUIRE(drawn.size() == 4);
    CHECK(drawn[0].column == 0);
    CHECK(drawn[1].column == 1);
    CHECK(drawn[2].column == 1);
    CHECK(drawn[3].column == 0);
    // Accompaniment in the left column, where it used to be, is a problem.
    std::vector<Drawn> wrong = everything_at_home();
    for (Drawn& d : wrong) {
        if (d.setting == Setting::AccompanimentVolume) {
            d.column = 0;
        }
    }
    CHECK(mentions(check_drawn(wrong), "column 1 of Practice"));
}

TEST_CASE("pages: the rail is Notes, Effects, View, Practice, Input, Configs, Add songs, About") {
    const std::vector<std::string> titles = [] {
        std::vector<std::string> t;
        for (const Page p : kRail) {
            t.push_back(page_title(p));
        }
        return t;
    }();
    CHECK(titles == std::vector<std::string>{"Notes", "Effects", "View", "Practice", "Input",
                                             "Configs", "Add songs", "About"});
    CHECK(kRail.size() == static_cast<std::size_t>(kRailPages));
    for (const Page p : kRail) {
        CHECK_FALSE(is_subpage(p));
        CHECK(std::string(page_caption(p)).size() <= 8); // fits under an icon in the narrow rail
    }
}

TEST_CASE("--panel: new names open their page and the old names map to the page that took over") {
    CHECK(parse_page_name("notes") == Page::Notes);
    CHECK(parse_page_name("colours") == Page::Colours);
    CHECK(parse_page_name("view") == Page::View);
    CHECK(parse_page_name("practice") == Page::Practice);
    // The song's sections and the practice list are on the section band, not in the settings
    // (`--panel sections` and `--panel plan` open the band, see parse_panel_tab).
    CHECK_FALSE(parse_page_name("sections").has_value());
    CHECK_FALSE(parse_page_name("plan").has_value());
    CHECK(parse_page_name("input") == Page::Input);
    CHECK(parse_page_name("configs") == Page::Configs);
    CHECK(parse_page_name("presets") == Page::Configs); // the old name still opens it
    CHECK(parse_page_name("add-songs") == Page::AddSongs);
    CHECK(parse_page_name("upload") == Page::AddSongs);
    // The old sheet's names.
    CHECK(parse_page_name("quick") == Page::Notes);
    CHECK(parse_page_name("look") == Page::Notes);
    CHECK(parse_page_name("names") == Page::Notes);
    CHECK(parse_page_name("effects") == Page::Colours);
    CHECK(parse_page_name("keyboard") == Page::Input);
    CHECK(parse_page_name("microphone") == Page::Input);
    CHECK(parse_page_name("metronome") == Page::Practice);
    CHECK_FALSE(parse_page_name("").has_value());
    CHECK_FALSE(parse_page_name("Notes").has_value());
    CHECK_FALSE(parse_page_name("nonsense").has_value());
}
