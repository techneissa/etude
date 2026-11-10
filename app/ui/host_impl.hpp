// SPDX-License-Identifier: GPL-3.0-or-later
// The Ui's private state (`Ui::Impl`) and what one `Ui::draw` frame works out once and its stages
// read (`DrawFrame`). Shared by host.cpp and the host_*.cpp files that hold the stages.
#pragma once

#include "../ui.hpp"

#include "bar.hpp"
#include "crash_card.hpp"
#include "crash_screen.hpp"
#include "device_check_card.hpp"
#include "editor.hpp"
#include "fit_text.hpp"
#include "glyph_warm.hpp"
#include "key_ring_demo.hpp"
#include "medal_demo.hpp"
#include "kit.hpp"
#include "learn_screen.hpp"
#include "library_screen.hpp"
#include "medal_atlas.hpp"
#include "preview.hpp"
#include "profile_ui.hpp"
#include "replay_bar.hpp"
#include "results_card.hpp"
#include "run_card.hpp"
#include "scale_pill.hpp"
#include "section_band.hpp"
#include "sheet.hpp"
#include "status_fit.hpp"
#include "top_menu.hpp"
#include "transpose.hpp"

#include "../sheet/sheet_preview.hpp"
#include "etude/finger_count.hpp"
#include "etude/screen_layout.hpp"
#include "etude/top_indicator.hpp"
#include "etude/touch_gesture.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <imgui.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace etude::app {

// Renders the frame's draw data (and hashes it for the idle gate); host.cpp.
void render_draw_data(SDL_Renderer* renderer);

// Whether the point is inside the box (its right and bottom edges outside).
inline bool box_has(const ui::Box& b, ImVec2 p) {
    return p.x >= b.x0 && p.x < b.x1 && p.y >= b.y0 && p.y < b.y1;
}

// The UI's types are in `ui`; this header is internal to host.cpp and the host_*.cpp stages.
using namespace ui;

// ImGui points from output pixels.
inline ImVec2 to_points(float x, float y, float scale) { return {x * scale, y * scale}; }

struct DrawFrame {
    UiModel& model;
    SongPort& song;
    SectionsPort& sections;
    const UiFrameInfo& info;
    Kit& k;
    const ScreenLayout& lay;
    const SongView& view;
    const float scale; // output pixels to ImGui points
    Frame f;
    ImVec2 gear_min{0, 0};
    ImVec2 gear_max{0, 0};
    // The status text, measured once (measure_status) and drawn later (draw_status).
    float status_size = 0.0f;
    FitText label_text;
    std::span<const StatusPart> status_parts;
    std::array<float, kMaxStatusParts> status_part_w{};
    StatusFitInput status_in;
    bool library_button = false; // the library's button sits at the start of the status area
    bool learn_button = false;   // and Learn's beside it
    float status_lead = 0.0f;    // the room that button takes
    float status_w = 0.0f;       // what the status text takes with every note whole
    bool profile_chip = false;   // the profile's chip sits after the library button
    Box profile_box;             // its hit area, in points
    bool menu_button = false;    // a narrow row's one menu button sits in their place
    Box menu_box;                // its hit area, in points
    TopMenuEntries menu_entries; // what its popover holds
    float pill_w = 0.0f;         // the scale pill's width, 0 when it is not shown
    // The bar's decisions (decide_bar, ease_bar).
    bool band_reserved = false; // the section band has a row of its own
    float hanging_band = 0.0f;  // the height of the band hanging under the revealed bar
    float under_bar = 0.0f;     // what counts as part of the bar below the top row
    bool library_up = false; // the library or the Learn screen is up
    bool modal = false; // the settings, the editor or the library cover the screen
    bool pinned = false;
    bool lane = false;
    bool at_keys = false;
    float open_ease = 0.0f; // how far the settings sheet is in, eased
    TopRowSplit split;
    // The section band (place_band).
    BandPlace band_place;
    bool band_shown = false;
    RectF lane_rect;
    // Beside the status text (draw_status).
    bool in_band = false; // a count-in or countdown shows in the count band over the notes
    TopIndicator indicator;

    DrawFrame(UiModel& model_, SongPort& song_, SectionsPort& sections_, const UiFrameInfo& info_,
              Kit& kit, const SongView& view_, float scale_)
        : model(model_), song(song_), sections(sections_), info(info_), k(kit), lay(info_.layout),
          view(view_), scale(scale_) {}
};

struct Ui::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool renderer_dropped = false; // drop_renderer() ran and adopt_renderer() has not
    std::unique_ptr<LivePreview> preview;
    std::unique_ptr<LivePreview> library_preview;
    std::array<std::unique_ptr<LivePreview>, kUserPresetSlots> config_previews; // the Configs cards
    std::unique_ptr<SheetPreview> library_sheet;
    std::unique_ptr<MedalAtlas> medals; // the medals' pixels, rendered once on a worker thread
    std::unique_ptr<BarLook> bar;
    BarStyle bar_style = BarStyle::Strip;
    RectF toast_slot; // the toasts' own slot as the last frame placed it, in points
    float hanging_band_bottom = 0.0f; // where the band hanging from the bar ends, in points; 0: none
    SettingsSheet sheet;
    std::unique_ptr<SectionEditor> editor;
    bool want_editor = false;    // open the editor on the next frame (it needs the song's sections)
    bool editor_suggest = false; // ... with the suggested cuts shown
    SectionBand band;
    LibraryScreen library;
    ProfileUi profile;
    KeyRingDemo key_ring_demo; // --panel key-ring
    MedalDemo medal_demo;      // --panel medal-sheet, medal-titles
    ScalePill scale_pill;      // free play's scale overlay: the pill in the top row and its popover
    TopMenu top_menu;          // a narrow row's menu button's popover
    LibraryPort* library_port = nullptr;
    LearnScreen learn;
    LearnPort* learn_port = nullptr;
    const PortLists* midi_ports = nullptr;
    bool want_band = false;  // reveal the bar with the band (--panel sections, plan)
    Kit kit;
    bool slider_held = false; // a slider was held in the last frame drawn (Ui::slider_held)
    bool open = false;
    float open_p = 0.0f;   // linear progress of the settings open/close
    float reveal_p = 0.0f; // and of the bar
    bool revealed = false;
    double last_use = -1e9;
    bool last_use_touch = false; // the last use of the bar was a finger
    double last_touch = -1e9;
    // The bar's swipes: real finger events (a second finger cancels the swipe, so a pinch is never
    // one) and a real mouse as one finger, never the mouse SDL makes up from a finger.
    GestureRecognizer swipe_input;
    BarSwipeTracker swipe;
    double swipe_clock_s = 0.0;
    double last_finger_s = -1e9;
    FingerCount fingers; // fingers on the screen, to see a second one land (core finger_count.hpp)
    bool bar_down_for_toast = false; // the bar was hidden to give a warning its row
    bool fallback_told = false; // the pinned bar did not fit this window, and the toast said so
    ResultsCardView results;
    ShiftOfferCard shift_offer; // the octave-shift offer, at the top of the note area
    DeviceCheckCard device_check; // the device check, centred in the note area
    DeviceCheckChoice device_check_choice = DeviceCheckChoice::None; // tapped, until the app takes it
    CrashCard crash;                                      // the notice after a crash and its sheet
    CrashScreenUi crash_screen;                           // the crash screen (--crash-screen)
    CrashChoice crash_choice = CrashChoice::None;         // tapped, until the app takes it
    CardChoice results_choice; // tapped on the card, until the app takes it
    RunCardView run_card;
    ReplayBarView replay_bar;
    ReplayChoice replay_choice;  // tapped on the replay's bar, until the app takes it
    ReviewChoice review_choice;  // tapped on the summary page, until the app takes it
    bool want_summary = false;   // open the summary page once the card is up
    bool want_board = false;     // ... on its Board tab
    bool want_runs = false;      // ... on its Runs tab
    RunCardAction run_choice = RunCardAction::None; // tapped on the check-in or summary
    GlyphWarmUp glyph_warm;
    float card_scale = 0.0f; // the scale the results card would draw at in the note area now
    int frames = 0;          // frames drawn, for the glyph warm-up to start after the first few

    void swipe_event(const SDL_Event& e, ImVec2 display, std::int64_t now_ns);

    // The stages of Ui::draw, in the order it runs them.
    bool begin_frame(const UiFrameInfo& info);
    float prepare_kit_frame(const UiFrameInfo& info, bool reduced_motion, bool high_contrast);
    float prepare_kit(UiModel& model, SongPort& song, SectionsPort& sections, InputPort& input,
                      const UiFrameInfo& info);
    void lay_out_top_row(DrawFrame& d);
    void measure_status(DrawFrame& d);
    bool scale_pill_wanted(const DrawFrame& d) const;
    void decide_bar(DrawFrame& d);
    void swipe_bar(DrawFrame& d);
    void ease_bar(DrawFrame& d);
    void place_bar(DrawFrame& d);
    void place_band(DrawFrame& d);
    void draw_panels(DrawFrame& d);
    void draw_cards(DrawFrame& d);
    bool sheet_side_waits_now(const DrawFrame& d) const;
    void draw_sheet_view_button(DrawFrame& d);
    void draw_sheet_helps(DrawFrame& d);
    void draw_bar(DrawFrame& d);
    void draw_band(DrawFrame& d);
    void draw_gear(DrawFrame& d);
    void draw_library_button(DrawFrame& d);
    void draw_profile(DrawFrame& d);
    void draw_learn_button(DrawFrame& d);
    void place_menu_button(DrawFrame& d);
    void draw_menu_button(DrawFrame& d);
    void show_library(Kit& k);
    void show_learn(Kit& k);
    void draw_status(DrawFrame& d);
    void draw_overlays(DrawFrame& d);
    void draw_crash(DrawFrame& d);
    void place_toasts(DrawFrame& d);
    void end_frame(DrawFrame& d);

    void use_bar(const Kit& k) {
        if (bar_down_for_toast) {
            return;
        }
        revealed = true;
        last_use = k.now;
        last_use_touch = k.touch;
    }
};

} // namespace etude::app
