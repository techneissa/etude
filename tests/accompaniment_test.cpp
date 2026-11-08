// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_library.hpp"

#include "etude/accompaniment.hpp"
#include "etude/instrument.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace etude;

namespace {

Part part(std::uint16_t track, std::uint8_t channel, const std::string& name, bool accompaniment,
          int program = 0, bool has_program = false, bool drum = false) {
    Part p;
    p.track = track;
    p.channel = channel;
    p.name = name;
    p.accompaniment = accompaniment;
    p.program = static_cast<std::uint8_t>(program);
    p.has_program = has_program;
    p.drum = drum;
    p.note_count = 10;
    return p;
}

Song song_of(std::vector<Part> parts) {
    Song s;
    s.parts = std::move(parts);
    return s;
}

} // namespace

TEST_CASE("accompaniment: the parts are the ones that are not a hand, never drums") {
    const Song s = song_of({part(0, 0, "Piano", false), part(1, 1, "Violin", true),
                            part(2, 2, "Voice", true), part(3, 9, "Drums", false, 0, false, true)});
    const std::vector<AccompanimentPart> parts = accompaniment_parts(s);
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].name == "Violin");
    CHECK(parts[0].index == 1);
    CHECK(parts[0].key == part_key(1, 1));
    CHECK_FALSE(parts[0].voice);
    CHECK(parts[1].name == "Voice");
    CHECK(parts[1].voice);
    CHECK(accompaniment_parts(song_of({part(0, 0, "Piano", false)})).empty());
}

TEST_CASE("accompaniment: a part without a name takes its instrument or a number, and a repeated "
          "name is numbered") {
    Part a = part(1, 1, "", true);
    a.instrument_name = "  Flute ";
    Part b = part(2, 2, "", true);
    const std::vector<AccompanimentPart> parts = accompaniment_parts(
        song_of({a, b, part(3, 3, "Strings", true), part(4, 4, "Strings", true),
                 part(5, 5, "Strings", true)}));
    REQUIRE(parts.size() == 5);
    CHECK(parts[0].name == "Flute");
    CHECK(parts[1].name == "Part 2");
    CHECK(parts[2].name == "Strings");
    CHECK(parts[3].name == "Strings 2");
    CHECK(parts[4].name == "Strings 3");
}

TEST_CASE("accompaniment: no more than kMaxAccompanimentParts are listed") {
    std::vector<Part> many;
    for (int i = 0; i < 40; ++i) {
        many.push_back(part(static_cast<std::uint16_t>(i), 0, "P" + std::to_string(i), true));
    }
    CHECK(accompaniment_parts(song_of(many)).size() == kMaxAccompanimentParts);
}

TEST_CASE("accompaniment: a voice part is told by its name or, with no other name, its program") {
    CHECK(is_voice_part(part(0, 0, "Soprano", true)));
    CHECK(is_voice_part(part(0, 0, "Lead vocals", true)));
    CHECK(is_voice_part(part(0, 0, "Track 2", true, 53, true))); // voice oohs
    CHECK(is_voice_part(part(0, 0, "Chor", true)));
    CHECK_FALSE(is_voice_part(part(0, 0, "Violin", true)));
    CHECK_FALSE(is_voice_part(part(0, 0, "Violin", true, 53, true))); // the name wins
    CHECK_FALSE(is_voice_part(part(0, 0, "Track 2", true, 40, true)));
    CHECK_FALSE(is_voice_part(part(0, 0, "Track 2", true)));
    Part by_instrument = part(0, 0, "Track 2", true);
    by_instrument.instrument_name = "Choir Aahs";
    CHECK(is_voice_part(by_instrument));
}

TEST_CASE("accompaniment: the button's state is on, partial or off") {
    CHECK(accompaniment_state(0, 0) == AccompanimentState::On);
    CHECK(accompaniment_state(3, 0) == AccompanimentState::On);
    CHECK(accompaniment_state(3, 1) == AccompanimentState::Partial);
    CHECK(accompaniment_state(3, 3) == AccompanimentState::Off);
    const Song s = song_of({part(1, 1, "A", true), part(2, 2, "B", true)});
    const std::vector<AccompanimentPart> parts = accompaniment_parts(s);
    CHECK(accompaniment_state(parts, {}) == AccompanimentState::On);
    CHECK(accompaniment_state(parts, {part_key(1, 1)}) == AccompanimentState::Partial);
    CHECK(accompaniment_state(parts, all_keys(parts)) == AccompanimentState::Off);
    // A key the song has no part for mutes nothing.
    CHECK(accompaniment_state(parts, {part_key(9, 9)}) == AccompanimentState::On);
}

TEST_CASE("accompaniment: a tap mutes all when all sound, else lets all sound") {
    const std::vector<AccompanimentPart> parts =
        accompaniment_parts(song_of({part(1, 1, "A", true), part(2, 2, "B", true)}));
    CHECK(after_button_tap(parts, {}) == all_keys(parts));
    CHECK(after_button_tap(parts, all_keys(parts)).empty());
    CHECK(after_button_tap(parts, {part_key(2, 2)}).empty()); // some muted: all sound
}

TEST_CASE("accompaniment: a part toggles on its own, and keys stay sorted and distinct") {
    std::vector<std::uint32_t> muted;
    muted = after_part_toggle(muted, part_key(2, 2));
    muted = after_part_toggle(muted, part_key(1, 1));
    CHECK(muted == std::vector<std::uint32_t>{part_key(1, 1), part_key(2, 2)});
    muted = after_part_toggle(muted, part_key(2, 2));
    CHECK(muted == std::vector<std::uint32_t>{part_key(1, 1)});
    const std::vector<AccompanimentPart> parts =
        accompaniment_parts(song_of({part(1, 1, "A", true)}));
    CHECK(muted_keys_in(parts, {part_key(1, 1), part_key(1, 1), part_key(7, 7)}) ==
          std::vector<std::uint32_t>{part_key(1, 1)});
}

TEST_CASE("accompaniment: the real files give their voice and instrument parts") {
    const std::string dir = ETUDE_TEST_DATA_DIR;
    // A song for voice and piano: one part, and it is a voice (the button shows the voice glyph).
    const app::SongFileLoad lied =
        app::load_song_file(dir + "/beethoven-op48-4-ehre-gottes-musescore-export.mid");
    REQUIRE(lied.ok);
    const std::vector<AccompanimentPart> voice = accompaniment_parts(lied.song.song);
    REQUIRE(voice.size() == 1);
    CHECK(voice[0].voice);

    // The trio: a violin and a choir over the piano, so two parts and not all of them voices.
    const app::SongFileLoad trio = app::load_song_file(dir + "/accompaniment-trio.mid");
    REQUIRE(trio.ok);
    const std::vector<AccompanimentPart> parts = accompaniment_parts(trio.song.song);
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].name == "Violin");
    CHECK_FALSE(parts[0].voice);
    CHECK(parts[1].name == "Choir");
    CHECK(parts[1].voice);

    // A song with no other instrument has none.
    const app::SongFileLoad satie = app::load_song_file(dir + "/satie-gymnopedie-1.mid");
    REQUIRE(satie.ok);
    CHECK(accompaniment_parts(satie.song.song).empty());
}

TEST_CASE("accompaniment: no two rows share a name, however the repeats number") {
    // "Violin", "Violin" and "Violin 2": the second cannot take "Violin 2".
    const std::vector<AccompanimentPart> parts = accompaniment_parts(song_of(
        {part(1, 1, "Violin", true), part(2, 2, "Violin", true), part(3, 3, "Violin 2", true),
         part(4, 4, "Violin", true), part(5, 5, "Viola", true)}));
    REQUIRE(parts.size() == 5);
    std::vector<std::string> names;
    for (const AccompanimentPart& p : parts) {
        names.push_back(p.name);
    }
    CHECK(names == std::vector<std::string>{"Violin", "Violin 3", "Violin 2", "Violin 4", "Viola"});
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}
