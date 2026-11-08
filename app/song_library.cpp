// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_library.hpp"
#include "crash_thread.hpp"

#include "song_library_internal.hpp"

#include "hand_split_cache.hpp"

#include "etude/abc.hpp"
#include "etude/content_hash.hpp"
#include "etude/hand_split.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <utility>

namespace etude::app {

namespace fs = std::filesystem;

void SongLibrary::stop_worker() {
    stop_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    stop_ = false;
    if (restart_pending_) {
        // A change of the ABC setting was waiting for the worker to end: the songs parsed under
        // the old one are parsed again.
        invalidate_abc();
        restart_pending_ = false;
    }
}

void SongLibrary::invalidate_abc() {
    for (std::size_t i = 0; i < state_.items.size(); ++i) {
        LibraryItem& item = state_.items[i];
        if (lower(paths_[i].extension().string()) == ".abc" && item.state == EntryState::Loaded) {
            item.state = EntryState::Pending;
            item.entry = SongEntry{};
        }
    }
}

std::string SongLibrary::canonical_key(const fs::path& file) const {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(file, ec);
    return (ec ? file.lexically_normal() : canonical).generic_string();
}

std::vector<SongLibrary::Found> SongLibrary::list_folders(const std::vector<fs::path>& roots,
                                                          const std::vector<fs::path>& root_keys) {
    std::vector<Found> found;
    std::set<std::string> seen;
    for (std::size_t k = 0; k < roots.size(); ++k) {
        const fs::path& root = roots[k];
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            continue;
        }
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied,
                                            ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const fs::path& p = it->path();
            if (!it->is_regular_file(ec) || !is_song_file_name(p)) {
                continue;
            }
            // The path under this root; the same relative path in an earlier folder wins.
            const fs::path under = p.lexically_relative(root);
            const std::string rel = generic(under);
            if (rel.empty() || rel.rfind("..", 0) == 0 || !seen.insert(rel).second) {
                continue;
            }
            std::error_code ec2;
            const std::uintmax_t size = it->file_size(ec2);
            const auto time = it->last_write_time(ec2);
            LibraryFile file;
            file.relative = rel;
            file.size = ec2 ? 0 : static_cast<std::uint64_t>(size);
            file.mtime = ec2 ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
            found.push_back({p, std::move(file), {}, generic(root_keys[k] / under)});
        }
    }
    return found;
}

std::size_t SongLibrary::scan() {
    stop_worker();
    stop_lister();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        done_.clear();
        priority_.clear();
        finished_ = true;
    }
    adopt(list_folders(roots_, root_keys_));
    return state_.items.size();
}

void SongLibrary::adopt(std::vector<Found> files_found) {
    // An ABC file is one entry; the worker splits it into its tunes. One that was split and has
    // not changed stays split, with the results it had.
    std::vector<Found> found;
    found.reserve(files_found.size());
    std::map<std::string, Expanded> still_expanded;
    for (Found& f : files_found) {
        const std::string& rel = f.file.relative;
        const auto split = expanded_.find(rel);
        if (split != expanded_.end() &&
            split->second.stamp == file_stamp(f.file.size, f.file.mtime) &&
            lower(f.path.extension().string()) == ".abc") {
            for (const std::string& address : split->second.addresses) {
                LibraryFile tune_file = f.file;
                tune_file.relative = join_abc_location(rel, address);
                found.push_back({f.path, std::move(tune_file), address, f.key});
            }
            still_expanded[rel] = split->second;
            continue;
        }
        found.push_back(std::move(f));
    }
    expanded_ = std::move(still_expanded);
    std::vector<LibraryFile> files;
    files.reserve(found.size());
    for (const Found& f : found) {
        files.push_back(f.file);
    }
    LibraryState next = list_state(files, &state_);
    // The paths and tunes in the order of the new state.
    std::map<std::string, std::size_t> by_rel;
    for (std::size_t i = 0; i < found.size(); ++i) {
        by_rel[found[i].file.relative] = i;
    }
    paths_.clear();
    tunes_.clear();
    file_keys_.clear();
    paths_.reserve(next.items.size());
    tunes_.reserve(next.items.size());
    file_keys_.reserve(next.items.size());
    for (const LibraryItem& item : next.items) {
        Found& f = found[by_rel[item.relative]];
        paths_.push_back(f.path);
        tunes_.push_back(f.tune);
        file_keys_.push_back(f.key);
    }
    state_ = std::move(next);
    index_dirty_ = true;
}

void SongLibrary::rescan() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (lister_running_) {
            rescan_again_ = true; // what it lists may be from before the change
            return;
        }
        lister_running_ = true;
    }
    if (lister_.joinable()) {
        lister_.join(); // done already
    }
    lister_ = spawn_thread([this, roots = roots_, keys = root_keys_] {
        std::vector<Found> found = list_folders(roots, keys);
        const std::lock_guard<std::mutex> lock(mutex_);
        listed_ = std::move(found);
        listed_ready_ = true; wake_loop(WakeSource::Library);
        lister_running_ = false;
    });
}

bool SongLibrary::rescanning() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return lister_running_ || listed_ready_ || rescan_again_;
}

void SongLibrary::stop_lister() {
    if (lister_.joinable()) {
        lister_.join();
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    listed_.clear();
    listed_ready_ = false;
    lister_running_ = false;
    rescan_again_ = false;
}

void SongLibrary::ensure_index() const {
    if (!index_dirty_) {
        return;
    }
    by_relative_.clear();
    by_song_.clear();
    by_file_.clear();
    by_relative_.reserve(state_.items.size());
    for (std::size_t i = 0; i < state_.items.size(); ++i) {
        by_relative_[state_.items[i].relative] = i;
        by_song_.emplace(file_keys_[i] + '#' + tunes_[i], i);
        by_file_.emplace(file_keys_[i], i);
    }
    index_dirty_ = false;
}

std::vector<SongLibrary::Job> SongLibrary::pending_jobs() const {
    std::vector<Job> jobs;
    for (std::size_t i = 0; i < state_.items.size(); ++i) {
        if (state_.items[i].state == EntryState::Pending) {
            jobs.push_back({state_.items[i].relative, state_.items[i].stamp, paths_[i], tunes_[i]});
        }
    }
    return jobs;
}

void SongLibrary::set_abc_chords(bool on) {
    if (on == abc_chords_.load()) {
        return;
    }
    abc_chords_ = on;
    ++generation_; // what the running pass parses is for the old setting
    restart_pending_ = true;
    stop_ = true; // the worker leaves at its next job; poll() starts the next pass
}

void SongLibrary::prioritize(const fs::path& file) {
    const std::lock_guard<std::mutex> lock(mutex_);
    priority_.push_back(file);
}

std::vector<std::string> SongLibrary::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

std::optional<AbcKnown> SongLibrary::abc_known(const fs::path& file, std::string_view tune) const {
    if (lower(file.extension().string()) != ".abc") {
        return std::nullopt; // no other format has a listing: no need to canonicalise the path
    }
    return abc_known_by_key(canonical_key(file), tune);
}

std::optional<AbcKnown> SongLibrary::abc_known_at(std::size_t i) const {
    if (i >= paths_.size() || lower(paths_[i].extension().string()) != ".abc") {
        return std::nullopt;
    }
    return abc_known_by_key(file_keys_[i], tunes_[i]); // the key the listing made: no file looked at
}

std::optional<AbcKnown> SongLibrary::abc_known_by_key(const std::string& key,
                                                      std::string_view tune) const {
    const auto at = abc_files_.find(key);
    if (at == abc_files_.end()) {
        return std::nullopt;
    }
    for (const AbcTune& t : at->second.tunes) {
        if (tune.empty() || t.address == tune) {
            AbcKnown known;
            known.tune = t;
            known.first = &t == &at->second.tunes.front();
            known.tune_count = at->second.tunes.size();
            known.file_size = at->second.size;
            known.file_mtime = at->second.mtime;
            return known;
        }
    }
    return std::nullopt;
}

AbcStatus SongLibrary::abc_status(const fs::path& file, std::string_view tune) const {
    if (lower(file.extension().string()) != ".abc") {
        return AbcStatus::NotInLibrary;
    }
    const std::string key = canonical_key(file);
    const auto listed = abc_files_.find(key);
    if (listed != abc_files_.end()) {
        for (const AbcTune& t : listed->second.tunes) {
            if (tune.empty() || t.address == tune) {
                return AbcStatus::Listed;
            }
        }
        return AbcStatus::NoSuchTune;
    }
    const std::optional<std::size_t> at = index_by_key(key, {});
    if (at && state_.items[*at].state == EntryState::Pending) {
        return AbcStatus::Pending;
    }
    return AbcStatus::NotInLibrary;
}

AbcRestoreStep decide_abc_restore(const AbcRestoreState& state) {
    if (state.cancelled) {
        return AbcRestoreStep::Cancel;
    }
    if (!state.abc || state.scripted) {
        return AbcRestoreStep::Restore;
    }
    switch (state.status) {
    case AbcStatus::Listed:
    case AbcStatus::NotInLibrary:
        return AbcRestoreStep::Restore;
    case AbcStatus::Pending:
        return state.library_pending ? AbcRestoreStep::Wait : AbcRestoreStep::NotFound;
    case AbcStatus::NoSuchTune:
        return AbcRestoreStep::NotFound;
    }
    return AbcRestoreStep::Restore;
}

std::string SongLibrary::relative_path(const fs::path& file, std::string_view tune) const {
    const fs::path key = canonical_key(file);
    for (const fs::path& root : root_keys_) {
        const fs::path rel = key.lexically_relative(root);
        const std::string text = generic(rel);
        if (rel.empty() || rel.is_absolute() || text == "." || text.rfind("..", 0) == 0) {
            continue;
        }
        // A file listed per tune is addressed per tune; any other file by its path alone.
        if (const auto at = index_by_key(generic(key), tune)) {
            if (!tunes_[*at].empty()) {
                return join_abc_location(text, tunes_[*at]);
            }
        }
        return text;
    }
    return {};
}

std::optional<fs::path> SongLibrary::resolve(std::string_view relative) const {
    const std::optional<SongPath> song = resolve_song(relative);
    return song ? std::optional<fs::path>(song->path) : std::nullopt;
}

std::optional<SongPath> SongLibrary::resolve_song(std::string_view relative) const {
    const AbcLocation where = split_abc_location(relative);
    if (where.file.empty()) {
        return std::nullopt;
    }
    const fs::path rel = fs::path(std::string(where.file));
    if (rel.is_absolute()) {
        return std::nullopt;
    }
    for (const fs::path& part : rel) {
        if (part == "..") {
            return std::nullopt;
        }
    }
    for (const fs::path& root : roots_) {
        std::error_code ec;
        const fs::path candidate = root / rel;
        if (fs::is_regular_file(candidate, ec)) {
            return SongPath{candidate, std::string(where.tune)};
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> SongLibrary::index_of(const fs::path& file,
                                                 std::string_view tune) const {
    return index_by_key(canonical_key(file), tune);
}

std::optional<std::size_t> SongLibrary::index_by_key(const std::string& key,
                                                     std::string_view tune) const {
    ensure_index();
    const auto exact = by_song_.find(key + '#' + std::string(tune));
    if (exact != by_song_.end()) {
        return exact->second;
    }
    const auto first = by_file_.find(key);
    if (first != by_file_.end()) {
        return first->second;
    }
    return std::nullopt;
}

std::optional<std::size_t> SongLibrary::step(std::optional<std::size_t> current, int step) const {
    return step_item(state_, current, step);
}

} // namespace etude::app
