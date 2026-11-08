// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_prefs.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

SongPrefEdit edit(std::uint64_t hash, SongPrefEdit::Field field, std::string text = {},
                  bool on = false) {
    SongPrefEdit e;
    e.hash = hash;
    e.field = field;
    e.text = std::move(text);
    e.on = on;
    return e;
}

} // namespace

TEST_CASE("song prefs: an edit changes its own field only") {
    SongPrefs p{"My name", "Satie", "Piano", false, 1000};
    apply_edit(p, edit(1, SongPrefEdit::Field::Favourite, {}, true));
    CHECK(p == SongPrefs{"My name", "Satie", "Piano", true, 1000});
    apply_edit(p, edit(1, SongPrefEdit::Field::Composer, "Erik Satie"));
    CHECK(p == SongPrefs{"My name", "Erik Satie", "Piano", true, 1000});
    apply_edit(p, edit(1, SongPrefEdit::Field::Title));
    CHECK(p == SongPrefs{"", "Erik Satie", "Piano", true, 1000});
}

TEST_CASE("song prefs: choosing a version sets its time and nothing else") {
    SongPrefs p{"My name", "Satie", "Piano", true, 1000};
    SongPrefEdit e = edit(1, SongPrefEdit::Field::Chosen);
    e.time_utc_s = 5000;
    apply_edit(p, e);
    CHECK(p.chosen_utc_s == 5000);
    CHECK(p.title == "My name");
    CHECK(p.favourite);
    CHECK(p.added_utc_s == 1000);
    // A time before 1970 is none.
    p.chosen_utc_s = -3;
    CHECK(sanitized(p).chosen_utc_s == 0);
    // Held before the store was read, it lands on the stored row and keeps its other fields.
    const std::unordered_map<std::uint64_t, SongPrefs> stored{{7, SongPrefs{"Name", "", "", true, 500}}};
    e.hash = 7;
    const auto rows = apply_edits(stored, std::vector<SongPrefEdit>{e});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].second.title == "Name");
    CHECK(rows[0].second.chosen_utc_s == 5000);
}

TEST_CASE("song prefs: edits held before the store was read keep what it holds") {
    // A favourite tapped while the history loads must not wipe the stored name and composer.
    const std::unordered_map<std::uint64_t, SongPrefs> stored{
        {7, SongPrefs{"Gymnopédie 1", "Erik Satie", "Classical", false, 500}},
        {8, SongPrefs{"", "", "", true, 600}}};
    const std::vector<SongPrefEdit> held{edit(7, SongPrefEdit::Field::Favourite, {}, true),
                                         edit(9, SongPrefEdit::Field::Title, "New one"),
                                         edit(7, SongPrefEdit::Field::Title, "Gymnopédie No. 1"),
                                         edit(8, SongPrefEdit::Field::Favourite, {}, false),
                                         edit(0, SongPrefEdit::Field::Title, "no hash")};
    const auto rows = apply_edits(stored, held);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].first == 7);
    CHECK(rows[0].second == SongPrefs{"Gymnopédie No. 1", "Erik Satie", "Classical", true, 500});
    CHECK(rows[1].first == 9);
    CHECK(rows[1].second == SongPrefs{"New one", "", "", false, 0});
    CHECK(rows[2].first == 8);
    CHECK(rows[2].second == SongPrefs{"", "", "", false, 600});
}
