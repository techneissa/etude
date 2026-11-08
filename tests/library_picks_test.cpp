// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_picks.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

LibraryItem song(std::string relative, std::uint64_t hash, std::string title) {
    LibraryItem item;
    item.relative = std::move(relative);
    item.stamp = hash * 7 + 1;
    item.state = EntryState::Loaded;
    item.entry.hash = hash;
    item.entry.title = std::move(title);
    item.entry.difficulty = 2;
    item.entry.duration_s = 60.0;
    item.entry.right_notes = 20;
    return item;
}

std::vector<std::string> titles(const LibraryCatalogue& cat, const std::vector<std::size_t>& at) {
    std::vector<std::string> out;
    for (const std::size_t i : at) {
        out.push_back(cat.song(i).title);
    }
    return out;
}

} // namespace

TEST_CASE("picks: an action is for the picked songs shown, not those a search or place hides") {
    LibraryState library;
    library.items = {song("Classical/fur_elise.mid", 1, "Für Elise"),
                     song("Classical/moonlight.mid", 2, "Moonlight Sonata"),
                     song("Folk/greensleeves.abc", 3, "Greensleeves")};
    LibraryCatalogue cat;
    LibraryCatalogue::Prefs prefs;
    REQUIRE(cat.sync(library, {}, prefs));
    cat.update_view(20000, 1.0);
    LibraryPicks picks;
    for (const std::uint32_t i : cat.shown()) {
        picks.add(cat.song(i).relative);
    }
    std::vector<std::size_t> out;
    picks.shown(cat, cat.shown(), out);
    CHECK(out.size() == 3);
    // A search for "moon": one of the three shown, so one acted on; the picks are kept.
    cat.set_query("moon");
    cat.update_view(20000, 2.0);
    picks.shown(cat, cat.shown(), out);
    CHECK(titles(cat, out) == std::vector<std::string>{"Moonlight Sonata"});
    CHECK(picks.size() == 3);
    // The Folk collection: Greensleeves alone.
    cat.set_query("");
    LibraryScope folk;
    folk.kind = LibraryScope::Kind::Folder;
    folk.path = "Folk";
    cat.set_scope(folk);
    cat.update_view(20000, 3.0);
    picks.shown(cat, cat.shown(), out);
    CHECK(titles(cat, out) == std::vector<std::string>{"Greensleeves"});
    // Unpicked, it is not acted on even though it is shown.
    picks.toggle("Folk/greensleeves.abc");
    picks.shown(cat, cat.shown(), out);
    CHECK(out.empty());
    CHECK_FALSE(picks.has("Folk/greensleeves.abc"));
}

TEST_CASE("picks: a song the library no longer lists is let go") {
    LibraryState library;
    library.items = {song("a.mid", 1, "A"), song("b.mid", 2, "B")};
    LibraryCatalogue cat;
    LibraryCatalogue::Prefs prefs;
    REQUIRE(cat.sync(library, {}, prefs));
    LibraryPicks picks;
    picks.add("a.mid");
    picks.add("b.mid");
    // b.mid was deleted: the list comes back without it.
    library.items.pop_back();
    cat.library_changed();
    REQUIRE(cat.sync(library, {}, prefs));
    picks.prune(cat);
    CHECK(picks.size() == 1);
    CHECK(picks.has("a.mid"));
    CHECK_FALSE(picks.has("b.mid"));
}
