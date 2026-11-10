// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_catalogue.hpp"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <tuple>
#include <utility>

namespace etude::app {

namespace {

std::int64_t clock_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr std::size_t kNearMatches = 6;
constexpr std::size_t kQueryReserve = 128;

constexpr std::string_view kLevelLabels[] = {"Not read yet",
                                             "Level 1 \xC2\xB7 Easy",
                                             "Level 2 \xC2\xB7 Simple",
                                             "Level 3 \xC2\xB7 Medium",
                                             "Level 4 \xC2\xB7 Hard",
                                             "Level 5 \xC2\xB7 Expert"};

} // namespace

std::optional<std::size_t> LibraryCatalogue::index_of(std::string_view relative) const {
    for (std::size_t i = 0; i < songs_.size(); ++i) {
        if (songs_[i].relative == relative) {
            return i;
        }
    }
    return std::nullopt;
}

void LibraryCatalogue::set_query(std::string_view query) {
    if (query == query_) {
        return;
    }
    query_.reserve(kQueryReserve); // once: a keystroke allocates nothing
    query_.assign(query);
    view_dirty_ = true;
}

void LibraryCatalogue::set_order(const BrowseOrder& order) {
    if (order == order_) {
        return;
    }
    order_ = order;
    view_dirty_ = true;
}

void LibraryCatalogue::set_scope(const LibraryScope& scope) {
    if (scope == scope_) {
        return;
    }
    scope_ = scope;
    view_dirty_ = true;
    members_dirty_ = true; // a group's mask
    collections_dirty_ = true;
}

void LibraryCatalogue::set_groups(const LibraryGroups& groups) {
    group_ids_.clear();
    group_names_.clear();
    group_hashes_.clear();
    for (const SongGroup& g : groups.groups()) {
        group_ids_.push_back(g.id);
        group_names_.push_back(g.name);
        const std::span<const std::uint64_t> songs = groups.songs_of(g.id);
        group_hashes_.emplace_back(songs.begin(), songs.end());
    }
    members_dirty_ = true;
    view_dirty_ = true;
}

std::optional<std::size_t> LibraryCatalogue::group_index(std::uint64_t id) const {
    for (std::size_t g = 0; g < group_ids_.size(); ++g) {
        if (group_ids_[g] == id) {
            return g;
        }
    }
    return std::nullopt;
}

void LibraryCatalogue::set_now(std::int64_t utc_s) {
    constexpr std::int64_t kHour = 3600;
    // The Inbox is a matter of days: worked out again once an hour at most.
    if (utc_s / kHour != now_utc_s_ / kHour) {
        songs_dirty_ = true;
    }
    now_utc_s_ = utc_s;
}

void LibraryCatalogue::rebuild_members() {
    const std::size_t n = songs_.size();
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> by_hash;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (songs_[i].hash != 0) {
            by_hash[songs_[i].hash].push_back(i);
        }
        songs_[i].grouped = false;
    }
    group_offsets_.assign(1, 0);
    group_songs_.clear();
    scope_mask_.assign(scope_.kind == LibraryScope::Kind::Group ? n : 0, 0);
    for (std::size_t g = 0; g < group_hashes_.size(); ++g) {
        const bool scoped =
            scope_.kind == LibraryScope::Kind::Group && group_ids_[g] == scope_.group;
        for (const std::uint64_t h : group_hashes_[g]) {
            const auto it = by_hash.find(h);
            if (it == by_hash.end()) {
                continue;
            }
            for (const std::uint32_t i : it->second) {
                group_songs_.push_back(i);
                songs_[i].grouped = true;
                if (scoped) {
                    scope_mask_[i] = 1;
                }
            }
        }
        group_offsets_.push_back(static_cast<std::uint32_t>(group_songs_.size()));
    }
}

void LibraryCatalogue::prefs_changed(std::uint64_t hash) {
    for (std::size_t i = 0; i < songs_.size(); ++i) {
        if (songs_[i].hash == hash && dirty_flag_[i] == 0) {
            dirty_flag_[i] = 1;
            dirty_.push_back(static_cast<std::uint32_t>(i));
        }
    }
}

std::vector<std::uint64_t> LibraryCatalogue::take_unseen() { return std::exchange(unseen_, {}); }

// Whether the entry holds other facts than the song does for what the grouping of versions and the
// Melodies row read, with the same file stamp and content hash (an ABC tune read again with its chords
// as a left hand).
static bool entry_facts_differ(const SongEntry& e, const CatalogueSong& s) {
    return e.half_stars != s.half_stars || e.left_keys != s.left_keys || e.right_keys != s.right_keys ||
           e.bar_count != s.bars || e.note_count != s.notes || (e.left_notes > 0) != s.left;
}

void LibraryCatalogue::remap(const LibraryState& library) {
    const std::vector<LibraryItem>& items = library.items;
    bool structural = items.size() != songs_.size();
    for (std::size_t i = 0; !structural && i < items.size(); ++i) {
        structural = items[i].relative != songs_[i].relative;
    }
    if (structural) {
        // The list was rearranged (a scan, an ABC file split into its tunes): songs keep what they
        // showed under their name while they are built again.
        std::unordered_map<std::string_view, std::size_t> old;
        old.reserve(songs_.size());
        for (std::size_t j = 0; j < songs_.size(); ++j) {
            old.emplace(songs_[j].relative, j);
        }
        std::vector<CatalogueSong> next(items.size());
        std::vector<std::uint32_t> moved; // rows whose search document is another song's
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto it = old.find(items[i].relative);
            if (it == old.end() || it->second != i) {
                moved.push_back(static_cast<std::uint32_t>(i));
            }
            if (it != old.end()) {
                next[i] = songs_[it->second];
            } else {
                next[i].relative = items[i].relative;
                next[i].file_title = title_from_file_name(
                    std::string_view(items[i].relative).substr(0, items[i].relative.find('#')));
                next[i].title = next[i].file_title;
            }
        }
        songs_ = std::move(next);
        ++songs_version_;
        const std::size_t n = songs_.size();
        dirty_flag_.assign(n, 0);
        dirty_.clear();
        dirty_head_ = 0;
        recency_.resize(n, -1);
        favourite_.resize(n, 0);
        include_.resize(n, 1);
        facts_.resize(n);
        title_keys_.resize(n);
        composer_keys_.resize(n);
        genre_keys_.resize(n);
        folders_.resize(n);
        members_dirty_ = true;
        collections_dirty_ = true;
        index_.resize(n);
        // Until they are built again the moved rows match nothing, never as the song that was
        // there.
        for (const std::uint32_t i : moved) {
            index_.set(i, SearchDoc{});
        }
        for (std::size_t i = 0; i < n; ++i) {
            dirty_flag_[i] = 1;
            dirty_.push_back(static_cast<std::uint32_t>(i));
        }
        ranks_dirty_ = true;
        songs_dirty_ = true;
        view_dirty_ = true; // the shown list holds the old indices: never left for later
    } else {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const LibraryItem& item = items[i];
            const CatalogueSong& s = songs_[i];
            const bool changed =
                item.stamp != s.stamp || item.state != s.state ||
                (item.state == EntryState::Loaded && (item.entry.hash != s.hash || entry_facts_differ(item.entry, s)));
            if (changed && dirty_flag_[i] == 0) {
                dirty_flag_[i] = 1;
                dirty_.push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
    pending_ = 0;
    for (const LibraryItem& item : items) {
        pending_ += item.state == EntryState::Pending ? 1 : 0;
    }
}

// What a song takes from its entry: its facts, or none while it is not read. True when what the
// grouping of versions and the Melodies row's step sort read changed: the rating, the hands, the keys
// each covers, the bars and the notes (an ABC tune's chords turn on a left hand).
static bool take_entry(CatalogueSong& s, const LibraryItem& item, std::string_view path) {
    const auto sorted_by = [&] {
        return std::tuple(s.half_stars, s.left, s.right, s.left_keys, s.right_keys, s.bars, s.notes);
    };
    const auto before = sorted_by();
    if (item.state == EntryState::Loaded) {
        const SongEntry& e = item.entry;
        s.hash = e.hash;
        s.file_title = e.title;
        s.file_composer = e.composer;
        s.duration_s = e.duration_s;
        s.difficulty = e.difficulty;
        s.half_stars = e.half_stars;
        s.left = e.left_notes > 0;
        s.right = e.right_notes > 0 || (e.unknown_notes > 0 && e.left_notes == 0);
        s.left_keys = e.left_keys;
        s.right_keys = e.right_keys;
        s.bars = e.bar_count;
        s.notes = e.note_count;
    } else {
        s.hash = 0;
        s.file_title = title_from_file_name(path);
        s.file_composer.clear();
        s.duration_s = 0.0;
        s.difficulty = 0;
        s.half_stars = 0;
        s.left = s.right = false;
        s.left_keys = s.right_keys = s.bars = s.notes = 0;
    }
    return sorted_by() != before;
}

void LibraryCatalogue::build_song(std::size_t i, const LibraryItem& item, const Prefs& prefs) {
    CatalogueSong& s = songs_[i];
    const std::uint64_t old_hash = s.hash;
    const EntryState old_state = s.state;
    const bool same_file = s.relative == item.relative;
    s.relative = item.relative;
    s.stamp = item.stamp;
    s.state = item.state;
    const std::string_view path =
        std::string_view(item.relative).substr(0, item.relative.find('#'));
    const std::size_t slash = item.relative.rfind('/', path.size());
    s.folder = slash == std::string::npos ? std::string() : item.relative.substr(0, slash);
    s.file_name = slash == std::string::npos ? item.relative : item.relative.substr(slash + 1);
    const bool facts_changed = take_entry(s, item, path);
    if (s.hash != old_hash) {
        ++songs_version_;
    }
    const SongPrefs* p = nullptr;
    if (s.hash != 0) {
        if (const auto it = prefs.find(s.hash); it != prefs.end()) {
            p = &it->second;
        }
    }
    const std::string_view new_title =
        shown_text(p != nullptr ? std::string_view(p->title) : std::string_view(), s.file_title);
    const std::string_view new_composer = shown_text(
        p != nullptr ? std::string_view(p->composer) : std::string_view(), s.file_composer);
    // What the grouping of versions and the Melodies row read: when it changed, a new pass is due.
    if (s.hash != old_hash || facts_changed || s.state != old_state || !same_file ||
        s.title != new_title || s.composer != new_composer) {
        ++facts_version_;
        version_words_dirty_ = true; // a piece is named by its versions' titles
    }
    s.title.assign(new_title);
    s.renamed = p != nullptr && !p->title.empty();
    s.composer.assign(new_composer);
    s.chosen_s = p != nullptr ? p->chosen_utc_s : 0;
    const std::string_view first_folder =
        std::string_view(s.folder).substr(0, std::min(s.folder.find('/'), s.folder.size()));
    s.genre.assign(
        shown_text(p != nullptr ? std::string_view(p->genre) : std::string_view(), first_folder));
    s.favourite = p != nullptr && p->favourite;
    s.added_s = p != nullptr ? p->added_utc_s : 0;
    if (s.hash != 0 && s.added_s == 0) {
        unseen_.push_back(s.hash);
    }
    if (s.hash != old_hash) {
        members_dirty_ = true; // its groups are found by hash
    }
    if (folders_[i] != s.folder) {
        folders_[i] = s.folder;
        collections_dirty_ = true;
    }
    title_keys_[i] = fold_for_search(s.title);
    composer_keys_[i] = s.composer.empty() ? std::string() : composer_sort_key(s.composer);
    genre_keys_[i] = fold_for_search(s.genre);
    index_.set(i, {s.title, s.composer, s.genre, s.folder, s.file_name});
    apply_plays(i);
}

void LibraryCatalogue::apply_plays(std::size_t i) {
    CatalogueSong& s = songs_[i];
    const auto it = s.hash != 0 ? plays_.find(s.hash) : plays_.end();
    const int old_best = s.best_permille;
    if (it != plays_.end()) {
        s.runs = it->second.runs;
        s.best_permille = it->second.best_permille;
        s.recency = it->second.last_run_index;
        s.last_day = it->second.last_day;
    } else {
        s.runs = 0;
        s.best_permille = -1;
        s.recency = -1;
        s.last_day = 0;
    }
    recency_[i] = s.recency;
    favourite_[i] = s.favourite ? 1 : 0;
    if (s.best_permille != old_best) {
        version_words_dirty_ = true; // a chip shows the best score
    }
}

bool LibraryCatalogue::sync(const LibraryState& library, std::span<const RunRecord> runs,
                            const Prefs& prefs, std::int64_t budget_ns) {
    const std::int64_t start = budget_ns > 0 ? clock_ns() : 0;
    if (library_dirty_) {
        library_dirty_ = false;
        remap(library);
    }
    if (prefs_dirty_) {
        // Every song may have a setting now (the history loaded, the player renamed one).
        prefs_dirty_ = false;
        unseen_.clear();
        for (std::size_t i = 0; i < songs_.size(); ++i) {
            if (dirty_flag_[i] == 0) {
                dirty_flag_[i] = 1;
                dirty_.push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
    if (history_dirty_) {
        history_dirty_ = false;
        plays_ = summarise_plays(runs);
        for (std::size_t i = 0; i < songs_.size(); ++i) {
            apply_plays(i);
        }
        songs_dirty_ = true;
    }
    std::size_t done = 0;
    while (dirty_head_ < dirty_.size()) {
        const std::uint32_t i = dirty_[dirty_head_++];
        dirty_flag_[i] = 0;
        if (i < library.items.size()) {
            build_song(i, library.items[i], prefs);
        }
        ++done;
        ranks_dirty_ = true;
        songs_dirty_ = true;
        if (budget_ns > 0 && (done & 15) == 0 && clock_ns() - start > budget_ns) {
            break;
        }
    }
    if (dirty_head_ >= dirty_.size()) {
        dirty_.clear();
        dirty_head_ = 0;
    }
    return caught_up();
}

void LibraryCatalogue::rank() {
    const std::size_t n = songs_.size();
    std::vector<std::uint32_t> by(n);
    std::iota(by.begin(), by.end(), 0u);
    std::sort(by.begin(), by.end(), [&](std::uint32_t a, std::uint32_t b) {
        if (title_keys_[a] != title_keys_[b]) {
            return title_keys_[a] < title_keys_[b];
        }
        return songs_[a].relative < songs_[b].relative;
    });
    for (std::uint32_t r = 0; r < n; ++r) {
        facts_[by[r]].name_rank = r;
    }
    // Composers and genres: one rank per distinct key, the name shown from its first song.
    const auto rank_texts = [&](const std::vector<std::string>& keys, auto shown,
                                std::vector<std::string>& names, auto set_rank) {
        std::vector<std::uint32_t> with;
        with.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            if (keys[i].empty()) {
                set_rank(i, kNoRank);
            } else {
                with.push_back(i);
            }
        }
        std::sort(with.begin(), with.end(), [&](std::uint32_t a, std::uint32_t b) {
            return keys[a] != keys[b] ? keys[a] < keys[b]
                                      : facts_[a].name_rank < facts_[b].name_rank;
        });
        names.clear();
        for (std::size_t k = 0; k < with.size(); ++k) {
            if (k == 0 || keys[with[k]] != keys[with[k - 1]]) {
                names.push_back(shown(with[k]));
            }
            set_rank(with[k], static_cast<std::uint32_t>(names.size() - 1));
        }
    };
    rank_texts(
        composer_keys_, [&](std::uint32_t i) { return songs_[i].composer; }, composer_names_,
        [&](std::uint32_t i, std::uint32_t r) { facts_[i].composer_rank = r; });
    rank_texts(
        genre_keys_, [&](std::uint32_t i) { return songs_[i].genre; }, genre_names_,
        [&](std::uint32_t i, std::uint32_t r) { facts_[i].genre_rank = r; });
}

void LibraryCatalogue::browse(std::int32_t today) {
    if (members_dirty_) {
        members_dirty_ = false;
        rebuild_members();
    }
    if (collections_dirty_) {
        collections_dirty_ = false;
        child_collections(folders_,
                          scope_.kind == LibraryScope::Kind::Folder ? std::string_view(scope_.path)
                                                                    : std::string_view(),
                          collections_);
    }
    if (versions_dirty_ || versions_built_for_ != songs_version_) {
        rebuild_versions();
    }
    // A piece is a favourite when any of its versions is, so the heart, the Favourites filter and
    // its shelf, and the search's ranking all count the piece (#215).
    for (std::size_t i = 0; i < songs_.size(); ++i) {
        songs_[i].piece_favourite = songs_[i].favourite;
        favourite_[i] = songs_[i].favourite ? 1 : 0;
    }
    for (std::uint32_t g = 0; g + 1 < ver_offsets_.size(); ++g) {
        bool any = false;
        for (std::uint32_t k = ver_offsets_[g]; k < ver_offsets_[g + 1]; ++k) {
            any = any || songs_[ver_songs_[k]].favourite;
        }
        for (std::uint32_t k = ver_offsets_[g]; k < ver_offsets_[g + 1]; ++k) {
            if (ver_group_[ver_songs_[k]] != kNoGroup) {
                songs_[ver_songs_[k]].piece_favourite = any;
                favourite_[ver_songs_[k]] = any ? 1 : 0;
            }
        }
    }
    inbox_count_ = 0;
    for (std::size_t i = 0; i < songs_.size(); ++i) {
        CatalogueSong& s = songs_[i];
        s.inbox = s.state == EntryState::Loaded &&
                  is_new_import(s.added_s, now_utc_s_, s.runs > 0, s.grouped);
        inbox_count_ += s.inbox ? 1 : 0;
    }
    for (std::size_t i = 0; i < songs_.size(); ++i) {
        const CatalogueSong& s = songs_[i];
        BrowseSong& f = facts_[i];
        f.difficulty = static_cast<std::uint8_t>(std::clamp(s.difficulty, 0, 5));
        f.best_permille = static_cast<std::int16_t>(std::clamp(s.best_permille, -1, 1000));
        f.recency = s.recency;
        f.last_day = s.last_day;
        f.duration_s = s.duration_s;
        f.added_s = s.added_s;
        f.favourite = s.piece_favourite;
        f.left = s.left;
        f.right = s.right;
        bool in_scope = true;
        switch (scope_.kind) {
        case LibraryScope::Kind::All:
            break;
        case LibraryScope::Kind::Folder:
            in_scope = in_collection(s.folder, scope_.path);
            break;
        case LibraryScope::Kind::Inbox:
            in_scope = s.inbox;
            break;
        case LibraryScope::Kind::Group:
            in_scope = i < scope_mask_.size() && scope_mask_[i] != 0;
            break;
        }
        // A file that cannot be read is not shown.
        include_[i] = s.state != EntryState::Failed && in_scope &&
                              passes_filter(f, order_.filter, today) && passes_facets(f, order_)
                          ? 1
                          : 0;
    }
    if (version_words_dirty_) {
        rebuild_version_words();
    }
    // Versions of one piece are one card: a song shows when it is its group's card. The player's
    // own groups list songs, so they keep every file.
    const bool collapse = has_versions_;
    near_.clear();
    near_.reserve(kNearMatches);
    searching_ = false;
    // A query with no word in it (a dash, an apostrophe) is no search: everything shows.
    const bool search = !query_.empty() && index_.query_words(query_) > 0;
    if (collapse) {
        pick_cards(!search); // a search finds every version, then shows each piece once
        card_groups();
    }
    view_groups_cards_ = collapse;
    if (search) {
        const std::vector<SearchHit>& hits =
            index_.search(query_, {recency_, favourite_}, include_);
        searching_ = true;
        if (collapse) {
            collapse_hits(hits);
            browser_.ranked(collapsed_);
        } else {
            browser_.ranked(hits);
        }
        if (hits.empty()) {
            for (const SearchHit& h :
                 index_.closest(query_, kNearMatches, {recency_, favourite_})) {
                if (songs_[h.song].state != EntryState::Failed) {
                    near_.push_back(h.song);
                }
            }
        }
    } else {
        browser_.browse(facts_, order_, today, include_,
                        collapse ? LibraryBrowser::GroupLists{view_offsets_, view_songs_}
                                 : LibraryBrowser::GroupLists{group_offsets_, group_songs_});
    }
    has_alpha_ = !searching_ && order_.sort == BrowseSort::Name &&
                 order_.group == BrowseGroup::None && !browser_.order().empty();
    if (has_alpha_) {
        alpha_index(browser_.order(), title_keys_, alpha_);
    }
    build_shelves();
}

void LibraryCatalogue::set_group_versions(bool on) {
    if (on == group_versions_) {
        return;
    }
    group_versions_ = on;
    ++versions_serial_;
    versions_dirty_ = true;
    view_dirty_ = true;
}

void LibraryCatalogue::set_versions(std::shared_ptr<const VersionIndex> index) {
    versions_ = std::move(index);
    ++versions_serial_;
    versions_dirty_ = true;
    view_dirty_ = true;
}

std::span<const std::uint32_t> LibraryCatalogue::versions_of(std::size_t i) const {
    if (i >= ver_group_.size() || ver_group_[i] == kNoGroup) {
        return {};
    }
    const std::uint32_t g = ver_group_[i];
    return {ver_songs_.data() + ver_offsets_[g], ver_offsets_[g + 1] - ver_offsets_[g]};
}

std::string_view LibraryCatalogue::version_name(std::size_t i) const {
    return i < ver_group_.size() && ver_group_[i] != kNoGroup && i < ver_name_.size()
               ? std::string_view(ver_name_[i])
               : std::string_view();
}

std::string_view LibraryCatalogue::version_scored(std::size_t i) const {
    return i < ver_group_.size() && ver_group_[i] != kNoGroup && i < ver_scored_.size()
               ? std::string_view(ver_scored_[i])
               : std::string_view();
}

std::string_view LibraryCatalogue::card_title(std::size_t i) const {
    if (i < ver_group_.size() && ver_group_[i] != kNoGroup && ver_group_[i] < ver_piece_.size() &&
        !ver_piece_[ver_group_[i]].empty()) {
        return ver_piece_[ver_group_[i]];
    }
    return songs_[i].title;
}

void LibraryCatalogue::piece_songs(std::size_t i, std::vector<std::size_t>& out) const {
    out.clear();
    const std::span<const std::uint32_t> versions = versions_of(i);
    if (versions.empty()) {
        out.push_back(i);
        return;
    }
    out.assign(versions.begin(), versions.end());
}

std::size_t LibraryCatalogue::card_of(std::size_t i) const {
    if (i >= ver_group_.size() || ver_group_[i] == kNoGroup || ver_card_[ver_group_[i]] == kNoGroup) {
        return i;
    }
    return ver_card_[ver_group_[i]];
}

void LibraryCatalogue::rebuild_versions() {
    versions_dirty_ = false;
    version_words_dirty_ = true;
    versions_built_for_ = songs_version_;
    const std::size_t n = songs_.size();
    has_versions_ = false;
    ver_group_.assign(n, kNoGroup);
    ver_member_.assign(n, 0);
    if (!group_versions_ || versions_ == nullptr || versions_->groups.empty()) {
        ver_offsets_.assign(1, 0);
        ver_songs_.clear();
        ver_card_.clear();
        return;
    }
    const std::vector<std::vector<VersionIndex::Member>>& groups = versions_->groups;
    // A file in the library twice (the same bytes under two names) is on its own, whatever the
    // pass said: a folder shows the copy that is in it, so neither can stand for the other.
    std::unordered_map<std::uint64_t, std::uint32_t> copies;
    for (const CatalogueSong& song : songs_) {
        if (song.hash != 0) {
            ++copies[song.hash];
        }
    }
    std::vector<std::uint32_t> base(groups.size() + 1, 0);
    for (std::size_t g = 0; g < groups.size(); ++g) {
        base[g + 1] = base[g] + static_cast<std::uint32_t>(groups[g].size());
    }
    std::vector<std::uint32_t> first(base.back(), kNoGroup);
    for (std::uint32_t i = 0; i < n; ++i) {
        if (songs_[i].hash == 0 || songs_[i].state != EntryState::Loaded ||
            copies[songs_[i].hash] != 1) {
            continue;
        }
        const VersionIndex::Place* place = versions_->find(songs_[i].hash);
        if (place == nullptr) {
            continue;
        }
        ver_group_[i] = place->group;
        ver_member_[i] = place->member;
        first[base[place->group] + place->member] = i;
    }
    ver_offsets_.assign(1, 0);
    ver_songs_.clear();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (std::uint32_t m = 0; m < groups[g].size(); ++m) {
            if (first[base[g] + m] != kNoGroup) {
                ver_songs_.push_back(first[base[g] + m]);
            }
        }
        ver_offsets_.push_back(static_cast<std::uint32_t>(ver_songs_.size()));
    }
    // A piece with fewer than two versions in the library is on its own.
    for (std::uint32_t i = 0; i < n; ++i) {
        if (ver_group_[i] != kNoGroup) {
            const std::uint32_t g = ver_group_[i];
            if (ver_offsets_[g + 1] - ver_offsets_[g] < 2) {
                ver_group_[i] = kNoGroup;
            } else {
                has_versions_ = true;
            }
        }
    }
    ver_card_.assign(groups.size(), kNoGroup);
    ver_mark_.assign(n, 0);
    ver_emitted_.assign(groups.size(), 0);
    ver_stamp_.assign(n, 0);
    collapsed_.clear();
    collapsed_.reserve(n);
    view_offsets_.clear();
    view_offsets_.reserve(group_offsets_.size() + 1);
    view_songs_.clear();
    view_songs_.reserve(group_songs_.size() + n);
}

// The words the version chips and the cards show, made once for each song when the versions or a
// score changed: the chip's name, the name with the best score, and each piece's name.
void LibraryCatalogue::rebuild_version_words() {
    version_words_dirty_ = false;
    ++version_words_version_;
    const std::size_t n = songs_.size();
    ver_name_.resize(n);
    ver_scored_.resize(n);
    ver_piece_.assign(ver_offsets_.empty() ? 0 : ver_offsets_.size() - 1, std::string());
    if (!has_versions_) {
        return;
    }
    for (std::uint32_t g = 0; g + 1 < ver_offsets_.size(); ++g) {
        std::string piece;
        for (std::uint32_t k = ver_offsets_[g]; k < ver_offsets_[g + 1]; ++k) {
            const std::uint32_t i = ver_songs_[k];
            ver_name_[i].assign(versions_->groups[g][ver_member_[i]].name);
            ver_scored_[i] = ver_name_[i];
            if (songs_[i].best_permille >= 0) {
                ver_scored_[i] += " " + std::to_string((songs_[i].best_permille + 5) / 10) + "%";
            }
            std::string name = piece_name(songs_[i].title);
            if (piece.empty() || name.size() < piece.size()) {
                piece = std::move(name);
            }
        }
        ver_piece_[g] = std::move(piece);
    }
}

// Each group's card among its versions that pass the filters: the one chosen last, else the one
// played last, else the easiest. With `prune`, every other version of a piece is out of the list.
void LibraryCatalogue::pick_cards(bool prune) {
    for (std::uint32_t g = 0; g + 1 < ver_offsets_.size(); ++g) {
        std::uint32_t best = kNoGroup;
        for (std::uint32_t k = ver_offsets_[g]; k < ver_offsets_[g + 1]; ++k) {
            const std::uint32_t j = ver_songs_[k];
            if (include_[j] == 0) {
                continue;
            }
            if (best == kNoGroup) {
                best = j;
                continue;
            }
            const CatalogueSong& a = songs_[j];
            const CatalogueSong& b = songs_[best];
            if (a.chosen_s != b.chosen_s ? a.chosen_s > b.chosen_s : a.recency > b.recency) {
                best = j;
            }
        }
        ver_card_[g] = best;
    }
    if (prune) {
        for (std::uint32_t i = 0; i < songs_.size(); ++i) {
            if (ver_group_[i] != kNoGroup && ver_card_[ver_group_[i]] != i) {
                include_[i] = 0;
            }
        }
    }
}

// A piece is in a group when any of its versions is, and shows there as its card (the one chosen
// last that passes the filters), once.
void LibraryCatalogue::card_groups() {
    view_offsets_.clear();
    view_offsets_.push_back(0);
    view_songs_.clear();
    for (std::uint32_t g = 0; g + 1 < group_offsets_.size(); ++g) {
        for (std::uint32_t m = group_offsets_[g]; m < group_offsets_[g + 1]; ++m) {
            std::uint32_t song = group_songs_[m];
            if (ver_group_[song] != kNoGroup) {
                song = ver_card_[ver_group_[song]];
                if (song == kNoGroup) {
                    continue; // no version of the piece passes the filters
                }
            }
            if (ver_stamp_[song] != g + 1) {
                ver_stamp_[song] = g + 1;
                view_songs_.push_back(song);
            }
        }
        view_offsets_.push_back(static_cast<std::uint32_t>(view_songs_.size()));
    }
    // The stamps are for this pass only.
    for (const std::uint32_t song : view_songs_) {
        ver_stamp_[song] = 0;
    }
}

// A search's hits with each piece once, at the place of its best hit: its card when that is a hit
// too, else the best-ranked version that is.
void LibraryCatalogue::collapse_hits(const std::vector<SearchHit>& hits) {
    collapsed_.clear();
    for (const SearchHit& h : hits) {
        ver_mark_[h.song] = 1;
    }
    for (const SearchHit& h : hits) {
        const std::uint32_t g = ver_group_[h.song];
        if (g == kNoGroup) {
            collapsed_.push_back(h);
        } else if (ver_emitted_[g] == 0) {
            ver_emitted_[g] = 1;
            SearchHit hit = h;
            const std::uint32_t card = ver_card_[g];
            if (card != kNoGroup && ver_mark_[card] != 0) {
                hit.song = card;
            }
            collapsed_.push_back(hit);
        }
    }
    for (const SearchHit& h : hits) {
        ver_mark_[h.song] = 0;
        if (ver_group_[h.song] != kNoGroup) {
            ver_emitted_[ver_group_[h.song]] = 0;
        }
    }
}

bool LibraryCatalogue::update_view(std::int32_t today, double now_s) {
    // While songs keep arriving the names are put in order now and then, not on every one.
    if (ranks_dirty_ && (caught_up() || view_dirty_ || now_s - ranked_at_s_ >= kRankEveryS)) {
        rank();
        ranks_dirty_ = false;
        ranked_at_s_ = now_s;
        songs_dirty_ = true;
    }
    const bool background = songs_dirty_ && !view_dirty_;
    if (!view_dirty_ && !songs_dirty_) {
        return false;
    }
    if (background && !caught_up() && now_s - browsed_at_s_ < kRankEveryS) {
        return false;
    }
    browse(today);
    browsed_at_s_ = now_s;
    view_dirty_ = false;
    songs_dirty_ = false;
    ++view_version_;
    return true;
}

std::string_view LibraryCatalogue::section_label(const BrowseSection& section) const {
    if (searching_) {
        return {};
    }
    switch (order_.group) {
    case BrowseGroup::None:
        return {};
    case BrowseGroup::Composer:
        return section.key < composer_names_.size() ? std::string_view(composer_names_[section.key])
                                                    : std::string_view("Unknown composer");
    case BrowseGroup::Genre:
        return section.key < genre_names_.size() ? std::string_view(genre_names_[section.key])
                                                 : std::string_view("Other");
    case BrowseGroup::Difficulty:
        return kLevelLabels[std::min<std::size_t>(section.key, 5)];
    case BrowseGroup::MyGroups:
        return section.key < group_names_.size() ? std::string_view(group_names_[section.key])
                                                 : std::string_view("Not in a group");
    case BrowseGroup::Progress:
        switch (static_cast<Progress>(section.key)) {
        case Progress::NotStarted:
            return "Not started";
        case Progress::InProgress:
            return "In progress";
        case Progress::Mastered:
            return "Mastered";
        }
        break;
    }
    return {};
}

} // namespace etude::app
