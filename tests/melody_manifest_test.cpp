// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/melody_manifest.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

using namespace etude;

namespace {

constexpr const char* kGood =
    "# a comment\n"
    "\n"
    "file | a.mid | Piece A | Traditional | 1 | Own transcription | CC0-1.0\n"
    "file | b.mid | Piece B | Someone | 2 | Own transcription | CC0-1.0\n"
    "file | c.mid | Piece C | Someone | 3 | Mutopia 1 | Public Domain\n"
    "path | one | Path one | sprout\n"
    "goal | one | a.mid | First goal\n"
    "goal | one | b.mid | Second goal\n"
    "path | two | Path two | award\n"
    "goal | two | c.mid | Third goal\n";

bool says(const MelodyManifest& m, const char* what) {
    return std::any_of(m.errors.begin(), m.errors.end(), [&](const std::string& e) {
        return e.find(what) != std::string::npos;
    });
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

} // namespace

TEST_CASE("melody manifest: files, paths and goals are read in order") {
    const MelodyManifest m = parse_melody_manifest(kGood);
    REQUIRE(m.ok());
    REQUIRE(m.files.size() == 3);
    CHECK(m.files[1].file == "b.mid");
    CHECK(m.files[1].title == "Piece B");
    CHECK(m.files[1].composer == "Someone");
    CHECK(m.files[1].step == 2);
    CHECK(m.files[2].licence == "Public Domain");
    REQUIRE(m.paths.size() == 2);
    CHECK(m.paths[0].id == "one");
    CHECK(m.paths[0].icon == "sprout");
    CHECK(m.paths[0].files == std::vector<std::size_t>{0, 1});
    CHECK(m.paths[0].goals == std::vector<std::string>{"First goal", "Second goal"});
    CHECK(m.paths[1].files == std::vector<std::size_t>{2});
}

TEST_CASE("melody manifest: a file with no licence line is an error") {
    CHECK(says(parse_melody_manifest(replaced(kGood, "Own transcription | CC0-1.0\nfile | b", "Own transcription | \nfile | b")),
               "seven fields"));
    CHECK(says(parse_melody_manifest(replaced(kGood, " | Own transcription | CC0-1.0\nfile | b", "\nfile | b")),
               "seven fields"));
}

TEST_CASE("melody manifest: a step outside 1 to 5 is an error") {
    CHECK(says(parse_melody_manifest(replaced(kGood, "| 1 |", "| 0 |")), "not 1 to 5"));
    CHECK(says(parse_melody_manifest(replaced(kGood, "| 1 |", "| 6 |")), "not 1 to 5"));
    CHECK(says(parse_melody_manifest(replaced(kGood, "| 1 |", "| x |")), "not 1 to 5"));
}

TEST_CASE("melody manifest: a path's order never steps down in difficulty") {
    // b (step 2) before a (step 1).
    const MelodyManifest m = parse_melody_manifest(
        replaced(kGood, "goal | one | a.mid | First goal\ngoal | one | b.mid | Second goal",
                 "goal | one | b.mid | Second goal\ngoal | one | a.mid | First goal"));
    CHECK_FALSE(m.ok());
    CHECK(says(m, "comes after"));
}

TEST_CASE("melody manifest: every file is in exactly one path") {
    CHECK(says(parse_melody_manifest(replaced(kGood, "goal | two | c.mid | Third goal\n", "")),
               "c.mid is in no path"));
    CHECK(says(parse_melody_manifest(std::string(kGood) + "goal | two | a.mid | Again\n"),
               "is in a path already"));
}

TEST_CASE("melody manifest: names are unique and goals name what exists") {
    CHECK(says(parse_melody_manifest(std::string(kGood) + "file | a.mid | Again | X | 1 | s | l\n"),
               "listed twice"));
    CHECK(says(parse_melody_manifest(std::string(kGood) + "goal | nope | a.mid | g\n"),
               "unknown path"));
    CHECK(says(parse_melody_manifest(std::string(kGood) + "goal | one | z.mid | g\n"),
               "unlisted file"));
    CHECK(says(parse_melody_manifest(std::string(kGood) + "path | one | Again | sprout\n"),
               "listed twice"));
    CHECK(says(parse_melody_manifest(std::string(kGood) + "path | three | Empty | sprout\n"),
               "has no pieces"));
}

TEST_CASE("melody manifest: a file is a .mid name and an odd record is reported") {
    CHECK(says(parse_melody_manifest(replaced(kGood, "a.mid", "a.txt")), "ending in .mid"));
    CHECK(says(parse_melody_manifest(replaced(kGood, "a.mid", "dir/a.mid")), "ending in .mid"));
    CHECK(says(parse_melody_manifest(std::string(kGood) + "star | x\n"), "unknown record"));
    const MelodyManifest m = parse_melody_manifest("file | only | three\n");
    CHECK_FALSE(m.ok());
    CHECK(m.errors[0].find("line 1") == 0);
}

TEST_CASE("melody manifest: Windows line endings and stray spaces are fine") {
    std::string text = kGood;
    for (std::size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 2)) {
        text.insert(at, "\r");
    }
    CHECK(parse_melody_manifest(text).ok());
}

TEST_CASE("melody manifest: an icon the page does not draw is an error, not a fallback") {
    CHECK(parse_melody_manifest(kGood).ok());
    for (const std::string_view icon : kMelodyIcons) {
        CHECK(parse_melody_manifest(replaced(kGood, "path | one | Path one | sprout",
                                             "path | one | Path one | " + std::string(icon)))
                  .ok());
    }
    const MelodyManifest m = parse_melody_manifest(replaced(kGood, "| award", "| trophy"));
    CHECK_FALSE(m.ok());
    CHECK(says(m, "icon trophy"));
}

TEST_CASE("melody manifest: more paths or pieces than the page lays out is an error, never cut short") {
    // kMelodyMaxPaths paths of one piece each are fine, one more is refused.
    std::string paths;
    std::string files;
    for (int i = 0; i <= kMelodyMaxPaths; ++i) {
        const std::string n = std::to_string(i);
        files += "file | f" + n + ".mid | T" + n + " | C | 1 | s | l\n";
        paths += "path | p" + n + " | P" + n + " | sprout\n" + "goal | p" + n + " | f" + n + ".mid | g\n";
    }
    CHECK(says(parse_melody_manifest(files + paths), "more than 6 paths"));
    std::string fewer;
    std::string fewer_files;
    for (int i = 0; i < kMelodyMaxPaths; ++i) {
        const std::string n = std::to_string(i);
        fewer_files += "file | f" + n + ".mid | T" + n + " | C | 1 | s | l\n";
        fewer += "path | p" + n + " | P" + n + " | sprout\ngoal | p" + n + " | f" + n + ".mid | g\n";
    }
    CHECK(parse_melody_manifest(fewer_files + fewer).ok());
    // A path of kMelodyMaxPieces pieces is fine, one more is refused.
    std::string many_files;
    std::string many = "path | p | P | sprout\n";
    for (int i = 0; i <= kMelodyMaxPieces; ++i) {
        const std::string n = std::to_string(i);
        many_files += "file | f" + n + ".mid | T" + n + " | C | 1 | s | l\n";
        many += "goal | p | f" + n + ".mid | g\n";
    }
    const MelodyManifest over = parse_melody_manifest(many_files + many);
    CHECK(says(over, "more than 16 pieces"));
    CHECK(over.paths[0].files.size() == static_cast<std::size_t>(kMelodyMaxPieces));
}

TEST_CASE("melody manifest: a version line gives a file its tag, and a file with none has none") {
    const MelodyManifest none = parse_melody_manifest(kGood);
    REQUIRE(none.ok());
    for (const MelodyFileEntry& f : none.files) {
        CHECK(f.version.empty());
    }
    const std::string tagged = std::string(kGood) + "version | b.mid | Easy\nversion | c.mid | Original\n";
    const MelodyManifest m = parse_melody_manifest(tagged);
    REQUIRE(m.ok());
    CHECK(m.files[0].version.empty());
    CHECK(m.files[1].version == "Easy");
    CHECK(m.files[2].version == "Original");
    // A version line may come before the file's goal line, and the order of the files is not changed.
    CHECK(m.paths[0].files == std::vector<std::size_t>{0, 1});
}

TEST_CASE("melody manifest: a version line for an unlisted file, an empty or long tag or a second tag is an error") {
    const std::string base = kGood;
    CHECK(says(parse_melody_manifest(base + "version | nope.mid | Easy\n"), "unlisted file nope.mid"));
    CHECK(says(parse_melody_manifest(base + "version | b.mid | \n"), "three fields"));
    CHECK(says(parse_melody_manifest(base + "version | b.mid\n"), "three fields"));
    CHECK(says(parse_melody_manifest(base + "version | b.mid | Easy | extra\n"), "three fields"));
    CHECK(says(parse_melody_manifest(base + "version | b.mid | A tag far too long\n"), "over 12 characters"));
    CHECK(says(parse_melody_manifest(base + "version | b.mid | Easy\nversion | b.mid | Hard\n"), "a version tag already"));
    // A file line is still seven fields: a tag is its own line, not an eighth field.
    CHECK(says(parse_melody_manifest(replaced(kGood, "Own transcription | CC0-1.0\nfile | b", "Own transcription | CC0-1.0 | Easy\nfile | b")),
               "seven fields"));
}
