// SPDX-License-Identifier: GPL-3.0-or-later
// The song library: a screen over everything under the top row, opened from the library button
// beside the gear. While it is open the song is paused and silent, as with the settings, and the
// falling notes are covered whole: the library is a place to choose, not something to read while
// playing, and a sheet over half the notes would leave the other half falling unplayed.
//
//   search row   the search field (a finger's tap brings up the on-screen keyboard; a mouse
//                click focuses it for the computer keyboard, which types into it either way), the
//                Select button and the view button (Grid, List or Shelves).
//   chips row    one Sort chip and one Group chip (each opens a short popover; tapping the sort
//                that is on turns it round), then the quick filters (All, Favourites, Recent, In
//                progress) and the Filters chip, whose popover holds the level and hands filters
//                and any quick filter the row had no room for. While songs are being picked it is
//                the selection's bar instead: Done, how many, Add to group, Favourite, Move,
//                Delete.
//   results      the places (the Inbox while it holds new imports, the folders as collections,
//                the player's groups; inside one, a breadcrumb back), then the songs in the view
//                chosen: the Grid's cards, the List's rows (two side by side where they fit), or
//                the Shelves, a row per group of songs that scrolls sideways. Only what is in view
//                is drawn; a finger scrolls with momentum. Sorted by name, an A to Z index runs
//                down the right edge. A tap on Play opens the song; a tap elsewhere opens its
//                details (play, preview, best runs, rename, composer, favourite, groups); a long
//                press starts picking songs and a right click opens a song's actions.
//
// Sources. The songs come from the library folders; a catalogue of free songs to download ("Get
// songs") joins as a second `LibrarySource`, shown in the same screen, its downloads landing in a
// group of the catalogue's (GroupKind::Catalogue).
#pragma once

#include "bar.hpp"
#include "fit_text.hpp"
#include "kit.hpp"
#include "library_layout.hpp"
#include "library_port.hpp"
#include "osk.hpp"
#include "progress_page.hpp"

#include "../library_picks.hpp"

#include "etude/library_browse.hpp"
#include "etude/melody_added.hpp"
#include "etude/progress.hpp"
#include "etude/sections.hpp"
#include "etude/scroll_drag.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace etude::app::ui {

enum class LibrarySource : std::uint8_t { Songs };

// How wide the chips of each song's version words are, measured when first asked and kept while
// the words (the catalogue's `version_words_version`) and the UI scale stay: a frame only reads.
struct ChipWidthCache {
    struct Widths {
        float name = -1.0f;   // the chip with the version's name
        float scored = -1.0f; // the chip with the name and its best score
        float row = -1.0f;    // the List row's chip, with its icon
    };
    std::vector<Widths> by_song;
    std::uint64_t words = ~std::uint64_t{0};
    float unit = -1.0f;
    float more_w = -1.0f; // the "+k" chip, and the scale it was measured at
    float more_unit = -1.0f;
    // The slot for `song`, the cache started again when the words or the scale changed.
    Widths& at(std::size_t song, std::size_t songs, std::uint64_t words_version, float unit_now) {
        if (words != words_version || unit != unit_now || by_song.size() != songs) {
            by_song.assign(songs, Widths{});
            words = words_version;
            unit = unit_now;
        }
        return by_song[song];
    }
};

// The sections of a song as chips in rows (details sheet): a section's name, with its bars after
// it when the player named it (a default name is "Bars 5-8" already).
struct SectionChips {
    struct Chip {
        std::size_t index = 0; // into SongSections::sections
        FitText text;
        float x = 0.0f;
        float w = 0.0f;
        int row = 0;
    };
    std::vector<Chip> chips;
    int rows = 0;
};

// What a plan of section chips was made from.
struct SectionChipsKey {
    std::uint64_t song = 0;
    std::size_t count = 0;
    std::uint32_t used = 0; // SongSections::last_used: moves with every edit
    float width = 0.0f;
    float unit = 0.0f;
    bool operator==(const SectionChipsKey&) const = default;
};

class LibraryScreen {
  public:
    // A name or the search is being typed into.
    bool field_open() const { return open_ && edit_ != Edit::None; }
    // Shows the screen; the first time it opens on the song that is open.
    void open();
    void close();
    bool is_open() const { return open_; }
    // The Progress page instead of the songs (or its Household view); Back returns to the songs.
    void open_progress(bool household) { progress_.open(household); }
    void open_medals(int ladder_of) { progress_.open_medals(ladder_of); }
    // What the Progress page asked of the app since the last call: an edit sheet, another profile.
    ProgressPage::Ask take_progress_ask() { return progress_.take_ask(); }
    // Still drawn while it fades out.
    bool showing() const { return open_ || open_p_ > 0.0f; }
    // Sliding away (closed, still on screen): the screen and its keyboard take no pointer input
    // from the next frame on. Called every frame of the slide, from the frame the close starts.
    void stop_input() const { block_input("##library"); }
    // Draws it over everything under the top row. `model.app.library` is the order it keeps and
    // `model.app.library_view` the view.
    void draw(Kit& k, UiModel& model, LibraryPort& port, const Frame& f);
    // Escape: closes the innermost thing open on it (a popover, a name field, the details, the
    // picking of songs, the keyboard). False when nothing was open: the screen itself should close.
    bool back();
    // The search field or a name field takes the computer keyboard's keys.
    bool typing() const { return open_ && edit_ != Edit::None; }
    // PageUp and PageDown scroll the cards by a screen.
    void page(int direction) { page_ += direction; }
    // The search's text (a developer's start: --library-query).
    void set_query(std::string_view text) { query_.assign(text); }

  private:
    enum class Edit : std::uint8_t { None, Search, Rename, Composer, NewGroup, GroupName, NewFolder };
    enum class Over : std::uint8_t {
        None,
        Sort,
        Group,
        View,
        Filters,
        Actions,
        Details,
        Name,
        AddToGroup, // the groups, each a check for the songs the action is for
        MoveTo,     // the folders of the Songs folder
        Confirm,    // Delete the songs, or delete a group
        GroupMenu,  // a group's rename, order and delete
        Get,        // a catalogue song's details: its preview, its licence and Download
        Credits,    // the catalogues, their licences and credits, and the online switch
        MelodyStep, // "Add to Melodies": the step picker over a song's details
    };

    // The songs' side of the screen: the search row, the chips, the results and the keyboard.
    void draw_browse(Kit& k, UiModel& model, LibraryPort& port, const LibraryFrame& lay,
                     LibraryCatalogue& cat);
    void draw_search(Kit& k, LibraryCatalogue& cat, const Box& box);
    void draw_chips(Kit& k, UiModel& model, LibraryCatalogue& cat, const Box& box);
    void draw_selection_bar(Kit& k, LibraryPort& port, const Box& box);
    void draw_results(Kit& k, UiModel& model, LibraryPort& port, const Box& box, bool compact);
    // The places row at the top of the results: crumbs, the Inbox, collections and groups. Its
    // height (0 when there is nothing to show).
    float places_height(const Kit& k, const LibraryCatalogue& cat, BrowseView view) const;
    void draw_places(Kit& k, LibraryPort& port, ImVec2 at, float width);
    using PlaceChip = std::function<bool(const char* id, const char* glyph_text, std::string_view text,
                                         const char* trail, bool on)>;
    void draw_place_crumbs(Kit& k, LibraryCatalogue& cat, const PlaceChip& chip,
                           std::optional<LibraryScope>& go, const float& x, float y);
    // The buttons beside the search field: Get songs, Select and View (none while the strip is up).
    void draw_row_buttons(Kit& k, UiModel& model, const LibraryFrame& lay);
    // Grid and List. With `compact_results_` it is the one row beside the search field while the
    // keyboard is up on a short screen (`LibraryFrame::strip`): List rows a hit tall, one-line
    // messages.
    void draw_flat(Kit& k, UiModel& model, LibraryPort& port, const Box& box, BrowseView view);
    void draw_group_heads(Kit& k, ImDrawList* dl, LibraryCatalogue& cat,
                          const std::vector<BrowseSection>& sections, bool near, ImVec2 at,
                          ImVec2 size, float grid_top, float scroll);
    void draw_shelves(Kit& k, UiModel& model, LibraryPort& port, const Box& box);
    void draw_alpha(Kit& k, LibraryCatalogue& cat, const Box& strip, float grid_top);
    void draw_card(Kit& k, LibraryPort& port, std::size_t song, ImVec2 a, ImVec2 b);
    // The chips of the versions of song's piece, `song` the one shown (chosen), in a row from `at`
    // (or on rows where `wrap`), `width` across; `live` false draws them without taking taps. A tap
    // on a version's chip or on the "+k" chip is handled here: the version is chosen, or the
    // details open. Returns the version tapped (its song), if any.
    std::optional<std::size_t> draw_version_chips(Kit& k, LibraryPort& port, std::size_t song,
                                                  ImVec2 at, float width, bool wrap, bool live);
    void draw_row(Kit& k, LibraryPort& port, std::size_t song, ImVec2 a, ImVec2 b);
    // "Same piece as another song": the row that replaces the filters while a song is waiting for the one
    // it is the same piece as; a tap on a card then joins them.
    void draw_join_bar(Kit& k, LibraryPort& port, const Box& box);
    // The strip's form of the bar: the way out in the View button's place and, left of it, "Tap the
    // song" over "it joins" (`LibraryFrame::hint`).
    void draw_join_strip(Kit& k, LibraryPort& port, const LibraryFrame& lay);
    // The bar's way out as a round button at `c`; true when it was tapped (the pick has stopped).
    bool join_cancel(Kit& k, ImVec2 c);
    void stop_joining() { joining_.reset(); }
    // A card's or a row's tap, long press, heart and Play, done after it is drawn.
    void song_input(Kit& k, LibraryPort& port, std::size_t song, bool tap, bool hold, bool right,
                    bool heart, bool play, ImVec2 a, ImVec2 b);
    void sideways(Kit& k, int id, const Box& row, float& offset, float max);
    void draw_keyboard(Kit& k, const Box& box);
    void draw_over(Kit& k, UiModel& model, LibraryPort& port, const Box& area);
    void draw_menu(Kit& k, UiModel& model, LibraryPort& port, const Box& area);
    void draw_filters(Kit& k, UiModel& model, const Box& area);
    void draw_list_popover(Kit& k, LibraryPort& port, const Box& area);
    void draw_confirm(Kit& k, LibraryPort& port, const Box& area);
    void draw_details(Kit& k, UiModel& model, LibraryPort& port, const Box& area);
    // "Add to Melodies" (library_screen_melody.cpp): the step picker over the held song's details, the
    // suggested step chosen; the song is kept per profile in `AppSettings::melody_added`.
    void draw_melody_step(Kit& k, UiModel& model, LibraryPort& port, const Box& area);
    // Opens the picker for `song`: at the step the player gave it, else the step suggested for it.
    void open_melody_pick(const AppSettings& app, const CatalogueSong& song);
    void melody_buttons(Kit& k, UiModel& model, LibraryPort& port, std::uint64_t hash, int own, const Box& card,
                        float y);
    // The profile's added songs, read again only when the settings' text changed.
    const MelodyAdded& melody_list(const AppSettings& app);
    // The opening bars as falling notes or as sheet music, `w` by `h` at `pos`. `waiting`: what
    // shows before the song is read, when not the usual words.
    void draw_preview(Kit& k, UiModel& model, LibraryPort& port, ImVec2 pos, float w, float h,
                      std::string_view waiting = {});
    void stop_preview(Kit& k, LibraryPort& port);
    // `keys_top`: where the keyboard starts.
    void draw_name(Kit& k, LibraryPort& port, const Box& area, float keys_top);
    // "Get songs": the catalogues' matches for the search, under the library's own, `width` wide
    // from `at` (window coordinates, scrolled); its height.
    float draw_get_songs(Kit& k, UiModel& model, LibraryPort& port, ImVec2 at, float width);
    // Row `row` of them (its controls are named by it: "##get-action-2").
    // One result: the catalogue entry `entry` shown, `group` its piece's group (kNone when it is on
    // its own, else a row of version chips under it, `chips_h` tall, and `b` below them).
    void draw_get_row(Kit& k, LibraryPort& port, std::uint32_t entry, std::uint32_t group, int row,
                      ImVec2 a, ImVec2 b, float chips_h);
    // The version chips of a result's piece: the entry chosen is the one the row shows. A tap on
    // a chip chooses it; the "+k" chip chooses the next version after those shown.
    void draw_get_chips(Kit& k, LibraryPort& port, std::uint32_t group, std::uint32_t shown, int row,
                        ImVec2 at, float width);
    // The entry of `group` the row shows: the one the player chose, else `matched`, the version
    // the search found first.
    std::uint32_t get_shown(const CatalogueSnapshot& snap, std::uint32_t group,
                            std::uint32_t matched) const;
    void draw_get_details(Kit& k, UiModel& model, LibraryPort& port, const Box& area);
    void draw_credits(Kit& k, UiModel& model, LibraryPort& port, const Box& area);
    // Opens a catalogue song's details, its preview playing; finds it again each frame by its key.
    void open_get(LibraryPort& port, std::uint32_t entry);
    bool find_held_entry(LibraryPort& port);
    void finish_edit(Kit& k, LibraryPort& port, bool save);
    std::string& text();
    // Keeps song `song` for the card's actions, its details or a name field, by its name in the
    // library, which an index does not survive (the list can be rearranged while they are open).
    void hold_song(LibraryPort& port, std::size_t song);
    // Finds that song's index again; false when it has left the library.
    bool find_held_song(LibraryPort& port);
    void open_details(LibraryPort& port, std::size_t song);
    void start_name(LibraryPort& port, std::size_t song, Edit what);
    void start_text(Edit what, std::string_view text, Over back_to);

    // Picking songs.
    void start_selecting();
    void stop_selecting();
    void toggle_selected(const CatalogueSong& s);
    bool is_selected(const CatalogueSong& s) const;
    // The songs an action on many is for: the picked ones, or the one whose details are open.
    // Found again when the list changed.
    const std::vector<std::size_t>& targets(LibraryPort& port);
    // Opens a popover for the songs picked (`many`) or for the one held.
    void open_for(Over what, bool many, ImVec2 anchor);
    // The picked songs' count, and whether every one is a favourite, for the selection's bar.
    std::size_t targets_for_bar(LibraryPort& port);
    bool all_targets_favourite(LibraryPort& port);
    // Every version of the pieces the cards stand for, each once (#215): what Favourite and Add to
    // group act on, where Move, Delete, Rename and Composer act on the card's own version.
    const std::vector<std::size_t>& piece_of(LibraryPort& port, std::span<const std::size_t> cards);
    // Favourite for one card: sets or clears every version of its piece.
    void toggle_favourite(Kit& k, LibraryPort& port, std::size_t card);
    void set_scope(LibraryCatalogue& cat, const LibraryScope& scope);

    bool open_ = false;
    ProgressPage progress_;
    float open_p_ = 0.0f;
    bool opened_once_ = false;
    bool reveal_current_ = false; // scroll the open song into view on the next draw
    // Where the results were scrolled to, kept in `AppSettings::library_scroll` too so a restart
    // opens there (`ScrollKeeper`). `scroll_plain_`: the list shown is the plain one (All filter,
    // whole library, no search), the only one kept; `filling_`: songs are still being read.
    ScrollKeeper scroll_;
    bool scroll_loaded_ = false;
    bool scroll_plain_ = false;
    bool filling_ = false;
    SectionChips section_chips_;
    SectionChipsKey section_chips_key_{~0ull, 0, 0, 0.0f, 0.0f};
    const SectionChips& section_chips(const Kit& k, const SongSections* marked, float width);
    // The results' scroll this frame: puts it back while it is being restored, then notes it in
    // `scroll_` and, once the player has stopped, the settings.
    float keep_scroll(const Kit& k, UiModel& model);
    // The view from the settings every frame, and once the scroll the last run was left at.
    void take_model(const UiModel& model, const LibraryCatalogue& cat);
    Edit edit_ = Edit::None;
    osk::Keyboard keyboard_;
    bool keys_ = false; // the on-screen keyboard is wanted: the field was focused by a finger
    float keyboard_full_ = 0.0f; // the keyboard's height when all the way up
    float keyboard_h_ = 0.0f;    // and as it is drawn
    std::string query_;
    std::string name_; // the rename, composer, group or folder field
    Over over_ = Over::None;
    Over name_back_ = Over::Details; // where a name field goes back to
    std::size_t over_song_ = 0; // found again each frame from over_relative_
    std::string over_relative_; // its name in the library: "folk.abc#X=3"
    ImVec2 anchor_{0, 0};       // where a popover hangs from
    int melody_pick_ = 0;       // the step chosen in the picker (0: none yet)
    int melody_suggested_ = 0;  // the step suggested for the song the picker is about (0: none)
    MelodyAdded melody_added_;  // the settings' added list, parsed, and the text it was parsed from
    std::string melody_added_text_;
    std::vector<BestEntry> bests_;
    ChipWidthCache chip_widths_;
    // The song waiting to be joined to another (its place and content hash, to find it again).
    std::optional<std::size_t> joining_;
    std::uint64_t joining_hash_ = 0;
    // The widths of the details' two piece actions, measured once for a scale.
    struct PieceActionWidths {
        float unit = -1.0f;
        float not_version = 0.0f;
        float same = 0.0f;
        float again = 0.0f;
    };
    PieceActionWidths piece_w_;
    int page_ = 0;
    // The grid or list as laid out for the shown list.
    GridRows rows_;
    GridColumns columns_;
    std::uint64_t rows_version_ = ~std::uint64_t{0};
    float rows_width_ = -1.0f;
    float rows_unit_ = -1.0f;
    BrowseView rows_view_ = BrowseView::Grid;
    bool rows_versions_ = false; // laid out for cards that carry a row of version chips
    bool compact_results_ = false; // this frame's results are the strip's (draw_results sets it)
    bool rows_compact_ = false; // laid out for rows a hit tall (the strip beside the field)
    bool rows_near_ = false; // laid out for the nearest matches of a search that found nothing
    std::vector<BrowseSection> near_sections_;
    std::vector<std::uint32_t> near_order_;
    bool clear_filters_ = false; // "Clear filters" was tapped
    bool done_name_ = false;     // the keyboard's Done saves the name being typed
    Box results_box_; // where the cards were last frame
    BrowseView view_ = BrowseView::Grid; // as drawn this frame

    // Picking songs: by their names in the library, so a rescan keeps them.
    bool selecting_ = false;
    LibraryPicks picks_;
    std::uint64_t pruned_version_ = ~std::uint64_t{0};
    bool many_ = false; // the open popover is for the picked songs, not the held one
    std::vector<std::size_t> targets_;
    std::vector<std::size_t> piece_;
    std::uint64_t targets_version_ = ~std::uint64_t{0};
    std::size_t targets_size_ = 0;

    // Groups and folders.
    std::uint64_t menu_group_ = 0;    // the group the group menu or its rename is for
    bool confirm_group_ = false;      // the confirmation deletes a group, not songs
    std::vector<std::string> folders_; // MoveTo's list, read as it opens
    std::vector<Crumb> crumbs_;
    float list_scroll_ = 0.0f; // a long popover list's scroll

    // The places row and the shelves scroll sideways, each on its own.
    struct Sideways {
        LibraryShelf::Kind kind = LibraryShelf::Kind::Section;
        std::uint32_t key = 0;
        float x = 0.0f;
    };
    std::vector<Sideways> sideways_;
    float places_x_ = 0.0f;
    ScrollDrag side_drag_;
    int side_touch_ = -1; // the shelf being dragged (kPlacesRow: the places row), -1 none
    static constexpr int kPlacesRow = 1 << 20;
    bool side_seen_ = false; // the dragged row was drawn this frame
    bool scroll_top_ = false; // the scope changed: the results start at the top
    std::uint8_t folded_ = 0; // quick filters the chips row had no room for (bit per filter)
    float grid_top_ = 0.0f;   // where the rows start in the results' scroll, last frame
    float alpha_jump_ = -1.0f; // the A to Z index asked to scroll here

    // The A to Z index.
    int alpha_slot_ = -1; // the letter under the finger while it is held

    // The preview of the opening bars in the details.
    bool previewing_ = false;
    bool preview_sound_ = false;
    bool preview_sheet_ = false; // shown as sheet music, not falling notes

    // Get songs: the catalogue song the details are about (by its key, found again each frame),
    // and where the credits go back to.
    std::uint64_t get_key_ = 0;
    std::uint32_t get_entry_ = 0;
    Over credits_back_ = Over::None;
    // The results as rows: a piece with versions is one row, the entry it shows and its group.
    struct GetRow {
        std::uint32_t entry = 0;
        std::uint32_t group = 0;
    };
    std::vector<GetRow> get_list_;
    // The versions the player chose, by their entry's own key (a longer listing does not change
    // it): a piece shows the member in this set, kept while the screen is.
    std::unordered_set<std::uint64_t> get_choice_;
    // What `get_list_` was made from: it is made again only when one of them changes.
    struct GetListKey {
        std::uint64_t hits = ~std::uint64_t{0};
        const CatalogueSnapshot* snap = nullptr;
        bool grouped = false;
        std::uint64_t choices = 0;
        friend bool operator==(const GetListKey&, const GetListKey&) = default;
    };
    GetListKey get_list_key_;
    std::uint64_t get_choices_ = 0; // bumped when a version is chosen
    ChipWidthCache get_chip_widths_;
    int get_rows_ = 0;  // catalogue rows drawn last frame
};

} // namespace etude::app::ui
