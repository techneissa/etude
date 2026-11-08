// SPDX-License-Identifier: GPL-3.0-or-later
// The song library as the library screen shows it: one row of facts per song of the SongLibrary
// list (the name, composer, genre and folder, length, level, hands, best score, favourite, date
// added), the search index over them and the order the screen shows them in. No SDL and no files:
// the frame loop hands it the library's state, the history's runs and the player's song settings.
//
// Cost. Nothing is redone per frame. A song is rebuilt when its listing changed (a parse arrived,
// the file changed, the player renamed it), in slices of a time budget per frame, so a library of
// thousands of songs listed at start-up fills in over a few frames instead of stalling one; the
// history is summarised in one pass when it changes. The order is worked out again only when the
// view asks for it after something changed (`update_view`), and a keystroke allocates nothing.
#pragma once

#include "version_grouper.hpp"

#include "etude/library.hpp"
#include "etude/library_browse.hpp"
#include "etude/library_collections.hpp"
#include "etude/library_groups.hpp"
#include "etude/library_search.hpp"
#include "etude/library_state.hpp"
#include "etude/run_record.hpp"
#include "etude/song_prefs.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace etude::app {

struct CatalogueSong {
    std::string relative; // the library's name for it: "Classical/fur.mid", "folk.abc#X=3"
    std::uint64_t stamp = 0;
    EntryState state = EntryState::Pending;
    std::uint64_t hash = 0;    // 0 until it is read
    std::string file_title;    // the title the file gives, or its cleaned file name
    std::string file_composer; // the composer the file names
    std::string title;         // shown: the player's name, else file_title
    std::string composer;      // shown: the player's, else the file's; may be empty
    std::string genre;         // shown: the player's, else its first folder; empty is "Other"
    std::string folder;        // the folders it is in, '/' separated
    std::string file_name;     // its own name, without the folders
    double duration_s = 0.0;   // 0 until read
    int difficulty = 0;        // 1 to 5; 0 until read
    int half_stars = 0;        // the song's rating in half stars, 2 to 10 (rate_song); 0 until read
    bool left = false;         // has notes for the left hand (after the hand split)
    bool right = false;
    int left_keys = 0;  // the keys each hand covers (the Melodies row's step sort), 0 until read
    int right_keys = 0;
    int bars = 0;
    int notes = 0;      // the notes the player plays
    int runs = 0;
    int best_permille = -1;
    std::int32_t recency = -1; // the latest run's place in the history; -1 never played
    std::int32_t last_day = 0;
    bool favourite = false;
    bool piece_favourite = false; // it or any other version of its piece is a favourite (#215)
    std::int64_t added_s = 0;
    std::int64_t chosen_s = 0; // when the player last chose it as the version to open; 0 never
    bool renamed = false; // the player gave it a name of their own
    bool grouped = false; // in at least one of the player's groups
    bool inbox = false;   // a new import (is_new_import)
};

// What part of the library the screen shows: all of it, one collection (a folder and those inside
// it), the Inbox, or one of the player's groups.
struct LibraryScope {
    enum class Kind : std::uint8_t { All, Folder, Inbox, Group };
    Kind kind = Kind::All;
    std::string path;        // Folder
    std::uint64_t group = 0; // Group

    friend bool operator==(const LibraryScope&, const LibraryScope&) = default;
};

// One shelf of the Shelves view: a row of songs shown[begin, end) of shelf_order(), `total` songs
// in it in all (a shelf shows at most kShelfSongs).
struct LibraryShelf {
    enum class Kind : std::uint8_t {
        Recent, // played lately, the latest first
        Favourites,
        InProgress,
        Inbox,
        Collection, // key: into collections()
        Group,      // key: into the groups' order
        Section,    // grouped or searching: key into sections()
        Here,       // the songs right in the folder shown (the whole of a group or the Inbox)
    };
    Kind kind = Kind::Section;
    std::uint32_t key = 0;
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::uint32_t total = 0;
};

class LibraryCatalogue {
  public:
    using Prefs = std::unordered_map<std::uint64_t, SongPrefs>;

    // What changed since the last sync (each a flag; cheap).
    void library_changed() { library_dirty_ = true; }
    void history_changed() { history_dirty_ = true; }
    void prefs_changed() { prefs_dirty_ = true; }
    // Only the songs with this content hash have new settings (the player renamed one).
    void prefs_changed(std::uint64_t hash);

    // Brings the songs in line with the library's list (`library`, whose items it shadows one for
    // one), the history's runs and the player's settings, spending at most `budget_ns` on songs
    // that changed (0: no limit; at least one song is done per call). True when it has caught up.
    bool sync(const LibraryState& library, std::span<const RunRecord> runs, const Prefs& prefs,
              std::int64_t budget_ns = 0);
    bool caught_up() const {
        return dirty_.empty() && !library_dirty_ && !history_dirty_ && !prefs_dirty_;
    }

    // Read songs whose settings have no date added yet, by hash, each reported once after the
    // settings changed (the caller stores the date once the history is loaded).
    std::vector<std::uint64_t> take_unseen();

    std::size_t size() const { return songs_.size(); }
    const CatalogueSong& song(std::size_t i) const { return songs_[i]; }
    // The folded title the library sorts and searches by (fold_for_search of the shown title).
    const std::string& title_key(std::size_t i) const { return title_keys_[i]; }
    std::optional<std::size_t> index_of(std::string_view relative) const;

    // The view: the search text, the sort, grouping and filter.
    void set_query(std::string_view query);
    const std::string& query() const { return query_; }
    void set_order(const BrowseOrder& order);
    const BrowseOrder& order() const { return order_; }
    // Works the shown list out again when the songs, the query or the order changed since the
    // last call. Names are put in order again at most every `kRankEveryS` seconds while songs keep
    // arriving (until then a song not ranked yet may sort out of its place). `today` is the local
    // day for the Recent filter, `now_s` any steady clock in seconds. True when the shown list
    // changed.
    bool update_view(std::int32_t today, double now_s);
    // The shown list must be worked out again before it is read: the query, the order or the list
    // of songs changed. (Songs that only filled in can wait: the screen holds the cards still
    // under a finger.)
    bool view_stale() const { return view_dirty_; }
    // The songs to show, in order, and their sections. While searching, one section in the
    // search's ranked order.
    const std::vector<std::uint32_t>& shown() const { return browser_.order(); }
    const std::vector<BrowseSection>& sections() const { return browser_.sections(); }
    bool searching() const { return searching_; }
    // A search that found nothing: the songs nearest to what was typed (any filter aside).
    const std::vector<std::uint32_t>& near() const { return near_; }
    // Counts for the screen: songs listed, songs still being read.
    std::size_t pending() const { return pending_; }
    // A section's header: the composer or genre, the level, the progress.
    std::string_view section_label(const BrowseSection& section) const;
    // Bumped every time the shown list changes.
    std::uint64_t view_version() const { return view_version_; }
    // Bumped when the list of songs is laid out again or a song's content hash changes.
    std::uint64_t songs_version() const { return songs_version_; }

    // The part of the library shown (any search and filter apply within it).
    void set_scope(const LibraryScope& scope);
    const LibraryScope& scope() const { return scope_; }
    // The player's groups, again whenever their version changed.
    void set_groups(const LibraryGroups& groups);
    std::size_t group_count() const { return group_ids_.size(); }
    std::uint64_t group_id(std::size_t g) const { return group_ids_[g]; }
    std::string_view group_name(std::size_t g) const { return group_names_[g]; }
    // The songs of group `g` in the library (a song listed twice counts twice).
    std::uint32_t group_size(std::size_t g) const {
        return g + 1 < group_offsets_.size() ? group_offsets_[g + 1] - group_offsets_[g] : 0;
    }
    // The groups' place of group `id`, or none.
    std::optional<std::size_t> group_index(std::uint64_t id) const;
    // The wall clock, for the Inbox.
    void set_now(std::int64_t utc_s);
    // The collections inside the scope's folder (the top ones for All), and the Inbox's songs.
    const std::vector<Collection>& collections() const { return collections_; }
    std::size_t inbox_count() const { return inbox_count_; }
    // The Shelves view's rows, worked out with the shown list.
    const std::vector<LibraryShelf>& shelves() const { return shelves_; }
    const std::vector<std::uint32_t>& shelf_order() const { return shelf_order_; }
    std::string_view shelf_label(const LibraryShelf& shelf) const;
    // Sorted by name, no search, no grouping: the first place of each letter in shown().
    bool has_alpha() const { return has_alpha_; }
    const AlphaIndex& alpha() const { return alpha_; }

    // ---- Versions of one piece (#164). The songs a pass of the grouper found to be one piece show
    // as one card: the version chosen last (else the one played last, else the easiest), or the
    // first that passes the filters and the search. Grouping is by content hash and runs off the
    // frame thread; until a pass arrives, or with the setting off, every song is a card of its own.
    void set_group_versions(bool on);
    bool group_versions() const { return group_versions_; }
    // A pass's groups. A song the pass did not know is on its own.
    void set_versions(std::shared_ptr<const VersionIndex> index);
    // Bumped whenever a pass's groups (or the setting) changed what `versions_of` answers. The answer is
    // laid over the songs when the shown list is made again; `refresh_versions` does that without the
    // list, for whoever reads `versions_of` while the library screen is not drawn (the Melodies row).
    std::uint64_t versions_serial() const { return versions_serial_; }
    void refresh_versions() {
        if (versions_dirty_ || versions_built_for_ != songs_version_) {
            rebuild_versions();
        }
    }
    // Bumped when something grouping reads changed (a song's hash, title, composer, file name or
    // rating), so the binding asks for a new pass.
    std::uint64_t facts_version() const { return facts_version_; }
    // The versions of the piece song `i` is one of, easiest first, `i` among them; empty when it is
    // on its own, grouping is off or the pass has not come.
    std::span<const std::uint32_t> versions_of(std::size_t i) const;
    // The chip's word for song `i` among its versions, and the same with its best score ("Easy
    // 87%"); empty when it has none. Made when the versions or the scores change, not on a read.
    std::string_view version_name(std::size_t i) const;
    std::string_view version_scored(std::size_t i) const;
    // The title a card or a row of song `i` shows: the piece's name when it has versions (the
    // shortest of its versions' titles with their level words left out, so it does not change
    // with the version chosen), else its own title.
    std::string_view card_title(std::size_t i) const;
    // Bumped whenever those words changed, for whoever measures them.
    std::uint64_t version_words_version() const { return version_words_version_; }
    // The version a card of song `i`'s piece shows (`i` itself when it is on its own).
    std::size_t card_of(std::size_t i) const;
    // Every version of song `i`'s piece, `i` among them, as indices (just `i` when it is on its
    // own). Favourite and Add to group act on all of them (#215); Move and Delete on one.
    void piece_songs(std::size_t i, std::vector<std::size_t>& out) const;
    // Some piece has two or more versions in the library.
    bool has_versions() const { return has_versions_; }

    // Names are put in order again at most this often while songs keep changing.
    static constexpr double kRankEveryS = 0.3;
    // The most songs a shelf shows (its header opens the rest).
    static constexpr std::uint32_t kShelfSongs = 40;

  private:
    void remap(const LibraryState& library);
    void build_song(std::size_t i, const LibraryItem& item, const Prefs& prefs);
    void apply_plays(std::size_t i);
    void rank();
    void browse(std::int32_t today);
    void rebuild_members();
    void build_shelves();
    void rebuild_versions();
    void rebuild_version_words();
    void pick_cards(bool prune);
    // The player's groups as the list shows them: each piece (a card) once in a group that holds
    // any of its versions.
    void card_groups();
    void collapse_hits(const std::vector<SearchHit>& hits);

    std::vector<CatalogueSong> songs_;
    std::vector<std::uint8_t> dirty_flag_;
    std::vector<std::uint32_t> dirty_;
    std::size_t pending_ = 0;
    bool library_dirty_ = true;
    bool history_dirty_ = true;
    bool prefs_dirty_ = true;
    std::unordered_map<std::uint64_t, SongPlays> plays_;
    std::vector<std::uint64_t> unseen_;

    SearchIndex index_;
    std::vector<std::int32_t> recency_; // by song, for the search's ties
    std::vector<std::uint8_t> favourite_;
    std::vector<std::uint8_t> include_; // by song: passes the filter
    std::vector<BrowseSong> facts_;
    std::vector<std::string> title_keys_;
    std::vector<std::string> composer_keys_;  // composer_sort_key of the shown composer
    std::vector<std::string> genre_keys_;     // the shown genre, folded
    std::vector<std::string> composer_names_; // by composer rank
    std::vector<std::string> genre_names_;    // by genre rank
    LibraryBrowser browser_;
    std::vector<std::uint32_t> near_;

    LibraryScope scope_;
    std::vector<std::uint64_t> group_ids_;
    std::vector<std::string> group_names_;
    std::vector<std::vector<std::uint64_t>> group_hashes_; // by group, its songs' hashes
    std::vector<std::uint32_t> group_offsets_;             // CSR by group into group_songs_
    std::vector<std::uint32_t> group_songs_;
    std::vector<std::uint8_t> scope_mask_; // Group scope: by song
    bool members_dirty_ = true;            // the groups or the songs' hashes
    bool collections_dirty_ = true;
    std::int64_t now_utc_s_ = 0;
    std::vector<std::string> folders_; // by song
    std::vector<Collection> collections_;
    std::size_t inbox_count_ = 0;
    std::vector<LibraryShelf> shelves_;
    std::vector<std::uint32_t> shelf_order_;
    std::vector<std::uint32_t> shelf_scratch_;
    std::vector<std::uint8_t> shelf_mark_; // by song: in the group whose shelf is being made
    bool has_alpha_ = false;
    AlphaIndex alpha_{};

    // Versions: by song, its group (kNoGroup for none) and place in it; the groups' songs in CSR,
    // easiest first; each group's card; and the scratch the search uses (sized once, so a keystroke
    // allocates nothing).
    static constexpr std::uint32_t kNoGroup = ~std::uint32_t{0};
    bool group_versions_ = true;
    std::shared_ptr<const VersionIndex> versions_;
    bool versions_dirty_ = true;
    std::uint64_t versions_built_for_ = ~std::uint64_t{0};
    bool has_versions_ = false;
    std::vector<std::uint32_t> ver_group_;
    std::vector<std::uint32_t> ver_offsets_;
    std::vector<std::uint32_t> ver_songs_;
    std::vector<std::uint32_t> ver_card_;     // by group: the song its card shows; kNoGroup for none
    std::vector<std::uint32_t> ver_member_;   // by song: its place among the group's members
    std::vector<std::string> ver_name_;       // by song: its chip's word
    std::vector<std::string> ver_scored_;     // by song: the word with its best score
    std::vector<std::string> ver_piece_;      // by group: the piece's name
    bool version_words_dirty_ = true;
    std::uint64_t version_words_version_ = 0;
    std::vector<std::uint8_t> ver_mark_;      // by song: a hit of this search
    std::vector<std::uint8_t> ver_emitted_;   // by group: its card is in the hits already
    std::vector<std::uint32_t> ver_stamp_;    // by song: the last of the player's groups it was put in
    std::vector<std::uint32_t> view_offsets_; // the player's groups as the list shows them (CSR)
    std::vector<std::uint32_t> view_songs_;
    bool view_groups_cards_ = false;          // view_* hold them by card, not by file
    std::vector<SearchHit> collapsed_;
    std::uint64_t facts_version_ = 0;
    std::uint64_t versions_serial_ = 0;

    std::string query_;
    BrowseOrder order_;
    bool searching_ = false;
    bool view_dirty_ = true;  // the query or the order changed
    bool songs_dirty_ = true; // a song's facts changed since the last browse
    bool ranks_dirty_ = true; // a name, composer or genre changed since the last ranking
    double ranked_at_s_ = -1e9;
    double browsed_at_s_ = -1e9;
    std::size_t dirty_head_ = 0; // dirty_ before this are done
    std::uint64_t view_version_ = 0;
    std::uint64_t songs_version_ = 0;
};

} // namespace etude::app
