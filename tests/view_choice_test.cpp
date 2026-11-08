// SPDX-License-Identifier: GPL-3.0-or-later
// The side button right of the score (Off, On, Full) and Settings > View's "Show" row are one
// setting: the mapping between them, the refusal for a song with no sheet music, and the two always
// agreeing (app/ui/view_choice.hpp, the rules both go through).
#include "ui/view_choice.hpp"

#include "ui/morph_glyphs.hpp"

#include "ui/toast_regions.hpp"

#include "etude/chord_label_layout.hpp"
#include "etude/combo_display.hpp"
#include "etude/key_label.hpp"
#include "etude/screen_layout.hpp"
#include "etude/sheet_view.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <span>
#include <iterator>
#include <string>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr ViewMode kModes[] = {ViewMode::Falling, ViewMode::Sheet, ViewMode::Both};

} // namespace

TEST_CASE("side button: Off is Falling, On is Both, Full is Sheet, and back") {
    CHECK(view_mode_of(ViewChoice::Off) == ViewMode::Falling);
    CHECK(view_mode_of(ViewChoice::On) == ViewMode::Both);
    CHECK(view_mode_of(ViewChoice::Full) == ViewMode::Sheet);
    CHECK(view_choice_of(ViewMode::Falling) == ViewChoice::Off);
    CHECK(view_choice_of(ViewMode::Both) == ViewChoice::On);
    CHECK(view_choice_of(ViewMode::Sheet) == ViewChoice::Full);
    for (const ViewChoice c : kViewChoices) {
        CHECK(view_choice_of(view_mode_of(c)) == c);
    }
    for (const ViewMode m : kModes) {
        CHECK(view_mode_of(view_choice_of(m)) == m);
    }
}

TEST_CASE("side button: a tap sets the setting, and the same choice again changes nothing") {
    for (const ViewMode start : kModes) {
        for (const ViewChoice c : kViewChoices) {
            ViewMode mode = start;
            const ViewTap tap = tap_view_choice(mode, c, false);
            CAPTURE(static_cast<int>(start));
            CAPTURE(static_cast<int>(c));
            CHECK(mode == view_mode_of(c));
            CHECK(tap == (start == view_mode_of(c) ? ViewTap::Same : ViewTap::Set));
        }
    }
}

TEST_CASE("side button: a song with no sheet music refuses On and Full, and Off still works") {
    for (const ViewMode start : kModes) {
        ViewMode mode = start;
        CHECK(tap_view_choice(mode, ViewChoice::On, true) == ViewTap::Refused);
        CHECK(mode == start);
        CHECK(tap_view_choice(mode, ViewChoice::Full, true) == ViewTap::Refused);
        CHECK(mode == start);
    }
    ViewMode mode = ViewMode::Both; // set before the song with no sheet music was opened
    CHECK(tap_view_choice(mode, ViewChoice::Off, true) == ViewTap::Set);
    CHECK(mode == ViewMode::Falling);
    // A song that has sheet music (or one whose answer is not in yet) refuses nothing.
    for (const ViewChoice c : kViewChoices) {
        ViewMode m = ViewMode::Falling;
        CHECK(tap_view_choice(m, c, false) != ViewTap::Refused);
    }
    CHECK(view_refusal("too many notes") == "No sheet music for this song: too many notes");
}

TEST_CASE("side button: the button and the row refuse the same modes") {
    // The row's cells are Falling, Sheet, Both; the mask says which are greyed. It is the button's
    // rule, cell by cell.
    CHECK(view_refused_mask(false) == 0u);
    for (int cell = 0; cell < kViewCells; ++cell) {
        const std::uint32_t bit = 1u << static_cast<unsigned>(cell);
        ViewMode probe = ViewMode::Falling;
        const bool button_refuses =
            tap_view_choice(probe, view_choice_of(view_mode_of_cell(cell)), true) ==
            ViewTap::Refused;
        CAPTURE(cell);
        CHECK(((view_refused_mask(true) & bit) != 0u) == button_refuses);
    }
    CHECK(view_refused_mask(true) == 0b110u); // Sheet and Both
}

TEST_CASE("side button: the row's cells and the button's choices are the same three, by name") {
    // What the row writes for each cell, and the choice the button shows for it. A swapped mapping
    // (Both for Full, Sheet for On) fails here by name.
    struct Case {
        int cell;
        const char* label;
        ViewMode mode;
        ViewChoice choice;
    };
    const Case cases[] = {{0, "Falling", ViewMode::Falling, ViewChoice::Off},
                          {1, "Sheet", ViewMode::Sheet, ViewChoice::Full},
                          {2, "Both", ViewMode::Both, ViewChoice::On}};
    static_assert(std::size(cases) == kViewCells);
    for (const Case& c : cases) {
        CAPTURE(c.label);
        CHECK(std::string(view_cell_label(c.cell)) == c.label);
        CHECK(view_mode_of_cell(c.cell) == c.mode); // the row's write
        CHECK(view_cell_of(c.mode) == c.cell);      // and its reading of the setting
        CHECK(view_choice_of(view_mode_of_cell(c.cell)) == c.choice); // the button shows it
        // The button's tap writes the same mode as the row's cell.
        ViewMode setting = ViewMode::Falling;
        tap_view_choice(setting, c.choice, false);
        CHECK(setting == view_mode_of_cell(c.cell));
    }
    // The key's cycle (`next_view_mode`) lands on a choice the button names, all three in turn.
    ViewMode m = ViewMode::Falling;
    int seen = 0;
    for (int i = 0; i < 3; ++i) {
        m = next_view_mode(m);
        seen |= 1 << static_cast<int>(view_choice_of(m));
    }
    CHECK(seen == 0b111);
}

TEST_CASE("side button: a song with no sheet music shows Off on the button, the setting stays") {
    for (const ViewMode m : kModes) {
        CHECK(drawn_view_mode(m, false) == m);
        CHECK(drawn_view_mode(m, true) == ViewMode::Falling);
    }
    // The saved setting is the caller's: nothing here writes it, so the next song gets it back.
    const ViewMode setting = ViewMode::Both;
    CHECK(view_choice_of(drawn_view_mode(setting, true)) == ViewChoice::Off);
    CHECK(view_glyph(view_choice_of(drawn_view_mode(setting, true))) == MorphGlyph::ViewOff);
    CHECK(view_choice_of(drawn_view_mode(setting, false)) == ViewChoice::On);
}

TEST_CASE("side button: its icon shows the current state, a staff that gets fuller from Off "
          "to Full") {
    // Off, On and Full each have their own picture, in that order of ink: a staff crossed out, a
    // staff and two falling notes, a staff of four lines with a note. A swapped glyph fails.
    CHECK(view_glyph(ViewChoice::Off) == MorphGlyph::ViewOff);
    CHECK(view_glyph(ViewChoice::On) == MorphGlyph::ViewOn);
    CHECK(view_glyph(ViewChoice::Full) == MorphGlyph::ViewFull);
    for (const ViewChoice c : kViewChoices) {
        CHECK(glyph_ok(view_glyph(c)));
    }
    const int off = morph_data(view_glyph(ViewChoice::Off)).path.subpath_count;
    const int on = morph_data(view_glyph(ViewChoice::On)).path.subpath_count;
    const int full = morph_data(view_glyph(ViewChoice::Full)).path.subpath_count;
    CHECK(off == 4); // three staff lines and the slash
    CHECK(on == 5);  // three staff lines and two falling notes
    CHECK(full == 6); // four staff lines, the note's head and its stem
    CHECK(glyph_style(MorphGlyph::ViewOff).stroke == doctest::Approx(glyph_style(MorphGlyph::ViewFull).stroke));
}

TEST_CASE("side button: one tap goes Off, On, Full, Off, and says the new state") {
    CHECK(next_view_choice(ViewChoice::Off) == ViewChoice::On);
    CHECK(next_view_choice(ViewChoice::On) == ViewChoice::Full);
    CHECK(next_view_choice(ViewChoice::Full) == ViewChoice::Off);
    // Three taps from any state are back where they began.
    for (const ViewChoice c : kViewChoices) {
        CHECK(next_view_choice(next_view_choice(next_view_choice(c))) == c);
    }
    // On the setting: Falling, then Both, then Sheet, then Falling.
    ViewMode mode = ViewMode::Falling;
    const ViewMode expect[] = {ViewMode::Both, ViewMode::Sheet, ViewMode::Falling};
    for (const ViewMode want : expect) {
        const ViewChoice shown = view_choice_of(drawn_view_mode(mode, false));
        CHECK(tap_view_choice(mode, next_view_choice(shown), false) == ViewTap::Set);
        CHECK(mode == want);
    }
    CHECK(std::string(view_choice_toast(ViewChoice::Off)) == "Falling notes only");
    CHECK(std::string(view_choice_toast(ViewChoice::On)) == "Sheet and falling notes");
    CHECK(std::string(view_choice_toast(ViewChoice::Full)) == "Sheet only");
}

TEST_CASE("side button: on a song with no sheet music a tap stays on Off and says why, the setting stays") {
    for (const ViewMode saved : kModes) {
        ViewMode mode = saved;
        // The button shows Off, so its tap asks for On, which the song refuses.
        const ViewChoice shown = view_choice_of(drawn_view_mode(mode, true));
        CHECK(shown == ViewChoice::Off);
        CHECK(tap_view_choice(mode, next_view_choice(shown), true) == ViewTap::Refused);
        CHECK(mode == saved); // the next song with sheet music gets it back
    }
}

TEST_CASE("side button: the side button and the View row always agree") {
    // One setting, two ways to write it. Whatever one writes, the other shows.
    for (const ViewMode start : kModes) {
        for (const bool no_sheet : {false, true}) {
            ViewMode setting = start;
            // The side button's tap.
            const ViewChoice shown = view_choice_of(drawn_view_mode(setting, no_sheet));
            tap_view_choice(setting, next_view_choice(shown), no_sheet);
            // The row of Settings > View has the cell checked that the choice the setting now
            // holds names.
            const int cell = view_cell_of(setting);
            CAPTURE(static_cast<int>(start));
            CAPTURE(no_sheet);
            CHECK(view_choice_of(view_mode_of_cell(cell)) == view_choice_of(setting));
            // A write from the row shows on the side button.
            ViewMode from_row = view_mode_of_cell(2); // the row's "Both"
            CHECK(view_choice_of(drawn_view_mode(from_row, false)) == ViewChoice::On);
        }
    }
}

TEST_CASE("side button: in Off it stands where no combo, key label or chord name goes, at every size "
          "and scale") {
    const float sizes[][2] = {{1920, 1080}, {1366, 768}, {800, 480}};
    for (const auto& size : sizes) {
        for (const float scale : {1.0f, 1.5f}) {
            const float ppmm = kDefaultPxPerMm * scale;
            const ScreenLayout lay = compute_layout(size[0], size[1], ppmm, LookSettings{}, {});
            const float u = ui_scale(size[0], size[1], ppmm, 1.0f, 1.0f);
            const RectF room = score_area(lay);
            const RectF button = sheet_view_button(room, tok::kCell * u);
            CAPTURE(size[0]);
            CAPTURE(scale);
            REQUIRE_FALSE(button.empty());
            CHECK(button.right() <= room.right());
            // The same corner in the Off view as in the others: it is a function of the room only.
            CHECK(button.y >= room.y);
            // The combo number is stepped under it (the scene passes the button's bottom as the
            // top it keeps clear), and never over it.
            const RectF combo = place_combo(lay.note_area, std::max(button.bottom() - lay.note_area.y, 0.0f),
                                            u, 120.0f * u, 40.0f * u, {}, 30.0f * u);
            if (!combo.empty()) {
                const bool over = combo.x < button.right() && button.x < combo.right() &&
                                  combo.y < button.bottom() && button.y < combo.bottom();
                CHECK_FALSE(over);
            }
            // The key label dodges it (it stands bottom left; where the room is small it moves up
            // or hides, never over it).
            const RectF buttons[] = {button};
            const KeyLabelPlacement key = place_key_label(lay.note_area, 120.0f * u, 30.0f * u, 10.0f * u, buttons);
            if (key.visible) {
                const bool over = key.rect.x < button.right() && button.x < key.rect.right() &&
                                  key.rect.y < button.bottom() && button.y < key.rect.bottom();
                CHECK_FALSE(over);
            }
            // A chord name in the strip steps aside from it (the button is a blocker where it shares
            // the strip's height) and is never over it.
            if (lay.chord_label_visible) {
                std::array<ChordSpan, 4> blocked{};
                const std::size_t n = chord_label_blockers(lay.chord_label, buttons, 8.0f, blocked);
                const ChordNameWant want{80.0f * u, button.x + button.w * 0.5f, 0};
                ChordNamePlace place;
                place_chord_names(std::span<const ChordNameWant>(&want, 1), lay.chord_label.x,
                                  lay.chord_label.right(), 8.0f,
                                  std::span<const ChordSpan>(blocked.data(), n),
                                  std::span<ChordNamePlace>(&place, 1));
                if (place.placed) {
                    const bool over = place.left < button.right() && button.x < place.left + want.width;
                    // Only a name that shares the strip's height has a blocker to step aside from.
                    if (n > 0) {
                        CHECK_FALSE(over);
                    }
                }
            }
        }
    }
}

TEST_CASE("side button: it waits behind the bar's popovers and the band's list and menu, and is "
          "back when they close") {
    CHECK_FALSE(sheet_button_waits(false, false, false));
    CHECK(sheet_button_waits(true, false, false));
    CHECK(sheet_button_waits(false, true, false));
    CHECK(sheet_button_waits(false, false, true));
    CHECK(sheet_button_waits(true, true, true));
    // One opens, then closes: shown, hidden, shown.
    bool list_open = false;
    CHECK_FALSE(sheet_button_waits(false, list_open, false));
    list_open = true;
    CHECK(sheet_button_waits(false, list_open, false));
    list_open = false;
    CHECK_FALSE(sheet_button_waits(false, list_open, false));
}

TEST_CASE("side buttons: the sheet-music button and the two switches show and hide on one condition") {
    SideCover c;
    CHECK_FALSE(sheet_side_waits(c));
    // Each thing that covers the corner hides all three, as the cycle button alone was hidden.
    c.covered = true; // a modal, the results card, a run's cards or the replay
    CHECK(sheet_side_waits(c));
    c = {};
    c.bar_popover = true;
    CHECK(sheet_side_waits(c));
    c = {};
    c.band_list = true;
    CHECK(sheet_side_waits(c));
    c = {};
    c.band_menu = true;
    CHECK(sheet_side_waits(c));
    c = {};
    c.band_over = true; // the band hanging under the revealed bar
    CHECK(sheet_side_waits(c));
    // Closed again: back.
    c = {};
    CHECK_FALSE(sheet_side_waits(c));
}

TEST_CASE("side buttons: the shared box holds the button and the switches together") {
    const RectF button{100.0f, 10.0f, 44.0f, 44.0f};
    const RectF column{100.0f, 54.0f, 44.0f, 88.0f};
    const RectF row{12.0f, 10.0f, 88.0f, 44.0f};
    const RectF in_column = sheet_side_box(button, column);
    CHECK(in_column.x == doctest::Approx(100.0f));
    CHECK(in_column.y == doctest::Approx(10.0f));
    CHECK(in_column.h == doctest::Approx(132.0f));
    const RectF in_row = sheet_side_box(button, row);
    CHECK(in_row.x == doctest::Approx(12.0f));
    CHECK(in_row.right() == doctest::Approx(144.0f));
    // No score (no switches): the button alone; no button: the switches alone.
    CHECK(sheet_side_box(button, {}).h == doctest::Approx(44.0f));
    CHECK(sheet_side_box({}, column).h == doctest::Approx(88.0f));
    CHECK(sheet_side_box({}, {}).empty());
}
