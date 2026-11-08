// SPDX-License-Identifier: GPL-3.0-or-later
// The panels Ui::draw lays over the notes (its stages): the library, the settings sheet and the
// section editor, the results and run cards with the octave-shift offer, and the sheet music's
// reading helps.
#include "host_impl.hpp"

#include "editor.hpp"
#include "icons.hpp"
#include "kit.hpp"
#include "learn_screen.hpp"
#include "library_screen.hpp"
#include "results_card.hpp"
#include "run_card.hpp"
#include "settings_catalogue.hpp"
#include "sheet.hpp"
#include "transpose.hpp"
#include "view_choice.hpp"
#include "etude/results_card.hpp"

#include <cmath>

namespace etude::app {

// The song library under everything else that opens (the settings may open over it), the sheet
// above it, and the full section editor.
void Ui::Impl::draw_panels(DrawFrame& d) {
    Impl& im = *this;
    UiModel& model = d.model;
    Kit& k = d.k;
    Frame& f = d.f;
    SectionsPort& sections = d.sections;
    if (im.library_port != nullptr && im.library.showing()) {
        im.library.draw(k, model, *im.library_port, f);
        // The Progress page's pencil opens the profile's sheet; a card of the Household view makes
        // that profile the one in use.
        if (const ProgressPage::Ask ask = im.library.take_progress_ask();
            ask.kind != ProgressPage::Ask::Kind::None && d.info.profiles != nullptr) {
            if (ask.kind == ProgressPage::Ask::Kind::Edit) {
                im.profile.open_edit(*d.info.profiles, ask.id);
            } else {
                im.profile.choose({ProfileAction::Switch, ask.id, {}, 0, -1});
            }
        }
        if (im.library.is_open()) {
            k.toast_regions.surface = {0.0f, f.row_max.y, f.display.x, f.display.y - f.row_max.y};
        }
    }
    if (im.learn_port != nullptr && im.learn.showing()) {
        im.learn.draw(k, model, *im.learn_port, im.library_port, f);
        if (im.learn.take_library_ask()) {
            im.show_library(k); // the Melodies page's "Open library"
        }
        if (im.learn.is_open()) {
            k.toast_regions.surface = {0.0f, f.row_max.y, f.display.x, f.display.y - f.row_max.y};
        }
    }
    im.key_ring_demo.draw(k, f);
    im.medal_demo.draw(k, f);
    im.scale_pill.popover(k, model, f);
    // The sheet below, the bar and the gear above.
    im.sheet.measure(k, model, f, im.open_p > 0.0f);
    if (im.open_p > 0.0f) {
        if (!im.open) {
            k.confirm_id = 0; // a two-tap row armed in Settings does not survive its closing
        }
        if (im.sheet.draw(k, model, f, d.open_ease) && im.open) {
            im.open = false;
        }
    }
    // Settings > Input's "Run the device check" is the card's answer Run.
    if (k.run_device_check) {
        k.run_device_check = false;
        im.device_check_choice = DeviceCheckChoice::Run;
    }
    // The full section editor, asked for by the section band's editor button or a script.
    if (im.want_editor || k.open_editor) {
        const bool suggest = im.editor_suggest;
        im.want_editor = false;
        im.editor_suggest = false;
        k.open_editor = false;
        if (sections.view().available) {
            im.open = false;
            im.bar->close_popovers();
            im.band.close_popovers();
            im.editor->open(k);
            if (suggest) {
                sections.set_suggestions(true);
            }
        } else {
            k.say_free("Open a song first");
        }
    }
    if (im.editor->is_open()) {
        if (im.editor->draw(k, f)) {
            im.editor->close(k);
        }
    }
}

// The results card over the note area once a run has ended; the sheet and the editor cover the
// notes, so it waits behind them. A trouble-bar run's check-in or summary takes its place, and
// the octave-shift offer waits for both.
void Ui::Impl::draw_cards(DrawFrame& d) {
    Impl& im = *this;
    Kit& k = d.k;
    const ScreenLayout& lay = d.lay;
    const UiFrameInfo& info = d.info;
    const float scale = d.scale;
    const bool modal = d.modal;
    {
        const RectF area{lay.note_area.x * scale, lay.note_area.y * scale, lay.note_area.w * scale,
                         lay.note_area.h * scale};
        im.card_scale = card_size(area, k.u).scale;
        if (info.order_demo && info.results != nullptr) {
            im.results.start_order(*info.results);
        }
        if (info.replay != nullptr) {
            // The replay is up: the card and its page wait, as they were, and the replay's bar
            // (draw_bar) is the screen's. The run's cards cannot be up with it.
            im.results.rest();
        } else {
            static const ReviewInfo kNoReview;
            const RectF page = summary_area(lay);
            // The page never moves for the band hanging from the revealed bar: the band draws over
            // it and takes the touches in its own area (`SummaryPageView::draw` keeps it behind).
            const RectF page_area{page.x * scale, page.y * scale, page.w * scale, page.h * scale};
            const CardChoice tapped = im.results.draw(
                k, modal ? nullptr : info.results, area,
                info.review != nullptr ? *info.review : kNoReview, page_area);
            if (tapped.action != CardAction::None) {
                im.results_choice = tapped;
            }
            if (im.want_summary && im.results.shown()) {
                im.want_summary = false;
                im.results.open_summary(im.want_board, im.want_runs);
            }
            const ReviewChoice review_tapped = im.results.take_review_choice();
            if (review_tapped.action != ReviewAction::None) {
                im.review_choice = review_tapped;
            }
            // A trouble-bar run's check-in or summary takes the card's place (the two never meet: a
            // run starts from the card and clears it).
            const RunCardAction run_tapped = im.run_card.draw(k, modal ? nullptr : info.run, area);
            if (run_tapped != RunCardAction::None) {
                im.run_choice = run_tapped;
            }
        }
    }
    // The device check waits for the same things as the offer (and for a song that is playing: the
    // card never sits over the falling notes) and comes back after.
    const bool clear = !modal && !im.bar->holds_bar() && !im.band.list_open() &&
                       !im.band.menu_open() && info.results == nullptr && !im.run_card.shown() && info.replay == nullptr;
    {
        const RectF area{lay.note_area.x * scale, lay.note_area.y * scale, lay.note_area.w * scale,
                         lay.note_area.h * scale};
        const DeviceCheckChoice tapped = im.device_check.draw(
            k, info.device_check, area, info.octave_offset, clear && !d.view.playing);
        if (tapped != DeviceCheckChoice::None) {
            im.device_check_choice = tapped;
        }
    }
    // The octave-shift offer waits while a panel, the bar's popovers, the section band's list or
    // menu (they open at the same edge), the results card, a run's card or the device check is up,
    // and comes back after.
    im.shift_offer.draw(k, d.song, d.f, d.f.band_bottom + k.s(tok::kS2),
                        clear && !im.device_check.shown());
}

// The one condition for the three side buttons (the sheet-music button and the two reading switches),
// so they always show and hide together. The results card, its summary page and a run's cards
// cover the notes: the buttons wait behind them, as the octave-shift offer does, and never show or
// take a touch through them. The bar's popovers and the band's list and menu open over this
// corner of the notes, and the band hanging under the revealed bar (or its lane) can cover it.
bool Ui::Impl::sheet_side_waits_now(const DrawFrame& d) const {
    const UiFrameInfo& info = d.info;
    const RectF box = sheet_side_box(info.sheet_button, info.sheet_corner);
    const ImVec2 lo = to_points(box.x, box.y, d.scale);
    const ImVec2 hi = to_points(box.right(), box.bottom(), d.scale);
    const auto covers = [&](const RectF& b) {
        return !b.empty() && b.x < hi.x && lo.x < b.right() && b.y < hi.y && lo.y < b.bottom();
    };
    SideCover cover;
    cover.covered = d.modal || results.shown() || run_card.shown() || info.results != nullptr ||
                    info.replay != nullptr;
    cover.bar_popover = bar->holds_bar();
    cover.band_list = band.list_open();
    cover.band_menu = band.menu_open();
    cover.band_over = d.band_shown && (covers(d.band_place.band) || covers(d.lane_rect));
    return sheet_side_waits(cover);
}

// The sheet-music button at the top right of the notes, above the reading helps' column: one tap
// goes Off (falling notes only), On (the sheet above the notes), Full (the sheet only), Off. It
// shows in every view, so with Off, where no paper or column is drawn, it stands in the same spot.
// The same setting as the View page's row and the I key (view_choice.hpp): a song with no
// sheet music shows Off, stays on it and says why. It gives way while the band hanging under the
// revealed bar (or its lane) covers it, as the helps do.
void Ui::Impl::draw_sheet_view_button(DrawFrame& d) {
    Kit& k = d.k;
    const UiFrameInfo& info = d.info;
    const RectF& r = info.sheet_button;
    if (r.empty()) {
        return;
    }
    const ImVec2 min = to_points(r.x, r.y, d.scale);
    const ImVec2 max = to_points(r.right(), r.bottom(), d.scale);
    if (begin_layer("##sheet-view", min, {max.x - min.x, max.y - min.y})) {
        // The hit area is the cell less the dead gap, centred in it, on whole pixels.
        const float side = (max.x - min.x) - k.s(tok::kHitGap);
        const ImVec2 centre{std::round((min.x + max.x) * 0.5f), std::round((min.y + max.y) * 0.5f)};
        catalogue_sweep::drawn(Setting::ViewMode);
        const bool no_sheet = k.sheet.refused();
        const ViewChoice shown =
            view_choice_of(drawn_view_mode(d.model.app.view_mode, no_sheet));
        if (icon_toggle(k, "##sheet-view", view_glyph(shown), "Sheet music", centre, {side, side},
                        k.s(tok::kFace), shown != ViewChoice::Off, true)) {
            // From what is drawn (a song with no sheet music is on Off whatever the setting says).
            const ViewChoice next = next_view_choice(shown);
            if (tap_view_choice(d.model.app.view_mode, next, no_sheet) == ViewTap::Refused) {
                k.say(view_refusal(k.sheet.reason));
            } else {
                k.say(view_choice_toast(next));
            }
        }
    }
    ImGui::End();
}

// Sheet music's reading helps beside its paper: icon-only switches of the Notes page's two
// settings (declared bar shortcuts). They wait behind what covers their corner, on the condition they
// share with the sheet-music button (`sheet_side_waits_now`), and come back when it goes.
void Ui::Impl::draw_sheet_helps(DrawFrame& d) {
    UiModel& model = d.model;
    Kit& k = d.k;
    const UiFrameInfo& info = d.info;
    const float scale = d.scale;
    if (sheet_side_waits_now(d)) {
        return;
    }
    draw_sheet_view_button(d);
    if (!info.sheet_corner.empty()) {
        const ImVec2 helps_min = to_points(info.sheet_corner.x, info.sheet_corner.y, scale);
        const ImVec2 helps_max =
            to_points(info.sheet_corner.right(), info.sheet_corner.bottom(), scale);
        const ImVec2 size{helps_max.x - helps_min.x, helps_max.y - helps_min.y};
        if (begin_layer("##sheet-helps", helps_min, size)) {
            // Each toggle's hit area is its cell less the dead gap, centred in it, on whole pixels.
            const auto centre = [&](const RectF& cell) {
                return ImVec2{std::round((cell.x + cell.w * 0.5f) * scale),
                              std::round((cell.y + cell.h * 0.5f) * scale)};
            };
            const float side = info.sheet_names.w * scale - k.s(tok::kHitGap);
            const ImVec2 hit{side, side};
            catalogue_sweep::drawn(Setting::NamesOnSheet);
            const bool names = model.look.names_on_sheet;
            if (icon_toggle(k, "##sheet-names", icon::kNames, "Note names on the sheet",
                            centre(info.sheet_names), hit, k.s(tok::kFace), names, true)) {
                model.look.names_on_sheet = !names;
                k.say(names ? "No note names on the sheet" : "Note names on the sheet");
            }
            catalogue_sweep::drawn(Setting::SheetNoteColours);
            const bool colours = model.look.sheet_note_colours;
            if (icon_toggle(k, "##sheet-colours", icon::kPalette, "Note colours on the sheet",
                            centre(info.sheet_colours), hit, k.s(tok::kFace), colours, true)) {
                model.look.sheet_note_colours = !colours;
                k.say(colours ? "No note colours on the sheet" : "Note colours on the sheet");
            }
        }
        ImGui::End();
    }
}

} // namespace etude::app
