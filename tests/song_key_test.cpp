// SPDX-License-Identifier: GPL-3.0-or-later
// The song identity the practice history keys on.
#include "etude/song_key.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("song key: the content hash is the identity, the path does not matter") {
    const std::string a = song_key(0x1234abcdULL, "Songs/for-elise.mid");
    CHECK(a == "h:000000001234abcd");
    // A renamed or moved file is the same song...
    CHECK(song_key(0x1234abcdULL, "Songs/Beethoven/elise.mid") == a);
    CHECK(song_key(0x1234abcdULL, "") == a);
    // ...and an edited file (other bytes, same path) is another.
    CHECK(song_key(0x1234abceULL, "Songs/for-elise.mid") != a);
}

TEST_CASE("song key: a hash with every bit set keeps all sixteen digits") {
    CHECK(song_key(~0ULL, "x") == "h:ffffffffffffffff");
}

TEST_CASE("song key: without a hash the normalized path stands in") {
    CHECK(song_key(0, "Songs/a.mid") == "p:Songs/a.mid");
    CHECK(song_key(0, "Songs\\a.mid") == "p:Songs/a.mid");
    CHECK(song_key(0, "./Songs//a.mid") == "p:Songs/a.mid");
    CHECK(song_key(0, "Songs/A.mid") != song_key(0, "Songs/a.mid")); // case is kept
}

TEST_CASE("song key: neither a hash nor a path gives no key") {
    CHECK(song_key(0, "").empty());
    CHECK(song_key(0, "./").empty());
    CHECK(song_key(0, ".").empty());
    CHECK(song_key(0, "///").empty());
}

TEST_CASE("song key: path normalization") {
    CHECK(normalized_song_path("a\\b\\c.mid") == "a/b/c.mid");
    CHECK(normalized_song_path("a//b///c") == "a/b/c");
    CHECK(normalized_song_path("a/b/") == "a/b");
    CHECK(normalized_song_path("./././a") == "a");
    CHECK(normalized_song_path("/abs/a.mid") == "/abs/a.mid");
    CHECK(normalized_song_path("../a.mid") == "../a.mid");
    CHECK(normalized_song_path("").empty());
    CHECK(normalized_song_path(normalized_song_path("x\\\\y/")) == normalized_song_path("x\\\\y/"));
}
