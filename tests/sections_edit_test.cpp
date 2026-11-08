// SPDX-License-Identifier: GPL-3.0-or-later
// sections: edit (split from sections_test.cpp).
#include "etude/sections.hpp"
#include "section_fixtures.hpp"
#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <limits>
#include <string>

#include "sections_test_support.hpp"

using namespace etude;
using namespace etude::testing;
using namespace sections_test_support;

// ---- Lookup

TEST_CASE("sections: at() finds the section holding a time") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    CHECK(s.at(0.0)->id == s.sections[0].id);
    CHECK(s.at(15.99)->id == s.sections[0].id);
    CHECK(s.at(16.0)->id == s.sections[1].id);
    CHECK(s.at(32.0)->id == s.sections[1].id);
    CHECK(s.at(33.0) == nullptr);
    CHECK(s.at(-1.0) == nullptr);
    CHECK(s.at(std::nan("")) == nullptr);
}

TEST_CASE("sections: span_of gives the loop to set") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    LoopSpan span = s.span_of(s.sections[1].id);
    CHECK(span.ok);
    CHECK(span.start_s == 16.0);
    CHECK(span.end_s == 32.0);
    CHECK_FALSE(s.span_of(99).ok);
}

// ---- Split

TEST_CASE("sections: split cuts one section and keeps the left id and name") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SectionId left = s.sections[0].id;
    REQUIRE(s.rename(left, "Intro").ok);
    EditResult r = s.split(song, left, 8.0);
    REQUIRE(r.ok);
    REQUIRE(s.sections.size() == 3);
    CHECK(s.sections[0].id == left);
    CHECK(s.sections[0].name == "Intro");
    CHECK(s.sections[0].end_s == 8.0);
    CHECK(s.sections[1].id == r.id);
    CHECK(s.sections[1].start_s == 8.0);
    CHECK(s.sections[1].end_s == 16.0);
}

TEST_CASE("sections: split refuses a time outside the section") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SongSections before = s;
    CHECK(s.split(song, s.sections[0].id, 20.0).error == EditError::OutOfRange);
    CHECK(s.split(song, s.sections[0].id, 16.0).error == EditError::AtMarker);
    CHECK(s.split(song, 99, 4.0).error == EditError::NotFound);
    CHECK(s == before);
}

// ---- Remove a marker, merge, delete

TEST_CASE("sections: removing a marker merges its neighbours into the left one") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0});
    SectionId first = s.sections[0].id;
    SectionId middle = s.sections[1].id;
    REQUIRE(s.rename(first, "Intro").ok);
    EditResult r = s.remove_marker(song, 8.5);
    REQUIRE(r.ok);
    REQUIRE(s.sections.size() == 2);
    CHECK(r.id == first);
    CHECK(s.sections[0].id == first);
    CHECK(s.sections[0].name == "Intro");
    CHECK(s.sections[0].end_s == 16.0);
    CHECK(s.find(middle) == nullptr);
    CHECK(s.valid(song));
}

TEST_CASE("sections: the marker nearest the given time is the one removed") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0, 24.0});
    REQUIRE(s.remove_marker(song, 15.0).ok);
    CHECK(s.markers() == std::vector<double>{8.0, 24.0});
}

TEST_CASE("sections: with no marker there is nothing to remove") {
    Song song = song16();
    SongSections s;
    CHECK(s.remove_marker(song, 5.0).error == EditError::NotFound);
    REQUIRE(s.add_marker(song, 16.0).ok);
    REQUIRE(s.remove_marker(song, 16.0).ok);
    CHECK(s.sections.size() == 1);
    CHECK(s.sections[0].start_s == 0.0);
    CHECK(s.sections[0].end_s == 32.0);
    CHECK(s.remove_marker(song, 5.0).error == EditError::NotFound);
}

TEST_CASE("sections: merge joins neighbours into the earliest and keeps its name") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0, 24.0});
    SectionId a = s.sections[1].id;
    SectionId b = s.sections[2].id;
    REQUIRE(s.rename(a, "Verse").ok);
    std::array<SectionId, 2> pick{b, a}; // the order of the selection does not matter
    EditResult r = s.merge(song, pick);
    REQUIRE(r.ok);
    CHECK(r.id == a);
    REQUIRE(s.sections.size() == 3);
    CHECK(s.sections[1].id == a);
    CHECK(s.sections[1].name == "Verse");
    CHECK(s.sections[1].start_s == 8.0);
    CHECK(s.sections[1].end_s == 24.0);
    CHECK(s.find(b) == nullptr);
    CHECK(s.valid(song));
}

TEST_CASE("sections: merge of three updates the default name") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0, 24.0});
    std::array<SectionId, 3> pick{s.sections[0].id, s.sections[1].id, s.sections[2].id};
    REQUIRE(s.merge(song, pick).ok);
    CHECK(names(s) == std::vector<std::string>{"Bars 1-12", "Bars 13-16"});
}

TEST_CASE("sections: merge refuses sections that are not neighbours") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0, 24.0});
    SongSections before = s;
    std::array<SectionId, 2> gap{s.sections[0].id, s.sections[2].id};
    CHECK(s.merge(song, gap).error == EditError::NotAdjacent);
    std::array<SectionId, 1> one{s.sections[0].id};
    CHECK(s.merge(song, one).error == EditError::NotEnough);
    std::array<SectionId, 2> same{s.sections[0].id, s.sections[0].id};
    CHECK(s.merge(song, same).error == EditError::NotEnough);
    std::array<SectionId, 2> unknown{s.sections[0].id, 99};
    CHECK(s.merge(song, unknown).error == EditError::NotFound);
    CHECK(s == before);
}

TEST_CASE("sections: deleting a section gives its time to the one before it") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0, 16.0});
    SectionId first = s.sections[0].id;
    SectionId middle = s.sections[1].id;
    EditResult r = s.erase(song, middle);
    REQUIRE(r.ok);
    CHECK(r.id == first);
    REQUIRE(s.sections.size() == 2);
    CHECK(s.sections[0].end_s == 16.0);
    CHECK(s.find(middle) == nullptr);
    CHECK(s.valid(song));
}

TEST_CASE("sections: deleting the first section gives its time to the next one") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0});
    SectionId second = s.sections[1].id;
    EditResult r = s.erase(song, s.sections[0].id);
    REQUIRE(r.ok);
    CHECK(r.id == second);
    REQUIRE(s.sections.size() == 1);
    CHECK(s.sections[0].start_s == 0.0);
    CHECK(s.sections[0].end_s == 32.0);
}

TEST_CASE("sections: deleting the only section leaves none") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0});
    REQUIRE(s.erase(song, s.sections[0].id).ok);
    EditResult r = s.erase(song, s.sections[0].id);
    REQUIRE(r.ok);
    CHECK(r.id == kNoSection);
    CHECK(s.empty());
    CHECK(s.valid(song));
    CHECK(s.erase(song, 1).error == EditError::NotFound);
}

TEST_CASE("sections: ids are not reused after a delete") {
    Song song = song16();
    SongSections s = with_markers(song, {8.0});
    SectionId gone = s.sections[1].id;
    REQUIRE(s.erase(song, gone).ok);
    EditResult r = s.add_marker(song, 8.0);
    REQUIRE(r.ok);
    CHECK(r.id != gone);
    CHECK(r.id > gone);
}

// ---- Save an A-B loop as a section

TEST_CASE("sections: add_range makes a named section of the range") {
    Song song = song16();
    SongSections s;
    EditResult r = s.add_range(song, 22.0, 26.0, true, "Hard run");
    REQUIRE(r.ok);
    REQUIRE(s.sections.size() == 3);
    CHECK(s.sections[1].id == r.id);
    CHECK(s.sections[1].name == "Hard run");
    CHECK(s.sections[1].start_s == 22.0);
    CHECK(s.sections[1].end_s == 26.0);
    CHECK(s.sections[1].first_measure == 12);
    CHECK(s.sections[1].last_measure == 13);
    CHECK(s.valid(song));
}

TEST_CASE("sections: add_range snaps both ends to bars") {
    Song song = song16();
    SongSections s;
    EditResult r = s.add_range(song, 22.4, 25.7, true);
    REQUIRE(r.ok);
    CHECK(s.find(r.id)->start_s == 22.0);
    CHECK(s.find(r.id)->end_s == 26.0);
    CHECK(s.find(r.id)->name == "Bars 12-13");
}

TEST_CASE("sections: add_range accepts the ends in either order") {
    Song song = song16();
    SongSections s;
    EditResult r = s.add_range(song, 26.0, 22.0);
    REQUIRE(r.ok);
    CHECK(s.find(r.id)->start_s == 22.0);
}

TEST_CASE("sections: add_range at the start of the song needs one marker") {
    Song song = song16();
    SongSections s;
    EditResult r = s.add_range(song, 0.0, 8.0, true, "Intro");
    REQUIRE(r.ok);
    CHECK(s.sections.size() == 2);
    CHECK(s.sections[0].name == "Intro");
}

TEST_CASE("sections: add_range inside one existing section splits it twice") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    REQUIRE(s.add_range(song, 20.0, 24.0).ok);
    CHECK(s.markers() == std::vector<double>{16.0, 20.0, 24.0});
}

TEST_CASE("sections: add_range equal to a section just names it") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    EditResult r = s.add_range(song, 16.0, 32.0, true, "Chorus");
    REQUIRE(r.ok);
    CHECK(r.id == s.sections[1].id);
    CHECK(s.sections.size() == 2);
    CHECK(s.sections[1].name == "Chorus");
}

TEST_CASE("sections: add_range across a marker is refused") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SongSections before = s;
    EditResult r = s.add_range(song, 12.0, 20.0);
    CHECK_FALSE(r.ok);
    CHECK(r.error == EditError::Overlaps);
    CHECK(s == before);
}

TEST_CASE("sections: add_range that is too short or past the limit is refused") {
    Song song = song16();
    SongSections s;
    CHECK(s.add_range(song, 10.0, 10.05, false).error == EditError::TooShort);
    CHECK(s.add_range(song, 10.0, 10.0).error == EditError::TooShort);
    CHECK(s.add_range(song, std::nan(""), 10.0).error == EditError::OutOfRange);
    CHECK(s.empty());
}

TEST_CASE("sections: add_range does not make a section when it would exceed the limit") {
    Song song = bars_song(100);
    SongSections s;
    for (std::size_t i = 1; i < kMaxSectionsPerSong - 1; ++i) {
        REQUIRE(s.add_marker(song, 2.0 * static_cast<double>(i)).ok);
    }
    REQUIRE(s.sections.size() == kMaxSectionsPerSong - 1);
    SongSections before = s;
    // Inside the last section: two markers, three pieces, one too many.
    CHECK(s.add_range(song, 150.0, 160.0).error == EditError::TooMany);
    CHECK(s == before);
}
