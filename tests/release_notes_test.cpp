// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_feed.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;
using Lines = std::vector<std::string>;

TEST_CASE("release notes: the first lines, without markdown") {
    const std::string notes =
        "## Changes\r\n\r\n* **Faster** start, `--version` flag\r\n- See [the guide](https://x.y/z)"
        " for more\r\n\r\n> a quote\r\n";
    CHECK(release_notes_lines(notes, 4, 80) ==
          Lines{"Changes", "Faster start, --version flag", "See the guide for more", "> a quote"});
    CHECK(release_notes_lines(notes, 2, 80) == Lines{"Changes", "Faster start, --version flag"});
    CHECK(release_notes_lines(notes, 0, 80).empty());
}

TEST_CASE("release notes: generated notes lose their headings, authors and changelog line") {
    const std::string notes =
        "## What's Changed\n"
        "* Learn: ring of fifths by @techneissa in https://github.com/techneissa/etude/pull/12\n"
        "* Fix a crash by @someone in https://github.com/techneissa/etude/pull/13\n"
        "\n"
        "## New Contributors\n"
        "* @someone made their first contribution in https://github.com/x/y/pull/13\n"
        "\n"
        "**Full Changelog**: https://github.com/techneissa/etude/compare/v1.0.0...v1.1.0\n";
    const Lines lines = release_notes_lines(notes, 5, 80);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "Learn: ring of fifths");
    CHECK(lines[1] == "Fix a crash");
    CHECK(lines[2].find("first contribution") != std::string::npos);
}

TEST_CASE("release notes: blank notes, rules, comments and code blocks give nothing") {
    CHECK(release_notes_lines("", 4, 80).empty());
    CHECK(release_notes_lines("\n\n  \n", 4, 80).empty());
    CHECK(release_notes_lines("---\n<!-- hidden -->\n```\ncode here\n```\nafter", 4, 80) ==
          Lines{"after"});
}

TEST_CASE("release notes: a long line is cut at a word with an ellipsis, by characters") {
    const Lines lines = release_notes_lines("one two three four five six seven", 1, 14);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "one two three\xE2\x80\xA6");
    // Characters, not bytes: accents count once and the cut is never inside one.
    const Lines accented = release_notes_lines("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 1, 4);
    REQUIRE(accented.size() == 1);
    CHECK(accented[0] == "\xC3\xA9\xC3\xA9\xC3\xA9\xE2\x80\xA6");
    CHECK(release_notes_lines("short", 1, 14) == Lines{"short"});
    CHECK(release_notes_lines("exactly fourteen", 1, 16) == Lines{"exactly fourteen"});
}
