// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/icon_colour.hpp"
#include "etude/medals.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace etude;

namespace {

namespace fs = std::filesystem;

const fs::path kIcons = fs::path(ETUDE_SOURCE_DIR) / "app" / "assets" / "icon";

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The width and height a PNG file's header says (0, 0 when it is not one).
std::pair<std::uint32_t, std::uint32_t> png_size(const fs::path& path) {
    const std::string b = read_text(path);
    if (b.size() < 24 || b.compare(1, 3, "PNG") != 0) {
        return {0, 0};
    }
    const auto be32 = [&](std::size_t at) {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(b[at])) << 24) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 2])) << 8) |
               static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 3]));
    };
    return {be32(16), be32(20)};
}

} // namespace

TEST_CASE("icon colours: one colour for each of the eight titles, in order") {
    constexpr std::array<const char*, 8> titles{"Beginner", "Student",         "Performer", "Soloist",
                                                "Pianist",  "Concert Pianist", "Maestro",   "Virtuoso"};
    constexpr std::array<const char*, 8> keys{"wine",     "teal",   "petrol",   "cobalt",
                                              "midnight", "aubergine", "graphite", "black"};
    static_assert(kIconColourCount == kTierCount);
    for (int i = 0; i < kIconColourCount; ++i) {
        const IconColourInfo& info = kIconColours[static_cast<std::size_t>(i)];
        CAPTURE(info.key);
        CHECK(static_cast<int>(info.id) == i);
        CHECK(info.key == keys[static_cast<std::size_t>(i)]);
        CHECK(info.title == i + 1);
        CHECK(tier_title(info.title) == titles[static_cast<std::size_t>(i)]);
        CHECK(&icon_colour_info(info.id) == &info);
        CHECK(icon_colour_from_key(info.key) == info.id);
    }
    CHECK(kDefaultIconColour == IconColour::Wine);
    CHECK_FALSE(icon_colour_from_key("emerald").has_value());
    CHECK_FALSE(icon_colour_from_key("").has_value());
}

TEST_CASE("icon colours: a colour is unlocked by its title or any higher one, wine always") {
    CHECK(icon_colour_unlocked(IconColour::Wine, 0));
    CHECK(icon_colour_unlocked(IconColour::Wine, 1));
    CHECK_FALSE(icon_colour_unlocked(IconColour::Teal, 0));
    CHECK_FALSE(icon_colour_unlocked(IconColour::Teal, 1));
    CHECK(icon_colour_unlocked(IconColour::Teal, 2));
    CHECK(icon_colour_unlocked(IconColour::Cobalt, 4));
    CHECK_FALSE(icon_colour_unlocked(IconColour::Cobalt, 3));
    CHECK_FALSE(icon_colour_unlocked(IconColour::Black, 7));
    CHECK(icon_colour_unlocked(IconColour::Black, 8));
    for (const IconColourInfo& info : kIconColours) {
        CHECK(icon_colour_unlocked(info.id, kTierCount)); // the top title has all of them
    }
}

TEST_CASE("icon colours: the colour in use is available even when its title is gone") {
    // A profile that earned Cobalt (title 4) was deleted: the best title is now 2.
    CHECK(icon_colour_available(IconColour::Cobalt, IconColour::Cobalt, 2));
    CHECK_FALSE(icon_colour_available(IconColour::Cobalt, IconColour::Teal,
                                      2)); // locked once another is in use
    CHECK(icon_colour_available(IconColour::Teal, IconColour::Cobalt, 2));
    CHECK(icon_colour_available(IconColour::Black, IconColour::Black, 0));
    CHECK_FALSE(icon_colour_available(IconColour::Black, IconColour::Wine, 7));
    CHECK(icon_colour_available(IconColour::Black, IconColour::Wine, 8));
}

TEST_CASE("icon colours: the Linux icon name is the colour's own") {
    CHECK(icon_name(IconColour::Wine) == "etude-wine");
    CHECK(icon_name(IconColour::Black) == "etude-black");
}

TEST_CASE("icon colours: every colour's icon set exists at every size") {
    for (const IconColourInfo& info : kIconColours) {
        CAPTURE(info.key);
        const fs::path dir = kIcons / std::string(info.key);
        for (const int px : kIconSizes) {
            CAPTURE(px);
            const fs::path file = dir / ("etude-" + std::to_string(px) + ".png");
            REQUIRE(fs::is_regular_file(file));
            const auto size = png_size(file);
            CHECK(size.first == static_cast<std::uint32_t>(px));
            CHECK(size.second == static_cast<std::uint32_t>(px));
        }
        CHECK(fs::is_regular_file(kIcons / "svg" / (std::string(info.key) + ".svg")));
        CHECK(fs::is_regular_file(kIcons / "svg" / (std::string(info.key) + "-small.svg")));
    }
    CHECK(fs::is_regular_file(kIcons / "etude.icns"));
    CHECK(fs::is_regular_file(kIcons / "src" / "bravura-gclef-E050.txt"));
}

TEST_CASE("icon colours: the Pi setup installs every colour's set") {
    const std::string setup = read_text(fs::path(ETUDE_SOURCE_DIR) / "deploy" / "pi" / "setup.sh");
    std::string keys;
    for (const IconColourInfo& info : kIconColours) {
        keys += (keys.empty() ? "" : " ") + std::string(info.key);
    }
    CHECK(setup.find("ICON_COLOURS=(" + keys + ")") != std::string::npos);
    std::string sizes;
    for (const int px : kIconHicolorSizes) { // hicolor stops at 512; the 1024 is for the .icns
        sizes += (sizes.empty() ? "" : " ") + std::to_string(px);
    }
    CHECK(setup.find("ICON_SIZES=(" + sizes + ")") != std::string::npos);
}

TEST_CASE("icon colours: the app embeds every colour's icon, in the catalogue's order") {
    // The colours the CMake embedding loops over (app/CMakeLists.txt) are the catalogue's.
    const std::string cmake = read_text(fs::path(ETUDE_SOURCE_DIR) / "app" / "CMakeLists.txt");
    std::string keys;
    for (const IconColourInfo& info : kIconColours) {
        keys += (keys.empty() ? "" : " ") + std::string(info.key);
    }
    CHECK(cmake.find("foreach(colour " + keys + ")") != std::string::npos);
}

namespace {

// "#rrggbb" as 0xrrggbb.
std::uint32_t hex_of(const std::string& text) { return static_cast<std::uint32_t>(std::stoul(text.substr(1, 6), nullptr, 16)); }

std::uint32_t channel(std::uint32_t rgb, int shift) { return (rgb >> shift) & 0xffu; }

// `from` moved `t` of the way to `to`, each channel rounded.
std::uint32_t lift(std::uint32_t from, std::uint32_t to, double t) {
    std::uint32_t out = 0;
    for (const int shift : {16, 8, 0}) {
        const double v = static_cast<double>(channel(from, shift)) +
                         (static_cast<double>(channel(to, shift)) - static_cast<double>(channel(from, shift))) * t;
        out |= static_cast<std::uint32_t>(std::lround(v)) << shift;
    }
    return out;
}

bool near_rgb(std::uint32_t a, std::uint32_t b) {
    for (const int shift : {16, 8, 0}) {
        const int d = static_cast<int>(channel(a, shift)) - static_cast<int>(channel(b, shift));
        if (d > 1 || d < -1) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("icon colours: each colour's highlight comes from the icon's own palette") {
    // scripts/make-icons.py PLAIN rows: key: (centre of the gradient, its edge, staff line, opacity).
    const std::string script = read_text(fs::path(ETUDE_SOURCE_DIR) / "scripts" / "make-icons.py");
    for (const IconColourInfo& info : kIconColours) {
        CAPTURE(info.key);
        const std::string key = "\"" + std::string(info.key) + "\": (\"";
        const std::size_t at = script.find(key);
        if (info.id == IconColour::Black) {
            // Virtuoso has no row: its highlight is the clef's gold (GOLD_RICH's middle and first stops).
            CHECK(script.find("\"#ffd04a\"") != std::string::npos);
            CHECK(script.find("\"#fff5c8\"") != std::string::npos);
            CHECK(info.rim == 0xffd04a);
            CHECK(info.ink == 0xfff5c8);
            continue;
        }
        REQUIRE(at != std::string::npos);
        const std::size_t row = at + key.size() - 1; // the first quote of the centre colour
        const std::uint32_t inner = hex_of(script.substr(row + 1, 7));
        const std::uint32_t line = hex_of(script.substr(script.find("\"#", row + 10 + 6) + 1, 7));
        // The name is the staff line's light shade, which reads on the dark page.
        CHECK(info.ink == line);
        switch (info.id) {
        case IconColour::Wine:
        case IconColour::Teal:
        case IconColour::Cobalt:
            CHECK(info.rim == inner); // bright enough as it is
            break;
        case IconColour::Graphite:
            CHECK(info.rim == line); // light silver, so it shows on the dark page
            break;
        default: // Petrol, Midnight, Aubergine: the centre is too dark, so lifted towards the line colour
            CHECK(near_rgb(info.rim, lift(inner, line, 0.45)));
            break;
        }
    }
}
