// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_catalogue.hpp"

#include <algorithm>

namespace etude::app::ui {

namespace {

constexpr Home page(Page p) { return {false, p}; }
constexpr Home bar() { return {true, Page::Notes}; }
constexpr Home library() { return {false, Page::Notes, true}; }
constexpr bool kShortcut = true; // the bar has a control for it too

constexpr std::array<CatalogueEntry, kSettingCount> kCatalogue{{
    {Setting::NoteStyle, "note_style", page(Page::Notes)},
    {Setting::StyleIntensity, "style_intensity", page(Page::Notes)},
    {Setting::BlockGlow, "block_glow", page(Page::Notes)},
    {Setting::BeatLines, "beat_lines", page(Page::Notes)},
    {Setting::BlockLayout, "block_layout", page(Page::Notes)},
    {Setting::NameMode, "name_mode", page(Page::Notes)},
    {Setting::NamesWhere, "names_on_blocks, names_on_keys", page(Page::Notes)},
    {Setting::NamesAccidentals, "names_spelling", page(Page::Notes)},
    {Setting::BlackKeyNames, "black_key_names", page(Page::Notes)},
    {Setting::OctaveNumber, "names_show_octave", page(Page::Notes)},
    {Setting::CMarkers, "c_markers", page(Page::Notes)},
    {Setting::MiddleC, "octave_numbering", page(Page::Notes)},
    // Also the sheet's corner toggles (drawn over the score, like the bar's own buttons).
    {Setting::NamesOnSheet, "names_on_sheet", page(Page::Notes), kShortcut},
    {Setting::SheetNaming, "sheet_naming", page(Page::Notes)},
    {Setting::SheetChords, "sheet_chords", page(Page::Notes)},
    {Setting::SheetNoteColours, "sheet_note_colours", page(Page::Notes), kShortcut},
    {Setting::KeyLabel, "show_key", page(Page::Notes)},

    {Setting::ColourScheme, "color_scheme", page(Page::Colours)},
    {Setting::Palette, "palette_by_hand ... palette_single", page(Page::Colours)},
    {Setting::SchemeColours,
     "left_color, right_color, unknown_color, pitch_color_N, octave_color_N, velocity_color_N, "
     "single_color",
     page(Page::Colours)},
    {Setting::HighContrast, "high_contrast", page(Page::Colours)},
    {Setting::HitEffect, "hit_effect", page(Page::Colours)},
    {Setting::KeyFlash, "key_flash", page(Page::Colours)},
    {Setting::HeldGlow, "held_glow", page(Page::Colours)},
    {Setting::TrailFade, "trail_fade", page(Page::Colours)},
    {Setting::ComboNumber, "combo_number", page(Page::Colours)},
    {Setting::RisingInSong, "rising_in_song", page(Page::Colours)},
    {Setting::Background, "background", page(Page::Colours)},
    {Setting::ReducedEffects, "reduced_effects", page(Page::Colours)},

    {Setting::ViewMode, "view_mode", page(Page::View), kShortcut},
    {Setting::KeySize, "key_size", page(Page::View)},
    {Setting::KeyboardHeight, "keyboard_height", page(Page::View)},
    {Setting::NotesAhead, "visible_seconds", page(Page::View)},
    {Setting::ChordName, "chord_label", page(Page::View)},
    {Setting::ChordNameSize, "chord_label_size", page(Page::View)},
    {Setting::GradeWords, "grade_words", page(Page::View)},
    {Setting::GradePosition, "grade_position", page(Page::View)},
    {Setting::GradeOpacity, "grade_opacity", page(Page::View)},
    {Setting::BarStyle, "bar_style", page(Page::View)},
    {Setting::KeepBarShown, "bar_pinned", page(Page::View), kShortcut},
    {Setting::BandDifficulty, "band_difficulty", page(Page::View)},
    {Setting::Renderer, "renderer", page(Page::View)},
    {Setting::ScreenSize, "screen_width_mm", page(Page::View)},
    {Setting::LearnColours, "learn_scales_colour, learn_chords_colour, learn_ear_colour, learn_melodies_colour",
     page(Page::View)},

    {Setting::Hands, "split_point", page(Page::Practice)},
    {Setting::HitWindow, "hit_window_ms", page(Page::Practice)},
    {Setting::WeightedScore, "weighted_score", page(Page::Practice)},
    {Setting::RunsKept, "run_events_kept", page(Page::Practice)},
    {Setting::ResumeAfter, "resume_countdown_s", page(Page::Practice)},
    {Setting::SkipBack, "skip_back_seconds", page(Page::Practice)},
    {Setting::SkipForward, "skip_forward_seconds", page(Page::Practice)},
    {Setting::MetronomeVolume, "metronome_volume", page(Page::Practice)},
    {Setting::MetronomeSound, "metronome_sound", page(Page::Practice)},
    {Setting::MetronomeAccent, "metronome_accent", page(Page::Practice)},
    {Setting::MetronomeSubdivision, "metronome_subdivision", page(Page::Practice)},
    {Setting::MetronomePulse, "metronome_pulse", page(Page::Practice)},
    {Setting::AccompanimentOn, "accompaniment", page(Page::Practice)},
    {Setting::AccompanimentVolume, "accompaniment_volume", page(Page::Practice), kShortcut},

    {Setting::InputSource, "input_source", page(Page::Input)},
    {Setting::MicDevice, "mic_device", page(Page::Input)},
    {Setting::CalibrateNoise, "mic_noise_floor_ppm", page(Page::Input)},
    {Setting::LearnTuning, "mic_learn_tuning", page(Page::Input)},
    {Setting::Tuning, "mic_a4_tenths", page(Page::Input)},
    {Setting::KeyboardProfile, "keyboard", page(Page::Input)},
    {Setting::MidiInputPort, "midi_in_port", page(Page::Input)},
    {Setting::MidiOutputPort, "midi_out_port", page(Page::Input)},
    {Setting::DeviceCheck, "device check", page(Page::Input)},
    {Setting::TypingLetters, "show_typing_keys", page(Page::Input)},
    {Setting::Latency, "latency_correction_ms", page(Page::Input)},
    {Setting::AbcChords, "abc_chords", page(Page::Input)},

    {Setting::Configs, "configs", page(Page::Configs)},
    {Setting::ResetAll, "reset all", page(Page::Configs)},

    {Setting::UpdateAutoCheck, "update_auto_check", page(Page::About)},
    {Setting::IconColour, "icon_colour", page(Page::About)},

    {Setting::LibrarySort, "library_sort, library_sort_reversed", library()},
    {Setting::LibraryGroup, "library_group", library()},
    {Setting::LibraryFilter, "library_filter", library()},
    {Setting::LibraryFacets, "library_levels, library_hands", library()},
    {Setting::LibraryView, "library_view", library()},
    {Setting::GetSongsOnline, "get_songs_online", library()},
    {Setting::GroupVersions, "group_versions", library()},

    {Setting::MetronomeOn, "metronome", bar()},
    {Setting::CountIn, "count_in_bars", bar()},
}};

constexpr bool in_order() {
    for (std::size_t i = 0; i < kCatalogue.size(); ++i) {
        if (static_cast<std::size_t>(kCatalogue[i].id) != i) {
            return false;
        }
    }
    return true;
}
static_assert(in_order(), "the catalogue lists the settings in Setting order, each once");

bool g_recording = false;
std::optional<Page> g_page;
bool g_library = false;
std::vector<Drawn> g_drawn;
int g_column = 0;

} // namespace

int fixed_column(Setting s) {
    switch (s) {
    case Setting::MetronomeVolume:
    case Setting::MetronomeSound:
    case Setting::MetronomeAccent:
    case Setting::MetronomeSubdivision:
    case Setting::MetronomePulse:
    case Setting::AccompanimentOn:
    case Setting::AccompanimentVolume:
        return 1;
    default:
        return -1;
    }
}

std::span<const CatalogueEntry, kSettingCount> catalogue() { return kCatalogue; }

const CatalogueEntry& entry_of(Setting s) { return kCatalogue[static_cast<std::size_t>(s)]; }

std::vector<std::string> check_drawn(std::span<const Drawn> drawn) {
    std::vector<std::string> problems;
    for (const CatalogueEntry& e : kCatalogue) {
        std::vector<Page> pages;
        bool on_bar = false;
        bool in_library = false;
        for (const Drawn& d : drawn) {
            if (d.setting != e.id) {
                continue;
            }
            if (d.in_library) {
                in_library = true;
            } else if (d.on_bar) {
                on_bar = true;
            } else if (std::find(pages.begin(), pages.end(), d.page) == pages.end()) {
                pages.push_back(d.page);
            }
        }
        const std::string name(e.name);
        if (e.home.in_library) {
            for (const Page p : pages) {
                problems.push_back(name + " lives in the library but is drawn on " +
                                   page_title(p));
            }
            if (on_bar) {
                problems.push_back(name + " lives in the library but is drawn on the bar");
            }
            if (!in_library) {
                problems.push_back(name + " lives in the library but the library drew no row "
                                          "for it");
            }
            continue;
        }
        if (in_library) {
            problems.push_back(name + " is drawn in the library but does not live there");
        }
        if (e.home.on_bar) {
            for (const Page p : pages) {
                problems.push_back(name + " lives on the bar but is drawn on " + page_title(p));
            }
            if (!on_bar) {
                problems.push_back(name + " lives on the bar but the bar drew no row for it");
            }
            continue;
        }
        if (on_bar && !e.bar_shortcut) {
            problems.push_back(name + " is drawn on the bar but is not a declared bar shortcut");
        }
        if (pages.empty()) {
            problems.push_back(name + " is drawn on no page (home: " + page_title(e.home.page) +
                               ")");
            continue;
        }
        if (pages.size() > 1) {
            std::string list;
            for (const Page p : pages) {
                list += (list.empty() ? "" : ", ");
                list += page_title(p);
            }
            problems.push_back(name + " is drawn on " + std::to_string(pages.size()) +
                               " pages: " + list);
        }
        for (const Drawn& d : drawn) {
            const int want = fixed_column(e.id);
            if (d.setting == e.id && !d.on_bar && !d.in_library && want >= 0 && d.column != want) {
                problems.push_back(name + " is drawn in column " + std::to_string(d.column + 1) +
                                   " of " + page_title(d.page) + ", it belongs in column " +
                                   std::to_string(want + 1));
            }
        }
        for (const Page p : pages) {
            if (p != e.home.page) {
                problems.push_back(name + " is drawn on " + page_title(p) + ", its home is " +
                                   page_title(e.home.page));
            }
        }
    }
    return problems;
}

namespace catalogue_sweep {

bool recording() { return g_recording; }

void start() {
    g_recording = true;
    g_page.reset();
    g_library = false;
    g_column = 0;
    g_drawn.clear();
}

std::vector<Drawn> stop() {
    g_recording = false;
    g_page.reset();
    g_library = false;
    return std::move(g_drawn);
}

void set_page(Page p) {
    if (g_recording) {
        g_page = p;
        g_column = 0;
    }
}

void next_column() {
    if (g_recording) {
        ++g_column;
    }
}

void clear_page() { g_page.reset(); }

void set_library(bool drawing) { g_library = g_recording && drawing; }

void drawn(Setting s) {
    if (!g_recording) {
        return;
    }
    if (g_library) {
        g_drawn.push_back({false, Page::Notes, s, true});
    } else if (g_page) {
        g_drawn.push_back({false, *g_page, s, false, g_column});
    } else {
        g_drawn.push_back({true, Page::Notes, s});
    }
}

} // namespace catalogue_sweep

} // namespace etude::app::ui
