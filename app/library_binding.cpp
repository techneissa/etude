// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_binding.hpp"

#include "frame_log.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <random>
#include <set>
#include <utility>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

// "3 songs", "1 song".
std::string songs_text(std::size_t n) {
    return std::to_string(n) + (n == 1 ? " song" : " songs");
}

// A folder the player typed, as a path under the Songs folder: its parts cleaned of what a file
// system would not take, none of them "." or "..".
std::string clean_folder(std::string_view folder) {
    std::string out;
    std::string part;
    const auto flush = [&] {
        while (!part.empty() && (part.back() == ' ' || part.back() == '.')) {
            part.pop_back();
        }
        std::size_t lead = 0;
        while (lead < part.size() && part[lead] == ' ') {
            ++lead;
        }
        part.erase(0, lead);
        if (!part.empty() && part != "." && part != "..") {
            if (!out.empty()) {
                out += '/';
            }
            out += part;
        }
        part.clear();
    };
    for (const char c : folder) {
        if (c == '/' || c == '\\') {
            flush();
        } else if (static_cast<unsigned char>(c) >= 32 && c != ':' && c != '*' && c != '?' &&
                   c != '"' && c != '<' && c != '>' && c != '|') {
            part += c;
        }
    }
    flush();
    return out;
}

// The file part of a library name: "folk.abc" of "folk.abc#X=3".
std::string_view file_of(std::string_view relative) {
    const std::size_t hash = relative.find('#');
    return hash == std::string_view::npos ? relative : relative.substr(0, hash);
}

// The folders part of a library name: "Classical/Baroque" of "Classical/Baroque/x.mid".
std::string_view folder_of(std::string_view relative) {
    const std::string_view file = file_of(relative);
    const std::size_t slash = file.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : file.substr(0, slash);
}

} // namespace

LibraryBinding::LibraryBinding(SongLibrary& library, HistoryStore& history, fs::path removed)
    : library_(library), history_(history), removed_(std::move(removed)) {
    preview_sound_.reserve(256);
}

void LibraryBinding::update(bool library_changed, std::int64_t now_utc_s) {
    const FrameDetailScope scope(FrameDetail::Library);
    now_utc_s_ = now_utc_s;
    if (library_changed) {
        catalogue_.library_changed();
    }
    if (history_.groups().version() != groups_seen_) {
        groups_seen_ = history_.groups().version();
        catalogue_.set_groups(history_.groups());
    }
    catalogue_.set_now(now_utc_s);
    // A move or a delete that finished: what it did, and the folders listed again.
    while (std::optional<FileResult> r = files_.take()) {
        std::string line;
        if (r->moved > 0) {
            line = r->kind == FileJob::Kind::Remove
                       ? "Removed " + songs_text(r->moved) + " (in Removed songs, to take back)"
                       : "Moved " + songs_text(r->moved);
        }
        if (r->skipped > 0) {
            line += (line.empty() ? "" : ". ") + songs_text(r->skipped) +
                    (r->skipped == 1 ? " was" : " were") + " in that folder already";
        }
        if (r->failed > 0) {
            line += (line.empty() ? "" : ". ") + songs_text(r->failed) + " could not move: " +
                    r->error;
        }
        if (!line.empty()) {
            notices_.push_back(std::move(line));
        }
        library_.rescan();
    }
    update_catalogues();
    // The preview's song, read.
    if (std::optional<SongOpener::Opened> o = preview_opener_.take()) {
        if (o->load.ok) {
            preview_ = song_opening(o->load.song.song, kPreviewS);
            preview_failed_ = preview_->notes.empty();
            preview_song_ = std::make_shared<const LoadedSong>(std::move(o->load.song));
            // The song's whole range, C to C, once (for a player who lets each song choose).
            int lo = 127;
            int hi = 0;
            for (const Note& n : preview_song_->song.notes) {
                if (!n.drum) {
                    lo = std::min<int>(lo, n.pitch);
                    hi = std::max<int>(hi, n.pitch);
                }
            }
            if (lo <= hi) {
                const int low = std::max(0, lo - lo % 12);
                const int high = std::min(127, std::max(low + 24, hi + (12 - hi % 12) % 12));
                preview_song_keys_ = KeyRange{low, high};
            } else {
                preview_song_keys_ = preview_->keys;
            }
        } else {
            preview_failed_ = true;
        }
    }
    if (history_.runs().size() != runs_seen_) {
        runs_seen_ = history_.runs().size();
        catalogue_.history_changed();
    }
    if (history_was_loading_ && !history_.loading()) {
        // The settings saved in the history are in now: the edits held meanwhile go over them,
        // each changing only its own field.
        history_was_loading_ = false;
        if (!held_.empty()) {
            history_.set_song_prefs(apply_edits(history_.song_prefs(), held_));
            held_.clear();
            held_view_.clear();
        }
        catalogue_.prefs_changed();
    }
    catalogue_.sync(library_.state(), history_.runs(),
                    history_.loading() ? held_view_ : history_.song_prefs(), kSyncBudgetNs);
    if (!history_.loading()) {
        // The first time the library lists a song is its date added.
        std::vector<std::uint64_t> unseen = catalogue_.take_unseen();
        if (!unseen.empty()) {
            std::sort(unseen.begin(), unseen.end());
            unseen.erase(std::unique(unseen.begin(), unseen.end()), unseen.end());
            std::vector<std::pair<std::uint64_t, SongPrefs>> dated;
            dated.reserve(unseen.size());
            for (const std::uint64_t hash : unseen) {
                const auto it = history_.song_prefs().find(hash);
                SongPrefs p = it != history_.song_prefs().end() ? it->second : SongPrefs{};
                if (p.added_utc_s == 0) {
                    p.added_utc_s = now_utc_s;
                    dated.emplace_back(hash, std::move(p));
                }
            }
            if (!dated.empty()) {
                history_.set_song_prefs(dated);
                catalogue_.prefs_changed();
            }
        }
    }
    // After the sync, so the downloaded songs' indices are this frame's.
    if (catalogues_ != nullptr) {
        index_downloaded();
    }
    update_versions();
}

void LibraryBinding::update_versions() {
    if (std::shared_ptr<const VersionIndex> index = grouper_.take()) {
        pieces_ = piece_map(*index);
        catalogue_.set_versions(std::move(index));
    }
    const bool on = catalogue_.group_versions();
    if (!on) {
        versions_were_on_ = false;
        return;
    }
    if (!versions_were_on_) {
        versions_were_on_ = true;
        versions_asked_for_ = ~std::uint64_t{0}; // turned on: a pass for what the library holds
    }
    if (catalogue_.facts_version() == versions_asked_for_ &&
        history_.version_overrides_serial() == overrides_asked_for_) {
        return;
    }
    // While the library is still filling in, now and then; once it has, at once.
    if (!catalogue_.caught_up() && now_utc_s_ - versions_asked_at_s_ < kVersionsEveryS) {
        return;
    }
    const std::vector<LibraryItem>& items = library_.state().items;
    if (catalogue_.size() != items.size()) {
        return; // the catalogue has not followed a rescan yet
    }
    // A file the library holds twice (the same bytes under two names) is left out: it is no
    // version of anything.
    std::unordered_map<std::uint64_t, std::uint32_t> copies;
    for (std::size_t i = 0; i < catalogue_.size(); ++i) {
        ++copies[catalogue_.song(i).hash];
    }
    std::vector<VersionInput> songs;
    songs.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        const CatalogueSong& s = catalogue_.song(i);
        if (s.state != EntryState::Loaded || s.hash == 0 || copies[s.hash] != 1 ||
            items[i].state != EntryState::Loaded || items[i].relative != s.relative) {
            continue;
        }
        VersionInput in;
        in.hash = s.hash;
        in.song.title = s.title;
        in.song.composer = s.composer;
        in.song.file_name = s.file_name;
        in.song.folder = s.folder;
        in.song.half_stars = s.half_stars;
        in.song.melody = items[i].entry.melody;
        songs.push_back(std::move(in));
    }
    versions_asked_for_ = catalogue_.facts_version();
    overrides_asked_for_ = history_.version_overrides_serial();
    versions_asked_at_s_ = now_utc_s_;
    grouper_.request(std::move(songs), history_.version_overrides());
}

void LibraryBinding::open(std::size_t i) {
    open_ = i;
    section_open_.reset();
    bars_open_.reset();
    // Opening a version makes it the one its piece's card opens.
    if (!catalogue_.versions_of(i).empty()) {
        choose_version(i);
    }
}

void LibraryBinding::open_at_section(std::size_t i, double start_s, double end_s, int section_id) {
    if (i >= catalogue_.size()) {
        return;
    }
    open(i);
    section_open_ = SectionOpen{catalogue_.song(i).hash, start_s, end_s, section_id};
}

const SongPractice& LibraryBinding::practice(std::size_t i) const {
    static const SongPractice defaults;
    if (i >= catalogue_.size() || catalogue_.song(i).hash == 0) {
        return defaults;
    }
    const auto& all = history_.song_practice();
    const auto it = all.find(catalogue_.song(i).hash);
    return it == all.end() ? defaults : it->second;
}

void LibraryBinding::choose_version(std::size_t i) {
    if (i >= catalogue_.size()) {
        return;
    }
    // Later than every other version's choice, even within the same second.
    std::int64_t latest = 0;
    for (const std::uint32_t j : catalogue_.versions_of(i)) {
        latest = std::max(latest, catalogue_.song(j).chosen_s);
    }
    // A choice made this frame is not in the catalogue yet: then nothing is "chosen last" already.
    if (chosen_clock_ <= latest && catalogue_.song(i).chosen_s != 0 &&
        catalogue_.song(i).chosen_s == latest) {
        return; // it is the one chosen last already
    }
    SongPrefEdit e;
    e.field = SongPrefEdit::Field::Chosen;
    // Each choice later than the last, though the catalogue has not caught up with it yet.
    chosen_clock_ = std::max({now_utc_s_, latest + 1, chosen_clock_ + 1});
    e.time_utc_s = chosen_clock_;
    edit_prefs(i, std::move(e));
}

void LibraryBinding::split_version(std::size_t i) {
    if (i < catalogue_.size() && catalogue_.song(i).hash != 0) {
        history_.edit_version_overrides({VersionEdit::Kind::Split, catalogue_.song(i).hash, 0});
    }
}

ui::LibraryPort::JoinOutcome LibraryBinding::join_versions(std::size_t i, std::size_t with) {
    if (i >= catalogue_.size() || with >= catalogue_.size() || catalogue_.song(i).hash == 0 ||
        catalogue_.song(with).hash == 0) {
        return JoinOutcome::NotRead;
    }
    const std::uint64_t hash = catalogue_.song(i).hash;
    // The song is one piece with the one picked and with the versions that piece has, if the piece
    // has room (a group never passes kMaxVersions; the grouper would drop the join).
    const std::span<const std::uint32_t> mates = catalogue_.versions_of(with);
    if (mates.size() >= kMaxVersions) {
        return JoinOutcome::Full;
    }
    std::vector<std::uint64_t> others{catalogue_.song(with).hash};
    for (const std::uint32_t j : mates) {
        others.push_back(catalogue_.song(j).hash);
    }
    for (const std::uint64_t other : others) {
        if (other != 0 && other != hash) {
            history_.edit_version_overrides({VersionEdit::Kind::Join, hash, other});
        }
    }
    return JoinOutcome::Joined;
}

void LibraryBinding::reset_versions(std::size_t i) {
    if (i < catalogue_.size() && catalogue_.song(i).hash != 0) {
        history_.edit_version_overrides({VersionEdit::Kind::Clear, catalogue_.song(i).hash, 0});
    }
}

ui::LibraryPort::VersionSaid LibraryBinding::version_said(std::size_t i) const {
    if (i >= catalogue_.size() || catalogue_.song(i).hash == 0) {
        return VersionSaid::Auto;
    }
    const VersionOverrides& o = history_.version_overrides();
    const std::uint64_t hash = catalogue_.song(i).hash;
    return o.split(hash) ? VersionSaid::Split : o.joined(hash) ? VersionSaid::Joined
                                                               : VersionSaid::Auto;
}

std::optional<std::size_t> LibraryBinding::take_open() {
    return std::exchange(open_, std::nullopt);
}

void LibraryBinding::edit_prefs(std::size_t i, SongPrefEdit edit) {
    if (i >= catalogue_.size()) {
        return;
    }
    const std::uint64_t hash = catalogue_.song(i).hash;
    if (hash == 0) {
        return;
    }
    edit.hash = hash;
    if (history_.loading()) {
        // Not read yet: a whole row written now would wipe what is stored. Held, and shown.
        apply_edit(held_view_[hash], edit);
        held_.push_back(std::move(edit));
    } else {
        history_.set_song_prefs(apply_edits(history_.song_prefs(), {&edit, 1}));
    }
    catalogue_.prefs_changed(hash);
}

void LibraryBinding::set_favourite(std::size_t i, bool on) {
    SongPrefEdit e;
    e.field = SongPrefEdit::Field::Favourite;
    e.on = on;
    edit_prefs(i, std::move(e));
}

void LibraryBinding::rename(std::size_t i, std::string_view title) {
    if (i < catalogue_.size() && title == catalogue_.song(i).file_title) {
        title = {}; // the file's own name: no name of the player's
    }
    SongPrefEdit e;
    e.field = SongPrefEdit::Field::Title;
    e.text.assign(title);
    edit_prefs(i, std::move(e));
}

void LibraryBinding::set_composer(std::size_t i, std::string_view composer) {
    if (i < catalogue_.size() && composer == catalogue_.song(i).file_composer) {
        composer = {};
    }
    SongPrefEdit e;
    e.field = SongPrefEdit::Field::Composer;
    e.text.assign(composer);
    edit_prefs(i, std::move(e));
}

std::vector<BestEntry> LibraryBinding::bests(std::size_t i, std::size_t max) const {
    if (i >= catalogue_.size() || catalogue_.song(i).hash == 0) {
        return {};
    }
    std::vector<BestEntry> out = song_bests(history_.runs(), catalogue_.song(i).hash);
    std::sort(out.begin(), out.end(), [](const BestEntry& a, const BestEntry& b) {
        if (a.accuracy != b.accuracy) {
            return a.accuracy > b.accuracy;
        }
        return a.speed_percent > b.speed_percent;
    });
    if (out.size() > max) {
        out.resize(max);
    }
    return out;
}

void LibraryBinding::set_favourites(std::span<const std::size_t> songs, bool on) {
    // One edit per song (a content hash once), the rows written in one go.
    std::vector<SongPrefEdit> edits;
    std::set<std::uint64_t> seen;
    for (const std::size_t i : songs) {
        if (i >= catalogue_.size() || catalogue_.song(i).favourite == on) {
            continue;
        }
        const std::uint64_t hash = catalogue_.song(i).hash;
        if (hash == 0 || !seen.insert(hash).second) {
            continue;
        }
        SongPrefEdit e;
        e.field = SongPrefEdit::Field::Favourite;
        e.on = on;
        e.hash = hash;
        edits.push_back(std::move(e));
    }
    if (edits.empty()) {
        return;
    }
    if (history_.loading()) {
        for (SongPrefEdit& e : edits) {
            apply_edit(held_view_[e.hash], e);
            catalogue_.prefs_changed(e.hash);
            held_.push_back(std::move(e));
        }
        return;
    }
    std::vector<std::pair<std::uint64_t, SongPrefs>> rows;
    rows.reserve(edits.size());
    for (const SongPrefEdit& e : edits) {
        const auto it = history_.song_prefs().find(e.hash);
        rows.emplace_back(e.hash, it != history_.song_prefs().end() ? it->second : SongPrefs{});
        apply_edit(rows.back().second, e);
    }
    history_.set_song_prefs(rows);
    for (const SongPrefEdit& e : edits) {
        catalogue_.prefs_changed(e.hash);
    }
}

std::uint64_t LibraryBinding::new_group_id() const {
    static std::mt19937_64 rng(std::random_device{}() ^
                               static_cast<std::uint64_t>(
                                   std::chrono::steady_clock::now().time_since_epoch().count()));
    for (;;) {
        const std::uint64_t id = rng();
        if (id != 0 && history_.groups().find(id) == nullptr) {
            return id;
        }
    }
}

std::uint64_t LibraryBinding::create_group(std::string_view name) {
    GroupEdit e;
    e.kind = GroupEdit::Kind::Create;
    e.group.id = new_group_id();
    e.group.name = clean_group_name(name);
    e.group.position = history_.groups().next_position();
    e.group.created_utc_s = now_utc_s_;
    history_.edit_groups(e);
    return e.group.id;
}

void LibraryBinding::rename_group(std::uint64_t id, std::string_view name) {
    GroupEdit e;
    e.kind = GroupEdit::Kind::Rename;
    e.group.id = id;
    e.group.name.assign(name);
    history_.edit_groups(e);
}

void LibraryBinding::remove_group(std::uint64_t id) {
    GroupEdit e;
    e.kind = GroupEdit::Kind::Remove;
    e.group.id = id;
    history_.edit_groups(e);
}

void LibraryBinding::move_group(std::uint64_t id, int step) {
    std::vector<std::uint64_t> order;
    for (const SongGroup& g : history_.groups().groups()) {
        order.push_back(g.id);
    }
    const auto it = std::find(order.begin(), order.end(), id);
    if (it == order.end()) {
        return;
    }
    const auto at = static_cast<std::ptrdiff_t>(it - order.begin());
    const std::ptrdiff_t to = at + (step < 0 ? -1 : 1);
    if (to < 0 || to >= static_cast<std::ptrdiff_t>(order.size())) {
        return;
    }
    std::swap(order[static_cast<std::size_t>(at)], order[static_cast<std::size_t>(to)]);
    // Every group at its place in the new order (the positions stored may tie or have gaps).
    for (std::size_t k = 0; k < order.size(); ++k) {
        const SongGroup* g = history_.groups().find(order[k]);
        if (g != nullptr && g->position != static_cast<std::int32_t>(k)) {
            GroupEdit e;
            e.kind = GroupEdit::Kind::Move;
            e.group.id = order[k];
            e.group.position = static_cast<std::int32_t>(k);
            history_.edit_groups(e);
        }
    }
}

void LibraryBinding::set_in_group(std::uint64_t id, std::span<const std::size_t> songs, bool in) {
    std::vector<GroupEdit> edits;
    edits.reserve(songs.size());
    for (const std::size_t i : songs) {
        if (i >= catalogue_.size() || catalogue_.song(i).hash == 0) {
            continue;
        }
        GroupEdit e;
        e.kind = in ? GroupEdit::Kind::Add : GroupEdit::Kind::Drop;
        e.group.id = id;
        e.hash = catalogue_.song(i).hash;
        e.at_utc_s = now_utc_s_;
        edits.push_back(std::move(e));
    }
    history_.edit_groups(edits);
}

std::string_view LibraryBinding::why_not_movable(std::size_t i) const {
    if (i >= catalogue_.size() || i >= library_.size()) {
        return "It is not in the library";
    }
    if (library_.roots().empty() || library_.roots()[0].empty()) {
        return "There is no Songs folder";
    }
    // Under the Songs folder: its path and a separator start the song's (the library lists the
    // songs by their place under each folder, so no path needs making canonical here).
    const auto& file = library_.path_at(i).native();
    const auto& root = library_.roots()[0].native();
    constexpr auto kSep = fs::path::preferred_separator;
    if (file.size() <= root.size() + 1 || file.compare(0, root.size(), root) != 0 ||
        (root.back() != kSep && file[root.size()] != kSep)) {
        return "Only songs in your Songs folder move";
    }
    if (current_ == i) {
        return "The song that is open stays where it is";
    }
    return {};
}

std::vector<std::string> LibraryBinding::folders() const {
    std::set<std::string> found{""};
    for (std::size_t i = 0; i < catalogue_.size() && i < library_.size(); ++i) {
        if (why_not_movable(i).empty() || current_ == i) {
            std::string_view f = folder_of(catalogue_.song(i).relative);
            // And the folders it is inside.
            while (!f.empty()) {
                found.emplace(f);
                const std::size_t slash = f.rfind('/');
                f = slash == std::string_view::npos ? std::string_view{} : f.substr(0, slash);
            }
        }
    }
    return {found.begin(), found.end()};
}

std::vector<std::size_t> LibraryBinding::movable_files(std::span<const std::size_t> songs,
                                                       std::size_t& skipped) const {
    skipped = 0;
    // The songs asked for, by file; a file of several tunes moves only when all of them were.
    std::map<std::string, std::vector<std::size_t>> by_file;
    for (const std::size_t i : songs) {
        if (!why_not_movable(i).empty()) {
            ++skipped;
            continue;
        }
        by_file[library_.path_at(i).generic_string()].push_back(i);
    }
    std::map<std::string, std::size_t> tunes;
    for (std::size_t i = 0; i < library_.size(); ++i) {
        const auto f = by_file.find(library_.path_at(i).generic_string());
        if (f != by_file.end()) {
            ++tunes[f->first];
        }
    }
    std::vector<std::size_t> out;
    for (const auto& [file, picked] : by_file) {
        std::vector<std::size_t> unique = picked;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        if (unique.size() < tunes[file]) {
            skipped += unique.size(); // other tunes of the file were not chosen
        } else {
            out.push_back(unique.front());
        }
    }
    return out;
}

void LibraryBinding::move_songs(std::span<const std::size_t> songs, std::string_view folder) {
    std::size_t skipped = 0;
    const std::vector<std::size_t> files = movable_files(songs, skipped);
    const fs::path to = library_.roots()[0] / fs::path(clean_folder(folder));
    FileJob job;
    job.kind = FileJob::Kind::Move;
    for (const std::size_t i : files) {
        job.files.emplace_back(library_.path_at(i), to);
    }
    if (skipped > 0) {
        notices_.push_back(songs_text(skipped) +
                           " stayed: only whole files in your Songs folder move, not the open song");
    }
    if (!job.files.empty()) {
        files_.request(std::move(job));
    }
}

void LibraryBinding::remove_songs(std::span<const std::size_t> songs) {
    std::size_t skipped = 0;
    const std::vector<std::size_t> files = movable_files(songs, skipped);
    FileJob job;
    job.kind = FileJob::Kind::Remove;
    for (const std::size_t i : files) {
        // Under Removed songs in the folders it had, so it can go back where it was.
        job.files.emplace_back(library_.path_at(i),
                               removed_ / fs::path(std::string(folder_of(
                                              catalogue_.song(i).relative))));
        // A song downloaded from a catalogue can be downloaded again.
        CatalogueId id{};
        if (const SongOrigin* o = origin(i); catalogues_ && o && catalogue_from_key(o->catalogue, id)) {
            catalogues_->forget(catalogue_entry_key(id, o->id));
        }
    }
    if (skipped > 0) {
        notices_.push_back(songs_text(skipped) +
                           " stayed: only whole files in your Songs folder go, not the open song");
    }
    if (!job.files.empty()) {
        files_.request(std::move(job));
    }
}

std::optional<std::string> LibraryBinding::take_files_notice() {
    if (notices_.empty()) {
        return std::nullopt;
    }
    std::string s = std::move(notices_.front());
    notices_.erase(notices_.begin());
    return s;
}

void LibraryBinding::request_preview(std::size_t i) {
    if (i >= catalogue_.size() || i >= library_.size()) {
        return;
    }
    if (catalogue_.song(i).relative == preview_relative_ && (preview_ || preview_opener_.busy())) {
        return;
    }
    release_preview_keys();
    preview_relative_ = catalogue_.song(i).relative;
    preview_.reset();
    preview_song_.reset();
    preview_entry_key_ = 0;
    preview_failed_ = false;
    SongLoadOptions o;
    o.tune = library_.tune_at(i);
    o.abc_chords = library_.abc_chords();
    o.known = library_.abc_known_at(i);
    preview_opener_.request(library_.path_at(i), std::move(o));
}

void LibraryBinding::stop_preview() {
    preview_opener_.cancel();
    preview_relative_.clear();
    preview_song_.reset();
    preview_entry_key_ = 0;
    preview_.reset();
    preview_failed_ = false;
    release_preview_keys();
}

void LibraryBinding::preview_note(std::uint8_t key, std::uint8_t velocity) {
    // A key going down may be dropped when a frame holds more than the list reserves; a key
    // letting go never is (it would sound on).
    if (key >= 128 || (velocity > 0 && preview_sound_.size() >= preview_sound_.capacity())) {
        return;
    }
    preview_held_.set(key, velocity > 0);
    preview_sound_.emplace_back(key, velocity);
}

void LibraryBinding::release_preview_keys() {
    for (std::size_t k = 0; k < preview_held_.size(); ++k) {
        if (preview_held_.test(k)) {
            preview_sound_.emplace_back(static_cast<std::uint8_t>(k), 0);
        }
    }
    preview_held_.reset();
}

const SongOrigin* LibraryBinding::origin(std::size_t i) const {
    if (i >= catalogue_.size() || catalogue_.song(i).hash == 0) {
        return nullptr;
    }
    const auto& origins = history_.song_origins();
    const auto it = origins.find(catalogue_.song(i).hash);
    return it == origins.end() ? nullptr : &it->second;
}

KeyRange LibraryBinding::preview_keys() const {
    if (keyboard_) {
        return *keyboard_;
    }
    if (preview_song_) {
        return preview_song_keys_;
    }
    return preview_ ? preview_->keys : KeyRange{48, 72};
}

} // namespace etude::app
