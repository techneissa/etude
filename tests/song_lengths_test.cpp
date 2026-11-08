// SPDX-License-Identifier: GPL-3.0-or-later
// The song lengths the phone page lists come from what the library has parsed: nothing is read for
// them, a file of several ABC tunes has none, and a PDF has none.
#include "../app/song_lengths.hpp"
#include "../app/song_library.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace etude::app;
using namespace upload_test;

namespace {

fs::path data(const char* name) { return fs::path(ETUDE_SOURCE_DIR) / "tests/data" / name; }

} // namespace

TEST_CASE("song lengths: the library's parsed songs have one, a tune collection and a PDF do not") {
    TempDir root;
    const fs::path songs = root.path / "Songs";
    fs::create_directories(songs / "Inbox");
    fs::copy_file(data("edge-pickup.mid"), songs / "Inbox/pickup.mid");
    fs::copy_file(data("abc-collection.abc"), songs / "tunes.abc");
    write_file(songs / "scan.pdf", "%PDF-1.4");
    SongLibrary library({songs});
    library.scan();
    library.load_all_now();
    library.poll();
    const SongLengths lengths = song_lengths_of(library);
    SongLengthBook book;
    book.set(lengths);
    // The MIDI file: its parsed length, found by the path the upload service lists it under.
    const double seconds = book.get(songs / "Inbox/pickup.mid");
    CHECK(seconds > 0.5);
    const auto index = library.index_of(songs / "Inbox/pickup.mid");
    REQUIRE(index.has_value());
    CHECK(seconds == doctest::Approx(library.state().items[*index].entry.duration_s));
    // The same file under a path that is not spelled the same way (a "./" in it).
    CHECK(book.get(songs / "." / "Inbox/pickup.mid") == doctest::Approx(seconds));
    // Several tunes in one file: no one length is the file's. A PDF was never parsed.
    CHECK(book.get(songs / "tunes.abc") < 0.0);
    CHECK(book.get(songs / "scan.pdf") < 0.0);
    CHECK(book.get(songs / "nothing.mid") < 0.0);
}

TEST_CASE("song lengths: the book is read from any thread while the frame thread replaces it") {
    SongLengthBook book;
    CHECK(book.get("/a.mid") < 0.0);
    book.set({{"/a.mid", 61.0}});
    CHECK(book.get("/a.mid") == doctest::Approx(61.0));
    book.set({});
    CHECK(book.get("/a.mid") < 0.0);
}
