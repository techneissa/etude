// SPDX-License-Identifier: GPL-3.0-or-later
// The two-tap rows of Settings (confirm_arm.hpp): Reset all's first tap only arms, the second
// resets, and an arm does not survive leaving the page or closing the sheet. The page code is read
// for the calls that carry these rules, so a row that bypasses them fails here.
#include "../app/ui/confirm_arm.hpp"
#include "../app/ui_model.hpp"

#include "etude/look.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace etude;
using namespace etude::app::ui;

namespace {

std::string source(const char* name) {
    std::ifstream in(std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / "ui" / name);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// The text of the function whose header begins `header`, up to the next line that is only `}`.
std::string function_text(const std::string& file, const std::string& header) {
    const std::size_t at = file.find(header);
    REQUIRE(at != std::string::npos);
    const std::size_t end = file.find("\n}\n", at);
    REQUIRE(end != std::string::npos);
    return file.substr(at, end - at);
}

LookSettings changed_look() {
    LookSettings look;
    look.style_intensity = 0.2f;
    look.block_glow = 7;
    look.note_style = NoteStyle::Neon;
    return look;
}

} // namespace

TEST_CASE("Reset all: the first tap arms and changes nothing") {
    LookSettings look = changed_look();
    const LookSettings before = look;
    CHECK(reset_all_tap(look, false) == TapStep::Arm);
    CHECK(look == before);
}

TEST_CASE("Reset all: a tap while armed puts the look back, keeping the player's own choices") {
    LookSettings look = changed_look();
    look.octave_numbering = OctaveNumbering::Yamaha;
    const LookSettings expected = reset_look(look);
    CHECK(reset_all_tap(look, true) == TapStep::Fire);
    CHECK(look == expected);
    CHECK(look.style_intensity == LookSettings{}.style_intensity);
    CHECK(look.octave_numbering == OctaveNumbering::Yamaha);
}

TEST_CASE("Reset all: the second tap leaves the saved configs as they were") {
    etude::app::UiModel model;
    model.look = changed_look();
    REQUIRE(model.user_presets.save_slot(0, "Evening", changed_look()));
    const UserPresets saved = model.user_presets;
    CHECK(reset_all_tap(model.look, false) == TapStep::Arm);
    CHECK(reset_all_tap(model.look, true) == TapStep::Fire);
    CHECK(model.look == reset_look(changed_look()));
    CHECK(model.user_presets == saved); // the look is reset, the saved configs are not
}

TEST_CASE("a press elsewhere disarms: arm, tap a card, tap Reset resets nothing") {
    constexpr std::uint32_t kReset = 41;
    constexpr std::uint32_t kCard = 7;
    LookSettings look = changed_look();
    const LookSettings before = look;
    std::uint32_t armed = 0;
    // The first tap on Reset arms it.
    REQUIRE(reset_all_tap(look, armed == kReset) == TapStep::Arm);
    armed = kReset;
    // A press that lands on a card puts it back; so does one on nothing.
    CHECK_FALSE(arm_kept_by_press(armed, true, kCard));
    CHECK_FALSE(arm_kept_by_press(armed, true, 0));
    armed = arm_kept_by_press(armed, true, kCard) ? armed : 0;
    // The tap on Reset now is a first tap again: nothing is reset.
    CHECK(reset_all_tap(look, armed == kReset) == TapStep::Arm);
    CHECK(look == before);
}

TEST_CASE("a press on the armed control itself, or no press at all, keeps it armed") {
    constexpr std::uint32_t kReset = 41;
    CHECK(arm_kept_by_press(kReset, true, kReset)); // the second tap on Reset
    CHECK(arm_kept_by_press(kReset, false, 7));     // nothing pressed this frame
    CHECK(arm_kept_by_press(kReset, false, 0));
    CHECK(arm_kept_by_press(0, true, 7)); // nothing armed: nothing to keep
}

TEST_CASE("an arm lasts only on the page it was made on, while the sheet stays open") {
    CHECK(arm_kept(5, 100, 5, 101));  // the next frame, the same page
    CHECK(arm_kept(5, 100, 5, 100));  // the same frame (a page measured twice)
    CHECK_FALSE(arm_kept(5, 100, 2, 101)); // another page: the arm is gone
    CHECK_FALSE(arm_kept(5, 100, 5, 130)); // the sheet was closed and opened again
    CHECK_FALSE(arm_kept(-1, -10, 5, 0));  // nothing armed before
}

TEST_CASE("the pages and the sheet use these rules") {
    const std::string configs = source("pages_configs.cpp");
    const std::string row = function_text(configs, "void reset_all_row(");
    CHECK(row.find("reset_all_tap(model.look, reset_armed)") != std::string::npos);
    CHECK(row.find("reset_look(") == std::string::npos); // no reset outside the rule
    const std::string sheet_text = source("sheet.cpp");
    const std::string stale = function_text(sheet_text, "void SettingsSheet::end_stale_arm(");
    CHECK(stale.find("arm_kept_by_press(k.confirm_id, clicked,") !=
          std::string::npos);
    const std::string pages = source("pages.cpp");
    const std::string names = function_text(pages, "void sheet_names_row(");
    CHECK(names.find("sheet_names_choice(look)") != std::string::npos);
    CHECK(names.find("set_sheet_names_choice(look, shown)") != std::string::npos);
    CHECK(names.find("names_on_sheet =") == std::string::npos); // the write goes through the helper
    const std::string sheet = source("sheet.cpp");
    CHECK(sheet.find("arm_kept(arm_page_, arm_frame_,") != std::string::npos);
    CHECK(sheet.find("k.confirm_id = 0;") != std::string::npos);
}
