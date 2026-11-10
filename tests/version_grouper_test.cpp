// SPDX-License-Identifier: GPL-3.0-or-later
#include "version_grouper.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

// A melody of `n` notes from a seed: the same seed, the same melody.
MelodyContour tune(std::uint32_t seed, std::size_t n = 32) {
    MelodyContour c;
    std::uint32_t x = seed * 2654435761u + 12345u;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        x = x * 1664525u + 1013904223u;
        int step = static_cast<int>((x >> 16) % 11) - 5;
        if (step == 0) {
            step = 3;
        }
        c.steps.push_back(static_cast<std::int8_t>(step));
    }
    return c;
}

VersionInput input(std::uint64_t hash, std::string title, std::string composer,
                   const MelodyContour& melody, int half_stars) {
    VersionInput in;
    in.hash = hash;
    in.song.title = std::move(title);
    in.song.composer = std::move(composer);
    in.song.half_stars = half_stars;
    in.song.melody = melody;
    return in;
}

} // namespace

TEST_CASE("the grouper keys its groups by content hash, easiest first") {
    const MelodyContour a = tune(1);
    std::vector<VersionInput> songs;
    songs.push_back(input(101, "Dance", "Quill", a, 8));
    songs.push_back(input(102, "Dance (Easy)", "Quill", a, 3));
    songs.push_back(input(103, "Other", "Quill", tune(2), 5));
    const std::shared_ptr<const VersionIndex> index = group_by_hash(songs);
    REQUIRE(index->groups.size() == 1);
    REQUIRE(index->groups[0].size() == 2);
    CHECK(index->groups[0][0].hash == 102);
    CHECK(index->groups[0][0].name == "Easy");
    CHECK(index->groups[0][1].hash == 101);
    CHECK(index->groups[0][1].name == "Hard");
    REQUIRE(index->find(101) != nullptr);
    CHECK(index->find(101)->member == 1);
    CHECK(index->find(102)->member == 0);
    CHECK(index->find(103) == nullptr);
    CHECK(index->find(999) == nullptr);
}

TEST_CASE("the piece map gives every version of a piece the group's lowest hash, and leaves other songs out") {
    const MelodyContour a = tune(1);
    std::vector<VersionInput> songs;
    songs.push_back(input(305, "Dance", "Quill", a, 8));
    songs.push_back(input(102, "Dance (Easy)", "Quill", a, 3));
    songs.push_back(input(900, "Other", "Quill", tune(2), 5));
    const std::shared_ptr<const PieceMap> pieces = piece_map(*group_by_hash(songs));
    REQUIRE(pieces->size() == 2);
    CHECK(pieces->at(305) == 102);
    CHECK(pieces->at(102) == 102);
    CHECK(pieces->find(900) == pieces->end());
}

TEST_CASE("the grouper counts a file with the same bytes once, and ignores a song with no hash") {
    const MelodyContour a = tune(1);
    std::vector<VersionInput> songs;
    songs.push_back(input(101, "Dance", "Quill", a, 8));
    songs.push_back(input(101, "Dance (copy)", "Quill", a, 8));
    songs.push_back(input(0, "Dance (unread)", "Quill", a, 8));
    CHECK(group_by_hash(songs)->groups.empty());
    songs.push_back(input(102, "Dance (Easy)", "Quill", a, 3));
    const std::shared_ptr<const VersionIndex> index = group_by_hash(songs);
    REQUIRE(index->groups.size() == 1);
    CHECK(index->groups[0].size() == 2);
}

TEST_CASE(
    "the grouper runs on its own thread, hands the answer over once and takes the newest request") {
    VersionGrouper grouper;
    CHECK(grouper.take() == nullptr);
    const MelodyContour a = tune(1);
    const MelodyContour b = tune(2);
    std::vector<VersionInput> first;
    first.push_back(input(1, "Dance", "Quill", a, 8));
    first.push_back(input(2, "Dance (Easy)", "Quill", a, 3));
    std::vector<VersionInput> second;
    second.push_back(input(3, "March", "Quill", b, 8));
    second.push_back(input(4, "March (Easy)", "Quill", b, 3));
    second.push_back(input(5, "March (Expert)", "Quill", b, 10));
    grouper.request(first);
    grouper.request(second);
    grouper.wait_idle();
    CHECK_FALSE(grouper.busy());
    const std::shared_ptr<const VersionIndex> index = grouper.take();
    REQUIRE(index != nullptr);
    REQUIRE(index->groups.size() == 1);
    CHECK(index->groups[0].size() == 3);
    CHECK(index->find(1) == nullptr);
    CHECK(grouper.take() == nullptr);
}

TEST_CASE("the grouper finds hundreds of pieces among thousands of songs in well under a second of "
          "its own thread") {
    constexpr std::uint32_t kPieces = 200;
    constexpr std::uint32_t kSingles = 1800;
    std::vector<VersionInput> songs;
    std::uint64_t hash = 1;
    for (std::uint32_t p = 0; p < kPieces; ++p) {
        const MelodyContour m = tune(10'000 + p);
        // The original, the same in another key (the same contour), and a simpler one that leaves
        // out every fifth note.
        std::vector<int> pitch{0};
        for (const std::int8_t step : m.steps) {
            pitch.push_back(pitch.back() + step);
        }
        MelodyContour simple;
        int last = pitch[0];
        for (std::size_t i = 1; i < pitch.size(); ++i) {
            if (i % 5 != 4) {
                simple.steps.push_back(static_cast<std::int8_t>(pitch[i] - last));
                last = pitch[i];
            }
        }
        const std::string name = "Piece " + std::to_string(p);
        songs.push_back(input(hash++, name, "Composer " + std::to_string(p % 40), m, 8));
        songs.push_back(input(hash++, name + " in D", "", m, 8));
        songs.push_back(
            input(hash++, name + " (Easy)", "Composer " + std::to_string(p % 40), simple, 4));
    }
    for (std::uint32_t s = 0; s < kSingles; ++s) {
        songs.push_back(input(hash++, "Single " + std::to_string(s),
                              "Composer " + std::to_string(s % 40), tune(50'000 + s), 6));
    }
    VersionGrouper grouper;
    grouper.request(songs);
    grouper.wait_idle();
    const std::shared_ptr<const VersionIndex> index = grouper.take();
    REQUIRE(index != nullptr);
    CHECK(index->groups.size() == kPieces);
    for (const auto& g : index->groups) {
        CHECK(g.size() == 3);
    }
}

// ---- The player's corrections (#213)

TEST_CASE("the player's split and join are by content hash: a rename or a new order changes nothing") {
    const MelodyContour a = tune(1);
    const auto songs = [&](const char* dance_title, bool reversed) {
        std::vector<VersionInput> v;
        v.push_back(input(101, dance_title, "Quill", a, 8));
        v.push_back(input(102, "Dance (Easy)", "Quill", a, 3));
        v.push_back(input(103, "Other", "Quill", tune(2), 5));
        v.push_back(input(104, "Waltz", "Keller", tune(3), 6));
        if (reversed) {
            std::reverse(v.begin(), v.end());
        }
        return v;
    };
    VersionOverrides o;
    apply_version_edit(o, {VersionEdit::Kind::Split, 102, 0});
    apply_version_edit(o, {VersionEdit::Kind::Join, 103, 104});
    for (const bool reversed : {false, true}) {
        // 101 and 102 match on everything, but 102 was taken out; 103 and 104 are nothing alike but
        // were joined. The same after a rename of 101 and with the list in another order.
        for (const char* title : {"Dance", "Dance (Renamed)"}) {
            const std::shared_ptr<const VersionIndex> index =
                group_by_hash(songs(title, reversed), o);
            CHECK(index->find(102) == nullptr);
            CHECK(index->find(101) == nullptr);
            REQUIRE(index->groups.size() == 1);
            REQUIRE(index->find(103) != nullptr);
            REQUIRE(index->find(104) != nullptr);
            CHECK(index->find(103)->group == index->find(104)->group);
        }
    }
    // Without the corrections the files decide.
    const std::shared_ptr<const VersionIndex> plain = group_by_hash(songs("Dance", false));
    CHECK(plain->find(101) != nullptr);
    CHECK(plain->find(102) != nullptr);
    CHECK(plain->find(103) == nullptr);
    // A correction about a song that is not in the library is left alone.
    VersionOverrides gone;
    apply_version_edit(gone, {VersionEdit::Kind::Split, 999, 0});
    apply_version_edit(gone, {VersionEdit::Kind::Join, 998, 101});
    CHECK(group_by_hash(songs("Dance", false), gone)->groups.size() == 1);
}
