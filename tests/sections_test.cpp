// SPDX-License-Identifier: GPL-3.0-or-later
// sections: the first topics (split from sections_test.cpp).
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

// ---- Names

TEST_CASE("sections: a name keeps its words, loses control characters and extra blanks") {
    CHECK(sanitize_section_name("Intro") == "Intro");
    CHECK(sanitize_section_name("  Hard   run \t\n bars 12-13  ") == "Hard run bars 12-13");
    CHECK(sanitize_section_name("a\x01"
                                "b") == "a b");
    CHECK(sanitize_section_name("a\xC2\xA0"
                                "b") == "a b"); // no-break space
    CHECK(sanitize_section_name("a\xE2\x80\xA8"
                                "b") == "a b"); // line separator
    CHECK(sanitize_section_name("") == "");
    CHECK(sanitize_section_name(" \t ") == "");
}

TEST_CASE("sections: a name keeps accents and other scripts") {
    CHECK(sanitize_section_name("Sch\xC3\xB6n") == "Sch\xC3\xB6n");
    CHECK(sanitize_section_name("\xE3\x82\xA4\xE3\x83\xB3\xE3\x83\x88\xE3\x83\xAD") ==
          "\xE3\x82\xA4\xE3\x83\xB3\xE3\x83\x88\xE3\x83\xAD");
}

TEST_CASE("sections: invalid UTF-8 bytes in a name are dropped") {
    CHECK(sanitize_section_name("ab\xFFzz") == "abzz");
    CHECK(sanitize_section_name("ab\xC3") == "ab");       // cut off
    CHECK(sanitize_section_name("\xED\xA0\x80x") == "x"); // a surrogate
    CHECK(sanitize_section_name("\xC0\xAF"
                                "x") == "x");                 // overlong slash
    CHECK(sanitize_section_name("\xF4\x90\x80\x80y") == "y"); // above U+10FFFF
}

TEST_CASE("sections: bidi overrides and zero-width characters are removed from a name") {
    CHECK(sanitize_section_name("a\xE2\x80\xAE"
                                "b") == "ab"); // right-to-left override
    CHECK(sanitize_section_name("a\xE2\x80\x8B"
                                "b") == "ab"); // zero-width space
    CHECK(sanitize_section_name("\xEF\xBB\xBF"
                                "ab") == "ab"); // byte order mark
}

TEST_CASE("sections: a name is cut to the limit at a character boundary") {
    std::string long_ascii(200, 'x');
    CHECK(sanitize_section_name(long_ascii).size() == kMaxSectionNameBytes);

    std::string accents;
    for (int i = 0; i < 100; ++i) {
        accents += "\xC3\xA9"; // e acute, 2 bytes
    }
    std::string cut = sanitize_section_name(accents);
    CHECK(cut.size() == kMaxSectionNameBytes); // 32 whole characters
    CHECK(cut.size() % 2 == 0);

    std::string three(kMaxSectionNameBytes - 1, 'a');
    three += "\xE2\x82\xAC"; // euro sign would reach 66 bytes
    CHECK(sanitize_section_name(three).size() == kMaxSectionNameBytes - 1);
}

TEST_CASE("sections: sanitizing a name twice changes nothing") {
    for (std::string_view s : {"  a  b ", "x\xFF y", "\t",
                               "\xE2\x80\xAE"
                               "x"}) {
        std::string once = sanitize_section_name(s);
        CHECK(sanitize_section_name(once) == once);
    }
}

// ---- Markers

TEST_CASE("sections: a song starts with no sections") {
    SongSections s;
    CHECK(s.empty());
    CHECK(s.markers().empty());
    CHECK(s.valid(song16()));
}

TEST_CASE("sections: the first marker splits the song into two sections") {
    Song song = song16();
    SongSections s;
    s.song_hash = 7;
    EditResult r = s.add_marker(song, 16.0);
    REQUIRE(r.ok);
    REQUIRE(s.sections.size() == 2);
    CHECK(s.sections[0].start_s == 0.0);
    CHECK(s.sections[0].end_s == 16.0);
    CHECK(s.sections[1].start_s == 16.0);
    CHECK(s.sections[1].end_s == 32.0);
    CHECK(r.id == s.sections[1].id);
    CHECK(s.sections[0].id != s.sections[1].id);
    CHECK(s.sections[0].id != kNoSection);
    CHECK(s.valid(song));
}

TEST_CASE("sections: a marker snaps to the nearest bar by default") {
    Song song = song16();
    SongSections s;
    REQUIRE(s.add_marker(song, 16.9).ok); // bar 9 starts at 16, bar 10 at 18
    CHECK(s.sections[0].end_s == 16.0);
    SongSections t;
    REQUIRE(t.add_marker(song, 17.1).ok);
    CHECK(t.sections[0].end_s == 18.0);
    CHECK(t.sections[0].snapped_to_bars);
    CHECK(t.sections[1].snapped_to_bars);
}

TEST_CASE("sections: a marker placed without snapping stays where it was put") {
    Song song = song16();
    SongSections s;
    REQUIRE(s.add_marker(song, 16.9, false).ok);
    CHECK(s.sections[0].end_s == 16.9);
    CHECK_FALSE(s.sections[0].snapped_to_bars);
    CHECK_FALSE(s.sections[1].snapped_to_bars);
    CHECK(s.sections[0].first_measure == 1);
    CHECK(s.sections[0].last_measure == 9);
    CHECK(s.sections[1].first_measure == 9);
    CHECK(s.sections[1].last_measure == 16);
    CHECK(s.valid(song));
}

TEST_CASE("sections: the first and last measure of snapped sections are exact") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0, 24.0});
    REQUIRE(s.sections.size() == 3);
    CHECK(s.sections[0].first_measure == 1);
    CHECK(s.sections[0].last_measure == 8);
    CHECK(s.sections[1].first_measure == 9);
    CHECK(s.sections[1].last_measure == 12);
    CHECK(s.sections[2].first_measure == 13);
    CHECK(s.sections[2].last_measure == 16);
}

TEST_CASE("sections: markers come back as the boundaries between sections") {
    Song song = song16();
    SongSections s = with_markers(song, {24.0, 8.0, 16.0});
    CHECK(s.markers() == std::vector<double>{8.0, 16.0, 24.0});
    REQUIRE(s.sections.size() == 4);
    CHECK(s.sections[1].start_s == 8.0);
}

TEST_CASE("sections: a later marker splits the section it lands in and keeps the left id") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SectionId left = s.sections[0].id;
    SectionId right = s.sections[1].id;
    EditResult r = s.add_marker(song, 20.0);
    REQUIRE(r.ok);
    REQUIRE(s.sections.size() == 3);
    CHECK(s.sections[0].id == left);
    CHECK(s.sections[1].id == right);
    CHECK(s.sections[1].end_s == 20.0);
    CHECK(s.sections[2].start_s == 20.0);
    CHECK(s.sections[2].id == r.id);
    CHECK(r.id != left);
    CHECK(r.id != right);
}

TEST_CASE("sections: a marker on an existing marker, the start or the end is refused") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SongSections before = s;
    EditResult r = s.add_marker(song, 16.4); // snaps to 16
    CHECK_FALSE(r.ok);
    CHECK(r.error == EditError::AtMarker);
    CHECK(s.add_marker(song, 0.5).error == EditError::AtMarker);  // snaps to 0
    CHECK(s.add_marker(song, 31.5).error == EditError::AtMarker); // snaps to the end
    CHECK(s.add_marker(song, 99.0).error == EditError::AtMarker); // clamps to the end
    CHECK(s == before);
}

TEST_CASE("sections: a marker that would leave a piece shorter than a minimum loop is refused") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SongSections before = s;
    EditResult r = s.add_marker(song, 16.05, false);
    CHECK_FALSE(r.ok);
    CHECK(r.error == EditError::TooShort);
    CHECK(s.add_marker(song, 31.95, false).error == EditError::TooShort);
    CHECK(s == before);
    CHECK(s.add_marker(song, 16.0 + kMinSectionSeconds, false).ok);
}

TEST_CASE("sections: NaN and infinite times are handled") {
    Song song = song16();
    SongSections s;
    CHECK(s.add_marker(song, std::nan("")).error == EditError::OutOfRange);
    CHECK(s.empty());
    // Infinity clamps to the song's end, which is a boundary.
    CHECK(s.add_marker(song, std::numeric_limits<double>::infinity()).error == EditError::AtMarker);
    CHECK(s.empty());
}

TEST_CASE("sections: a song too short for two sections takes no marker") {
    Song tiny;
    tiny.duration_s = 0.15;
    SongSections s;
    CHECK(s.add_marker(tiny, 0.075, false).error == EditError::TooShort);
    Song none;
    CHECK(s.add_marker(none, 0.0, false).error == EditError::AtMarker);
    CHECK(s.empty());
}

TEST_CASE("sections: a song with no bars takes unsnapped markers") {
    Song song;
    song.duration_s = 40.0;
    SongSections s;
    REQUIRE(s.add_marker(song, 12.3).ok);
    CHECK(s.sections[0].end_s == 12.3);
    CHECK_FALSE(s.sections[0].snapped_to_bars);
    CHECK(s.sections[0].name == "Section 1");
    CHECK(s.sections[1].name == "Section 2");
    CHECK(s.valid(song));
}

TEST_CASE("sections: no more than the limit of sections") {
    Song song = bars_song(100);
    SongSections s;
    for (std::size_t i = 1; i < kMaxSectionsPerSong; ++i) {
        REQUIRE(s.add_marker(song, 2.0 * static_cast<double>(i)).ok);
    }
    CHECK(s.sections.size() == kMaxSectionsPerSong);
    SongSections before = s;
    EditResult r = s.add_marker(song, 150.0);
    CHECK(r.error == EditError::TooMany);
    CHECK(s == before);
    CHECK(s.valid(song));
}

// ---- Default names

TEST_CASE("sections: snapped sections are named by their bars") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0, 24.0});
    CHECK(names(s) == std::vector<std::string>{"Bars 1-8", "Bars 9-12", "Bars 13-16"});
}

TEST_CASE("sections: a one-bar section is named Bar N") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0, 18.0});
    CHECK(s.sections[1].name == "Bar 9");
}

TEST_CASE("sections: unsnapped sections are named Section N by place") {
    Song song = song16();
    SongSections s;
    REQUIRE(s.add_marker(song, 10.5, false).ok);
    REQUIRE(s.add_marker(song, 20.5, false).ok);
    CHECK(names(s) == std::vector<std::string>{"Section 1", "Section 2", "Section 3"});
}

TEST_CASE("sections: default names follow the section when it changes") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    CHECK(names(s) == std::vector<std::string>{"Bars 1-8", "Bars 9-16"});
    REQUIRE(s.add_marker(song, 20.0).ok);
    CHECK(names(s) == std::vector<std::string>{"Bars 1-8", "Bars 9-10", "Bars 11-16"});
}

// ---- Rename

TEST_CASE("sections: renaming sets a custom name that survives later edits") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SectionId id = s.sections[1].id;
    EditResult r = s.rename(id, "  Chorus ");
    REQUIRE(r.ok);
    CHECK(r.id == id);
    CHECK(s.find(id)->name == "Chorus");
    CHECK(s.find(id)->custom_name);
    REQUIRE(s.add_marker(song, 24.0).ok);
    CHECK(s.find(id)->name == "Chorus");
    CHECK(s.sections[2].name == "Bars 13-16");
}

TEST_CASE("sections: renaming cleans the text") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SectionId id = s.sections[0].id;
    REQUIRE(s.rename(id, "a\xFF\tb").ok);
    CHECK(s.find(id)->name == "a b");
    REQUIRE(s.rename(id, std::string(300, 'z')).ok);
    CHECK(s.find(id)->name.size() == kMaxSectionNameBytes);
}

TEST_CASE("sections: an empty name returns to the default name") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    SectionId id = s.sections[1].id;
    REQUIRE(s.rename(id, "Chorus").ok);
    REQUIRE(s.rename(id, " \t").ok);
    CHECK(s.find(id)->name == "Bars 9-16");
    CHECK_FALSE(s.find(id)->custom_name);
}

TEST_CASE("sections: renaming an unknown section fails") {
    SongSections s;
    EditResult r = s.rename(5, "x");
    CHECK_FALSE(r.ok);
    CHECK(r.error == EditError::NotFound);
}

TEST_CASE("sections: a section may be named like another") {
    Song song = song16();
    SongSections s = with_markers(song, {16.0});
    REQUIRE(s.rename(s.sections[0].id, "Verse").ok);
    REQUIRE(s.rename(s.sections[1].id, "Verse").ok);
    CHECK(names(s) == std::vector<std::string>{"Verse", "Verse"});
}
