// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/palette.hpp"

#include <algorithm>
#include <initializer_list>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>

#include "sample_looks.hpp"

#include "etude/hand.hpp"
#include "etude/pitch.hpp"

using namespace etude;
using namespace etude::test;

namespace {

LookSettings in_scheme(ColorScheme scheme) {
    LookSettings s;
    s.color_scheme = scheme;
    return s;
}

} // namespace

TEST_CASE("each scheme has its swatches, and every palette fills all of them, opaque") {
    CHECK(swatch_count(ColorScheme::ByHand) == 3);
    CHECK(swatch_count(ColorScheme::ByPitchClass) == 12);
    CHECK(swatch_count(ColorScheme::ByOctave) == 9);
    CHECK(swatch_count(ColorScheme::ByVelocity) == 3);
    CHECK(swatch_count(ColorScheme::Single) == 1);
    for (const ColorScheme scheme : kAllColorSchemes) {
        CHECK(swatch_count(scheme) <= kMaxSwatches);
        CHECK(scheme_swatches(LookSettings{}, scheme).count == swatch_count(scheme));
        for (const Palette p : kAllPalettes) {
            const Swatches s = palette_swatches(p, scheme);
            REQUIRE(s.count == swatch_count(scheme));
            for (const RGBA c : s.view()) {
                CHECK(c.a == 255);
            }
        }
    }
}

TEST_CASE("the palettes differ from each other in every scheme") {
    for (const ColorScheme scheme : kAllColorSchemes) {
        for (std::size_t i = 0; i < kAllPalettes.size(); ++i) {
            for (std::size_t j = i + 1; j < kAllPalettes.size(); ++j) {
                CHECK_FALSE(palette_swatches(kAllPalettes[i], scheme) ==
                            palette_swatches(kAllPalettes[j], scheme));
            }
        }
    }
}

TEST_CASE("the default look is Classic in every scheme, and Classic is the default colours") {
    const LookSettings d;
    for (const ColorScheme scheme : kAllColorSchemes) {
        CHECK(d.palettes[static_cast<std::size_t>(scheme)] == Palette::Classic);
        CHECK(matching_palette(d, scheme) == Palette::Classic);
        CHECK(palette_swatches(Palette::Classic, scheme) == scheme_swatches(d, scheme));
    }
}

TEST_CASE("swatches are the look's colours, in order") {
    LookSettings s;
    CHECK(swatch_color(s, ColorScheme::ByHand, 0) == s.left_color);
    CHECK(swatch_color(s, ColorScheme::ByHand, 1) == s.right_color);
    CHECK(swatch_color(s, ColorScheme::ByHand, 2) == s.unknown_color);
    CHECK(swatch_color(s, ColorScheme::ByPitchClass, 4) == s.pitch_colors[4]);
    CHECK(swatch_color(s, ColorScheme::ByOctave, 8) == s.octave_colors[8]);
    CHECK(swatch_color(s, ColorScheme::ByVelocity, 2) == s.velocity_colors[2]);
    CHECK(swatch_color(s, ColorScheme::Single, 0) == s.single_color);

    const RGBA pink{250, 100, 200, 128};
    CHECK(set_swatch_color(s, ColorScheme::ByOctave, 3, pink));
    CHECK(s.octave_colors[3] == RGBA{250, 100, 200, 255}); // made opaque
    CHECK(set_swatch_color(s, ColorScheme::ByHand, 2, pink));
    CHECK(s.unknown_color == RGBA{250, 100, 200, 255});

    // An index the scheme does not have changes nothing.
    const LookSettings before = s;
    CHECK_FALSE(set_swatch_color(s, ColorScheme::ByHand, 3, pink));
    CHECK_FALSE(set_swatch_color(s, ColorScheme::ByPitchClass, -1, pink));
    CHECK_FALSE(set_swatch_color(s, ColorScheme::Single, 1, pink));
    CHECK(s == before);
    CHECK(swatch_color(s, ColorScheme::ByVelocity, 3) == RGBA{0, 0, 0, 255});
}

TEST_CASE("the blocks take their colour from the scheme's swatches") {
    LookSettings s = in_scheme(ColorScheme::ByOctave);
    s.octave_colors[4] = {10, 200, 30, 255};
    CHECK(note_color(s, 60, 64, Hand::Left) == RGBA{10, 200, 30, 255}); // C4, a white key
    // Below octave 0 the notes take octave 0's colour, above 8 octave 8's.
    CHECK(note_color(s, 0, 64, Hand::Right) == s.octave_colors[0]);
    CHECK(note_color(s, 11, 64, Hand::Right) == s.octave_colors[0]);
    CHECK(note_color(s, 120, 64, Hand::Right) == s.octave_colors[8]);
    CHECK(note_color(s, 127, 64, Hand::Right) == s.octave_colors[8]);

    s = in_scheme(ColorScheme::ByVelocity);
    s.velocity_colors = {RGBA{0, 0, 255, 255}, RGBA{0, 255, 0, 255}, RGBA{255, 0, 0, 255}};
    CHECK(note_color(s, 60, 0, Hand::Right) == RGBA{0, 0, 255, 255});
    CHECK(note_color(s, 60, 127, Hand::Right) == RGBA{255, 0, 0, 255});
    const RGBA mid = note_color(s, 60, 64, Hand::Right);
    CHECK(mid.g > 240);
}

TEST_CASE("a palette applies to the current scheme only and is remembered for it") {
    LookSettings s = in_scheme(ColorScheme::ByPitchClass);
    const LookSettings before = s;
    apply_palette(s, Palette::Ocean);
    CHECK(scheme_swatches(s, ColorScheme::ByPitchClass) ==
          palette_swatches(Palette::Ocean, ColorScheme::ByPitchClass));
    CHECK(s.palettes[static_cast<std::size_t>(ColorScheme::ByPitchClass)] == Palette::Ocean);
    CHECK(matching_palette(s, ColorScheme::ByPitchClass) == Palette::Ocean);
    // The other schemes keep their colours and their palette.
    for (const ColorScheme other : kAllColorSchemes) {
        if (other == ColorScheme::ByPitchClass) {
            continue;
        }
        CHECK(scheme_swatches(s, other) == scheme_swatches(before, other));
        CHECK(matching_palette(s, other) == Palette::Classic);
    }
}

TEST_CASE("a changed swatch makes the colours Custom, and a reset brings the palette back") {
    for (const ColorScheme scheme : kAllColorSchemes) {
        LookSettings s = in_scheme(scheme);
        apply_palette(s, Palette::Sunset);
        const LookSettings applied = s;
        const int last = swatch_count(scheme) - 1;
        set_swatch_color(s, scheme, last, {1, 2, 3, 255});
        CHECK_FALSE(matching_palette(s, scheme).has_value());
        CHECK(s.palettes[static_cast<std::size_t>(scheme)] == Palette::Sunset);
        reset_to_palette(s);
        CHECK(s == applied);
        CHECK(matching_palette(s, scheme) == Palette::Sunset);
    }
    // A reset with nothing changed changes nothing.
    LookSettings s;
    reset_to_palette(s);
    CHECK(s == LookSettings{});
}

TEST_CASE("a look with the Pastel notes matches the Pastel palette") {
    const LookSettings frosted = sample_look(Sample::FrostedPastel);
    CHECK(matching_palette(frosted, ColorScheme::ByPitchClass) == Palette::Pastel);
    CHECK(frosted.palettes[static_cast<std::size_t>(ColorScheme::ByPitchClass)] ==
          Palette::Pastel);
    LookSettings s = frosted;
    reset_to_palette(s);
    CHECK(s == frosted);
}

TEST_CASE("a palette row's strip shows at most six of the scheme's swatches") {
    for (const ColorScheme scheme : kAllColorSchemes) {
        const auto strip = strip_swatches(scheme);
        CHECK(!strip.empty());
        CHECK(strip.size() <= kStripSwatches);
        CHECK(strip.size() == std::min<std::size_t>(kStripSwatches,
                                                    static_cast<std::size_t>(swatch_count(scheme))));
        for (const int i : strip) {
            CHECK(i >= 0);
            CHECK(i < swatch_count(scheme));
        }
    }
}

TEST_CASE("colour vision simulation: typical is unchanged, greys stay grey, red and green meet") {
    const RGBA orange{230, 159, 0, 77};
    CHECK(simulate_vision(orange, ColourVision::Typical) == orange);
    CHECK(simulate_vision(orange, ColourVision::Deuteranopia).a == 77);
    for (const ColourVision v :
         {ColourVision::Protanopia, ColourVision::Deuteranopia, ColourVision::Tritanopia}) {
        for (const int g : {0, 60, 128, 200, 255}) {
            const auto b = static_cast<std::uint8_t>(g);
            CHECK(perceptual_distance(simulate_vision({b, b, b, 255}, v), {b, b, b, 255}) < 0.01);
        }
    }
    // A red and a green of the same lightness are far apart for typical vision and close to one
    // colour for deuteranopia: the case the check exists for.
    const RGBA red = from_oklch(0.7, 0.15, 25.0);
    const RGBA green = from_oklch(0.7, 0.15, 145.0);
    CHECK(perceptual_distance(red, green) > 0.2);
    CHECK(perceptual_distance(simulate_vision(red, ColourVision::Deuteranopia),
                              simulate_vision(green, ColourVision::Deuteranopia)) < 0.05);
    CHECK(min_distance_seen(std::vector<RGBA>{red}, ColourVision::Typical) == 1.0);
}

TEST_CASE("HSV: known colours, the edges, and a round trip gives every colour back to the byte") {
    const Hsv red = to_hsv({255, 0, 0, 255});
    CHECK(red.hue == doctest::Approx(0.0));
    CHECK(red.saturation == doctest::Approx(1.0));
    CHECK(red.value == doctest::Approx(1.0));
    CHECK(to_hsv({0, 0, 255, 255}).hue == doctest::Approx(240.0));
    CHECK(to_hsv({255, 255, 0, 255}).hue == doctest::Approx(60.0));
    CHECK(to_hsv({255, 0, 128, 255}).hue == doctest::Approx(329.8824).epsilon(0.001));
    // S = 0: a grey has no hue and no saturation, its value is its level.
    const Hsv grey = to_hsv({128, 128, 128, 255});
    CHECK(grey.hue == 0.0);
    CHECK(grey.saturation == 0.0);
    CHECK(grey.value == doctest::Approx(128.0 / 255.0));
    CHECK(to_hsv({255, 255, 255, 255}).value == 1.0);
    // V = 0: black, whatever hue and saturation are given.
    const Hsv black = to_hsv({0, 0, 0, 255});
    CHECK(black.hue == 0.0);
    CHECK(black.saturation == 0.0);
    CHECK(black.value == 0.0);
    for (const double hue : {0.0, 90.0, 213.0, 360.0}) {
        for (const double s : {0.0, 0.5, 1.0}) {
            CHECK(from_hsv(hue, s, 0.0) == RGBA{0, 0, 0, 255});
        }
    }
    // Hue 0 and hue 360 are the same colour; S = 0 gives the grey of the value at any hue.
    for (const double v : {0.0, 0.3, 1.0}) {
        for (const double s : {0.0, 0.4, 1.0}) {
            CHECK(from_hsv(0.0, s, v) == from_hsv(360.0, s, v));
        }
        CHECK(from_hsv(77.0, 0.0, v) == from_hsv(250.0, 0.0, v));
    }
    CHECK(from_hsv(360.0, 1.0, 1.0) == RGBA{255, 0, 0, 255});
    CHECK(from_hsv(0.0, 0.0, 1.0) == RGBA{255, 255, 255, 255});
    for (int r = 0; r < 256; r += 5) {
        for (int g = 0; g < 256; g += 7) {
            for (int b = 0; b < 256; b += 11) {
                const RGBA c{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                             static_cast<std::uint8_t>(b), 255};
                const Hsv h = to_hsv(c);
                CHECK(from_hsv(h.hue, h.saturation, h.value) == c);
                CHECK(pick_color(pick_of(c)) == c);
            }
        }
    }
}

TEST_CASE("the picker: the hue bar and the square move one axis each") {
    const ColourPick p = pick_of({200, 80, 40, 255});
    const ColourPick blue = with_hue_at(p, 240.0 / 360.0);
    CHECK(blue.hue == doctest::Approx(240.0));
    CHECK(blue.saturation == p.saturation);
    CHECK(blue.value == p.value);
    CHECK(hue_position(blue) == doctest::Approx(240.0 / 360.0));
    CHECK(with_hue_at(p, -1.0).hue == 0.0);
    CHECK(with_hue_at(p, 2.0).hue == 360.0);
    CHECK(pick_color(with_hue_at(p, 1.0)) == pick_color(with_hue_at(p, 0.0)));

    // The square: saturation along x, brightness up the y axis (the top edge brightest).
    const ColourPick moved = with_square_at(p, 0.25, 0.4);
    CHECK(moved.hue == p.hue);
    CHECK(moved.saturation == doctest::Approx(0.25));
    CHECK(moved.value == doctest::Approx(0.6));
    CHECK(square_x(moved) == doctest::Approx(0.25));
    CHECK(square_y(moved) == doctest::Approx(0.4));
    // The corners, and positions off the square, are its edges.
    CHECK(pick_color(with_square_at(p, 0.0, 0.0)) == RGBA{255, 255, 255, 255});
    CHECK(pick_color(with_square_at(p, 1.0, 0.0)) == pick_color({p.hue, 1.0, 1.0}));
    CHECK(pick_color(with_square_at(p, 0.0, 1.0)) == RGBA{0, 0, 0, 255});
    CHECK(pick_color(with_square_at(p, 1.0, 1.0)) == RGBA{0, 0, 0, 255});
    CHECK(with_square_at(p, -3.0, 9.0).saturation == 0.0);
    CHECK(with_square_at(p, -3.0, 9.0).value == 0.0);
    CHECK(with_square_at(p, 9.0, -3.0).saturation == 1.0);
    CHECK(with_square_at(p, 9.0, -3.0).value == 1.0);

    // A grey or a black keeps the hue the control had: the hue bar stays where it was.
    const ColourPick grey = with_square_at(blue, 0.0, 0.5);
    CHECK(grey.hue == doctest::Approx(240.0));
    CHECK(hue_position(grey) == doctest::Approx(240.0 / 360.0));
    const ColourPick dark = with_square_at(blue, 1.0, 1.0);
    CHECK(dark.hue == doctest::Approx(240.0));
    // The positions of a colour read back, and a bar or square position gives the colour that sits
    // there (the vivid corner of every hue is a full-saturation colour).
    for (int i = 0; i <= 12; ++i) {
        const double t = static_cast<double>(i) / 12.0;
        const RGBA c = pick_color(with_square_at(with_hue_at(p, t), 1.0, 0.0));
        const Hsv h = to_hsv(c);
        CHECK(h.saturation == doctest::Approx(1.0));
        CHECK(h.value == doctest::Approx(1.0));
        CHECK(pick_color(pick_of(c)) == c);
    }
}

TEST_CASE("quick picks: eight hues in wheel order, light to deep down each column, greys last") {
    const QuickPickGrid& grid = quick_picks();
    static_assert(kQuickPickCount >= 30 && kQuickPickCount <= 40);
    // Every colour once, all opaque.
    std::vector<RGBA> all;
    for (const auto& row : grid) {
        for (const RGBA c : row) {
            CHECK(c.a == 255);
            all.push_back(c);
        }
    }
    CHECK(all.size() == kQuickPickCount);
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            CHECK(all[i] != all[j]);
        }
    }
    // Across each shade row the hues rise in wheel order, and the three rows share a hue per column.
    for (std::size_t r = 0; r < kQuickShades; ++r) {
        for (std::size_t c = 0; c < kQuickColumns; ++c) {
            const Hsv h = to_hsv(grid[r][c]);
            CHECK(h.hue == doctest::Approx(kQuickHues[c]).epsilon(0.01));
            CHECK(h.value > 0.5);
            if (c > 0) {
                CHECK(h.hue > to_hsv(grid[r][c - 1]).hue);
            }
        }
    }
    CHECK(kQuickHues.front() == 0.0);
    CHECK(kQuickHues.back() < 360.0);
    // Down each column: light (half way to white), bright (full saturation and brightness), deep.
    for (std::size_t c = 0; c < kQuickColumns; ++c) {
        CHECK(to_hsv(grid[0][c]).saturation == doctest::Approx(kQuickLightSaturation).epsilon(0.01));
        CHECK(to_hsv(grid[1][c]).saturation == doctest::Approx(1.0));
        CHECK(to_hsv(grid[1][c]).value == doctest::Approx(1.0));
        CHECK(to_hsv(grid[2][c]).value == doctest::Approx(kQuickDeepValue).epsilon(0.01));
        for (std::size_t r = 1; r < kQuickShades; ++r) {
            CHECK(relative_luminance(grid[r][c]) < relative_luminance(grid[r - 1][c]));
        }
    }
    // Pure red, yellow, green, cyan, blue are the bright row's columns.
    CHECK(grid[1][0] == RGBA{255, 0, 0, 255});
    CHECK(grid[1][2] == RGBA{255, 255, 0, 255});
    CHECK(grid[1][3] == RGBA{0, 255, 0, 255});
    CHECK(grid[1][4] == RGBA{0, 255, 255, 255});
    CHECK(grid[1][5] == RGBA{0, 0, 255, 255});
    // The greys: white to black, evenly, each darker than the one before.
    const auto& greys = grid[kQuickShades];
    CHECK(greys.front() == RGBA{255, 255, 255, 255});
    CHECK(greys.back() == RGBA{0, 0, 0, 255});
    for (std::size_t c = 0; c < kQuickColumns; ++c) {
        CHECK(greys[c].r == greys[c].g);
        CHECK(greys[c].g == greys[c].b);
        if (c > 0) {
            CHECK(greys[c].r < greys[c - 1].r);
        }
    }
    // Every one is a colour the picker can hold and give back.
    for (const RGBA c : all) {
        CHECK(pick_color(pick_of(c)) == c);
    }
}

TEST_CASE("the picker keeps a grey's or a black's hue and takes a colour's own") {
    const ColourPick blue{240.0, 1.0, 1.0};
    // A grey and a black have no hue: the previous one stays.
    CHECK(pick_keeping_hue({120, 120, 120, 255}, blue).hue == 240.0);
    CHECK(pick_keeping_hue({255, 255, 255, 255}, blue).hue == 240.0);
    CHECK(pick_keeping_hue({0, 0, 0, 255}, blue).hue == 240.0);
    // Whatever the previous pick was, the colour comes back to the byte.
    for (const RGBA c : {RGBA{120, 120, 120, 255}, RGBA{0, 0, 0, 255}, RGBA{200, 80, 40, 255}}) {
        CHECK(pick_color(pick_keeping_hue(c, blue)) == c);
    }
    // A colour takes its own hue.
    const ColourPick orange = pick_keeping_hue({200, 80, 40, 255}, blue);
    CHECK(orange.hue == pick_of({200, 80, 40, 255}).hue);
    CHECK(orange.hue != 240.0);
    CHECK(orange == pick_of({200, 80, 40, 255}));
}

TEST_CASE("a picked colour keeps its note name readable, white, yellow and near black included") {
    // Every quick pick, the corners and a grid of the whole square at several hues, and the extremes
    // named for the check: on a white key and a (darkened) black key, as the left and right hand.
    std::vector<RGBA> colours;
    for (const auto& row : quick_picks()) {
        colours.insert(colours.end(), row.begin(), row.end());
    }
    colours.push_back({255, 255, 255, 255});
    colours.push_back({255, 255, 0, 255});
    colours.push_back({1, 1, 1, 255});
    colours.push_back({0, 0, 0, 255});
    for (int h = 0; h < 360; h += 30) {
        for (int x = 0; x <= 4; ++x) {
            for (int y = 0; y <= 4; ++y) {
                colours.push_back(pick_color(with_square_at(
                    {static_cast<double>(h), 0.0, 1.0}, x / 4.0, y / 4.0)));
            }
        }
    }
    for (const RGBA c : colours) {
        CAPTURE(static_cast<int>(c.r));
        CAPTURE(static_cast<int>(c.g));
        CAPTURE(static_cast<int>(c.b));
        CHECK(contrast_ratio(label_color_for(c), c) >= kLabelMinContrast);
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            LookSettings look;
            look.color_scheme = ColorScheme::ByHand;
            REQUIRE(set_swatch_color(look, ColorScheme::ByHand, hand == Hand::Left ? 0 : 1, c));
            for (const int note : {60, 61}) { // C4 on a white key, C#4 on a black key
                const RGBA block = note_color(look, note, 100, hand);
                CHECK(contrast_ratio(note_label_color(look, note, 100, hand), block) >=
                      kLabelMinContrast);
            }
        }
    }
}

TEST_CASE("recent colours: newest first, no repeats, two rows of eight, opaque") {
    static_assert(kRecentColours == 16);
    RecentColours r;
    CHECK(r.empty());
    const RGBA a{1, 0, 0, 255};
    const RGBA b{2, 0, 0, 255};
    const RGBA c{3, 0, 0, 255};
    r.add(a);
    r.add(b);
    r.add(c);
    REQUIRE(r.size() == 3);
    CHECK(r.items()[0] == c);
    CHECK(r.items()[2] == a);
    // A colour already there moves to the front and is not added twice.
    r.add(a);
    REQUIRE(r.size() == 3);
    CHECK(r.items()[0] == a);
    CHECK(r.items()[1] == c);
    CHECK(r.items()[2] == b);
    // Exactly sixteen fit; the seventeenth pushes the oldest out.
    for (std::uint8_t i = 4; i <= kRecentColours; ++i) {
        r.add({i, 0, 0, 255});
    }
    CHECK(r.size() == kRecentColours);
    CHECK(r.items()[kRecentColours - 1] == b);
    r.add({100, 0, 0, 255});
    CHECK(r.size() == kRecentColours);
    CHECK(r.items()[0] == RGBA{100, 0, 0, 255});
    CHECK(r.items()[kRecentColours - 1] == c);
    // The front one again changes nothing; alpha is dropped.
    r.add({100, 0, 0, 10});
    CHECK(r.size() == kRecentColours);
    CHECK(r.items()[0] == RGBA{100, 0, 0, 255});
    CHECK(r.items()[1] == RGBA{static_cast<std::uint8_t>(kRecentColours), 0, 0, 255});
}

TEST_CASE("palettes and the octave and velocity colours are saved and read back") {
    LookSettings s = in_scheme(ColorScheme::ByOctave);
    apply_palette(s, Palette::Vivid);
    s.color_scheme = ColorScheme::ByVelocity;
    apply_palette(s, Palette::Mono);
    set_swatch_color(s, ColorScheme::ByVelocity, 1, {9, 99, 199, 255});
    const std::string text = serialize(s);
    CHECK(text.find("palette_by_octave = vivid\n") != std::string::npos);
    CHECK(text.find("palette_by_velocity = mono\n") != std::string::npos);
    CHECK(text.find("palette_by_hand = classic\n") != std::string::npos);
    CHECK(text.find("velocity_color_1 = #0963C7\n") != std::string::npos);
    CHECK(text.find("octave_color_8 = ") != std::string::npos);
    ParseReport report;
    const LookSettings back = parse_look(text, &report);
    CHECK(report.clean());
    CHECK(back == s);
    CHECK(matching_palette(back, ColorScheme::ByOctave) == Palette::Vivid);
    CHECK_FALSE(matching_palette(back, ColorScheme::ByVelocity).has_value());
    // The reset still knows which palette the changed colours came from.
    LookSettings reset = back;
    reset_to_palette(reset);
    CHECK(matching_palette(reset, ColorScheme::ByVelocity) == Palette::Mono);
}

TEST_CASE("a look saved before palettes reads as the colours it had") {
    // Default colours: Classic everywhere, and the octave and velocity ramps as they were.
    const LookSettings plain = parse_look("version = 1\ncolor_scheme = by_octave\n");
    CHECK(plain.octave_colors == default_octave_colors());
    CHECK(plain.velocity_colors == default_velocity_colors());
    for (const ColorScheme scheme : kAllColorSchemes) {
        CHECK(plain.palettes[static_cast<std::size_t>(scheme)] == Palette::Classic);
    }

    // Own hand colours: Custom, with Classic to reset to.
    ParseReport report;
    const LookSettings own =
        parse_look("version = 1\nleft_color = #112233\nright_color = #445566\n", &report);
    CHECK(report.clean());
    CHECK(own.left_color == RGBA{0x11, 0x22, 0x33, 255});
    CHECK_FALSE(matching_palette(own, ColorScheme::ByHand).has_value());
    CHECK(own.palettes[0] == Palette::Classic);

    // The Pastel notes, saved before palettes: shown as Pastel.
    LookSettings frosted = sample_look(Sample::FrostedPastel);
    std::string old_text;
    for (std::string_view line : {std::string_view("version = 1\n")}) {
        old_text += line;
    }
    const std::string full = serialize(frosted);
    std::size_t pos = 0;
    while (pos < full.size()) {
        const std::size_t eol = full.find('\n', pos);
        const std::string line = full.substr(pos, eol - pos + 1);
        pos = eol + 1;
        if (line.rfind("palette_", 0) != 0 && line.rfind("octave_color_", 0) != 0 &&
            line.rfind("velocity_color_", 0) != 0 && line.rfind("version", 0) != 0) {
            old_text += line;
        }
    }
    const LookSettings read = parse_look(old_text);
    CHECK(read.palettes[static_cast<std::size_t>(ColorScheme::ByPitchClass)] == Palette::Pastel);
    CHECK(read == frosted);
}

TEST_CASE("a bad palette name is reported and the palette comes from the colours") {
    ParseReport report;
    const LookSettings s = parse_look("version = 1\npalette_by_hand = rainbow\n", &report);
    REQUIRE(report.issues.size() == 1);
    CHECK(report.issues[0].kind == ParseIssue::Kind::BadValue);
    CHECK(report.issues[0].key == "palette_by_hand");
    CHECK(s.palettes[0] == Palette::Classic);
    // A stray value in memory is brought back to Classic.
    LookSettings stray;
    stray.palettes[2] = static_cast<Palette>(99);
    CHECK_FALSE(stray.is_valid());
    CHECK(stray.clamped().palettes[2] == Palette::Classic);
}

TEST_CASE("the piano's octaves, A0 to C8, keep the colours they had before they were settings") {
    // Before the octave colours became look colours: one lightness, hues 32 degrees apart from
    // blue at octave 0. Octaves -1 and 9 had colours of their own; now they take 0's and 8's.
    const LookSettings look = in_scheme(ColorScheme::ByOctave);
    for (int note = 21; note <= 108; ++note) { // A0 .. C8
        CAPTURE(note);
        const int index = octave(note) + 1;
        const RGBA before = from_oklch(0.78, 0.13, 250.0 + 32.0 * index);
        if (!is_black_key(note)) {
            CHECK(note_color(look, note, 64, Hand::Right) == before);
        }
    }
    // Outside the piano the end colours repeat.
    CHECK(note_color(look, 0, 64, Hand::Right) == look.octave_colors[0]);
    CHECK(note_color(look, 127, 64, Hand::Right) == look.octave_colors[8]);
}

TEST_CASE("a malformed octave or velocity colour keeps the default and is named in the report") {
    const LookSettings defaults;
    ParseReport report;
    const LookSettings got = parse_look("version = 1\noctave_color_3 = banana\n"
                                        "octave_color_9 = #123456\nvelocity_color_1 = #12\n",
                                        &report);
    CHECK(got.octave_colors == defaults.octave_colors);
    CHECK(got.velocity_colors == defaults.velocity_colors);
    CHECK_FALSE(report.clean());
    const auto named = [&](const char* key) {
        std::string all;
        for (const auto& issue : report.issues) {
            all += issue.key + "\n";
        }
        return all.find(key) != std::string::npos;
    };
    CHECK(named("octave_color_3"));
    CHECK(named("octave_color_9"));
    CHECK(named("velocity_color_1"));
}

TEST_CASE("high contrast lifts every palette the same way, and an edited swatch too") {
    constexpr ContrastTargets kNormal{kHighContrastTarget, kHighContrastMinimum};
    const auto same = [](ContrastTargets a, ContrastTargets b) {
        return a.white == b.white && a.black == b.black;
    };
    for (const Palette palette : kAllPalettes) {
        for (const ColorScheme scheme : kAllColorSchemes) {
            CAPTURE(static_cast<int>(palette));
            CAPTURE(static_cast<int>(scheme));
            LookSettings look = in_scheme(scheme);
            apply_palette(look, palette);
            look.high_contrast = true;
            CHECK(same(high_contrast_targets(look), kNormal));
            // High contrast off: no lift at all.
            LookSettings off = look;
            off.high_contrast = false;
            CHECK(same(high_contrast_targets(off), ContrastTargets{}));
            // Every note of a lifted palette reaches the white-key target on the background.
            for (int note = 21; note <= 108; ++note) {
                if (!is_black_key(note)) {
                    CHECK(contrast_ratio(note_color(look, note, 64, Hand::Right),
                                         kBackgroundGradientTop) >= kHighContrastTarget);
                }
            }
        }
    }
}

TEST_CASE("a settings file that names the removed colour-blind palette loads as Classic") {
    ParseReport report;
    const LookSettings got = parse_look("version = 1\ncolor_scheme = by_pitch_class\n"
                                        "palette_by_hand = colour_blind\n"
                                        "palette_by_pitch_class = colour_blind\n"
                                        "palette_single = vivid\n",
                                        &report);
    for (const ColorScheme scheme : {ColorScheme::ByHand, ColorScheme::ByPitchClass}) {
        CHECK(got.palettes[static_cast<std::size_t>(scheme)] == Palette::Classic);
    }
    CHECK(got.palettes[static_cast<std::size_t>(ColorScheme::Single)] == Palette::Vivid);
    CHECK(report.clean());
    // The same in a saved look slot.
    LookDocument doc;
    doc.user_presets.save_slot(1, "Old", LookSettings{});
    std::string text = serialize_document(doc);
    const std::string key = "slot_2_palette_by_octave = classic\n";
    const std::size_t at = text.find(key);
    REQUIRE(at != std::string::npos);
    text.replace(at, key.size(), "slot_2_palette_by_octave = colour_blind\n");
    ParseReport slot_report;
    const LookDocument back = parse_look_document(text, &slot_report);
    CHECK(back.user_presets.slots[1].look.palettes[static_cast<std::size_t>(
              ColorScheme::ByOctave)] == Palette::Classic);
    CHECK(slot_report.clean());
}
