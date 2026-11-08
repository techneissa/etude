// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_collections.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

TEST_CASE("collections: a folder holds its own songs and those of the folders inside it") {
    CHECK(in_collection("Classical/Baroque", "Classical"));
    CHECK(in_collection("Classical", "Classical"));
    CHECK_FALSE(in_collection("Classical2", "Classical"));
    CHECK_FALSE(in_collection("", "Classical"));
    CHECK(in_collection("", ""));
    CHECK(in_collection("Folk", ""));
}

TEST_CASE("collections: the ones inside a folder, counted and ordered by name") {
    const std::vector<std::string> folders{"Classical/Baroque",      "classical", "Folk",  "",
                                           "Classical/Baroque/Bach", "Classical", "Études"};
    std::vector<Collection> out;
    child_collections(folders, "", out);
    REQUIRE(out.size() == 4);
    CHECK(out[0].name == "Classical"); // "classical" folds the same: named apart, kept apart
    CHECK(out[0].songs == 3);
    CHECK(out[1].name == "classical");
    CHECK(out[2].name == "Études"); // é folds to e: between "classical" and "Folk"
    CHECK(out[3].name == "Folk");
    child_collections(folders, "Classical", out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].path == "Classical/Baroque");
    CHECK(out[0].songs == 2);
    child_collections(folders, "Folk", out);
    CHECK(out.empty());
}

TEST_CASE("collections: the breadcrumb back, top first") {
    std::vector<Crumb> c;
    breadcrumb("Classical/Baroque/Bach", c);
    REQUIRE(c.size() == 3);
    CHECK(c[0].name == "Classical");
    CHECK(c[0].path == "Classical");
    CHECK(c[2].name == "Bach");
    CHECK(c[2].path == "Classical/Baroque/Bach");
    breadcrumb("", c);
    CHECK(c.empty());
}

TEST_CASE("collections: a new import is in the Inbox for a week, until played or grouped") {
    constexpr std::int64_t kDay = 86400;
    const std::int64_t added = 1'000'000;
    CHECK(is_new_import(added, added + kDay, false, false));
    CHECK_FALSE(is_new_import(added, added + kInboxDays * kDay, false, false));
    CHECK_FALSE(is_new_import(added, added + kDay, true, false));
    CHECK_FALSE(is_new_import(added, added + kDay, false, true));
    CHECK_FALSE(is_new_import(0, added, false, false)); // date not known
}

TEST_CASE("alphabet index: first place of each letter, '#' for the rest, nearest on a gap") {
    const std::vector<std::string> titles{"bach prelude", "12 etudes", "air", "\"bolero\"",
                                          "zigeuner"};
    const std::vector<std::uint32_t> order{1, 2, 0, 3, 4};
    AlphaIndex idx;
    alpha_index(order, titles, idx);
    CHECK(idx[0] == 1);               // a: "air" at place 1
    CHECK(idx[1] == 2);               // b: "bach prelude", then "bolero" (quote skipped) later
    CHECK(idx[25] == 4);              // z
    CHECK(idx[kAlphaSlots - 1] == 0); // "12 etudes"
    CHECK(idx[2] == -1);
    CHECK(alpha_target(idx, 2) == 25); // c: nothing until z
    CHECK(alpha_target(idx, 1) == 1);
    AlphaIndex empty;
    empty.fill(-1);
    CHECK(alpha_target(empty, 3) == -1);
}
