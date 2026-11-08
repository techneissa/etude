// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/look.hpp"
#include "etude/settings_doc.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace etude {

// Which settings follow the person and which stay with the device. A profile keeps a person's
// practice, look and keyboard choices; the device keeps what describes this machine and its
// connections. Switching profile swaps the first group at once and leaves the second alone.
//
// Per profile ([app], [session] and whole sections of settings.conf):
//   [app]     hit_window_ms, daily_goal_minutes, resume_countdown_s, skip_back_seconds,
//             skip_forward_seconds, bar_style, bar_pinned, view_mode, band_difficulty,
//             weighted_score, abc_chords, the metronome, the accompaniment, recent_colors and how
//             the library is shown (library_sort, library_sort_reversed, library_group,
//             library_filter, library_levels, library_hands, library_view, library_scroll,
//             group_versions) and the free-play scale overlay (scale_overlay, scale_root,
//             scale_type, scale_fingers)
//   [session] every key: the song the person left, its position, speed, hands, loop, auto
//             speed-up, count-in, mode, transposition, section and plan item, and the keyboard
//             choice and range
//   [look], [user_presets]   the look and the person's saved looks
//   [sections.<hash>]        the sections and practice list of each song
// A song's own practice choices (speed, hands, count-in, transposition, the hand split, the loop,
// the muted parts) are rows of the history keyed by the profile, not settings.
//
// Per device ([app]): latency_correction_ms, midi_in_port, midi_out_port, input_source, mic_device,
// mic_learn_tuning, mic_a4_tenths, mic_noise_floor_ppm, renderer, run_events_kept,
// get_songs_online, device_check_done and device_check_seen (the device check is of this machine);
// the audio device and anything about the network or an upload; and every section or key this build
// does not know (a newer build's key is not copied between people).
//
// The library itself (favourites, names, groups, versions, where a song came from) and the active
// profile are the device's too.
enum class SettingScope : std::uint8_t { Profile, Device };

// The [app] keys of each scope, for a test that every key the build writes is placed in one.
std::span<const std::string_view> profile_app_keys();
std::span<const std::string_view> device_app_keys();

// The scope of one key of a section. A key this build does not know is Device's, so it is never
// swapped away.
SettingScope setting_scope(std::string_view section, std::string_view key);

// Whether a whole section (`look`, `user_presets`, `sections.<hash>`) is a profile's.
bool section_is_per_profile(std::string_view section);

// The profile's part of a settings document, as settings text: the per-profile keys of [app] and
// [session] that the document holds and the per-profile sections whole. Reading `text` back with
// SettingsDocument::parse gives the same keys.
std::string profile_settings_text(const SettingsDocument& doc);

// Replaces the document's per-profile part by the profile's: every per-profile key of [app] and
// [session] and every per-profile section the document has is removed, then those in `text` are
// written. A key the profile does not hold is therefore back at its default after the settings are
// read. The per-device keys and the sections of other readers are not touched, and a per-device key
// found in `text` (an edited store) is ignored. Empty `text` is a profile that never set anything.
void apply_profile_settings(SettingsDocument& doc, std::string_view text);

// The look a profile's stored settings hold (its `[look]`), the defaults for a profile that never
// saved one: what the Configs page copies from another person. Reads `text`, writes nothing.
LookSettings profile_look(std::string_view text);

} // namespace etude
