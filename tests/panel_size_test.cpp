// SPDX-License-Identifier: GPL-3.0-or-later
// The panel's physical size: the --panel-mm flag, an EDID block's size, the believable-size check
// and the pixels-per-millimetre maths (core panel_size.hpp).
#include "etude/panel_size.hpp"

#include <array>
#include <cstdint>

#include <doctest/doctest.h>

using namespace etude;

namespace {

// A 128-byte EDID block with the given first detailed timing size and basic-block centimetres.
std::array<std::uint8_t, 128> edid(int timing_w_mm, int timing_h_mm, int cm_w, int cm_h,
                                   bool timing = true) {
    std::array<std::uint8_t, 128> b{};
    const std::uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    for (int i = 0; i < 8; ++i) {
        b[static_cast<std::size_t>(i)] = header[i];
    }
    b[21] = static_cast<std::uint8_t>(cm_w);
    b[22] = static_cast<std::uint8_t>(cm_h);
    if (timing) {
        b[54] = 0x02; // a pixel clock
        b[55] = 0x3A;
        b[66] = static_cast<std::uint8_t>(timing_w_mm & 0xFF);
        b[67] = static_cast<std::uint8_t>(timing_h_mm & 0xFF);
        b[68] = static_cast<std::uint8_t>(((timing_w_mm >> 8) << 4) | (timing_h_mm >> 8));
    }
    return b;
}

} // namespace

TEST_CASE("panel size: the flag takes width x height in millimetres") {
    const auto mm = parse_panel_mm("344x194");
    REQUIRE(mm.has_value());
    CHECK(mm->w == 344.0f);
    CHECK(mm->h == 194.0f);
    CHECK(parse_panel_mm("154X86.5")->h == 86.5f);
    CHECK(parse_panel_mm(" 344 x 194 ")->w == 344.0f);
    CHECK(parse_panel_mm("344\xC3\x97" "194")->h == 194.0f);
}

TEST_CASE("panel size: a bad flag value is refused") {
    CHECK_FALSE(parse_panel_mm("").has_value());
    CHECK_FALSE(parse_panel_mm("344").has_value());
    CHECK_FALSE(parse_panel_mm("344x").has_value());
    CHECK_FALSE(parse_panel_mm("x194").has_value());
    CHECK_FALSE(parse_panel_mm("abcx194").has_value());
    CHECK_FALSE(parse_panel_mm("344x194mm").has_value());
    CHECK_FALSE(parse_panel_mm("0x0").has_value());
    CHECK_FALSE(parse_panel_mm("-344x194").has_value());
    CHECK_FALSE(parse_panel_mm("10x10").has_value());     // under 20 mm a side
    CHECK_FALSE(parse_panel_mm("99999x194").has_value()); // over 5 m
    CHECK_FALSE(parse_panel_mm("nanx194").has_value());
}

TEST_CASE("panel size: EDID gives the first detailed timing's size in millimetres") {
    // A 15.6 inch 16:9 panel: 344 x 194 mm in the timing, 34 x 19 cm in the basic block.
    const auto b = edid(344, 194, 34, 19);
    const auto mm = panel_mm_from_edid(b.data(), b.size());
    REQUIRE(mm.has_value());
    CHECK(mm->w == 344.0f);
    CHECK(mm->h == 194.0f);
}

TEST_CASE("panel size: EDID sizes over 255 mm use the high nibbles") {
    const auto b = edid(527, 296, 53, 30); // a 24 inch monitor
    const auto mm = panel_mm_from_edid(b.data(), b.size());
    REQUIRE(mm.has_value());
    CHECK(mm->w == 527.0f);
    CHECK(mm->h == 296.0f);
}

TEST_CASE("panel size: EDID without a timing size falls back to the centimetres") {
    auto b = edid(0, 0, 34, 19);
    auto mm = panel_mm_from_edid(b.data(), b.size());
    REQUIRE(mm.has_value());
    CHECK(mm->w == 340.0f);
    CHECK(mm->h == 190.0f);
    b = edid(0, 0, 34, 19, false); // no first timing at all
    mm = panel_mm_from_edid(b.data(), b.size());
    REQUIRE(mm.has_value());
    CHECK(mm->w == 340.0f);
}

TEST_CASE("panel size: a projector's EDID reports no size") {
    auto b = edid(0, 0, 0, 0);
    CHECK_FALSE(panel_mm_from_edid(b.data(), b.size()).has_value());
    b = edid(0, 0, 16, 0); // an aspect ratio, not a size
    CHECK_FALSE(panel_mm_from_edid(b.data(), b.size()).has_value());
}

TEST_CASE("panel size: bytes that are not an EDID are refused") {
    auto b = edid(344, 194, 34, 19);
    b[0] = 0x01;
    CHECK_FALSE(panel_mm_from_edid(b.data(), b.size()).has_value());
    const auto ok = edid(344, 194, 34, 19);
    CHECK_FALSE(panel_mm_from_edid(ok.data(), 64).has_value()); // cut short
    CHECK_FALSE(panel_mm_from_edid(ok.data(), 0).has_value());
    CHECK_FALSE(panel_mm_from_edid(nullptr, 128).has_value());
}

TEST_CASE("panel size: pixels per millimetre") {
    const PanelMm pi{344.0f, 194.0f};
    CHECK(panel_px_per_mm(1920.0f, 1080.0f, pi) == doctest::Approx(5.58f).epsilon(0.01));
    CHECK(panel_px_per_mm(1366.0f, 768.0f, pi) == doctest::Approx(3.97f).epsilon(0.01));
    const PanelMm seven{154.0f, 86.0f};
    CHECK(panel_px_per_mm(800.0f, 480.0f, seven) == doctest::Approx(5.38f).epsilon(0.01));
    // 96 dpi: 1920 pixels over 508 mm.
    CHECK(panel_px_per_mm(1920.0f, 1080.0f, {508.0f, 285.75f}) ==
          doctest::Approx(kFallbackPxPerMm).epsilon(0.001));
    CHECK(panel_px_per_mm(1920.0f, 1080.0f, {}) == 0.0f);
    CHECK(panel_px_per_mm(0.0f, 1080.0f, pi) == 0.0f);
    CHECK(panel_px_per_mm(1920.0f, -1.0f, pi) == 0.0f);
}

TEST_CASE("panel size: the Settings width gives the height from the pixels' shape") {
    const PanelMm mm = panel_mm_from_width(344.0f, 1920.0f, 1080.0f);
    CHECK(mm.w == 344.0f);
    CHECK(mm.h == doctest::Approx(193.5f));
    CHECK_FALSE(panel_mm_from_width(0.0f, 1920.0f, 1080.0f).known());
    CHECK_FALSE(panel_mm_from_width(344.0f, 0.0f, 1080.0f).known());
}

TEST_CASE("panel size: a believable size passes, a placeholder does not") {
    CHECK(plausible_panel_mm({344.0f, 194.0f}, 1920.0f, 1080.0f).has_value());
    CHECK(plausible_panel_mm({154.0f, 86.0f}, 800.0f, 480.0f).has_value());
    // The 16:9 placeholder 160 x 90 mm on a 1080p panel is 304 dpi: believable on its own, but a
    // 4:3 panel reported 16:9 has the wrong shape.
    CHECK_FALSE(plausible_panel_mm({400.0f, 225.0f}, 1024.0f, 768.0f).has_value());
    // 1 m wide for 1920 pixels is 49 dpi (a projector): allowed; 3 m is not.
    CHECK(plausible_panel_mm({1000.0f, 562.5f}, 1920.0f, 1080.0f).has_value());
    CHECK_FALSE(plausible_panel_mm({3000.0f, 1687.5f}, 1920.0f, 1080.0f).has_value());
    // Pixels in a 20 mm panel: far over 600 dpi.
    CHECK_FALSE(plausible_panel_mm({35.0f, 20.0f}, 1920.0f, 1080.0f).has_value());
    CHECK_FALSE(plausible_panel_mm({}, 1920.0f, 1080.0f).has_value());
    CHECK_FALSE(plausible_panel_mm({344.0f, 194.0f}, 0.0f, 1080.0f).has_value());
}

TEST_CASE("panel size: a portrait size on a landscape screen is turned round") {
    const auto mm = plausible_panel_mm({194.0f, 344.0f}, 1920.0f, 1080.0f);
    REQUIRE(mm.has_value());
    CHECK(mm->w == 344.0f);
    CHECK(mm->h == 194.0f);
}

TEST_CASE("panel size: sources are named for the log line") {
    CHECK(std::string_view(panel_source_name(PanelSource::Flag)) == "--panel-mm");
    CHECK(std::string_view(panel_source_name(PanelSource::Edid)) == "the display's EDID");
    CHECK(std::string_view(panel_source_name(PanelSource::System)) == "the system");
    CHECK(std::string_view(panel_source_name(PanelSource::Setting)) ==
          "the screen size setting");
    CHECK(std::string_view(panel_source_name(PanelSource::None)) == "nothing (96 dpi)");
}

TEST_CASE("panel size: 12 mm is 12 mm at every size the UI is audited at") {
    // Hit area px = kMinHitMm x px per mm; the design-point maths lives in tokens (kHit).
    CHECK(kMinHitMm * panel_px_per_mm(1920.0f, 1080.0f, {344.0f, 194.0f}) ==
          doctest::Approx(66.9f).epsilon(0.01));
    CHECK(kMinHitGapMm < kMinHitMm);
    CHECK(kTapSlopMm > 0.0f);
}
