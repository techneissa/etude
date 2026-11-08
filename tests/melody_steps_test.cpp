// SPDX-License-Identifier: GPL-3.0-or-later
// The step sort of the "From your library" row (core melody_steps.hpp), the list of songs a player
// added (melody_added.hpp), and that the cut-offs fit the bundled pieces they were calibrated on.
#include "melody_book.hpp"

#include "library_row.hpp"
#include "song_library.hpp"

#include "etude/melody_added.hpp"
#include "etude/melody_steps.hpp"

#include <doctest/doctest.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

struct Bundled {
    std::string file;
    int step = 0;
    MelodyStepFacts facts;
};

// Each bundled piece as the library holds it: the entry the scan makes of its file, taken into the
// catalogue, and the facts the app's own `step_facts` reads from there.
std::vector<Bundled> bundled() {
    const std::span<const unsigned char> text = embedded_melody_manifest();
    const MelodyManifest manifest =
        parse_melody_manifest({reinterpret_cast<const char*>(text.data()), text.size()});
    std::vector<Bundled> out;
    for (const MelodyFileEntry& file : manifest.files) {
        const std::span<const unsigned char> bytes = embedded_melody_file(file.file);
        const SongFileLoad load =
            load_song_bytes(std::span<const std::uint8_t>(bytes.data(), bytes.size()), file.file);
        REQUIRE(load.ok);
        LibraryState library;
        LibraryItem item;
        item.relative = file.file;
        item.stamp = 1;
        item.state = EntryState::Loaded;
        item.entry = load.entry;
        library.items.push_back(item);
        LibraryCatalogue catalogue;
        catalogue.library_changed();
        catalogue.prefs_changed();
        catalogue.history_changed();
        REQUIRE(catalogue.sync(library, {}, {}));
        Bundled b;
        b.file = file.file;
        b.step = file.step;
        b.facts = step_facts(catalogue.song(0));
        out.push_back(b);
    }
    return out;
}

} // namespace

TEST_CASE("steps: the bundled pieces land in their own step, but one") {
    // The real ratings, hands and ranges of the 14 pieces the cut-offs were set on. The library's
    // rating spans only 3 to 6 half stars here, so the keys split steps 1 from 2 and 3 from 4. Ode to
    // Joy (step 1 in the manifest) has the same rating and range as Amazing Grace (step 2), so no
    // cut-off can tell them apart: it lands in step 2.
    int misses = 0;
    for (const Bundled& b : bundled()) {
        CAPTURE(b.file);
        const int step = suggest_melody_step(b.facts);
        if (b.file == "ode-to-joy.mid") {
            CHECK(step == 2);
            ++misses;
            continue;
        }
        CHECK(step == b.step);
    }
    CHECK(misses == 1);
}

TEST_CASE("steps: the rating, the hands and the range decide, and a song that fits none has no step") {
    const auto step = [](int half, bool left, int left_keys, bool right, int right_keys, int bars = 24,
                         int notes = 40) {
        return suggest_melody_step({half, left, right, left_keys, right_keys, bars, notes});
    };
    // The right hand alone: a few keys is step 1, a wider range or a harder rating step 2.
    CHECK(step(3, false, 0, true, 10) == 1);
    CHECK(step(3, false, 0, true, 11) == 2);
    CHECK(step(4, false, 0, true, 8) == 2);
    CHECK(step(4, false, 0, true, 13) == 2);
    CHECK(step(4, false, 0, true, 14) == kMelodyNoStep);
    CHECK(step(5, false, 0, true, 8) == kMelodyNoStep);
    // Both hands: by rating, and the two hands' range between steps 3 and 4.
    CHECK(step(3, true, 14, true, 14) == 3);
    CHECK(step(4, true, 20, true, 20) == 3);
    CHECK(step(4, true, 21, true, 20) == 4);
    CHECK(step(5, true, 20, true, 20) == 5);
    CHECK(step(8, true, 30, true, 30) == 5);
    CHECK(step(9, true, 30, true, 30) == kMelodyNoStep);
    // Left only, not read yet, or too long: none.
    CHECK(step(3, true, 12, false, 0) == kMelodyNoStep);
    CHECK(step(0, true, 12, true, 12) == kMelodyNoStep);
    CHECK(step(3, false, 0, true, 8, melody_cut::kMaxBars + 1) == kMelodyNoStep);
    CHECK(step(3, false, 0, true, 8, melody_cut::kMaxBars) == 1);
    // A file too short to be a piece (a one-note test file rates easy): none, at any rating.
    CHECK(step(3, false, 0, true, 1, 24, 1) == kMelodyNoStep);
    CHECK(step(3, false, 0, true, 8, 24, melody_cut::kMinNotes - 1) == kMelodyNoStep);
    CHECK(step(3, false, 0, true, 8, 24, melody_cut::kMinNotes) == 1);
    CHECK(step(3, false, 0, true, 8, melody_cut::kMinBars - 1) == kMelodyNoStep);
    CHECK(step(3, false, 0, true, 8, melody_cut::kMinBars) == 1);
    CHECK(step(6, true, 20, true, 20, 1, 40) == kMelodyNoStep);
}

TEST_CASE("steps: every step has its words") {
    for (int step = kMelodyFirstStep; step <= kMelodyLastStep; ++step) {
        CHECK(std::string(melody_step_words(step)).size() > 3);
    }
    CHECK(std::string(melody_step_words(0)).empty());
    CHECK(std::string(melody_step_words(6)).empty());
}

TEST_CASE("added: songs are kept with their step, changed in place and taken out") {
    MelodyAdded list;
    CHECK(list.step_of(0xAB) == 0);
    CHECK(list.set(0xAB, 3));
    CHECK(list.set(0xCD, 1));
    CHECK(list.step_of(0xAB) == 3);
    CHECK(list.set(0xAB, 5)); // moved, not added twice
    CHECK(list.size() == 2);
    CHECK(list.step_of(0xAB) == 5);
    CHECK_FALSE(list.set(0xEF, 0)); // not a step
    CHECK_FALSE(list.set(0xEF, 6));
    CHECK_FALSE(list.set(0, 2));    // no song
    CHECK(list.remove(0xAB));
    CHECK_FALSE(list.remove(0xAB));
    CHECK(list.size() == 1);
    CHECK(list.at(0).hash == 0xCD);
}

TEST_CASE("added: the list is text, per profile, and read back whatever is in it") {
    MelodyAdded list;
    list.set(0x0123456789abcdefULL, 4);
    list.set(0xFFULL, 1);
    const std::string text = list.text();
    CHECK(text == "0123456789abcdef:4 00000000000000ff:1");
    const MelodyAdded back = MelodyAdded::parse(text);
    CHECK(back.size() == 2);
    CHECK(back.step_of(0x0123456789abcdefULL) == 4);
    CHECK(back.step_of(0xFFULL) == 1);
    // Anything that is not a hash with a step is left out; the first of a hash twice stays.
    const MelodyAdded odd = MelodyAdded::parse(
        "junk  0123456789abcdef:9 0123456789abcdeg:2 00000000000000ff:2 00000000000000ff:3 "
        "0000000000000000:2 00000000000000aa:1");
    CHECK(odd.size() == 2);
    CHECK(odd.step_of(0xFFULL) == 2);
    CHECK(odd.step_of(0xAAULL) == 1);
    CHECK(MelodyAdded::parse("").size() == 0);
}

TEST_CASE("added: no more than the most are kept, and the text fits the settings' room") {
    MelodyAdded list;
    for (std::uint64_t i = 1; i <= kMaxMelodyAdded; ++i) {
        CHECK(list.set(i * 0x1111111111111111ULL, 5));
    }
    CHECK_FALSE(list.set(0x7777ULL, 2));
    CHECK(list.size() == kMaxMelodyAdded);
    CHECK(list.text().size() <= kMaxMelodyAddedBytes);
    CHECK(MelodyAdded::parse(list.text()).size() == kMaxMelodyAdded);
}

TEST_CASE("added: the path is kept with the song, written safely and read back") {
    MelodyAdded list;
    list.set(0xABULL, 2, "Classical/Für Elise 100%.mid");
    list.set(0xCDULL, 4, "tunes.abc#X=3");
    list.set(0xEFULL, 1); // no path: found by its hash only
    const std::string text = list.text();
    CHECK(text.find(' ') != std::string::npos);
    CHECK(text.find("%20") != std::string::npos); // the spaces of the path are not word breaks
    CHECK(text.find("%25") != std::string::npos);
    const MelodyAdded back = MelodyAdded::parse(text);
    REQUIRE(back.size() == 3);
    CHECK(back.at(0).path == "Classical/Für Elise 100%.mid");
    CHECK(back.at(1).path == "tunes.abc#X=3");
    CHECK(back.at(2).path.empty());
    // A word with no path (as the first version wrote it) still reads; a bad escape is left out.
    CHECK(MelodyAdded::parse("00000000000000ab:3").step_of(0xABULL) == 3);
    CHECK(MelodyAdded::parse("00000000000000ab:3:bad%zz").size() == 0);
    CHECK(MelodyAdded::parse(std::string("00000000000000ab:3:") + std::string(kMaxMelodyPath + 1, 'a')).size() == 0);
    // A path too long to keep is dropped, not the entry.
    MelodyAdded long_path;
    long_path.set(0x1ULL, 2, std::string(kMaxMelodyPath + 1, 'a'));
    CHECK(long_path.at(0).path.empty());
}

TEST_CASE("added: an edited file follows its new hash and keeps its step and place") {
    MelodyAdded list;
    list.set(0x1ULL, 2, "a.mid");
    list.set(0x2ULL, 3, "b.mid");
    CHECK(list.step_of(0x9ULL, "a.mid") == 2); // found by its path
    CHECK(list.step_of(0x9ULL) == 0);
    CHECK(list.rebind(0x1ULL, 0x9ULL, "a.mid"));
    CHECK(list.at(0).hash == 0x9ULL); // the same place
    CHECK(list.at(0).step == 2);
    CHECK(list.step_of(0x1ULL) == 0);
    CHECK_FALSE(list.rebind(0x1ULL, 0x8ULL, "a.mid"));
    // The card adds the edited file again under its new hash: the entry is the same one.
    CHECK(list.set(0x7ULL, 5, "b.mid"));
    CHECK(list.size() == 2);
    CHECK(list.at(1).hash == 0x7ULL);
    CHECK(list.at(1).step == 5);
    // An entry already on the new hash makes the followed one a repeat.
    CHECK(list.rebind(0x9ULL, 0x7ULL, "a.mid"));
    CHECK(list.size() == 1);
    // Removing goes by hash or by path.
    CHECK(list.remove(0x77ULL, "b.mid"));
    CHECK(list.size() == 0);
}

TEST_CASE("added: songs the library lacks do not count toward the most, and the oldest of them make "
          "room") {
    MelodyAdded list;
    // Songs 25 to 48 were added first; the library has lost their files (hidden) since.
    std::vector<std::uint64_t> hidden;
    for (std::uint64_t i = kMaxMelodyAdded + 1; i <= kMaxMelodyStored; ++i) {
        REQUIRE(list.set(i, 3, "p" + std::to_string(i)));
        hidden.push_back(i);
    }
    // Hidden entries take no room from the 24 the library has: all 24 more are added.
    for (std::uint64_t i = 1; i <= kMaxMelodyAdded; ++i) {
        REQUIRE(list.set(i, 4, "p" + std::to_string(i), hidden));
    }
    REQUIRE(list.size() == kMaxMelodyStored);
    CHECK(list.visible(hidden) == kMaxMelodyAdded);
    // Full of songs the library has: a new one has no room.
    CHECK_FALSE(list.set(100, 2, "new", hidden));
    // One goes from the library: it counts for nothing now, and the new song takes the place of the
    // oldest hidden entry in the stored list.
    hidden.push_back(1);
    CHECK(list.visible(hidden) == kMaxMelodyAdded - 1);
    CHECK(list.set(100, 2, "new", hidden));
    CHECK(list.size() == kMaxMelodyStored);
    CHECK(list.step_of(25) == 0);  // the oldest hidden one was dropped
    CHECK(list.step_of(26) == 3);  // the later hidden ones stay, in case their files come back
    CHECK(list.step_of(1) == 4);   // and so does the one just hidden
    CHECK(list.step_of(100) == 2);
}
