// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/chord.hpp"
#include "etude/icon_colour.hpp"
#include "etude/keyboard.hpp"
#include "etude/library_browse.hpp"
#include "etude/metronome.hpp"
#include "etude/note_window.hpp"
#include "etude/palette.hpp"
#include "etude/renderer_path.hpp"
#include "etude/run_events.hpp"
#include "etude/runs_chart.hpp"
#include "etude/scales.hpp"
#include "etude/settings_doc.hpp"
#include "etude/transport.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// How the Learn scale card picks the key, and which hands its Play plays.
enum class LearnPicker : std::uint8_t { Ring, Keys };
enum class LearnHands : std::uint8_t { Both, Right, Left };
enum class LearnDirection : std::uint8_t { Up, Down, UpDown };
// What the Ear card asks (#374): one note to find, or an interval from a note shown: up, down, or the
// two notes together.
enum class LearnEarMode : std::uint8_t { Note, IntervalUp, IntervalDown, Together };
// How the medal wall shows the achievements: a category at a time as large medals (Category), or every
// category in a row of its own on one screen (Rows).
enum class BadgeWallView : std::uint8_t { Category, Rows };

// What the app keeps between runs, in two pieces: the session (where you left off
// in a song) and the app settings (what does not depend on the song). The look and the user preset
// slots are a third piece with their own model; they join the same file as the `[look]` and
// `[user_presets]` sections (see LoadedSettings).
//
// Every field has a default, a valid range, and a clamp. `clamped()` is the one place that knows
// the ranges: the file reader, the writer and the restore check all go through it, so no state
// that came from a file, however damaged, can reach the app out of range.

inline constexpr std::string_view kAppSection = "app";
inline constexpr std::string_view kSessionSection = "session";

inline constexpr double kMaxSessionSeconds =
    24.0 * 60.0 * 60.0; // a song longer than a day is a bug
inline constexpr std::size_t kMaxSongPathBytes = 4096;
inline constexpr std::size_t kMaxNameBytes = 256; // keyboard and MIDI port names
inline constexpr std::size_t kMaxDeviceCheckSeenBytes = 400; // the keyboards the check has seen
inline constexpr int kMaxCountInBars = 2;
inline constexpr int kMaxAccompanimentVolume = 100;
inline constexpr int kDefaultAccompanimentVolume = 100;
inline constexpr int kDefaultCountInBars = 1;
inline constexpr int kMaxSessionSectionId = 2'000'000'000; // as kMaxSectionId in sections.hpp
inline constexpr int kMaxSessionPlanItems = 64;            // as kMaxPlanItems in sections.hpp
inline constexpr int kMinKeyboardKeys = 12; // a manual range narrower than an octave is a typo

// Index 0 is the default of every enum below.
enum class PracticeMode : std::uint8_t {
    FreePlay, // no grading: the song plays or waits for nothing
    Wait,     // the song stops at the hit line until the right notes are down
    Flow,     // the song keeps going and each note is graded
};

// What you hear of the song while you practise (Playback mutes the rest).
// Your own playing always sounds.
enum class PianoPlays : std::uint8_t {
    Nothing,   // Mute: you hear only what you play
    OtherHand, // the hand you are not practising; nothing when you practise both
    Left,
    Right,
    Both, // the whole song: watching it
};

// How the control bar looks when revealed: a full-width strip (the default), floating groups, or a
// centred dock. A UI preference, not part of the look: presets leave it alone.
enum class BarStyle : std::uint8_t {
    Strip,  // a full-width translucent strip
    Groups, // floating button groups with captions under the icons
    Dock,   // a centred floating dock
};

// What the note area shows while a song is open: the falling notes, the song as sheet music, or a
// strip of sheet music above the falling notes. The keyboard stays below in all three. Free play
// (no song) always shows the falling notes.
enum class ViewMode : std::uint8_t {
    Falling,
    Sheet,
    Both,
};

enum class KeyboardChoice : std::uint8_t {
    Auto,    // match the connected device by name (find_profile), else ask
    Profile, // the built-in profile called `keyboard_name`
    Manual,  // `keyboard_lowest` .. `keyboard_highest`
};

// Where the player's notes come from. Typing and the on-screen keys always play.
enum class InputMode : std::uint8_t {
    Midi,       // the MIDI keyboard only; the microphone is never opened
    Microphone, // what the microphone hears; MIDI keyboards are ignored
    Both,       // both at once; a note the microphone hears just after the keyboard sent it is
                // not played twice
};

inline constexpr int kMinMicA4Tenths = 4150; // A4 in tenths of a hertz
inline constexpr int kMaxMicA4Tenths = 4650;
inline constexpr int kDefaultMicA4Tenths = 4400;
inline constexpr int kMaxMicNoiseFloorPpm = 500'000; // RMS in millionths of full scale
// The screen size override, the display's width in millimetres: 0 reads it from the display, else
// kMinScreenWidthMm..kMaxScreenWidthMm (a value under the least counts as 0).
inline constexpr int kMinScreenWidthMm = 60;
inline constexpr int kMaxScreenWidthMm = 1500;

struct SessionState {
    // Song identity. The path is relative to the song library folder, with '/' separators, and
    // never absolute or containing "..". Empty means no song (free play). The hash is
    // content_hash() of the file's bytes; 0 means none was recorded.
    std::string song_path;       // default empty; valid UTF-8 without control characters
    std::uint64_t song_hash = 0; // default 0
    // Where the song was opened from, as an absolute path, kept next to the library-relative one:
    // a song opened from outside the library has only this, and a song in the library falls back to
    // it when the library path no longer holds it. Empty when unknown; never relative or
    // containing "..". The hash is checked for either path.
    std::string song_abs_path; // default empty; valid UTF-8 without control characters

    double position_s = 0.0; // 0 .. kMaxSessionSeconds; default 0. Always paused.
    int speed_percent = 100; // 25 .. 200, a whole percent; default 100
    HandFilter practice_hands = HandFilter::Both; // the hand(s) you practise; default Both
    PianoPlays piano_plays = PianoPlays::Nothing; // default Mute

    // The A-B loop. A loop exists when both ends are in 0 .. kMaxSessionSeconds and are at least
    // kMinLoopSeconds apart (start before end). Without one both ends are 0 and it is off.
    double loop_start_s = 0.0;
    double loop_end_s = 0.0;
    bool loop_enabled = false;

    // The auto speed-up policy (AutoSpeedUp in transport.hpp).
    bool auto_speed_enabled = false;
    int auto_speed_start_percent = 60; // 25 .. 200, steps of 5
    int auto_speed_step_percent = 5;   // 5 .. 25, steps of 5
    int auto_speed_max_percent = 100;  // start .. 200, steps of 5

    // Where the player was in the song's sections (sections.hpp), so a restart returns to it:
    // the section looped (a SectionId of `song_hash`'s SongSections; 0 is none) and the item of the
    // practice plan that was playing (1-based place in SongSections::plan; 0 is no plan). Both
    // belong to the song: without one they are 0. The app checks them against the sections it
    // loads, since this layer does not know which sections exist, and drops what is gone.
    int section_id = 0; // 0 .. kMaxSessionSectionId
    int plan_item = 0;  // 0 .. kMaxSessionPlanItems

    // Bars of clicks before the song moves (metronome.hpp): 0 is off, 1 or 2 bars of the meter
    // where it starts. It is saved with the speed in this last-song session, so reopening that song
    // at startup (or named with --song) brings both back; every song also keeps its own in the
    // history (song_practice.hpp), and a song never opened starts at the default.
    int count_in_bars = kDefaultCountInBars; // 0 .. 2
    PracticeMode mode = PracticeMode::FreePlay;
    int transpose_semitones = 0; // -12 .. +12

    // The keyboard. In Profile mode the name picks a built-in profile and the range is the one
    // saved with it, used when this build does not know the name. A usable range is always kept,
    // even in Auto mode: ordered, inside 0..127, both ends white keys, at least 12 keys.
    KeyboardChoice keyboard = KeyboardChoice::Auto;
    std::string keyboard_name; // <= 256 bytes; empty in Profile mode becomes Auto
    int keyboard_lowest = 36;
    int keyboard_highest = 96;

    bool has_song() const { return !song_path.empty() || !song_abs_path.empty(); }
    bool has_loop() const { return loop_end_s - loop_start_s >= kMinLoopSeconds; }
    AutoSpeedUp auto_speed_up() const {
        return AutoSpeedUp{auto_speed_enabled, auto_speed_start_percent, auto_speed_step_percent,
                           auto_speed_max_percent};
    }
    KeyRange keyboard_range() const { return KeyRange{keyboard_lowest, keyboard_highest}; }

    // The same state with every field forced into its valid range (see above). A library path that
    // is absolute, climbs out with "..", is not UTF-8, holds a control character, or is too long
    // becomes empty; so does an absolute path that is relative, climbs out, or is bad the same way.
    // The song fields go back to their defaults when both paths are empty. Idempotent.
    SessionState clamped() const;

    friend bool operator==(const SessionState&, const SessionState&) = default;
};

inline constexpr int kMaxLibraryScroll = 1'000'000; // design points
inline constexpr int kMaxActiveProfile = 1'000'000;

// The resume countdown's range in whole seconds (the stepper in Settings > Practice, and the clamp on
// load).
inline constexpr int kMinResumeCountdownS = 1;
inline constexpr int kMaxResumeCountdownS = 10;

struct AppSettings {
    int hit_window_ms = 80;        // 20 .. 250: Flow Mode's window either side of a note
    int latency_correction_ms = 0; // -250 .. 250: the calibrated offset of this setup
    std::string midi_in_port;      // <= 256 bytes; empty picks the keyboard automatically
    std::string midi_out_port;     // <= 256 bytes; empty picks the output automatically
    int daily_goal_minutes = 15;   // 0 .. 720; 0 turns the goal off
    // Closing the settings while a song was playing counts down this many seconds, one soft tick
    // each, before the song plays on: 1 .. 10, default 3.
    int resume_countdown_s = 3;
    // Skip back and skip forward each move by their own number of seconds: 1 .. 30, default 5.
    // A tap moves one step; holding the button (or key) repeats it.
    int skip_back_seconds = 5;
    int skip_forward_seconds = 5;
    BarStyle bar_style = BarStyle::Strip;
    // The metronome belongs to the player, not the song (metronome.hpp). The count-in uses the
    // sound and volume too, whether the metronome is on or not.
    bool metronome = false;
    ClickVoice metronome_sound = ClickVoice::WoodBlock;
    int metronome_volume = kDefaultClickVolume; // 0 .. 100; 0 is silent
    bool metronome_accent = true;               // beat one is louder and higher
    ClickSubdivision metronome_subdivision = ClickSubdivision::Off;
    bool metronome_pulse = false;    // a pulse on the hit line with each beat, for silent practice
    // The accompaniment (the song's parts that are not a practice hand): whether a song the player
    // has not chosen for starts with it sounding, and how loud it sounds against the piano. Each
    // song keeps its own mutes (song_practice.hpp); the volume is the player's.
    bool accompaniment = true;
    int accompaniment_volume = kDefaultAccompanimentVolume; // 0 .. 100; 0 is silent
    // "Keep the bar shown": the control bar stays on screen, and the notes fall below it.
    bool bar_pinned = false;
    // "Show difficulty on the section band": each bar of the track is tinted by how hard it is,
    // green to red as the stars are. Off by default.
    bool band_difficulty = false;
    // "Weight score by difficulty": the results card shows a second score beside the plain one,
    // each bar counted by its difficulty. The stored score stays the plain one. Off by default.
    bool weighted_score = false;
    // How many of a song's newest runs keep the notes that were played in them (for the ghost and
    // the replay), besides the runs the player pinned: 0 .. kMaxRunEventsKept. Older runs keep their
    // scores only.
    int run_events_kept = kDefaultRunEventsKept;
    // Falling notes, sheet music or both (`view_mode`: falling, sheet, both).
    ViewMode view_mode = ViewMode::Falling;
    // Which renderer draws (`renderer`: basic, gpu, gl). Read at start, so a change shows after a
    // restart.
    RendererPath renderer = RendererPath::Basic;
    // ABC tunes: chord symbols ("Am", "G7") become a left-hand part, so a melody gives two-hand
    // practice. Off by default: a single line stays in one hand. Read when a tune is opened.
    bool abc_chords = false;
    // The free-play scale overlay (`scale_overlay`, `scale_root`, `scale_type`, `scale_fingers`):
    // on, the keys outside the scale are dimmed on the live keyboard and the root keys carry the
    // root's name; with fingers on, the right hand's fingering shows on the keys. The player's own
    // state, drawn in the pill of the top row, not on a settings page. `scale_root` is a pitch
    // class.
    bool scale_overlay = false;
    int scale_root = 0;
    ScaleType scale_type = ScaleType::Major;
    bool scale_fingers = false;
    InputMode input_mode = InputMode::Midi;
    std::string mic_device; // <= 256 bytes; empty picks the system's default microphone
    bool mic_learn_tuning = false;
    // The microphone's tuning: A4 in tenths of a hertz, kMinMicA4Tenths .. kMaxMicA4Tenths. With
    // mic_learn_tuning on, the app keeps it at what the instrument was heard to be.
    int mic_a4_tenths = kDefaultMicA4Tenths;
    // The room's noise floor from "Calibrate noise", as RMS in millionths of full scale; 0 means
    // not calibrated.
    int mic_noise_floor_ppm = 0;
    // The colours the picker on the Effects page was last used with, newest first, at
    // most kRecentColours (`recent_colors`, "#RRGGBB" words in one quoted text). In [app], not in
    // the look: they are the player's history, not part of a look, so a preset or a look reset
    // leaves them alone.
    RecentColours recent_colors;
    // How the song library is shown (library_browse.hpp): its sort and direction, grouping and quick
    // filter, so it opens the way the player left it. The player's state, not drawn on a page.
    // `library_levels` keeps the level filter as bits: 1 for level 1, 2 for 2, 4 for 3, 8 for 4,
    // 16 for 5, added up; 0 shows every level.
    BrowseOrder library;
    BrowseView library_view = BrowseView::Grid; // Grid, List or Shelves
    // Where the library's results were scrolled to when it was left (design points down from the
    // top, 0 .. kMaxLibraryScroll), so it opens there again, also after a restart.
    int library_scroll = 0;
    // "Get songs": the app may reach the free song catalogues (their listings and files). Off
    // until the player turns it on in the library; off, nothing is fetched.
    bool get_songs_online = false;
    // Versions of one piece (an easy and an original arrangement, the same piece in two keys) show
    // as one card with a chip for each version; off, every file is a card of its own. In the
    // library's view popover.
    bool group_versions = true;
    // The display's width in millimetres when it reports the wrong one (`screen_width_mm`): every
    // size of the UI is in real millimetres from it. 0 reads the size from the display; the height
    // follows the pixels' shape. The device's, not a profile's.
    int screen_width_mm = 0;
    // The Learn screen (#370): how the scale card picks its key (a ring of fifths or the twelve
    // keys as buttons), and the scale it was last showing, so the next visit and the "Continue" row
    // return to it. The player's choice, not drawn on a settings page.
    LearnPicker learn_picker = LearnPicker::Ring;
    int learn_root = 0; // 0..11, C = 0
    ScaleType learn_scale = ScaleType::Major;
    LearnHands learn_hands = LearnHands::Both; // the hands Listen and Practice play
    // The drill settings both cards share (#373): the tempo in beats a minute (an eighth note a
    // note, in steps of 5), the direction, the octaves (1 or 2) and, on the scale card, whether a
    // scale or its arpeggio is played.
    int learn_tempo = 80; // 40..200
    LearnDirection learn_direction = LearnDirection::Up;
    int learn_octaves = 1; // 1 or 2
    bool learn_arpeggio = false;
    // The Ear card (#374): what it asks, the octave of the C its keyboard starts on (the notes asked
    // lie on the keys shown), whether any octave of the note counts as right, and the largest
    // interval asked (the smallest is a semitone).
    LearnEarMode learn_ear_mode = LearnEarMode::Note;
    int learn_ear_octave = 4; // 1..6
    bool learn_ear_any_octave = false;
    int learn_ear_max = 12; // 2..12
    // The chord card's chord: the root (0..11), the quality (never None) and the inversion (0 root
    // position .. 4); the picker's look above is shared with the scale card.
    int learn_chord_root = 0;
    ChordQuality learn_chord = ChordQuality::Major;
    int learn_inversion = 0;
    // The medal wall of the Progress page (#396): the view last chosen with its toggle, kept like
    // `library_view` (not a row of a settings page).
    BadgeWallView badge_wall_view = BadgeWallView::Category;
    // The song-runs chart of the summary page's Board tab (#396): the view last chosen with its
    // button, kept per profile like `badge_wall_view`.
    RunsChartView runs_chart_view = RunsChartView::Bars;
    // The songs the player added to the Melodies page from a song's details card, each a content hash
    // with its step (melody_added.hpp), kept per profile as one line of text.
    std::string melody_added;
    // The device check (device_check_offer.hpp): it has ended once (so a later start does not open
    // it), and the keyboards it has seen, as ids joined by `|` (`device_check_seen`).
    bool device_check_done = false;
    std::string device_check_seen;
    // About > "Check automatically" (`update_auto_check`): look for a newer release once a day. Off
    // by default; it only looks and never installs. The device's, not a profile's.
    bool update_auto_check = false;
    // About > the app icon's colour (`icon_colour`, icon_colour.hpp): the device's, so the Dock or the
    // launcher shows it at every start. The page only offers the colours the profiles' titles have
    // unlocked; a stored colour is kept even when the profile that earned it is gone.
    IconColour icon_colour = kDefaultIconColour;
    // The profile in use (profile.hpp): the device's, so a restart opens as the same person. A
    // profile the history no longer holds falls back to the default one when it loads.
    int active_profile = 1; // 1 .. kMaxActiveProfile

    AppSettings clamped() const;

    friend bool operator==(const AppSettings&, const AppSettings&) = default;
};

// Reading never fails. A missing key keeps the default. A value that does not parse keeps the
// default (BadValue); one outside its range is clamped (OutOfRange); keys this build does not know,
// malformed lines and repeated keys are reported too. The result is always clamped. Line numbers in
// the issues refer to the text the document was parsed from.
SessionState read_session(const SettingsDocument& doc,
                          std::vector<SettingsIssue>* issues = nullptr);
AppSettings read_app_settings(const SettingsDocument& doc,
                              std::vector<SettingsIssue>* issues = nullptr);

// Writes every key of the (clamped) value into its section, creating it when missing. Other keys
// and sections of the document are not touched, and a key already holding the same value keeps its
// line as typed.
void write_session(SettingsDocument& doc, const SessionState& state);
void write_app_settings(SettingsDocument& doc, const AppSettings& settings);

// A whole file: the document with everything in it, and the typed sections read from it. The app
// edits `app` and `session` and calls serialize(); sections this build does not know, like a
// newer build's, come back byte for byte.
struct LoadedSettings {
    SettingsDocument document;
    AppSettings app;
    SessionState session;
    std::vector<SettingsIssue> issues;

    // Newer major version: load it, but writing it back could damage what the newer build means.
    bool read_only_recommended() const { return document.read_only_recommended(); }

    // The document with `app` and `session` written into a copy of it, with a format line.
    std::string serialize() const;
};

// Parses `text` and reads the `[app]` and `[session]` sections. Sections with other names are
// reported as UnknownSection unless listed in `other_known_sections`: a reader that owns another
// section (`look`, `user_presets`) names it there, takes its text with
// `document.section_text(name)`, and writes it back with `document.set_section_text`.
LoadedSettings load_settings(std::string_view text,
                             std::span<const std::string_view> other_known_sections = {});

} // namespace etude
