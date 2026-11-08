// SPDX-License-Identifier: GPL-3.0-or-later
// The touch UI on top of the scene, drawn with Dear ImGui (app/ui/): the top row with the status
// text on the left and the settings button on the right, the control bar that appears in that row
// on a hover or a tap (in the style Settings > View > Bar style picks) with the section band under
// it and the lane the band opens, the settings sheet, and the countdown before a song plays on
// after the settings close. Holds no music state: it edits the
// UiModel the frame loop owns (the loop notices the change by comparing it, applies it to the
// scene and hands it to the saver), and drives the song through a ui::SongPort.
#pragma once

#include "port_lists.hpp"
#include "status_parts.hpp"
#include "etude/count_band.hpp"
#include "etude/device_check.hpp"
#include "etude/results_card.hpp"
#include "etude/screen_layout.hpp"
#include "etude/toast.hpp"
#include "etude/trouble_run.hpp"
#include "ui/about_port.hpp"
#include "ui/crash_card.hpp"
#include "ui/crash_screen.hpp"
#include "ui/device_check_choice.hpp"
#include "ui/icon_port.hpp"
#include "ui/input_port.hpp"
#include "ui/learn_layout.hpp"
#include "ui/learn_port.hpp"
#include "ui/library_layout.hpp"
#include "ui/library_port.hpp"
#include "ui/profile_port.hpp"
#include "ui/replay_port.hpp"
#include "ui/sections_port.hpp"
#include "ui/song_port.hpp"
#include "ui/sources_port.hpp"
#include "ui/upload_port.hpp"
#include "ui_model.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace etude::app {

// Which page of the settings is showing, or what of the section band is open.
enum class PanelTab : std::uint8_t {
    Notes,
    Colours, // Effects (the colours, then the hit effects)
    View,
    Practice,
    Sections, // not a page: the section band with its lane open
    Plan,     // not a page: the section band with the practice list open
    Editor,   // the section editor
    Suggest,  // the section editor with the suggested cuts shown
    Input,
    Configs,
    ConfigsCopy, // not a page: Configs with "Copy Dani's config?" asked (a developer's view)
    AddSongs, // the phone upload page
    About,    // the version, the links and the updates
    Library,  // not a page: the song library
    KeyRing,  // not a page: the key ring alone (a developer's view)
    Learn,    // not a page: the Learn screen's gallery
    LearnScales, // not a page: the Learn screen's scale card
    LearnChords, // not a page: the Learn screen's chord card
    LearnEar,    // not a page: the Learn screen's ear card
    LearnMelodies, // not a page: the Learn screen's Melodies page
    Profiles,      // not a page: the profile panel, "Who's playing"
    ProfilesEdit,  // ... with its Edit mode on
    ProfileNew,    // not a page: the new-profile sheet
    ProfileKeys,   // ... with the on-screen keyboard up and a name typed
    ProfileEditSheet,  // ... the edit sheet of the household's last profile, with its Delete
    ProfileEditFirst,  // ... the edit sheet of the first profile, whose Delete is off
    ProfileDelete,     // ... the confirm that deletes the last profile (an empty field)
    ProfileDeleteKeys, // ... with the keyboard up and the name half typed
    ProfileDeleteTyped, // ... with the name typed, so Delete is on
    Progress,      // not a page: the song library on its Progress page
    Household,     // ... on its Household view
    Medals,        // ... on its Medals view (the wall)
    MedalsLadder,  // ... with the ladder of the first medal held open
    MedalSheet,    // not a page: the medals on a screen of their own (a developer's view)
    MedalTitles,   // ... the eight title medals large and small
};

// Parses "notes", "colours", "view", "practice", "input", "configs" ("presets" too),
// "configs-copy", "add-songs", "editor",
// "suggest", "library" (the song library), "learn" (the Learn gallery), "learn-scales" (its scale
// card), "learn-chords" (its chord card) or "key-ring" (the key ring alone), "profiles", "profiles-edit", "profile-new" and
// "profile-keys" (the profile switcher's panel, its Edit mode, and its sheet without and with the
// keyboard), "profile-edit-sheet", "profile-edit-first" and "profile-delete", "profile-delete-keys" and
// "profile-delete-typed" (the edit sheet of the last and of the first profile, and the confirm that
// deletes the last: empty, with the keyboard, with the name typed), "progress", "household", "medals" and "medals-ladder" (the library on its Progress page, its Household
// view, its Medals wall, and the wall with a medal's ladder open), the section band's "sections" (the lane) and "plan"
// (the practice list), and the names of the pages folded into others ("quick", "look", "names" open
// Notes; "effects" Effects; "keyboard" and "microphone" Input; "metronome" Practice); false for
// anything else. For the --panel option.
bool parse_panel_tab(const char* text, PanelTab* out);
// The Learn screen's tabs ("learn", "learn-scales", "learn-chords"), for parse_panel_tab and the
// panel opener (host_learn.cpp).
bool is_learn_panel_tab(PanelTab tab);

// Output pixels per millimetre the frame draws with (the panel's real size and the UI scale, see
// display_panel.hpp), as the window sets it each frame; before the first frame, SDL's pixel density
// at 96 dpi.
float display_px_per_mm(SDL_Window* window);
void set_display_px_per_mm(float px_per_mm);

// Whether the open song has sheet music, for the view switch on Settings > View.
struct SheetOfferInfo {
    bool song = false;    // a song is open
    bool known = false;   // the answer is in
    bool offered = false; // it has sheet music
    std::string_view reason; // why not, when known and not offered
};

struct UiFrameInfo {
    float width_px = 0.0f; // renderer output size
    float height_px = 0.0f;
    float px_per_mm = 0.0f;
    // The same without the UI scale: the display's real output pixels per millimetre, which the
    // audit measures hit areas in (0: not set, the audit takes px_per_mm).
    float real_px_per_mm = 0.0f;
    float pixel_density =
        1.0f;             // pixels per ImGui point (2 on a retina window): the scale floor's unit
    float ui_grow = 0.0f;       // the growth to draw with (UiGrowHold); 0: the window's own
    ScreenLayout layout; // compute_layout(...) for the current look
    // Where a count-in or the resume countdown shows over the falling notes (window pixels), and
    // how far it is shown (0..1); not visible: in the top row instead.
    CountBand count_band;
    float count_band_shown = 0.0f;
    // Where the scene put the key label (window pixels), empty when it shows none: a toast keeps
    // clear of it.
    RectF key_label;
    // Where it put the combo number (window pixels), empty when it shows none: a toast keeps
    // clear of it too.
    RectF combo;
    // Where each hand's chord name and line stand (window pixels, empty: none shown): a toast keeps
    // clear of them too.
    std::span<const RectF> chord_labels;
    std::int64_t now_ns = 0;
    // The player is at the piano: keys are held or were pressed a moment ago. The bar hides
    // sooner then.
    bool playing = false;
    // The status text in the top row: the keyboard name, then the notes (the song's line, messages,
    // warnings) as parts with a drop rank, which a narrow row gives up whole, in rank order
    // (status_parts.hpp); empty ones are skipped.
    std::string_view status_label;
    StatusParts status_parts;
    // The card for the run that just ended, shown over the note area until the app clears it (null:
    // none). The UI only draws it; `Ui::take_results_choice` says what was tapped.
    const ResultsCard* results = nullptr;
    // The trouble-bar run (null: none): its check-in or summary is drawn in the card's place while
    // the song waits; `Ui::take_run_choice` says what was tapped.
    const TroubleRun* run = nullptr;
    // The device check on screen (null: none): its card is drawn centred in the note area while the
    // check runs; `Ui::take_device_check_choice` says what was tapped. `octave_offset` is the
    // look's note numbering, for the note's name.
    const etude::DeviceCheck* device_check = nullptr;
    int octave_offset = 0;
    // The notice after a crash (null: none): its card is drawn over the screen until the player
    // answers; `Ui::take_crash_choice` says what was tapped.
    const ui::CrashView* crash = nullptr;
    // Developer: the results card opens on the order of its bars (--order-demo).
    bool order_demo = false;
    // The review of the run the results card is about (null: none): the card's "How it went" chip
    // and the page it opens. `Ui::take_review_choice` says what was tapped on the page.
    const ReviewInfo* review = nullptr;
    // The replay view (null: none). While it is up the replay's bar takes the place of the control
    // bar and the section band, the card and its page wait out of sight, and the layout the app
    // passes has the bar pinned; `Ui::take_replay_choice` says what was tapped.
    const ReplayInfo* replay = nullptr;
    // The profiles (null: none, so no chip in the top row). `Ui::take_profile_choice` says what was
    // chosen.
    const ProfileInfo* profiles = nullptr;
    SheetOfferInfo sheet;
    // Beside the sheet music's paper, in output pixels: the cells of its two reading-help toggles
    // (note names, note colours; empty while no score shows) and the two together. Each toggle's
    // hit area is its cell less the dead gap.
    RectF sheet_names;
    RectF sheet_colours;
    RectF sheet_corner;
    // The sheet-music button (Off, On, Full) at the top right of the score's room, in output
    // pixels: it shows in every view, with a song open (empty with none).
    RectF sheet_button;
};

class Ui {
  public:
    Ui(SDL_Window* window, SDL_Renderer* renderer);
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    // The renderer is being replaced (gfx::replace_renderer). The first lets go of every texture
    // the UI holds while the old renderer is alive: the ImGui font atlas, the clef, the roll and
    // the previews. The second gives the new renderer to all of them; the atlas and the pictures
    // are made again by the next frames. What is open, and every setting, stays as it is.
    void drop_renderer();
    void adopt_renderer(SDL_Renderer* renderer);

    // Feeds one SDL event. Returns true when the UI used it (typing into a text field), so the
    // caller must not also act on it.
    bool process_event(const SDL_Event& event, std::int64_t now_ns);

    // The settings sheet, the section editor or the song library. While one is open it covers
    // everything under the top row, and the song is paused.
    bool panel_open() const;
    // A settings slider was held (finger or mouse down on its track) in the last frame drawn. A
    // setting that costs work to apply waits for it to end: a drag changes the value every frame.
    bool slider_held() const;
    // The id of the (i) whose bubble is open, 0 for none (the settings check reads it).
    std::uint32_t info_bubble() const;
    // Takes the toast away at once (the settings check, which taps rows a toast would cover).
    void dismiss_toast();
    // True when a visible part of the UI (the bar, the settings sheet, the gear, a card) is under
    // the point, given as fractions of the window. It reads the windows of the last frame, not
    // where the pointer was left, so a finger that taps a key just after one that tapped a button
    // is judged by its own position.
    bool owns_point(float nx, float ny) const;
    // A card is open whose dismissing tap must not play a key (the scale popover).
    bool blocks_key_presses() const;
    // Fingers on the screen as the events so far count them (a trackpad's are not, a lift that
    // never came is forgotten when the pointer has been up for a few frames). The score reads it
    // after `process_event`, so a finger that just landed is in it.
    int fingers_down() const;
    void open_panel(PanelTab tab);
    // Closes the sheet or the editor; with neither open, the innermost thing open on the library
    // (a popover, a field, the details, the keyboard), else the library itself.
    void close_panel();
    // The song library (ui/library_screen.hpp), drawn from `port`, which the frame loop keeps
    // alive; null leaves the library out.
    void set_library(ui::LibraryPort* port);
    // The MIDI ports to offer on Settings > Input, kept alive by the frame loop; null offers Auto
    // only.
    void set_midi_ports(const PortLists* lists);
    // The phone upload server's port (ui/upload_port.hpp), read by the Add songs page and kept
    // alive by the frame loop; null leaves the page with a switch that does nothing.
    void set_upload(ui::UploadPort* port);
    // The network folder and the USB stick (ui/sources_port.hpp), for the same page.
    void set_sources(ui::SourcesPort* port);
    // The update check and the links (ui/about_port.hpp), for the About page.
    void set_about(ui::AboutPort* port);
    // The best title any profile holds (ui/icon_port.hpp), for the About page's icon colours.
    void set_icons(ui::IconPort* port);
    // Developer: the library's search starts with this text (--library-query).
    void set_library_query(std::string_view text);
    void open_library();
    // The resume point's name for the screen the player is on (core resume_screen_known): "play", "library",
    // "learn" and its cards, or a settings page; and the same opened at a start.
    const char* resume_screen() const;
    void open_resume_screen(std::string_view name);
    // The library, on its Progress page (or its Household view).
    void open_progress(bool household);
    // The library on the Progress page's Medals wall, with the ladder of `achievement` open (-1: none).
    void open_medals(int ladder_of);
    void close_library();
    bool library_open() const;
    // The Learn screen (ui/learn_screen.hpp), its sound played through `port`, which the frame loop
    // keeps alive; null leaves the Learn button out.
    void set_learn(ui::LearnPort* port);
    void open_learn();
    void open_learn_panel(PanelTab tab); // the gallery, or straight to a card
    void close_learn();
    bool learn_open() const;
    // Learn's Listen is sounding a scale: its keys light as the notes come, so frames must be drawn.
    bool learn_listening() const;
    // A key the player pressed on a keyboard of their own while the Learn screen is up: the Ear card
    // takes it as an answer (the screen ignores it elsewhere). Taken at the next draw.
    void learn_note_on(int note);
    // Developer flag (--ear-seed): the Ear card's questions come from this seed, for shots and the audit.
    void set_ear_seed(std::uint64_t seed);
    // The toasts' own slot under the top row as the last frame placed it (points; empty before the
    // first frame): the count band keeps its number below it.
    RectF toast_slot() const;
    // Where the section band hanging from the revealed bar ends over the top of the notes, as the
    // last frame drew it (points); 0 when none hangs (hidden, no song, or the band has a row of
    // its own). The combo number stays below it.
    float hanging_band_bottom() const;
    // PageUp and PageDown while the library is open: a screen of cards up or down.
    void library_page(int direction);
    // The section band's lane is asked for: the frame loop lays the screen out with it (core
    // PinnedBar::lane).
    bool lane_wanted() const;
    // Closes what the bar opened (the tempo popover, the transposition card, the accompaniment
    // menu), then what the section band opened (a popover, then the lane). False when nothing was
    // open.
    bool close_band_overlay();
    // The device check's card is up (drawn, not fading out or hidden): Escape leaves it.
    bool device_check_up() const;
    // The octave-shift offer card is on screen (Escape dismisses it, after the rest).
    bool offer_card_shown() const;
    // Where the card is (points), empty when not shown: the combo number steps under it.
    RectF offer_card_rect() const;
    // The settings sheet, or the section editor, is open (the crash notice names the screen).
    bool sheet_open() const;
    bool editor_open() const;

    // Builds and draws the UI on top of the finished scene, editing `model` and acting on `song`
    // the song's sections and `input`.
    void draw(UiModel& model, ui::SongPort& song, ui::SectionsPort& sections, ui::InputPort& input,
              const UiFrameInfo& info);

    // What was tapped on the results card since the last call (CardAction::None for nothing), and
    // cleared. The app acts on it, the same way as on the keys for the card.
    CardChoice take_results_choice();
    // The same for the run's check-in and summary (RunCardAction::None for nothing).
    RunCardAction take_run_choice();
    // What was tapped on the device check's card, or on Settings > Input's "Run the device check",
    // since the last call (None for nothing), and cleared.
    ui::DeviceCheckChoice take_device_check_choice();
    // What was tapped on the crash notice since the last call (None for nothing), and cleared.
    ui::CrashChoice take_crash_choice();
    // The crash notice's sheet, open at once ("Restart and send report" asked for it at this start).
    void open_crash_report();
    // The crash screen (`--crash-screen`), a whole frame of its own: no song, bar or settings, just
    // the screen and, when `report` is given and Send report was tapped (`open_crash_report`), the
    // report's sheet over it. Returns what was tapped. `info` needs the window's size and density
    // (width_px, height_px, px_per_mm, real_px_per_mm, pixel_density, now_ns).
    ui::CrashScreenTap draw_crash_screen(const ui::CrashScreenView& view, const ui::CrashView* report,
                                         const UiFrameInfo& info);
    // What was tapped on the summary page ("How it went"), and on the replay's bar since the last
    // call (the action None for nothing), and cleared. A drag on the run bar comes back every
    // frame.
    ReviewChoice take_review_choice();
    ReplayChoice take_replay_choice();
    // What was chosen on the profile switcher since the last call (action None for nothing), and
    // cleared. The app switches, adds or edits the profile.
    ProfileChoice take_profile_choice();
    // Escape: closes the profile sheet, else its panel. False when neither was open.
    bool close_profile();
    // The profile panel or its sheet is open (the app works the streaks out only then).
    bool profile_open() const;
    // Opens the results card's summary page (once the card is up), on its Board tab with `board`
    // and on its Runs tab with `runs` (the page shows the Board tab where it has no Runs one);
    // closes it with `close_summary`.
    void open_summary(bool board = false, bool runs = false);
    void close_summary();
    // The results card is showing the order list: Space and Return start it, Escape goes back.
    bool card_ordering() const;
    void card_order_key(bool start);

    // Shows `text` as the toast (the one floating pill for transient feedback: "Saved as Night",
    // "Song sound off"), replacing the one on screen. `warning` keeps it up longer, for something
    // the player must not miss. `name` is the part of it that is a song's title: the part a toast
    // too long for its room shortens. Empty text does nothing.
    void show_toast(std::string_view text, bool warning = false, TextSpan name = {},
                    SongLines song = {});

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace etude::app
