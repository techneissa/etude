// SPDX-License-Identifier: GPL-3.0-or-later
// The Configs page's rules (configs_logic.hpp): what a card applies and previews, what saving and
// clearing a slot replaces and how Undo puts it back, and the copy from another profile, which
// takes that profile's look, leaves their settings alone and is undone by putting yours back. The
// page's source is read for the calls that carry the preview rule, so a card that previewed the
// look in use fails here.
#include "../app/ui/configs_logic.hpp"

#include "etude/look.hpp"
#include "etude/profile.hpp"
#include "etude/profile_settings.hpp"
#include "etude/settings_doc.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#include "sample_looks.hpp"

using namespace etude;
using namespace etude::app;
using namespace etude::app::ui;
using namespace etude::test;

namespace {

std::string source(const char* name) {
    std::ifstream in(std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / "ui" / name);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string function_text(const std::string& file, const std::string& header) {
    const std::size_t at = file.find(header);
    REQUIRE(at != std::string::npos);
    const std::size_t end = file.find("\n}\n", at);
    REQUIRE(end != std::string::npos);
    return file.substr(at, end - at);
}

// A model whose look in use is `in_use` and whose slot 0 holds `saved`.
UiModel model_with(const LookSettings& in_use, const LookSettings& saved) {
    UiModel m;
    m.look = in_use;
    REQUIRE(m.user_presets.save_slot(0, "Evening", saved));
    return m;
}

// A household of the profile in use (Anna) and Dani, whose stored settings hold `dani`'s look.
struct Household {
    ProfileSet set;
    std::string dani_text;
    std::vector<ProfileLook> looks;
    ProfileInfo info;
    std::int64_t dani_id = 0;
};

Household household(const LookSettings& dani) {
    Household h;
    Profile made;
    REQUIRE(h.set.add("Dani", 1000, &made) == ProfileResult::Ok);
    h.dani_id = made.id;
    SettingsDocument doc;
    doc.set_section_text("look", serialize(dani));
    h.dani_text = profile_settings_text(doc);
    h.looks.push_back({made.id, profile_look(h.dani_text)});
    h.info.set = &h.set;
    h.info.looks = h.looks;
    h.info.active = kDefaultProfileId;
    return h;
}

} // namespace

TEST_CASE("a card previews its slot's own config, never the look in use") {
    const LookSettings neon = sample_look(Sample::NeonNight);
    const LookSettings glass = sample_look(Sample::Glass);
    const UiModel m = model_with(glass, neon);
    const std::optional<LookSettings> shown = config_card_look(m.user_presets, 0);
    REQUIRE(shown.has_value());
    CHECK(*shown == neon.clamped());
    CHECK_FALSE(*shown == m.look);
    CHECK_FALSE(config_card_look(m.user_presets, 1).has_value()); // an empty card previews nothing
}

TEST_CASE("the page draws each card's preview from config_card_look and not from the look in use") {
    const std::string page = source("pages_configs.cpp");
    const std::string card = function_text(page, "void config_card(");
    CHECK(card.find("config_card_look(model.user_presets, slot)") != std::string::npos);
    CHECK(card.find("preview->draw(k, *own,") != std::string::npos);
    CHECK(card.find("draw(k, model.look") == std::string::npos);
}

TEST_CASE("tapping a filled card applies its config, an empty one is not applied") {
    const LookSettings neon = sample_look(Sample::NeonNight);
    UiModel m = model_with(sample_look(Sample::Glass), neon);
    CHECK_FALSE(config_is_current(m, 0));
    CHECK(config_apply(m, 0));
    CHECK(m.look == neon.clamped());
    CHECK(config_is_current(m, 0));
    const LookSettings kept = m.look;
    CHECK_FALSE(config_apply(m, 2)); // empty
    CHECK(m.look == kept);
}

TEST_CASE("Save current here overwrites a filled slot under its name, and Undo puts it back") {
    const LookSettings neon = sample_look(Sample::NeonNight);
    const LookSettings glass = sample_look(Sample::Glass);
    UiModel m = model_with(glass, neon);
    const ConfigSaved saved = config_save_here(m, 0);
    CHECK(saved.name == "Evening"); // the name stays
    CHECK(config_card_look(m.user_presets, 0) == glass.clamped());
    CHECK(m.look == glass); // the look in use is not touched
    config_undo_slots(m, saved.before);
    CHECK(config_card_look(m.user_presets, 0) == neon.clamped());
    CHECK(m.user_presets.slot_name(0) == "Evening");
}

TEST_CASE("tapping an empty card saves the look in use under a default name, and Undo empties it") {
    UiModel m;
    m.look = sample_look(Sample::Glass);
    const ConfigSaved saved = config_save_here(m, 2);
    CHECK(saved.name == "Config 3");
    CHECK(m.user_presets.is_used(2));
    config_undo_slots(m, saved.before);
    CHECK_FALSE(m.user_presets.is_used(2));
}

TEST_CASE("Clear empties the slot at once and Undo restores its name and look") {
    const LookSettings neon = sample_look(Sample::NeonNight);
    UiModel m = model_with(sample_look(Sample::Glass), neon);
    const LookSettings in_use = m.look;
    const UserPresets before = config_clear(m, 0);
    CHECK_FALSE(m.user_presets.is_used(0));
    CHECK(m.look == in_use);
    config_undo_slots(m, before);
    CHECK(m.user_presets.slot_name(0) == "Evening");
    CHECK(config_card_look(m.user_presets, 0) == neon.clamped());
}

TEST_CASE("Rename keeps the slot's look and refuses an empty slot") {
    const LookSettings neon = sample_look(Sample::NeonNight);
    UiModel m = model_with(sample_look(Sample::Glass), neon);
    CHECK(config_rename(m, 0, "Night"));
    CHECK(m.user_presets.slot_name(0) == "Night");
    CHECK(config_card_look(m.user_presets, 0) == neon.clamped());
    CHECK_FALSE(config_rename(m, 1, "Nothing"));
    CHECK_FALSE(m.user_presets.is_used(1));
}

TEST_CASE("copying takes the other profile's config and leaves theirs alone") {
    const LookSettings dani = sample_look(Sample::FrostedPastel);
    const Household h = household(dani);
    const std::string text_before = h.dani_text;
    const LookSettings* theirs = config_of_profile(h.info, h.dani_id);
    REQUIRE(theirs != nullptr);
    UiModel m;
    m.look = sample_look(Sample::Glass);
    const LookSettings mine = m.look;
    const LookSettings replaced = config_copy(m, *theirs);
    CHECK(m.look == dani.clamped());   // yours is now theirs
    CHECK(replaced == mine);           // what the copy replaced, for Undo
    CHECK(h.dani_text == text_before); // their stored settings are the same text
    CHECK(profile_look(h.dani_text) == dani.clamped());
    CHECK(*theirs == dani.clamped()); // and the look read for them did not move
}

TEST_CASE("Undo of a copy puts your own look back and keeps your saved configs") {
    const Household h = household(sample_look(Sample::FrostedPastel));
    UiModel m = model_with(sample_look(Sample::Glass), sample_look(Sample::NeonNight));
    const LookSettings mine = m.look;
    const UserPresets slots = m.user_presets;
    const LookSettings replaced = config_copy(m, *config_of_profile(h.info, h.dani_id));
    CHECK_FALSE(m.look == mine);
    config_undo_copy(m, replaced);
    CHECK(m.look == mine);
    CHECK(m.user_presets == slots); // a copy never writes your saved configs
}

TEST_CASE("the toast names whose config was copied") {
    CHECK(config_copied_text("Dani") == "Copied Dani's config");
}

TEST_CASE("the copy row counts the other profiles; one profile alone has none to copy from") {
    CHECK(config_other_count(nullptr) == 0);
    ProfileInfo none;
    CHECK(config_other_count(&none) == 0);
    ProfileSet alone;
    ProfileInfo one;
    one.set = &alone;
    one.active = kDefaultProfileId;
    CHECK(config_other_count(&one) == 0);
    CHECK(std::string(kConfigNoOtherProfile) == "There is no other profile to copy from");
    const Household h = household(LookSettings{});
    CHECK(config_other_count(&h.info) == 1);
    // The page shows the line when the count is 0 and the pills otherwise.
    const std::string page = source("pages_configs.cpp");
    const std::string row = function_text(page, "void copy_row(");
    CHECK(row.find("config_other_count(k.profiles) == 0") != std::string::npos);
    CHECK(row.find("kConfigNoOtherProfile") != std::string::npos);
}

TEST_CASE("a profile whose look was not worked out is not copied") {
    const Household h = household(LookSettings{});
    CHECK(config_of_profile(h.info, 99) == nullptr);
}

TEST_CASE("the page never writes another profile's settings") {
    const std::string page = source("pages_configs.cpp");
    CHECK(page.find("set_profile_settings") == std::string::npos);
    CHECK(page.find("profile_settings_text") == std::string::npos);
    CHECK(page.find("HistoryStore") == std::string::npos);
}

TEST_CASE("the first tap on a profile only asks: nothing is copied") {
    const Household h = household(sample_look(Sample::FrostedPastel));
    UiModel m = model_with(sample_look(Sample::Glass), sample_look(Sample::NeonNight));
    const UiModel before = m;
    CopyAsk ask;
    CHECK_FALSE(ask.open());
    ask.ask(h.dani_id); // the tap on Dani's pill
    CHECK(ask.open());
    CHECK(ask.profile() == h.dani_id);
    CHECK(m == before); // asking touches nothing
    // The page's pill does no more than ask; the copy is only on the question's Copy.
    const std::string page = source("pages_configs.cpp");
    const std::string row = function_text(page, "void copy_row(");
    CHECK(row.find("g_copy_ask.ask(") != std::string::npos);
    CHECK(row.find("copy_from(") == std::string::npos);
    CHECK(row.find("config_copy(") == std::string::npos);
    const std::string top = function_text(page, "void page_configs(");
    CHECK(top.find("draw_copy_confirm(k, g_copy_ask)") != std::string::npos);
}

TEST_CASE("Copy on the question copies that profile's config, and Undo restores yours") {
    const LookSettings dani = sample_look(Sample::FrostedPastel);
    const Household h = household(dani);
    UiModel m = model_with(sample_look(Sample::Glass), sample_look(Sample::NeonNight));
    const LookSettings mine = m.look;
    CopyAsk ask;
    ask.ask(h.dani_id);
    const std::optional<std::int64_t> chosen = ask.copy(); // the card's Copy
    REQUIRE(chosen.has_value());
    CHECK(*chosen == h.dani_id);
    CHECK_FALSE(ask.open()); // the question closes
    const LookSettings replaced = config_copy(m, *config_of_profile(h.info, *chosen));
    CHECK(m.look == dani.clamped());
    config_undo_copy(m, replaced); // the toast's Undo
    CHECK(m.look == mine);
    CHECK_FALSE(ask.copy().has_value()); // a second Copy has nothing to copy
}

// A toast that keeps what the page said and its Undo, as the kit's toast does: the chip runs the
// closure it was given.
struct RecordingToaster : ConfigToaster {
    std::string text;
    std::function<void()> undo;
    int count = 0;
    void say(const std::string& said) override {
        text = said;
        undo = nullptr; // a toast with no Undo takes the earlier offer away
        ++count;
    }
    void say_undoable(const std::string& said, std::function<void()> run) override {
        text = said;
        undo = std::move(run);
        ++count;
    }
    // The player taps the toast's Undo chip.
    void tap_undo() {
        REQUIRE(undo != nullptr);
        undo();
    }
};

TEST_CASE("Cancel on the question copies nothing") {
    const Household h = household(sample_look(Sample::FrostedPastel));
    UiModel m = model_with(sample_look(Sample::Glass), sample_look(Sample::NeonNight));
    const UiModel before = m;
    CopyAsk ask;
    ask.ask(h.dani_id);
    ask.cancel();
    CHECK_FALSE(ask.open());
    CHECK_FALSE(ask.copy().has_value());
    CHECK(m == before);
}

TEST_CASE("a tap outside the question's card closes it and copies nothing") {
    const Household h = household(sample_look(Sample::FrostedPastel));
    UiModel m = model_with(sample_look(Sample::Glass), sample_look(Sample::NeonNight));
    const UiModel before = m;
    CopyAsk ask;
    ask.ask(h.dani_id);
    ask.outside_tap(false); // a tap that landed on the card
    CHECK(ask.open());
    ask.outside_tap(true); // a tap outside it
    CHECK_FALSE(ask.open());
    CHECK_FALSE(ask.copy().has_value());
    CHECK(m == before);
}

TEST_CASE("closing the sheet, or changing the page, closes the question and copies nothing") {
    const Household h = household(sample_look(Sample::FrostedPastel));
    UiModel m = model_with(sample_look(Sample::Glass), sample_look(Sample::NeonNight));
    const UiModel before = m;
    CopyAsk ask;
    ask.page_frame(100);
    ask.ask(h.dani_id);
    ask.page_frame(101); // drawn on the next frame: still asking
    CHECK(ask.open());
    ask.page_frame(102);
    CHECK(ask.open());
    ask.page_frame(140); // the page was not drawn for a while (the sheet was closed)
    CHECK_FALSE(ask.open());
    CHECK_FALSE(ask.copy().has_value());
    CHECK(m == before);
}

TEST_CASE("a question for a profile that is gone copies nothing and is not shown") {
    const Household h = household(sample_look(Sample::FrostedPastel));
    UiModel m;
    RecordingToaster toast;
    const UiModel before = m;
    CHECK_FALSE(config_copy_confirmed(m, h.info, 99, toast)); // no such profile
    CHECK(m == before);
    CHECK(toast.count == 0);
}

TEST_CASE("Copy on the question: the toast names the profile and its Undo puts yours back") {
    const LookSettings dani = sample_look(Sample::FrostedPastel);
    const Household h = household(dani);
    UiModel m = model_with(sample_look(Sample::Glass), sample_look(Sample::NeonNight));
    const LookSettings mine = m.look;
    const UserPresets slots = m.user_presets;
    RecordingToaster toast;
    CHECK(config_copy_confirmed(m, h.info, h.dani_id, toast));
    CHECK(toast.text == "Copied Dani's config");
    CHECK(m.look == dani.clamped());
    toast.tap_undo();
    CHECK(m.look == mine);
    CHECK(m.user_presets == slots);
}

TEST_CASE("an empty card's tap saves the look in use, its toast says so and its Undo empties it") {
    UiModel m;
    m.look = sample_look(Sample::Glass);
    RecordingToaster toast;
    config_tap(m, 1, toast);
    CHECK(m.user_presets.is_used(1));
    CHECK(m.user_presets.slot_name(1) == "Config 2");
    CHECK(config_card_look(m.user_presets, 1) == m.look.clamped());
    CHECK(toast.text == "Saved as Config 2");
    toast.tap_undo();
    CHECK_FALSE(m.user_presets.is_used(1));
}

TEST_CASE("a filled card's tap applies its config with a plain toast and no Undo") {
    const LookSettings neon = sample_look(Sample::NeonNight);
    UiModel m = model_with(sample_look(Sample::Glass), neon);
    RecordingToaster toast;
    config_tap(m, 0, toast);
    CHECK(m.look == neon.clamped());
    CHECK(toast.text == "Config: Evening");
    CHECK(toast.undo == nullptr);
}

TEST_CASE("each card has three buttons: Rename, Save current here, Clear") {
    CHECK(kConfigActions.size() == 3);
    CHECK(kConfigActions[0] == ConfigAction::Rename);
    CHECK(kConfigActions[1] == ConfigAction::SaveHere);
    CHECK(kConfigActions[2] == ConfigAction::Clear);
}

TEST_CASE("on an empty card Rename and Clear are greyed and Save current here works") {
    CHECK_FALSE(config_action_enabled(ConfigAction::Rename, false));
    CHECK_FALSE(config_action_enabled(ConfigAction::Clear, false));
    CHECK(config_action_enabled(ConfigAction::SaveHere, false));
    for (const ConfigAction action : kConfigActions) {
        CHECK(config_action_enabled(action, true)); // all three on a filled card
    }
}

TEST_CASE("a greyed button does nothing: no change, no toast") {
    UiModel m;
    m.look = sample_look(Sample::Glass);
    const UiModel before = m;
    RecordingToaster toast;
    CHECK_FALSE(config_button_act(m, ConfigAction::Rename, 1, toast)); // empty: no dialog
    CHECK_FALSE(config_button_act(m, ConfigAction::Clear, 1, toast));
    CHECK(m == before);
    CHECK(toast.count == 0);
}

TEST_CASE("Save current here on an empty card saves under the default name; Undo empties it") {
    UiModel m;
    m.look = sample_look(Sample::Glass);
    RecordingToaster toast;
    CHECK_FALSE(config_button_act(m, ConfigAction::SaveHere, 2, toast));
    CHECK(m.user_presets.slot_name(2) == "Config 3");
    CHECK(config_card_look(m.user_presets, 2) == m.look.clamped());
    CHECK(toast.text == "Saved as Config 3");
    toast.tap_undo();
    CHECK_FALSE(m.user_presets.is_used(2));
}

TEST_CASE("Rename asks for the dialog and changes nothing by itself") {
    UiModel m = model_with(sample_look(Sample::Glass), sample_look(Sample::NeonNight));
    const UiModel before = m;
    RecordingToaster toast;
    CHECK(config_button_act(m, ConfigAction::Rename, 0, toast));
    CHECK(m == before);
    CHECK(toast.count == 0);
}

TEST_CASE(
    "Save current here over a filled card overwrites at once; the toast's Undo puts it back") {
    const LookSettings neon = sample_look(Sample::NeonNight);
    const LookSettings glass = sample_look(Sample::Glass);
    UiModel m = model_with(glass, neon);
    RecordingToaster toast;
    CHECK_FALSE(config_button_act(m, ConfigAction::SaveHere, 0, toast));
    CHECK(config_card_look(m.user_presets, 0) == glass.clamped());
    CHECK(toast.text == "Saved over Evening");
    toast.tap_undo();
    CHECK(config_card_look(m.user_presets, 0) == neon.clamped());
    CHECK(m.user_presets.slot_name(0) == "Evening");
    CHECK(m.look == glass); // the look in use was never touched
}

TEST_CASE("Clear empties at once; the toast's Undo restores the name and look") {
    const LookSettings neon = sample_look(Sample::NeonNight);
    UiModel m = model_with(sample_look(Sample::Glass), neon);
    RecordingToaster toast;
    CHECK_FALSE(config_button_act(m, ConfigAction::Clear, 0, toast));
    CHECK_FALSE(m.user_presets.is_used(0));
    CHECK(toast.text == "Cleared Evening");
    toast.tap_undo();
    CHECK(m.user_presets.slot_name(0) == "Evening");
    CHECK(config_card_look(m.user_presets, 0) == neon.clamped());
}

TEST_CASE("the page draws the buttons under each card and has no hold menu") {
    const std::string page = source("pages_configs.cpp");
    const std::string buttons = function_text(page, "void config_buttons(");
    CHECK(buttons.find("icon_action(") != std::string::npos);
    CHECK(buttons.find("config_action_enabled(b.action, used)") != std::string::npos);
    CHECK(page.find("press.hold") == std::string::npos);
    CHECK(page.find("open_config_menu") == std::string::npos);
}

TEST_CASE("the question's words") {
    CHECK(config_copy_question("Dani") == "Copy Dani's config?");
    CHECK(std::string(kConfigCopyDetail) == "It replaces your current config. You can undo it.");
}
