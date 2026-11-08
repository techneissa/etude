// SPDX-License-Identifier: GPL-3.0-or-later
// The reset button of a slider or stepper is always there: greyed (and doing nothing) while the value
// is the setting's own default, active once it differs; a tap on the active one puts the default
// back in the setting itself, and the toast's Undo puts the exact earlier value back, drawn or not
// (app/ui/reset_to.hpp). The rows' wiring is checked by `etude --check-settings`, which taps the
// real buttons (app/settings_check.cpp); here the rules, and that every settings row takes its
// default from the settings structs.
#include "../app/ui/reset_to.hpp"
#include "../app/ui/settings_defaults.hpp"
#include "../app/ui_model.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <functional>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace etude::app::ui;

TEST_CASE("reset: a whole-number value has the button greyed at the default and active off it") {
    CHECK_FALSE(reset_active(5, 5));
    CHECK(reset_active(6, 5));
    CHECK(reset_active(4, 5));
}

TEST_CASE("reset: a slider that sits a hair off its default counts as at it") {
    const float def = 0.6f;
    CHECK_FALSE(reset_active(def, def, 1.0f)); // greyed
    CHECK_FALSE(reset_active(def + 0.0004f, def, 1.0f));
    CHECK(reset_active(def + 0.01f, def, 1.0f));
    CHECK(reset_active(def - 0.01f, def, 1.0f));
    // The slack is a share of the range: a wide range keeps its own.
    CHECK_FALSE(reset_active(4.0f + 0.2f, 4.0f, 2000.0f));
    CHECK(reset_active(4.0f + 5.0f, 4.0f, 2000.0f));
}

TEST_CASE("reset: a tap on the greyed button changes nothing and offers no Undo") {
    int value = 5;
    CHECK_FALSE(tap_reset(&value, 5).has_value());
    CHECK(value == 5);
    float f = 0.6f;
    CHECK_FALSE(tap_reset(&f, 0.6f, 1.0f).has_value());
    CHECK(f == 0.6f);
}

TEST_CASE("reset: a tap on the active button puts the default back, and Undo gives the exact value") {
    int value = 120;
    const auto prior = tap_reset(&value, 80);
    REQUIRE(prior.has_value());
    CHECK(value == 80);
    undo_of(&value, *prior)();
    CHECK(value == 120);

    // A float: the very value, not one rebuilt from a slider's position.
    float f = 0.123456789f;
    const auto fprior = tap_reset(&f, 0.6f, 1.0f);
    REQUIRE(fprior.has_value());
    CHECK(f == 0.6f);
    undo_of(&f, *fprior)();
    CHECK(f == 0.123456789f);
}

TEST_CASE("reset: Undo works on the setting itself, with no row drawn and the sheet closed") {
    etude::app::UiModel model;
    model.app.hit_window_ms = 120;
    // The row taps; then the sheet closes (nothing draws the row any more); then Undo.
    const auto prior = tap_reset(&model.app.hit_window_ms, app_defaults().hit_window_ms);
    REQUIRE(prior.has_value());
    CHECK(model.app.hit_window_ms == etude::AppSettings{}.hit_window_ms);
    const std::function<void()> undo = undo_of(&model.app.hit_window_ms, *prior);
    undo();
    CHECK(model.app.hit_window_ms == 120);
}

TEST_CASE("reset: resetting the Opacity strength leaves Grade words as they are, and Undo is exact") {
    etude::app::UiModel model;
    model.look.grade_words = false;
    model.look.grade_opacity = 60;
    const auto prior = tap_reset(&model.look.grade_opacity, look_defaults().grade_opacity);
    REQUIRE(prior.has_value());
    CHECK(model.look.grade_opacity == etude::LookSettings{}.grade_opacity);
    CHECK_FALSE(model.look.grade_words);
    undo_of(&model.look.grade_opacity, *prior)();
    CHECK(model.look.grade_opacity == 60);
    CHECK_FALSE(model.look.grade_words);
    // With the words on they stay on.
    model.look.grade_words = true;
    REQUIRE(tap_reset(&model.look.grade_opacity, look_defaults().grade_opacity).has_value());
    CHECK(model.look.grade_words);
}

TEST_CASE("reset: Resume after goes back to its default inside 1 to 10 seconds, and Undo is exact") {
    etude::app::UiModel model;
    const int def = app_defaults().resume_countdown_s;
    CHECK(def == 3);
    CHECK(def >= etude::kMinResumeCountdownS);
    CHECK(def <= etude::kMaxResumeCountdownS);
    // At the default the button is greyed and does nothing.
    CHECK_FALSE(reset_active(model.app.resume_countdown_s, def));
    CHECK_FALSE(tap_reset(&model.app.resume_countdown_s, def).has_value());
    // At both ends of the range it is active; a tap puts the default back and Undo the earlier value.
    for (const int seconds : {etude::kMinResumeCountdownS, etude::kMaxResumeCountdownS}) {
        model.app.resume_countdown_s = seconds;
        CHECK(reset_active(model.app.resume_countdown_s, def));
        const auto prior = tap_reset(&model.app.resume_countdown_s, def);
        REQUIRE(prior.has_value());
        CHECK(model.app.resume_countdown_s == def);
        undo_of(&model.app.resume_countdown_s, *prior)();
        CHECK(model.app.resume_countdown_s == seconds);
    }
}

namespace {

// The text of every call of `name(` in `text`, up to its closing parenthesis.
std::vector<std::string> calls_of(const std::string& text, const std::string& name) {
    std::vector<std::string> out;
    for (std::size_t at = text.find(name + "("); at != std::string::npos;
         at = text.find(name + "(", at + 1)) {
        if (at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) != 0 ||
                       text[at - 1] == '_')) {
            continue; // `slider_row_int(` is not `slider_row(`'s call, nor a longer name
        }
        int depth = 0;
        std::size_t end = at + name.size();
        for (; end < text.size(); ++end) {
            depth += text[end] == '(' ? 1 : text[end] == ')' ? -1 : 0;
            if (depth == 0) {
                break;
            }
        }
        out.push_back(text.substr(at, end - at + 1));
    }
    return out;
}

} // namespace

TEST_CASE("reset: every slider and stepper of the settings pages takes its default from the settings "
          "structs, never a number of its own") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(ETUDE_SOURCE_DIR) / "app" / "ui";
    // The settings pages' files; the sliders of the bar's popovers (the accompaniment menu, the
    // section band's plan list) are not Settings rows.
    int rows = 0;
    std::vector<std::string> literal;
    for (const char* file : {"pages.cpp", "pages_input.cpp", "tempo.cpp"}) {
        std::ifstream in(dir / file, std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream text;
        text << in.rdbuf();
        for (const char* name : {"slider_row", "slider_row_int", "stepper_row"}) {
            for (const std::string& call : calls_of(text.str(), name)) {
                ++rows;
                if (call.find("look_defaults()") == std::string::npos &&
                    call.find("app_defaults()") == std::string::npos) {
                    literal.push_back(std::string(file) + ": " + call);
                }
            }
        }
    }
    CHECK(rows >= 12); // the scan reads the tree it is meant to
    for (const std::string& call : literal) {
        INFO("a settings slider or stepper whose reset default is not look_defaults() or "
             "app_defaults(): " << call);
        CHECK(false);
    }
}
