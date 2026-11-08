// SPDX-License-Identifier: GPL-3.0-or-later
// Which settings follow the person and which stay with the device, and swapping the first group.
#include "etude/profile_settings.hpp"

#include "etude/look.hpp"
#include "etude/session_state.hpp"
#include "etude/settings_doc.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace etude;

namespace {

std::string value_of(const SettingsDocument& doc, std::string_view section, std::string_view key) {
    const SettingsSection s = doc.section(section);
    std::string out;
    for (const SettingsEntry& e : s.entries) {
        if (e.key == key) {
            out = e.value;
        }
    }
    return out;
}

bool has_key(const SettingsDocument& doc, std::string_view section, std::string_view key) {
    const SettingsSection s = doc.section(section);
    return std::any_of(s.entries.begin(), s.entries.end(),
                       [&](const SettingsEntry& e) { return e.key == key; });
}

// A settings file as one device might have it.
SettingsDocument device_file() {
    return SettingsDocument::parse(
        "format = 1.0\n\n"
        "[app]\n"
        "hit_window_ms = 120\n"
        "metronome = true\n"
        "midi_in_port = Yamaha PSR-E383\n"
        "renderer = gpu\n"
        "latency_correction_ms = 35\n"
        "get_songs_online = true\n"
        "future_key = 9\n\n"
        "[session]\n"
        "song = Songs/Elise.mid\n"
        "speed_percent = 60\n"
        "keyboard = manual\n"
        "keyboard_lowest = 48\n\n"
        "[look]\n"
        "note_style = glow\n\n"
        "[user_presets]\n"
        "slot_1_name = Mine\n\n"
        "[sections.0123456789abcdef]\n"
        "version = 1\n"
        "section.1 = 0 16 Intro\n\n"
        "[something_new]\n"
        "x = 1\n");
}

} // namespace

TEST_CASE("profile settings: every [app] key the build writes has a place") {
    SettingsDocument doc;
    write_app_settings(doc, AppSettings{});
    const SettingsSection app = doc.section(kAppSection);
    REQUIRE_FALSE(app.entries.empty());
    for (const SettingsEntry& e : app.entries) {
        const auto in = [&](std::span<const std::string_view> keys) {
            return std::count(keys.begin(), keys.end(), e.key);
        };
        INFO("key: " << e.key);
        CHECK(in(profile_app_keys()) + in(device_app_keys()) == 1);
    }
    for (const std::string_view key : profile_app_keys()) {
        INFO("listed but not written: " << key);
        CHECK(has_key(doc, kAppSection, key));
    }
    for (const std::string_view key : device_app_keys()) {
        INFO("listed but not written: " << key);
        CHECK(has_key(doc, kAppSection, key));
    }
}

TEST_CASE("profile settings: the scope of a key") {
    CHECK(setting_scope("app", "hit_window_ms") == SettingScope::Profile);
    CHECK(setting_scope("app", "metronome_volume") == SettingScope::Profile);
    CHECK(setting_scope("app", "library_view") == SettingScope::Profile);
    CHECK(setting_scope("app", "learn_picker") == SettingScope::Profile);
    CHECK(setting_scope("app", "learn_scale") == SettingScope::Profile);
    CHECK(setting_scope("app", "learn_chord_root") == SettingScope::Profile);
    CHECK(setting_scope("app", "learn_chord") == SettingScope::Profile);
    CHECK(setting_scope("app", "learn_inversion") == SettingScope::Profile);
    CHECK(setting_scope("app", "badge_wall_view") == SettingScope::Profile);
    CHECK(setting_scope("app", "runs_chart_view") == SettingScope::Profile);
    CHECK(setting_scope("app", "melody_added") == SettingScope::Profile);
    CHECK(setting_scope("app", "renderer") == SettingScope::Device);
    CHECK(setting_scope("app", "midi_in_port") == SettingScope::Device);
    CHECK(setting_scope("app", "latency_correction_ms") == SettingScope::Device);
    CHECK(setting_scope("app", "get_songs_online") == SettingScope::Device);
    CHECK(setting_scope("app", "mic_device") == SettingScope::Device);
    CHECK(setting_scope("app", "key_from_a_newer_build") == SettingScope::Device);
    CHECK(setting_scope("session", "song") == SettingScope::Profile);
    CHECK(setting_scope("session", "keyboard_lowest") == SettingScope::Profile);
    CHECK(setting_scope("session", "key_from_a_newer_build") == SettingScope::Profile);
    CHECK(setting_scope("look", "anything") == SettingScope::Device); // whole sections: below
    CHECK(section_is_per_profile("look"));
    CHECK(section_is_per_profile("user_presets"));
    CHECK(section_is_per_profile("sections.0123456789abcdef"));
    CHECK_FALSE(section_is_per_profile("app"));
    CHECK_FALSE(section_is_per_profile("session"));
    CHECK_FALSE(section_is_per_profile("something_new"));
    CHECK_FALSE(section_is_per_profile("sections")); // not a song's
}

TEST_CASE("profile settings: the profile's part has the per-profile keys and sections only") {
    const std::string text = profile_settings_text(device_file());
    const SettingsDocument part = SettingsDocument::parse(text);
    CHECK(value_of(part, "app", "hit_window_ms") == "120");
    CHECK(value_of(part, "app", "metronome") == "true");
    CHECK(value_of(part, "session", "song") == "Songs/Elise.mid");
    CHECK(value_of(part, "session", "keyboard_lowest") == "48");
    CHECK(part.section_text("look") == "note_style = glow\n");
    CHECK(part.section_text("user_presets") == "slot_1_name = Mine\n");
    CHECK(part.has_section("sections.0123456789abcdef"));
    for (const char* device_key : {"midi_in_port", "renderer", "latency_correction_ms",
                                   "get_songs_online", "future_key"}) {
        INFO(device_key);
        CHECK_FALSE(has_key(part, "app", device_key));
    }
    CHECK_FALSE(part.has_section("something_new"));
}

TEST_CASE("profile settings: switching swaps the person's keys and leaves the device's") {
    SettingsDocument ben = SettingsDocument::parse(
        "[app]\nhit_window_ms = 200\nbar_style = dock\nrenderer = basic\n\n"
        "[session]\nsong = Songs/Ode.mid\nspeed_percent = 100\n\n"
        "[look]\nnote_style = flat\n");
    const std::string ben_text = profile_settings_text(ben);

    SettingsDocument doc = device_file();
    const std::string anna_text = profile_settings_text(doc);
    apply_profile_settings(doc, ben_text);

    // Ben's keys are in force.
    CHECK(value_of(doc, "app", "hit_window_ms") == "200");
    CHECK(value_of(doc, "app", "bar_style") == "dock");
    CHECK(value_of(doc, "session", "song") == "Songs/Ode.mid");
    CHECK(doc.section_text("look") == "note_style = flat\n");
    // What Anna had and Ben never set is gone, so it reads as its default.
    CHECK_FALSE(has_key(doc, "app", "metronome"));
    CHECK_FALSE(has_key(doc, "session", "keyboard"));
    CHECK_FALSE(has_key(doc, "session", "keyboard_lowest"));
    CHECK_FALSE(doc.has_section("user_presets"));
    CHECK_FALSE(doc.has_section("sections.0123456789abcdef"));
    // The device's stay, including a key and a section this build does not know and the
    // renderer Ben's file carried (a device key in a profile's text is not applied).
    CHECK(value_of(doc, "app", "renderer") == "gpu");
    CHECK(value_of(doc, "app", "midi_in_port") == "Yamaha PSR-E383");
    CHECK(value_of(doc, "app", "latency_correction_ms") == "35");
    CHECK(value_of(doc, "app", "get_songs_online") == "true");
    CHECK(value_of(doc, "app", "future_key") == "9");
    CHECK(value_of(doc, "something_new", "x") == "1");

    // And back to Anna: everything she had returns.
    apply_profile_settings(doc, anna_text);
    CHECK(profile_settings_text(doc) == anna_text);
    CHECK(value_of(doc, "app", "renderer") == "gpu");
    CHECK(value_of(doc, "app", "hit_window_ms") == "120");
    CHECK(doc.has_section("sections.0123456789abcdef"));
}

TEST_CASE("profile settings: a profile that never set anything gets the defaults") {
    SettingsDocument doc = device_file();
    apply_profile_settings(doc, "");
    AppSettings app = read_app_settings(doc);
    CHECK(app.hit_window_ms == AppSettings{}.hit_window_ms);
    CHECK_FALSE(app.metronome);
    CHECK(app.renderer == RendererPath::Gpu); // the device's, still the one it had
    CHECK(app.midi_in_port == "Yamaha PSR-E383");
}

TEST_CASE("profile settings: each profile keeps its own runs chart view") {
    SettingsDocument doc = SettingsDocument::parse("[app]\nruns_chart_view = ranked\nrenderer = gpu\n");
    const std::string anna_text = profile_settings_text(doc);
    CHECK(read_app_settings(doc).runs_chart_view == RunsChartView::Ranked);

    apply_profile_settings(doc, "[app]\nruns_chart_view = line\n");
    CHECK(read_app_settings(doc).runs_chart_view == RunsChartView::Line);
    // A profile that never chose gets the default, not the last person's view.
    apply_profile_settings(doc, "");
    CHECK(read_app_settings(doc).runs_chart_view == RunsChartView::Bars);
    apply_profile_settings(doc, anna_text);
    CHECK(read_app_settings(doc).runs_chart_view == RunsChartView::Ranked);
}

TEST_CASE(
    "profile settings: a profile's look is read from its stored text and nothing is written") {
    // A profile that never saved a look has the defaults.
    CHECK(profile_look("") == LookSettings{});
    // One with a look: its [look] only, whatever else the text holds.
    LookSettings neon;
    neon.note_style = NoteStyle::Neon;
    neon.style_intensity = 0.9f;
    SettingsDocument doc = SettingsDocument::parse("[app]\nrenderer = gpu\nhit_window_ms = 90\n");
    doc.set_section_text("look", serialize(neon));
    const std::string text = profile_settings_text(doc);
    const std::string before = text;
    const LookSettings read = profile_look(text);
    CHECK(read == neon.clamped());
    CHECK(text == before); // read only
    // The look in a document that is being switched to is the one the profile's text holds.
    SettingsDocument other = SettingsDocument::parse("");
    apply_profile_settings(other, text);
    CHECK(parse_look(other.section_text("look")) == neon.clamped());
}
