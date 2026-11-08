// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/profile_settings.hpp"

#include "etude/session_state.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace etude {

namespace {

// Every [app] key this build writes is in exactly one of the two lists (a test holds it), so a new
// setting has to be placed. A key in neither is the device's.
constexpr auto kDeviceAppKeys = std::to_array<std::string_view>({
    "latency_correction_ms", "midi_in_port",     "midi_out_port",       "input_source",
    "mic_device",            "mic_learn_tuning", "mic_a4_tenths",       "mic_noise_floor_ppm",
    "renderer",              "run_events_kept",  "get_songs_online", "device_check_done",
    "device_check_seen",      "screen_width_mm",     "active_profile",
    "update_auto_check",      "icon_colour",
});

constexpr auto kProfileAppKeys = std::to_array<std::string_view>({
    "hit_window_ms",        "daily_goal_minutes",    "resume_countdown_s",
    "skip_back_seconds",    "skip_forward_seconds",  "bar_style",
    "bar_pinned",           "view_mode",             "band_difficulty",
    "weighted_score",       "abc_chords",            "metronome",
    "metronome_sound",      "metronome_volume",      "metronome_accent",
    "metronome_subdivision", "metronome_pulse",      "accompaniment",
    "accompaniment_volume", "recent_colors",         "library_sort",
    "library_sort_reversed", "library_group",        "library_filter",
    "library_levels",       "library_hands",         "library_view",
    "library_scroll",       "group_versions",         "learn_picker",
    "learn_root",           "learn_scale",            "learn_hands",
    "learn_chord_root",     "learn_chord",            "learn_inversion",
    "learn_tempo",          "learn_direction",        "learn_octaves",
    "learn_arpeggio",       "learn_ear_mode",         "learn_ear_octave",
    "learn_ear_any_octave", "learn_ear_max",
    "scale_overlay",        "scale_root",             "scale_type",
    "scale_fingers",        "badge_wall_view",        "runs_chart_view",
    "melody_added",
});

constexpr std::string_view kSongSectionsPrefix = "sections.";

bool listed(std::span<const std::string_view> keys, std::string_view key) {
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

// The sections of the document that are a profile's, in file order.
std::vector<std::string> per_profile_sections(const SettingsDocument& doc) {
    std::vector<std::string> out;
    for (std::string& name : doc.section_names()) {
        if (section_is_per_profile(name)) {
            out.push_back(std::move(name));
        }
    }
    return out;
}

void remove_per_profile(SettingsDocument& doc) {
    for (const std::string_view section : {kAppSection, kSessionSection}) {
        const SettingsSection held = doc.section(section);
        for (const SettingsEntry& entry : held.entries) {
            if (setting_scope(section, entry.key) == SettingScope::Profile) {
                doc.remove(section, entry.key);
            }
        }
    }
    for (const std::string& name : per_profile_sections(doc)) {
        doc.remove_section(name);
    }
}

} // namespace

std::span<const std::string_view> profile_app_keys() { return kProfileAppKeys; }
std::span<const std::string_view> device_app_keys() { return kDeviceAppKeys; }

SettingScope setting_scope(std::string_view section, std::string_view key) {
    if (section == kSessionSection) {
        return SettingScope::Profile;
    }
    if (section == kAppSection) {
        return listed(kProfileAppKeys, key) ? SettingScope::Profile : SettingScope::Device;
    }
    return SettingScope::Device;
}

bool section_is_per_profile(std::string_view section) {
    return section == "look" || section == "user_presets" ||
           section.substr(0, kSongSectionsPrefix.size()) == kSongSectionsPrefix;
}

std::string profile_settings_text(const SettingsDocument& doc) {
    SettingsDocument out;
    for (const std::string_view section : {kAppSection, kSessionSection}) {
        const SettingsSection held = doc.section(section);
        for (const SettingsEntry& entry : held.entries) {
            if (setting_scope(section, entry.key) == SettingScope::Profile) {
                out.set(section, entry.key, entry.value);
            }
        }
    }
    for (const std::string& name : per_profile_sections(doc)) {
        out.set_section_text(name, doc.section_text(name));
    }
    return out.serialize();
}

void apply_profile_settings(SettingsDocument& doc, std::string_view text) {
    remove_per_profile(doc);
    const SettingsDocument profile = SettingsDocument::parse(text);
    for (const std::string_view section : {kAppSection, kSessionSection}) {
        const SettingsSection held = profile.section(section);
        for (const SettingsEntry& entry : held.entries) {
            if (setting_scope(section, entry.key) == SettingScope::Profile) {
                doc.set(section, entry.key, entry.value);
            }
        }
    }
    for (const std::string& name : per_profile_sections(profile)) {
        doc.set_section_text(name, profile.section_text(name));
    }
}

LookSettings profile_look(std::string_view text) {
    return parse_look(SettingsDocument::parse(text).section_text("look"));
}

} // namespace etude
