// SPDX-License-Identifier: GPL-3.0-or-later
#include "pages.hpp"
#include "tempo.hpp"

#include "colour_widgets.hpp"
#include "hands_row.hpp"
#include "icons.hpp"
#include "input_port.hpp"
#include "item_probe.hpp"
#include "pages_columns.hpp"
#include "preview.hpp"
#include "row_layout.hpp"
#include "screen_size.hpp"
#include "settings_defaults.hpp"
#include "song_port.hpp"
#include "transport.hpp"
#include "view_choice.hpp"

#include "etude/accidental_text.hpp"
#include "etude/color.hpp"
#include "etude/hit_window.hpp"
#include "etude/keyboard.hpp"
#include "etude/note_style.hpp"
#include "etude/pitch.hpp"
#include "etude/run_events.hpp"
#include "etude/sheet_view.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

// The last non-zero grade opacity seen at rest, which the switch and a slider dragged to 0 restore.
int g_grade_restore = kMaxGradeOpacity;

// The Notes page's first column's share of the width, and its preview's height as a share of its
// width (shorter than the other pages' previews, so the page fits at 1366x768).
constexpr float kNotesLeftShare = 0.54f;
constexpr float kNotesPreviewAspect = 5.0f / 16.0f;

// Effects (colour_widgets.cpp).
void page_colours(Kit& k, UiModel& model, ImVec2 body) { colours_page(k, model, body); }

// Flow Mode's grade words: the switch, where they sit and how strong. The switch and the slider
// tell the same story: the slider reads 0 while the words are off, and the switch turns itself
// off when the slider reaches 0.
void grade_word_rows(Kit& k, LookSettings& look) {
    if (look.grade_opacity > kMinGradeOpacity && !k.slider_active) {
        g_grade_restore = look.grade_opacity;
    }
    bool on = look.grade_words_shown();
    catalogue_sweep::drawn(Setting::GradeWords);
    if (switch_row(k, icon::kPractice, "Grade words", &on,
                   "Perfect, Good, Early, Late and Miss over the notes in Flow Mode.")) {
        set_grade_words(look, on, g_grade_restore);
    }
    constexpr Seg kWhere[] = {{nullptr, "Bottom"}, {nullptr, "Top"}};
    int where = static_cast<int>(look.grade_position);
    catalogue_sweep::drawn(Setting::GradePosition);
    if (segmented(k, "gradepos", icon::kHeight, "Position", kWhere, &where)) {
        look.grade_position = static_cast<GradePosition>(where);
    }
    int percent = look.grade_words_shown() ? look.grade_opacity : kMinGradeOpacity;
    catalogue_sweep::drawn(Setting::GradeOpacity);
    // The reset is of the strength itself: it leaves Grade words as they are.
    if (slider_row_int(k, icon::kDrop, "Opacity", &percent, kMinGradeOpacity, kMaxGradeOpacity,
                       Unit::Percent, nullptr,
                       ResetTo<int>{look_defaults().grade_opacity, &look.grade_opacity})) {
        set_grade_opacity(look, percent, g_grade_restore);
    }
}

// Two choices on one row: a tap moves to the other.
void block_layout_row(Kit& k, LookSettings& look) {
    constexpr const char* kBlockLayouts[] = {"Key width", "Even lanes"};
    const int blocks = static_cast<int>(look.block_layout) % 2;
    catalogue_sweep::drawn(Setting::BlockLayout);
    if (cycle_row(k, icon::kPiano, "Blocks", kBlockLayouts[blocks],
                  "Key width: each block as wide as its key, black keys' blocks over the white "
                  "ones. Even lanes: every block the same width, none overlapping.")) {
        look.block_layout = static_cast<BlockLayout>((blocks + 1) % 2);
    }
}

// The sheet music's note names as one row, like the falling notes' own: off, or which names. The
// switch (Note names) and the choice of names (C D E, Do Re Mi) stay two settings.
void sheet_names_row(Kit& k, LookSettings& look) {
    constexpr Seg kSheetNaming[] = {{nullptr, "Off"}, {nullptr, "C D E"}, {nullptr, "Do Re Mi"}};
    int shown = sheet_names_choice(look);
    catalogue_sweep::drawn(Setting::NamesOnSheet);
    catalogue_sweep::drawn(Setting::SheetNaming);
    if (segmented(k, "sheet-naming", icon::kNames, "Names", kSheetNaming, &shown,
                  "Each note's name above the top staff's notes and below the others', with the "
                  "octave number and sharps or flats as above.")) {
        set_sheet_names_choice(look, shown);
    }
}

// Sheet music's reading helps; the sheet's corner has the same two switches.
void sheet_music_rows(Kit& k, LookSettings& look) {
    section(k, "Sheet music");
    sheet_names_row(k, look);
    // A recognised chord as one lead-sheet symbol ("C/E"), as the chord label names it.
    constexpr Seg kSheetChords[] = {{nullptr, "Notes"}, {nullptr, "Symbols"}};
    int sheet_chords = static_cast<int>(look.sheet_chords);
    catalogue_sweep::drawn(Setting::SheetChords);
    if (segmented(k, "sheet-chords", icon::kChord, "Chords", kSheetChords, &sheet_chords,
                  "A chord on the sheet as its notes' names, or as one chord symbol such as C/E.")) {
        look.sheet_chords = static_cast<SheetChords>(sheet_chords);
    }
    catalogue_sweep::drawn(Setting::SheetNoteColours);
    switch_row(k, icon::kPalette, "Note colours", &look.sheet_note_colours,
               "Each note head outlined in its note's colour, the colours Colour by Note gives the "
               "blocks; a played note still turns green.");
}

// Notes. First column, what the falling notes look like: the live preview, the style, then its
// strength, glow and block layout, then what is drawn around them (beat lines, the C markers, the
// song's key). Second column, what the notes say: the names on the notes and keys, their spelling
// and octave numbers, then the sheet music's names.
void page_notes(Kit& k, UiModel& model, int columns) {
    LookSettings& look = model.look;
    // The first column is a little wider than half, so all eight style tiles keep one row.
    Columns cols(k, columns, kNotesLeftShare);
    // The live preview first: every control below changes it at once. Short, so the page fits.
    if (k.preview != nullptr) {
        const float width = ImGui::GetContentRegionAvail().x;
        k.preview->draw(k, look, width, std::round(width * kNotesPreviewAspect),
                        PreviewLoop::Played);
        ImGui::Dummy({1.0f, k.s(2.0f)});
    }
    catalogue_sweep::drawn(Setting::NoteStyle);
    style_picker(k, look);
    catalogue_sweep::drawn(Setting::StyleIntensity);
    slider_row(k, icon::kBlend, "Intensity", &look.style_intensity, kMinStyleIntensity,
               kMaxStyleIntensity, 100.0f, Unit::Percent, 0, nullptr,
               look_defaults().style_intensity);
    catalogue_sweep::drawn(Setting::BlockGlow);
    slider_row_int(k, icon::kSparks, "Block glow", &look.block_glow, kMinBlockGlow, kMaxBlockGlow,
                   Unit::Percent, nullptr, look_defaults().block_glow);
    block_layout_row(k, look);
    constexpr Seg kBeatLines[] = {
        {nullptr, "Off"}, {nullptr, "Beat"}, {nullptr, "Half"}, {nullptr, "Bar"}};
    int beat_lines = static_cast<int>(look.beat_lines);
    catalogue_sweep::drawn(Setting::BeatLines);
    if (segmented(k, "beatlines", icon::kMetronome, "Beat lines", kBeatLines, &beat_lines,
                  "Faint lines across the falling notes: on every beat, every half beat or every "
                  "bar.")) {
        look.beat_lines = static_cast<BeatLines>(beat_lines);
    }
    catalogue_sweep::drawn(Setting::CMarkers);
    switch_row(k, icon::kCMarker, "C markers", &look.c_markers,
               "A mark and the octave number on every C key of the keyboard.");
    // The song's key, faint above the keyboard on the falling notes (not over the score).
    catalogue_sweep::drawn(Setting::KeyLabel);
    switch_row(k, icon::kFitSong, "Key", &look.show_key,
               "The song's key, such as G major, faint at the bottom left above the keyboard.");
    cols.next();
    section(k, "Note names");
    constexpr Seg kModes[] = {{nullptr, "Off"}, {nullptr, "C D E"}, {nullptr, "Do Re Mi"}};
    int mode = static_cast<int>(look.name_mode);
    catalogue_sweep::drawn(Setting::NameMode);
    if (segmented(k, "mode", nullptr, nullptr, kModes, &mode)) {
        look.name_mode = static_cast<NameMode>(mode);
        if (look.name_mode != NameMode::Off) {
            if (!look.names_on_blocks && !look.names_on_keys) {
                look.names_on_blocks = true;
            }
        }
    }
    constexpr Seg kWhere[] = {{nullptr, "Notes"}, {nullptr, "Keys"}, {nullptr, "Both"}};
    int where = look.names_on_blocks && look.names_on_keys ? 2
                : look.names_on_blocks                     ? 0
                : look.names_on_keys                       ? 1
                                                           : -1;
    catalogue_sweep::drawn(Setting::NamesWhere);
    if (segmented(k, "where", nullptr, "On", kWhere, &where)) {
        look.names_on_blocks = where != 1;
        look.names_on_keys = where != 0;
    }
    // By key spells a black key from the key at the playhead (E♭ in E♭ major, F♯ in G major).
    constexpr Seg kAcc[] = {{nullptr, "By key"}, {nullptr, "C#"}, {nullptr, "Db"}};
    int spelling = static_cast<int>(look.names_spelling);
    catalogue_sweep::drawn(Setting::NamesAccidentals);
    if (segmented(k, "acc", nullptr, "Black keys", kAcc, &spelling,
                  "How a black key is spelled. By key follows the song's key (E♭ in E♭ major, F♯ "
                  "in G major); C# and Db always use sharps or always flats.")) {
        look.names_spelling = static_cast<NameSpelling>(spelling);
    }
    // A black key's block is thin, so its name is the letter with a small accidental, inside the
    // block: Auto where it fits, Always at the smallest size even if it spills past the block,
    // Off none. Three choices on one row: a tap moves to the next.
    constexpr const char* kBlackNames[] = {"Auto", "Always", "Off"};
    const int black_names = static_cast<int>(look.black_key_names);
    catalogue_sweep::drawn(Setting::BlackKeyNames);
    if (cycle_row(k, icon::kPiano, "Black key names", kBlackNames[black_names % 3],
                  "Auto: a black key's name where it fits its block. Always: at the smallest "
                  "size, even past a thin block. Off: none.")) {
        look.black_key_names = static_cast<BlackKeyNames>((black_names + 1) % 3);
    }
    catalogue_sweep::drawn(Setting::OctaveNumber);
    switch_row(k, icon::kOctave, "Octave number", &look.names_show_octave);
    constexpr Seg kNumbering[] = {{nullptr, "C4"}, {nullptr, "C3"}};
    int numbering = static_cast<int>(look.octave_numbering);
    catalogue_sweep::drawn(Setting::MiddleC);
    if (segmented(k, "num", nullptr, "Middle C", kNumbering, &numbering,
                  "Which octave number middle C gets: C4 as in scientific pitch, C3 as on Yamaha "
                  "keyboards. It numbers every octave shown, on names and C markers.")) {
        look.octave_numbering = static_cast<OctaveNumbering>(numbering);
    }
    sheet_music_rows(k, look);
}

const char* key_size_label(KeySize s) {
    static constexpr const char* kNames[] = {"Fit keyboard", "Auto (song range)", "Small",
                                             "Medium",       "Large",             "Extra large"};
    return kNames[static_cast<std::size_t>(s)];
}

// Which renderer draws the frame. It is read at start, so a change shows after a restart, which
// the label and the toast say.
void renderer_row(Kit& k, UiModel& model) {
    constexpr Seg kPaths[] = {{nullptr, "Basic"}, {nullptr, "GPU"}, {nullptr, "OpenGL"}};
    int path = static_cast<int>(model.app.renderer);
    catalogue_sweep::drawn(Setting::Renderer);
    if (segmented(k, "renderer", icon::kLayers, "Renderer (restart)", kPaths, &path)) {
        model.app.renderer = static_cast<RendererPath>(path);
        k.say(std::string("Renderer: ") + kPaths[std::clamp(path, 0, 2)].label +
              ", after a restart");
    }
}

// The display's width, for a panel that reports the wrong size: every control's size is in real
// millimetres from it. Auto reads it from the display.
void screen_size_row(Kit& k, UiModel& model) {
    catalogue_sweep::drawn(Setting::ScreenSize);
    const std::string value = screen_size_text(model.app.screen_width_mm);
    if (cycle_row(k, icon::kSize, "Screen size", value.c_str(),
                  "Auto reads the display's size. Pick the panel's diagonal when controls look the "
                  "wrong size: they stay at least 12 mm.")) {
        model.app.screen_width_mm = next_screen_size(model.app.screen_width_mm);
        k.say("Screen size: " + screen_size_text(model.app.screen_width_mm));
    }
}

void bar_style_row(Kit& k, UiModel& model) {
    constexpr Seg kBars[] = {{nullptr, "Strip"}, {nullptr, "Groups"}, {nullptr, "Dock"}};
    constexpr MorphGlyph kBarIcons[] = {MorphGlyph::BarStrip, MorphGlyph::BarGroups,
                                        MorphGlyph::BarDock};
    int bar = static_cast<int>(model.app.bar_style);
    // The row's icon is the chosen style's picture and morphs into the next one's.
    catalogue_sweep::drawn(Setting::BarStyle);
    if (segmented(k, "barstyle", kBarIcons[std::clamp(bar, 0, 2)], "Bar style", kBars, &bar,
                  "Strip: a bar across the whole top, the controls in groups. Groups: floating cards, "
                  "each control with its name under it. Dock: one small bar in the middle, play the "
                  "one filled button.")) {
        model.app.bar_style = static_cast<BarStyle>(bar);
    }
    catalogue_sweep::drawn(Setting::KeepBarShown);
    switch_row(k, icon::kPin, "Keep the bar shown", &model.app.bar_pinned,
               "The bar stays on screen instead of hiding, and the falling notes start below it.");
    catalogue_sweep::drawn(Setting::BandDifficulty);
    switch_row(k, icon::kGauge, "Difficulty on the section band", &model.app.band_difficulty,
               "Tints each bar of the section band from green (easy) to red (hard).");
}

// What the note area shows with a song open: the falling notes, sheet music or both. A song with no
// sheet music greys Sheet and Both, and a tap on them says why. The side button (Off, On, Full)
// and the I key are this same setting and go through the same rules (view_choice.hpp).
void view_mode_row(Kit& k, UiModel& model) {
    const Seg kViews[kViewCells] = {{nullptr, view_cell_label(0)},
                                    {nullptr, view_cell_label(1)},
                                    {nullptr, view_cell_label(2)}};
    int view = view_cell_of(model.app.view_mode);
    int refused = -1;
    catalogue_sweep::drawn(Setting::ViewMode);
    if (segmented(k, "view", icon::kNote, "Show", kViews, &view, view_refused_mask(k.sheet.refused()),
                  &refused)) {
        model.app.view_mode = view_mode_of_cell(view);
        k.say(view_mode_label(model.app.view_mode));
    }
    if (refused >= 0) {
        k.say(view_refusal(k.sheet.reason));
    }
}

void key_size_row(Kit& k, LookSettings& look) {
    // Words short enough for the six cells to share one line under the label, so the row is one
    // tall: Fit fills the screen's width with the keyboard, Auto shows the song's whole range (chosen
    // when the song opens), then the fixed sizes as letters. A long press on a letter names it. No
    // size moves while the song plays.
    constexpr Seg kSizes[] = {{nullptr, "Fit"},
                              {nullptr, "Auto"},
                              {nullptr, "S"},
                              {nullptr, "M"},
                              {nullptr, "L"},
                              {nullptr, "XL"}};
    int size = static_cast<int>(look.key_size);
    catalogue_sweep::drawn(Setting::KeySize);
    if (segmented(k, "keysize", icon::kSize, "Key size", kSizes, &size)) {
        look.key_size = static_cast<KeySize>(size);
        k.say(std::string("Key size: ") + key_size_label(look.key_size));
    }
}

// Rows that belong together sit together (#468). The first column is the song screen: what the notes
// area shows (Show, the chord names and their size), how far ahead the notes fall, then the keyboard
// (its height, its key size) and last the grade words. The second is the app around it: the bar (its
// style, keeping it shown, the difficulty on its band), the renderer and screen size, Learn colours.
void page_view(Kit& k, UiModel& model, int columns) {
    LookSettings& look = model.look;
    Columns cols(k, columns);
    view_mode_row(k, model);
    catalogue_sweep::drawn(Setting::ChordName);
    switch_row(k, icon::kChord, "Chord name", &look.chord_label);
    catalogue_sweep::drawn(Setting::ChordNameSize);
    slider_row(k, icon::kTextSize, "Size", &look.chord_label_size, kMinChordLabelSize,
               kMaxChordLabelSize, 100.0f, Unit::Percent, 0, nullptr, look_defaults().chord_label_size);
    catalogue_sweep::drawn(Setting::NotesAhead);
    slider_row(k, icon::kSeconds, "Notes ahead", &look.visible_seconds, kMinVisibleSeconds,
               kMaxVisibleSeconds, 1.0f, Unit::Seconds, 1, nullptr, look_defaults().visible_seconds);
    catalogue_sweep::drawn(Setting::KeyboardHeight);
    slider_row(k, icon::kHeight, "Keyboard", &look.keyboard_height, kMinKeyboardHeight,
               kMaxKeyboardHeight, 100.0f, Unit::Percent, 0, nullptr,
               look_defaults().keyboard_height);
    key_size_row(k, look);
    grade_word_rows(k, look);
    cols.next();
    bar_style_row(k, model);
    renderer_row(k, model);
    screen_size_row(k, model);
    section(k, "Learn colours");
    learn_colour_rows(k, model);
}

// The hands of a song whose file does not name them, kept per song: they follow the sheet's staves
// (the treble staff the right hand, the bass staff the left: two tracks, or one staff divided by
// the smart split), or the player puts the line between them on a key. A song whose file names the
// hands has no row. The two rows are the same height in both states, so nothing moves.
void hands_rows(Kit& k) {
    if (k.song == nullptr || !k.song->view().has_song || !k.song->view().split_available) {
        return;
    }
    catalogue_sweep::drawn(Setting::Hands);
    const SongView& view = k.song->view();
    // "Split at" is chosen before it places anything (hands_row.hpp): the state lives with the page.
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID state_id = ImGui::GetID("##hands-pending");
    HandsRowState state{static_cast<std::uint32_t>(store->GetInt(state_id, 0))};
    const auto apply = [&](const HandsOutcome& out) {
        if (out.action == HandsAction::Place) {
            k.song->set_split_key(out.key, k.now_ns);
        } else if (out.action == HandsAction::Reset) {
            k.song->reset_split(k.now_ns);
        }
    };
    constexpr Seg kChoices[] = {{nullptr, "From the sheet"}, {nullptr, "Split at"}};
    int choice = hands_split_shown(view, state) ? 1 : 0;
    if (segmented(k, "hands", icon::kPiano, "Hands", kChoices, &choice)) {
        apply(hands_choose(view, state, choice == 1));
    }
    const std::string key = accidental_display(note_name(view.split_key, view.split_flats));
    if (hands_split_shown(view, state)) {
        // No reset on this stepper: its key depends on the song, and "From the sheet" above already
        // puts it back.
        const int step = stepper_text_row(k, icon::kKeyboard, "Right hand from", key.c_str(),
                                          hands_can_step(view, -1), hands_can_step(view, 1));
        apply(hands_step(view, state, step));
    } else {
        // Which rule gave the hands: the tracks' two staves, or the smart split of one staff.
        char rule[48] = "Two staves";
        if (!view.split_by_staves) {
            std::snprintf(rule, sizeof rule, "Split at %s", key.c_str());
        }
        readout_row(k, icon::kKeyboard, rule);
    }
    store->SetInt(state_id, static_cast<int>(state.pending_serial));
}

// The practice settings, then the metronome's own, in a column of their own. Its on-off switch and
// the count-in are on the bar and in the tempo popover.
void page_practice(Kit& k, UiModel& model, int columns) {
    Columns cols(k, columns);
    hands_rows(k);
    catalogue_sweep::drawn(Setting::HitWindow);
    // One setting in one group: three quick choices, then the slider that sets any value as a
    // sub-row with no icon of its own. A choice shows selected only while the window equals it.
    constexpr Seg kWindowPresets[] = {{nullptr, "Strict"}, {nullptr, "Normal"}, {nullptr, "Relaxed"}};
    int preset = hit_window_preset_index(model.app.hit_window_ms);
    if (segmented(k, "hitwindow", icon::kTimer, "Hit window", kWindowPresets, &preset) &&
        preset >= 0) {
        model.app.hit_window_ms = kHitWindowPresetMs[static_cast<std::size_t>(preset)];
    }
    slider_row_int(k, nullptr, "Custom", &model.app.hit_window_ms, 20, 250,
                   Unit::Milliseconds,
                   "How early or late a note may be and still count, in Flow mode.",
                   app_defaults().hit_window_ms);
    catalogue_sweep::drawn(Setting::WeightedScore);
    switch_row(k, icon::kGauge, "Weight score by difficulty", &model.app.weighted_score,
               "The results card also shows a score where a hard bar counts more than an easy one.");
    // Moving around the song: how long the resume countdown is after the settings close, then the
    // two skip steps (holding a skip button repeats its step).
    catalogue_sweep::drawn(Setting::ResumeAfter);
    stepper_row(k, icon::kCountdown, "Resume after", &model.app.resume_countdown_s,
                kMinResumeCountdownS, kMaxResumeCountdownS, Unit::Seconds, 0,
                app_defaults().resume_countdown_s);
    catalogue_sweep::drawn(Setting::SkipBack);
    stepper_row(k, icon::kSkipBack, "Skip back", &model.app.skip_back_seconds, 1, 30, Unit::Seconds,
                -1, app_defaults().skip_back_seconds);
    catalogue_sweep::drawn(Setting::SkipForward);
    stepper_row(k, icon::kSkipForward, "Skip forward", &model.app.skip_forward_seconds, 1, 30,
                Unit::Seconds, 1, app_defaults().skip_forward_seconds);
    // The replay of a run needs the notes that were played: the newest runs of each song keep
    // them, older ones keep their scores only. The value is applied when the next run is saved,
    // which prunes.
    catalogue_sweep::drawn(Setting::RunsKept);
    slider_row_int(k, icon::kReplay, "Replays kept per song", &model.app.run_events_kept, 0,
                   kMaxRunEventsKept, Unit::Count,
                   "The newest runs keep their played notes for replay; older runs keep scores only. "
                   "A pinned run always stays. A lower number applies after your next run.",
                   app_defaults().run_events_kept);
    cols.next();
    section(k, "Metronome");
    metronome_rows(k, model);
    section(k, "Accompaniment");
    // The song's other instruments (voice, strings, ...): whether songs start with them sounding,
    // and how loud. Each song's own choice is the accompaniment button on the bar.
    catalogue_sweep::drawn(Setting::AccompanimentOn);
    switch_row(k, icon::kListMusic, "Accompaniment", &model.app.accompaniment,
               "A song's voice, strings or other instruments sound with it, whatever the song sound "
               "is. Each song keeps its own choice on the bar.");
    catalogue_sweep::drawn(Setting::AccompanimentVolume);
    slider_row_int(k, icon::kSoundBoth, "Accompaniment volume", &model.app.accompaniment_volume, 0,
                   kMaxAccompanimentVolume, Unit::Percent, nullptr,
                   app_defaults().accompaniment_volume);
}
} // namespace

namespace {

const char* style_label(NoteStyle s) {
    static constexpr const char* kNames[] = {"Solid", "Outline", "Glass", "Neon",
                                             "Candy", "Soft",    "Pixel", "Ink"};
    return kNames[static_cast<std::size_t>(s) % std::size(kNames)];
}

} // namespace

bool style_picker(Kit& k, LookSettings& look) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = k.s(tok::kHitGap);
    // All eight in one row when each still gets a full hit area, else two rows of four.
    constexpr int kCount = static_cast<int>(std::size(kAllNoteStyles));
    const int per_row = avail >= kCount * k.s(tok::kHit) + (kCount - 1) * gap ? kCount : kCount / 2;
    const float tw = (avail - gap * static_cast<float>(per_row - 1)) / static_cast<float>(per_row);
    const float th = std::clamp(tw * 1.05f, k.s(54.0f), k.s(66.0f));
    bool changed = false;
    ImGui::PushID("styles");
    for (int i = 0; i < kCount; ++i) {
        if (i % per_row != 0) {
            ImGui::SameLine(0.0f, gap);
        }
        const NoteStyle style = kAllNoteStyles[i];
        const bool on = look.note_style == style;
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##style", {tw, th});
        const State st = item_state(k, on);
        ImGui::PopID();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b{pos.x + tw, pos.y + th};
        draw_state(k, dl, "##style", {pos.x, pos.y, b.x, b.y}, {pos.x, pos.y, b.x, b.y}, st, Idle::Fill);
        const float cap = k.s(tok::kCaption);
        const float ph = th - cap - k.s(tok::kS3) - k.s(tok::kS2);
        const float pw = std::min(tw * 0.62f, ph * 1.05f);
        note_preview(k, dl, {pos.x + (tw - pw) * 0.5f, pos.y + k.s(tok::kS2)},
                     {pos.x + (tw + pw) * 0.5f, pos.y + k.s(tok::kS2) + ph}, style,
                     look.color_scheme == ColorScheme::Single ? look.single_color
                                                              : look.right_color,
                     look.style_intensity, look.block_glow);
        const char* name = style_label(style);
        label_at(k, dl, on ? k.bold : k.regular, cap,
                 {pos.x, b.y - cap - k.s(tok::kS3), b.x, b.y - k.s(tok::kS1)}, LabelAlign::Centre,
                 on ? col::kText : col::kText2, name);
        if (pressed && !on) {
            look.note_style = style;
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

const char* page_icon(Page p) {
    switch (p) {
    case Page::Notes:
        return icon::kNames;
    case Page::Colours:
        return icon::kPalette;
    case Page::View:
        return icon::kView;
    case Page::Configs:
        return icon::kConfigs;
    case Page::Practice:
        return icon::kPractice;
    case Page::Input:
        return icon::kMic;
    case Page::AddSongs:
        return icon::kPhone;
    case Page::About:
        return icon::kInfo;
    }
    return "";
}

void draw_page_header(Kit& k, Page p, ImVec2 body, int columns) {
    (void)columns;
    if (p == Page::Colours) {
        colours_header(k, body);
    }
}

Page draw_page(Kit& k, Page p, UiModel& model, int columns, ImVec2 body) {
    catalogue_sweep::set_page(p);
    row_scope(static_cast<int>(p) + 1); // each page measures its own columns
    // The page's first frame measures its labels and is not seen (row_layout.hpp).
    const RowMeasure measure(static_cast<int>(p) + 1);
    Page next = p;
    switch (p) {
    case Page::Notes:
        page_notes(k, model, columns);
        break;
    case Page::Colours:
        page_colours(k, model, body);
        break;
    case Page::View:
        page_view(k, model, columns);
        break;
    case Page::Configs:
        page_configs(k, model, columns);
        break;
    case Page::Practice:
        page_practice(k, model, columns);
        break;
    case Page::Input:
        page_input(k, model, columns);
        break;
    case Page::AddSongs:
        page_add_songs(k, model, columns);
        break;
    case Page::About:
        page_about(k, model, columns);
        break;
    }
    catalogue_sweep::clear_page();
    row_scope(0);
    return next;
}

} // namespace etude::app::ui
