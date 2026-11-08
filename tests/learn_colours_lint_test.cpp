// SPDX-License-Identifier: GPL-3.0-or-later
// A section's colour is the player's pick, so a draw site must ask `learn_section_colour(look, section)`
// and never name a shade or write a section's colour out. This reads the sources and fails when one
// does: the palette (core learn_palette.hpp) and the few files that own it are the only ones that may.
#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Source {
    std::string path; // relative to the repository, with `/`
    std::string text; // without `//` comments
};

std::string without_line_comments(const std::string& text) {
    std::istringstream in(text);
    std::string out;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t at = line.find("//");
        out += (at == std::string::npos ? line : line.substr(0, at)) + "\n";
    }
    return out;
}

// Every C++ file of the app, the core and the tools.
std::vector<Source> sources() {
    std::vector<Source> out;
    const fs::path root = fs::path(ETUDE_SOURCE_DIR);
    for (const char* top : {"app", "core", "tools"}) {
        for (const fs::directory_entry& e : fs::recursive_directory_iterator(root / top)) {
            const std::string ext = e.path().extension().string();
            if (!e.is_regular_file() || (ext != ".cpp" && ext != ".hpp")) {
                continue;
            }
            std::ifstream in(e.path(), std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            out.push_back({fs::relative(e.path(), root).generic_string(), without_line_comments(text.str())});
        }
    }
    return out;
}

bool allowed(const std::string& path, const std::vector<std::string>& owners) {
    for (const std::string& owner : owners) {
        if (path == owner) {
            return true;
        }
    }
    return false;
}

// The files that may name `what`; every other file that does is listed in the failure.
std::string offenders(const std::vector<Source>& all, const std::regex& what,
                      const std::vector<std::string>& owners) {
    std::string out;
    for (const Source& s : all) {
        if (!allowed(s.path, owners) && std::regex_search(s.text, what)) {
            out += s.path + "\n";
        }
    }
    return out;
}

} // namespace

TEST_CASE("learn colours lint: no draw site names a shade or a section's colour; the palette's own files do") {
    const std::vector<Source> all = sources();
    REQUIRE(all.size() > 100);

    // The palette's entries and the lookups over them: the palette, the lookup, the look's mending
    // and text form, and the one page that shows the shades.
    const std::vector<std::string> palette_owners{
        "core/include/etude/learn_palette.hpp", "core/src/learn_colours.cpp", "core/src/look.cpp",
        "core/src/look_serialize.cpp", "app/ui/pages_learn_colours.cpp"};
    CHECK(offenders(all, std::regex(R"(\bkLearnShades\b|\blearn_shade_info\b|\bLearnShade::)"),
                    palette_owners) == "");
    CHECK(offenders(all, std::regex(R"(\blearn_picks\b)"),
                    {"core/include/etude/look.hpp", "core/src/learn_colours.cpp", "core/src/look.cpp",
                     "core/src/look_serialize.cpp", "app/ui/pages_learn_colours.cpp"}) == "");

    // The old names of the four colours, which a site could still reach for.
    CHECK(offenders(all, std::regex(R"(\bkLearn(Scales|Chords|Ear|Melodies)Colour\b)"), {}) == "");

    // A section's colour written out by hand: any of the palette's RGB triples outside the palette.
    for (const std::array<int, 3> rgb : {std::array<int, 3>{242, 119, 106}, {150, 210, 90}, {50, 190, 120},
                                         {46, 196, 182}, {167, 139, 250}, {220, 100, 215}, {242, 122, 181}}) {
        const std::string triple = R"(\{\s*)" + std::to_string(rgb[0]) + R"(\s*,\s*)" + std::to_string(rgb[1]) +
                                   R"(\s*,\s*)" + std::to_string(rgb[2]) + R"(\b)";
        CAPTURE(triple);
        CHECK(offenders(all, std::regex(triple), {"core/include/etude/learn_palette.hpp"}) == "");
    }
}

TEST_CASE("learn colours lint: every section's colour is asked through the lookup somewhere in the app") {
    const std::vector<Source> all = sources();
    for (const char* section : {"Scales", "Chords", "Ear", "Melodies"}) {
        bool used = false;
        for (const Source& s : all) {
            used = used || (s.path.rfind("app/", 0) == 0 &&
                            s.text.find(std::string("learn_section_colour(")) != std::string::npos &&
                            s.text.find(std::string("LearnSection::") + section) != std::string::npos);
        }
        CAPTURE(section);
        CHECK(used);
    }
}

TEST_CASE("learn colours lint: opening the settings sheet puts the Learn colours tab back on Scales") {
    const std::vector<Source> all = sources();
    bool wired = false;
    for (const Source& s : all) {
        if (s.path == "app/ui/sheet.cpp") {
            const std::size_t open_at = s.text.find("SettingsSheet::open_at(");
            const std::size_t call = s.text.find("learn_colours_sheet_opened()", open_at);
            const std::size_t next = s.text.find("\n}\n", open_at);
            wired = open_at != std::string::npos && call != std::string::npos && call < next;
        }
    }
    CHECK(wired);
}
