// SPDX-License-Identifier: GPL-3.0-or-later
// The toast's Undo chip is asked for the width its larger words need from 1366 points wide
// (undo_chip.hpp): without that it was drawn as wide as "Undo" needs at the kit's own size and the
// larger word was cut to "Un...".
#include "../app/ui/undo_chip.hpp"

#include "../app/ui/tokens.hpp"

#include "atkinson_metrics.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace etude::app::ui;

namespace {

// The chip's words at `pt` design points times `u`, plus the chip's own padding (the same either
// size: only the words grow).
float chip_w(bool bold, float pt, float u) {
    return etude::test::text_em(bold, "Undo") * pt * u + 2.0f * tok::kS5 * u;
}

} // namespace

TEST_CASE("from 1366 points wide the Undo chip is asked for what its larger words need") {
    for (const float window_w : {1366.0f, 1920.0f}) {
        const int step = tok::wide_step(window_w);
        REQUIRE(step >= 1);
        const float u = window_w / 1366.0f;
        const float button_pt = tok::popup_type(tok::kLabel, step, tok::PopupText::Button);
        REQUIRE(button_pt > tok::kLabel); // the words step up
        for (const bool bold : {false, true}) {
            const float natural = chip_w(bold, tok::kLabel, u); // what chip() measures on its own
            const float needed = chip_w(bold, button_pt, u);    // what the larger words need
            CAPTURE(window_w);
            REQUIRE(needed > natural); // so a chip left at its natural width cuts "Undo"
            // Not asked (the old code): drawn at the natural width, too narrow for the words.
            CHECK(drawn_chip_w(natural, 0.0f) < needed);
            // Asked as the toast now does: drawn wide enough.
            CHECK(drawn_chip_w(natural, undo_chip_asked_width(step, needed)) >= needed);
        }
    }
}

TEST_CASE("under 1366 points the Undo chip asks for nothing and keeps its own width") {
    const int step = tok::wide_step(800.0f);
    CHECK(step == 0);
    CHECK(undo_chip_asked_width(step, 123.0f) == 0.0f);
    CHECK(drawn_chip_w(60.0f, undo_chip_asked_width(step, 123.0f)) == 60.0f);
}

TEST_CASE("the toast and the chip use these rules (the kit cannot be drawn in a unit test)") {
    const auto source = [](const char* name) {
        std::ifstream in(std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / "ui" / name);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    };
    CHECK(source("kit_toast.cpp").find("undo_chip_asked_width(step, chip_w)") !=
          std::string::npos);
    CHECK(source("kit_controls.cpp")
              .find("drawn_chip_w(chip_width(k, o.icon, text, o.trail), o.width)") !=
          std::string::npos);
}
