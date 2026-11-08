// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_screen.hpp"

#include "library_screen_shared.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "preview.hpp"
#include "../sheet/sheet_preview.hpp"
#include "settings_catalogue.hpp"

#include "etude/level_stars.hpp"
#include "etude/tap_rule.hpp"
#include "etude/unit_format.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront, the IME request

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace etude::app::ui {

using namespace lib_shared;

// --- The screen --------------------------------------------------------------------------------

void LibraryScreen::open() {
    open_ = true;
    if (!opened_once_) {
        opened_once_ = true;
        reveal_current_ = true;
    }
    scroll_.begin_restore(scroll_.want_dp); // back where it was left
}

void LibraryScreen::close() {
    open_ = false;
    progress_.close();
    over_ = Over::None;
    edit_ = Edit::None;
    stop_selecting();
    stop_joining();
}

bool LibraryScreen::back() {
    if (progress_.is_open()) {
        progress_.close();
        return true;
    }
    if (over_ == Over::Name) {
        over_ = name_back_;
        edit_ = Edit::None;
        return true;
    }
    if (over_ == Over::Credits) {
        over_ = credits_back_;
        return true;
    }
    if (over_ != Over::None) {
        over_ = Over::None;
        return true;
    }
    if (joining_) {
        stop_joining();
        return true;
    }
    if (selecting_) {
        stop_selecting();
        return true;
    }
    if (edit_ != Edit::None) {
        edit_ = Edit::None;
        return true;
    }
    return false;
}

std::string& LibraryScreen::text() { return edit_ == Edit::Search ? query_ : name_; }

void LibraryScreen::hold_song(LibraryPort& port, std::size_t song) {
    over_song_ = song;
    over_relative_ = port.catalogue().song(song).relative;
}

bool LibraryScreen::find_held_song(LibraryPort& port) {
    const LibraryCatalogue& cat = port.catalogue();
    if (over_song_ < cat.size() && cat.song(over_song_).relative == over_relative_) {
        return true;
    }
    // The list was rearranged under it (a rescan, an ABC file split into its tunes).
    if (const std::optional<std::size_t> i = cat.index_of(over_relative_)) {
        over_song_ = *i;
        return true;
    }
    return false;
}

void LibraryScreen::open_details(LibraryPort& port, std::size_t song) {
    over_ = Over::Details;
    many_ = false;
    hold_song(port, song);
    bests_ = port.bests(song, 3);
    if (edit_ == Edit::Search) {
        edit_ = Edit::None; // the keyboard goes down for the details
    }
}

void LibraryScreen::start_name(LibraryPort& port, std::size_t song, Edit what) {
    const CatalogueSong& s = port.catalogue().song(song);
    hold_song(port, song);
    start_text(what, what == Edit::Rename ? std::string_view(s.title)
                                          : std::string_view(s.composer),
               Over::Details);
}

void LibraryScreen::start_text(Edit what, std::string_view text_in, Over back_to) {
    over_ = Over::Name;
    name_back_ = back_to;
    edit_ = what;
    keys_ = by_touch();
    name_.reserve(static_cast<std::size_t>(osk_tok::kTextMax) + 8);
    name_.assign(text_in.substr(0, static_cast<std::size_t>(osk_tok::kTextMax)));
}

void LibraryScreen::finish_edit(Kit& k, LibraryPort& port, bool save) {
    const Edit what = edit_;
    edit_ = Edit::None;
    over_ = name_back_;
    if (!save) {
        return;
    }
    FitText line;
    switch (what) {
    case Edit::Rename:
        if (over_song_ < port.catalogue().size()) {
            port.rename(over_song_, name_);
        }
        break;
    case Edit::Composer:
        if (over_song_ < port.catalogue().size()) {
            port.set_composer(over_song_, name_);
        }
        break;
    case Edit::NewGroup: {
        const std::uint64_t id = port.create_group(name_);
        const std::vector<std::size_t>& songs = targets(port);
        port.set_in_group(id, piece_of(port, songs), true);
        const SongGroup* g = port.groups().find(id);
        FitText count;
        songs_text(count, songs.size());
        line.format("%s in \xE2\x80\x9C%s\xE2\x80\x9D", count.c_str(),
                    g != nullptr ? g->name.c_str() : "");
        k.say_free(line.view());
        if (many_) {
            stop_selecting();
        }
        break;
    }
    case Edit::GroupName:
        port.rename_group(menu_group_, name_);
        break;
    case Edit::NewFolder:
        port.move_songs(targets(port), name_);
        if (many_) {
            stop_selecting();
        }
        break;
    case Edit::None:
    case Edit::Search:
        break;
    }
}

void LibraryScreen::start_selecting() {
    selecting_ = true;
    if (edit_ == Edit::Search) {
        edit_ = Edit::None;
    }
}

void LibraryScreen::stop_selecting() {
    selecting_ = false;
    picks_.clear();
    targets_version_ = ~std::uint64_t{0};
}

void LibraryScreen::toggle_selected(const CatalogueSong& s) {
    picks_.toggle(s.relative);
    targets_version_ = ~std::uint64_t{0};
}

bool LibraryScreen::is_selected(const CatalogueSong& s) const { return picks_.has(s.relative); }

const std::vector<std::size_t>& LibraryScreen::targets(LibraryPort& port) {
    LibraryCatalogue& cat = port.catalogue();
    if (!many_) {
        targets_.clear();
        if (find_held_song(port)) {
            targets_.push_back(over_song_);
        }
        targets_version_ = ~std::uint64_t{0};
        return targets_;
    }
    // The picked songs shown now: a pick that a search, a filter or another place hides stays
    // picked but is not acted on.
    if (targets_version_ != cat.view_version() || targets_size_ != cat.size()) {
        targets_version_ = cat.view_version();
        targets_size_ = cat.size();
        picks_.shown(cat, visible_order(cat), targets_);
    }
    return targets_;
}

void LibraryScreen::open_for(Over what, bool many, ImVec2 anchor) {
    over_ = what;
    many_ = many;
    anchor_ = anchor;
    list_scroll_ = 0.0f;
    targets_version_ = ~std::uint64_t{0};
}

void LibraryScreen::set_scope(LibraryCatalogue& cat, const LibraryScope& scope) {
    cat.set_scope(scope);
    crumbs_.clear();
    if (scope.kind == LibraryScope::Kind::Folder) {
        breadcrumb(cat.scope().path, crumbs_);
    }
    places_x_ = 0.0f;
    page_ = 0;
    reveal_current_ = false;
    scroll_top_ = true;
    scroll_.begin_restore(0.0f);
}

float LibraryScreen::keep_scroll(const Kit& k, UiModel& model) {
    if (scroll_.restoring()) {
        const float want = scroll_.want_dp * k.u;
        ImGui::SetScrollY(want);
        scroll_.restore_frame(ImGui::GetScrollY(), ImGui::GetScrollMaxY(), want, filling_,
                              ImGui::IsMouseDown(ImGuiMouseButton_Left), scroll_plain_, k.dt);
    }
    const float scroll = ImGui::GetScrollY();
    if (!scroll_.restoring()) {
        if (const auto kept = scroll_.observe(scroll, k.u, k.dt, scroll_plain_, kMaxLibraryScroll);
            kept && model.app.library_scroll != *kept) {
            model.app.library_scroll = *kept;
        }
    }
    return scroll;
}

void LibraryScreen::take_model(const UiModel& model, const LibraryCatalogue& cat) {
    view_ = model.app.library_view;
    filling_ = cat.pending() > 0;
    scroll_plain_ = !cat.searching() && cat.scope().kind == LibraryScope::Kind::All &&
                    model.app.library.filter == BrowseFilter::All;
    if (scroll_loaded_) {
        return;
    }
    scroll_loaded_ = true; // the scroll the last run was left at
    scroll_.begin_restore(static_cast<float>(model.app.library_scroll));
    reveal_current_ = reveal_current_ && !scroll_.restoring();
}

void LibraryScreen::stop_preview(Kit& k, LibraryPort& port) {
    if (k.library_preview != nullptr) {
        k.library_preview->release_all();
        for (const LivePreview::Struck& s : k.library_preview->struck()) {
            port.preview_note(s.key, s.velocity);
        }
        k.library_preview->clear_struck();
        k.library_preview->set_opening(nullptr, {});
    }
    if (k.library_sheet != nullptr) {
        k.library_sheet->clear();
    }
    port.stop_preview();
    previewing_ = false;
}

void LibraryScreen::draw(Kit& k, UiModel& model, LibraryPort& port, const Frame& f) {
    open_p_ = approach(open_p_, open_ ? 1.0f : 0.0f, k.dt, tok::kSheet);
    if (previewing_ && (!open_ || (over_ != Over::Details && over_ != Over::Get))) {
        stop_preview(k, port);
    }
    if (open_p_ <= 0.0f) {
        return;
    }
    LibraryCatalogue& cat = port.catalogue();
    take_model(model, cat);
    query_.reserve(static_cast<std::size_t>(osk_tok::kTextMax) + 8);
    cat.set_order(model.app.library);
    cat.set_query(query_);
    // What a move or a delete did.
    while (const std::optional<std::string> notice = port.take_files_notice()) {
        k.say_free(*notice);
    }
    // Songs that fill in while a finger is down on the cards wait until it lifts, so the card
    // under it stays the one it pressed.
    const ImVec2 pressed_at = ImGui::GetIO().MouseClickedPos[0];
    const bool holding =
        ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        results_box_.holds({pressed_at.x, pressed_at.y, pressed_at.x, pressed_at.y});
    if (!holding || cat.view_stale()) {
        cat.update_view(port.today(), k.now);
    }
    // Picks of songs that left the library (a rescan after a move or a delete) go, once per new
    // list.
    if (!picks_.empty() && cat.view_version() != pruned_version_) {
        pruned_version_ = cat.view_version();
        picks_.prune(cat);
        targets_version_ = ~std::uint64_t{0};
    }
    // A group that was shown and is gone (deleted): the whole library again.
    if (cat.scope().kind == LibraryScope::Kind::Group &&
        !cat.group_index(cat.scope().group).has_value()) {
        set_scope(cat, {});
    }
    // The song the actions, the details or a name field are about, found again by its name; gone
    // from the library, they close.
    const bool about_one = over_ == Over::Actions || over_ == Over::Details ||
                           (over_ == Over::Name && name_back_ == Over::Details) ||
                           ((over_ == Over::AddToGroup || over_ == Over::MoveTo ||
                             over_ == Over::Confirm || over_ == Over::MelodyStep) &&
                            !many_ && !confirm_group_);
    if (over_ == Over::Get && !find_held_entry(port)) {
        over_ = Over::None; // the catalogue's listing changed and the song left it
    }
    if (about_one && !find_held_song(port)) {
        over_ = Over::None;
        if (edit_ != Edit::None && edit_ != Edit::Search) {
            edit_ = Edit::None;
        }
    }

    const float shown = ease_out(open_p_);
    const Box area{0.0f, f.row_max.y + (1.0f - shown) * k.s(tok::kS3), f.display.x, f.display.y};
    const float keyboard_up = keyboard_.step(k, open_ && edit_ != Edit::None && keys_);
    const LibraryFrame lay = library_frame(area, k.u, keyboard_up, joining_.has_value());

    // The computer keyboard types into the field that is open (and plays no notes meanwhile); a
    // key typed on it puts the on-screen keyboard away.
    if (open_ && edit_ != Edit::None) {
        osk::want_text();
        const osk::Typed typed = osk::feed_keyboard(text(), osk::FieldRules{});
        if (typed.any) {
            keys_ = false;
        }
        if (typed.done) {
            if (over_ == Over::Name) {
                finish_edit(k, port, true);
            } else {
                edit_ = Edit::None;
            }
        }
        if (typed.cancel) {
            back();
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shown);
    catalogue_sweep::set_library(true); // the chips' settings live here
    if (begin_layer("##library", {area.x0, area.y0}, {area.w(), area.h()})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({area.x0, area.y0}, {area.x1, area.y1}, u32(kScreenBg));
        if (progress_.is_open()) {
            audit::set_quiet(!open_);
            ImGui::BeginDisabled(!open_);
            if (progress_.draw(k, port, area, model.app)) {
                progress_.close();
            }
            ImGui::EndDisabled();
        } else {
            draw_browse(k, model, port, lay, cat);
        }
        audit::set_quiet(false);
    }
    ImGui::End();
    if (!open_) {
        stop_input();
    }
    catalogue_sweep::set_library(false);
    ImGui::PopStyleVar();
    // A toast keeps off the screen's controls; the results are where it may stand.
    k.toast_avoid({lay.search.x0, lay.search.y0, lay.view.x1 - lay.search.x0, lay.search.h()});
    if (open_ && over_ != Over::None) {
        draw_over(k, model, port, area);
    }
}

void LibraryScreen::draw_row_buttons(Kit& k, UiModel& model, const LibraryFrame& lay) {
    if (lay.strip) {
        // The row holds the field and the results while the keyboard is up on a short screen: the
        // buttons wait for it to close (a pick's way out and its instruction stay, `draw_join_strip`).
    } else {
        // The free catalogues: their credits and the switch that lets the app reach them.
        catalogue_sweep::drawn(Setting::GetSongsOnline);
        if (round_button(k, "##library-get", lib_icon::kGlobe, {lay.get.cx(), lay.get.cy()},
                         model.app.get_songs_online, Idle::Fill)) {
            credits_back_ = Over::None;
            over_ = Over::Credits;
        }
        long_press_tip(k, "Free song catalogues");
        // Select: picks songs for the actions on many (a long press on a card does too).
        if (round_button(k, "##library-select", lib_icon::kSelect,
                         {lay.select.cx(), lay.select.cy()}, selecting_, Idle::Fill)) {
            if (selecting_) {
                stop_selecting();
            } else {
                start_selecting();
            }
        }
        long_press_tip(k, selecting_ ? "Done" : "Select songs");
        // The view: Grid, List or Shelves.
        {
            catalogue_sweep::drawn(Setting::LibraryView);
            catalogue_sweep::drawn(Setting::GroupVersions); // in the view's popover
            const ImVec2 c{lay.view.cx(), lay.view.cy()};
            if (round_button(k, "##library-view", view_icon(view_), c, over_ == Over::View,
                             Idle::Fill)) {
                over_ = Over::View;
                anchor_ = {lay.view.x1, lay.view.y1};
            }
            long_press_tip(k, "View");
        }
    }
}

void LibraryScreen::draw_browse(Kit& k, UiModel& model, LibraryPort& port, const LibraryFrame& lay,
                               LibraryCatalogue& cat) {
    // The screen under a popover or the details takes no taps, and its buttons and labels are
    // not the audit's: the card over them hides some, and none can be pressed.
    const bool frozen = over_ != Over::None || !open_;
    audit::set_quiet(frozen);
    ImGui::BeginDisabled(frozen);
    draw_search(k, cat, lay.search);
    draw_row_buttons(k, model, lay);
    if (lay.strip) {
        // No row of chips or bar here (see above); a pick keeps its way out and a line.
        if (joining_) {
            draw_join_strip(k, port, lay);
        }
    } else if (joining_) {
        // Always: the bar holds the way out of the pick (the frame keeps its row for it).
        draw_join_bar(k, port, lay.chips);
    } else if (lay.chips.h() > 0.0f) {
        if (selecting_) {
            // The order's settings are not drawn while the bar stands in their place; the
            // sweep draws the screen with nothing picked.
            draw_selection_bar(k, port, lay.chips);
        } else {
            draw_chips(k, model, cat, lay.chips);
        }
    }
    draw_results(k, model, port, lay.results, lay.strip);
    results_box_ = lay.results;
    if (clear_filters_) {
        clear_filters_ = false;
        model.app.library.filter = BrowseFilter::All;
        model.app.library.levels = kAllLevels;
        model.app.library.hands = BrowseHands::Any;
    }
    ImGui::EndDisabled();
    keyboard_full_ = lay.keyboard_full;
    keyboard_h_ = lay.keyboard.h();
    if (lay.keyboard.h() > 0.0f) {
        // Sliding away once the field is done (edit_ none): no key is taken.
        const bool keys_frozen = (frozen && over_ != Over::Name) || edit_ == Edit::None;
        audit::set_quiet(keys_frozen);
        ImGui::BeginDisabled(keys_frozen);
        draw_keyboard(k, lay.keyboard);
        ImGui::EndDisabled();
    }
}

void LibraryScreen::draw_keyboard(Kit& k, const Box& box) {
    if (keyboard_.draw(k, box, keyboard_full_, text(), osk::FieldRules{})) {
        if (over_ == Over::Name) {
            done_name_ = true;
        } else {
            edit_ = Edit::None;
            keyboard_.reset();
        }
    }
}

void LibraryScreen::draw_over(Kit& k, UiModel& model, LibraryPort& port, const Box& area) {
    // While a name is typed the layer stops at the keyboard, which stays lit and takes the keys.
    Box layer = area;
    if (over_ == Over::Name) {
        layer.y1 = std::max(layer.y0, area.y1 - keyboard_h_);
    }
    if (!begin_layer("##library-over", {layer.x0, layer.y0}, {layer.w(), layer.h()})) {
        ImGui::End();
        return;
    }
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool modal = over_ == Over::Details || over_ == Over::Name || over_ == Over::Confirm ||
                       over_ == Over::Get || over_ == Over::Credits || over_ == Over::MelodyStep;
    if (modal) {
        dl->AddRectFilled({layer.x0, layer.y0}, {layer.x1, layer.y1}, u32(col::kScrim));
    }
    // A tap anywhere off the card closes it (and does nothing else).
    ImGui::SetCursorScreenPos({layer.x0, layer.y0});
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##library-dismiss", {layer.w(), layer.h()})) {
        if (over_ == Over::Name) {
            edit_ = Edit::None;
        }
        over_ = Over::None;
    }
    switch (over_) {
    case Over::Sort:
    case Over::Group:
    case Over::View:
    case Over::Actions:
    case Over::GroupMenu:
        draw_menu(k, model, port, area);
        break;
    case Over::Filters:
        draw_filters(k, model, area);
        break;
    case Over::AddToGroup:
    case Over::MoveTo:
        draw_list_popover(k, port, area);
        break;
    case Over::Confirm:
        draw_confirm(k, port, area);
        break;
    case Over::Details:
        draw_details(k, model, port, area);
        break;
    case Over::MelodyStep:
        draw_melody_step(k, model, port, area);
        break;
    case Over::Get:
        draw_get_details(k, model, port, area);
        break;
    case Over::Credits:
        draw_credits(k, model, port, area);
        break;
    case Over::Name:
        draw_name(k, port, area, layer.y1);
        break;
    case Over::None:
        break;
    }
    ImGui::End();
}

} // namespace etude::app::ui
