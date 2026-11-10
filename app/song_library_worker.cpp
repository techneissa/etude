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

void SongLibrary::load_file(const fs::path& path, FileCache& file) {
    file = FileCache{};
    file.path = path;
    file.valid = true;
    if (!read_song_bytes(path, file.bytes, &file.error)) {
        return;
    }
    file.hash = content_hash(file.bytes.data(), file.bytes.size());
    file.format = sniff_song_format(file.bytes);
    std::error_code ec;
    file.size = static_cast<std::uint64_t>(fs::file_size(path, ec));
    if (ec) {
        file.size = file.bytes.size();
    }
    const auto time = fs::last_write_time(path, ec);
    file.mtime = ec ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
    if (file.format == SongFileFormat::Abc) {
        file.tunes = list_abc_tunes(file.bytes);
    }
}

SongLibrary::Result SongLibrary::parse_entry(const std::string& relative, std::uint64_t stamp,
                                             const std::string& tune, FileCache& file,
                                             const JobContext& ctx) {
    Result r;
    r.relative = relative;
    r.stamp = stamp;
    r.generation = ctx.generation;
    if (!file.error.empty()) {
        r.error = file.error;
        return r;
    }
    std::string name = file.path.filename().string();
    std::uint64_t hash = file.hash;
    LibraryLoadOptions options;
    if (file.format == SongFileFormat::Abc) {
        const AbcTune* found = nullptr;
        for (const AbcTune& t : file.tunes) {
            if (tune.empty() || t.address == tune) {
                found = &t;
                break;
            }
        }
        if (found == nullptr) {
            r.error = tune.empty() ? "no ABC tune in the file" : "the file has no tune " + tune;
            return r;
        }
        hash = found->hash;
        options.tune = found->address;
        options.abc_tunes = &file.tunes;
        options.abc_chords = ctx.abc_chords;
        // The entry depends on the setting, so the cache keeps the two apart.
        name += ctx.abc_chords ? "|chords" : "|plain";
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (const SongEntry* hit = cache_.find(hash, name)) {
            r.ok = true;
            r.entry = *hit;
            return r;
        }
    }
    // The scan splits the hands to make the entry; the open song takes the same split.
    options.split_hands = [](Song& song, std::uint64_t song_hash, bool abc_chords) {
        split_hands_cached(song, song_hash, abc_chords);
    };
    const LibraryLoad load = load_library_entry(file.bytes, file.path.filename().string(), options);
    if (!load.ok) {
        r.error = load.message.empty() ? std::string("not a song file") : load.message;
        return r;
    }
    r.ok = true;
    r.entry = load.entry;
    const std::lock_guard<std::mutex> lock(mutex_);
    cache_.insert(hash, name, r.entry, file.path.string() + "|" + tune + name);
    return r;
}

template <class Emit>
void SongLibrary::run_job(const Job& job, FileCache& file, const JobContext& ctx, Emit&& emit) {
    if (!file.valid || file.path != job.path) {
        load_file(job.path, file);
    }
    // The file's listing goes with the first result of the file, for abc_known().
    auto with_listing = [&](Result r) {
        if (file.format == SongFileFormat::Abc && file.error.empty() && !file.listing_sent) {
            file.listing_sent = true;
            r.has_listing = true;
            r.tunes = file.tunes;
            r.file_size = file.size;
            r.file_mtime = file.mtime;
        }
        return r;
    };
    if (file.format == SongFileFormat::Abc && job.tune.empty() && file.tunes.size() > 1) {
        // The file holds several tunes: its entry becomes one per tune, then each is parsed.
        Result expand;
        expand.kind = Result::Kind::Expand;
        expand.relative = job.relative;
        expand.stamp = job.stamp;
        expand.generation = ctx.generation;
        emit(with_listing(std::move(expand)));
        for (const AbcTune& t : file.tunes) {
            if (stop_) {
                return;
            }
            emit(parse_entry(join_abc_location(job.relative, t.address), job.stamp, t.address, file,
                             ctx));
        }
        return;
    }
    emit(with_listing(parse_entry(job.relative, job.stamp, job.tune, file, ctx)));
}

void SongLibrary::launch_worker() {
    std::vector<Job> jobs = pending_jobs();
    if (jobs.empty()) {
        return;
    }
    stop_ = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        finished_ = false;
    }
    const JobContext ctx{generation_, abc_chords_.load()};
    worker_ = spawn_thread([this, jobs = std::move(jobs), ctx] {
        FileCache file;
        std::vector<bool> taken(jobs.size(), false);
        std::size_t cursor = 0;
        for (;;) {
            if (stop_) {
                break;
            }
            // A file asked for first goes before the rest, which keep their order.
            std::size_t pick = jobs.size();
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                while (pick == jobs.size() && !priority_.empty()) {
                    const fs::path wanted = priority_.front();
                    priority_.erase(priority_.begin());
                    for (std::size_t i = cursor; i < jobs.size(); ++i) {
                        if (!taken[i] && jobs[i].path == wanted) {
                            pick = i;
                            break;
                        }
                    }
                }
            }
            if (pick == jobs.size()) {
                while (cursor < jobs.size() && taken[cursor]) {
                    ++cursor;
                }
                if (cursor == jobs.size()) {
                    break;
                }
                pick = cursor;
            }
            taken[pick] = true;
            run_job(jobs[pick], file, ctx, [this](Result&& r) {
                const std::lock_guard<std::mutex> lock(mutex_);
                done_.push_back(std::move(r)); wake_loop(WakeSource::Library);
            });
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        cv_.notify_all();
    });
}

void SongLibrary::start_loading() {
    stop_worker();
    launch_worker();
}

void SongLibrary::load_all_now() {
    stop_worker();
    FileCache file;
    const JobContext ctx{generation_, abc_chords_.load()};
    for (const Job& job : pending_jobs()) {
        run_job(job, file, ctx, [this](Result&& r) {
            const std::lock_guard<std::mutex> lock(mutex_);
            done_.push_back(std::move(r));
        });
    }
    poll();
}

bool SongLibrary::expand_all(std::vector<Result>& batch) {
    // Every file of the batch that became several tunes is replaced in one pass over the list, so
    // a library of hundreds of such files costs one rebuild, not one per file.
    ensure_index();
    std::unordered_map<std::size_t, const Result*> at; // list position -> its Expand
    for (Result& r : batch) {
        if (r.kind != Result::Kind::Expand || r.generation != generation_) {
            continue;
        }
        const auto found = by_relative_.find(r.relative);
        if (found == by_relative_.end()) {
            continue;
        }
        const LibraryItem& old = state_.items[found->second];
        if (old.stamp != r.stamp || old.state != EntryState::Pending || r.tunes.size() < 2) {
            continue;
        }
        at[found->second] = &r;
        std::vector<std::string> addresses;
        addresses.reserve(r.tunes.size());
        for (const AbcTune& t : r.tunes) {
            addresses.push_back(t.address);
        }
        expanded_[r.relative] = Expanded{r.stamp, std::move(addresses)};
        if (r.tunes.back().more_tunes_follow) {
            notices_.push_back("Only the first " + std::to_string(kMaxAbcTunes) + " tunes of " +
                               r.relative + " are listed");
        }
        if (r.has_listing) {
            abc_files_[file_keys_[found->second]] = AbcFile{r.file_size, r.file_mtime, r.tunes};
        }
    }
    if (at.empty()) {
        return false;
    }
    std::vector<LibraryItem> items;
    std::vector<fs::path> paths;
    std::vector<std::string> tunes;
    std::vector<std::string> keys;
    const std::size_t total = state_.items.size() + at.size() * 8;
    items.reserve(total);
    paths.reserve(total);
    tunes.reserve(total);
    keys.reserve(total);
    for (std::size_t i = 0; i < state_.items.size(); ++i) {
        const auto split = at.find(i);
        if (split == at.end()) {
            items.push_back(std::move(state_.items[i]));
            paths.push_back(std::move(paths_[i]));
            tunes.push_back(std::move(tunes_[i]));
            keys.push_back(std::move(file_keys_[i]));
            continue;
        }
        const Result& r = *split->second;
        for (const AbcTune& t : r.tunes) {
            LibraryItem item;
            item.relative = join_abc_location(r.relative, t.address);
            item.stamp = r.stamp;
            items.push_back(std::move(item));
            paths.push_back(paths_[i]);
            tunes.push_back(t.address);
            keys.push_back(file_keys_[i]);
        }
    }
    state_.items = std::move(items);
    paths_ = std::move(paths);
    tunes_ = std::move(tunes);
    file_keys_ = std::move(keys);
    index_dirty_ = true;
    return true;
}

bool SongLibrary::apply(Result& r) {
    const auto at = by_relative_.find(r.relative);
    if (at == by_relative_.end()) {
        return false;
    }
    if (r.has_listing) {
        abc_files_[file_keys_[at->second]] = AbcFile{r.file_size, r.file_mtime, std::move(r.tunes)};
    }
    return r.ok ? apply_loaded(state_, at->second, r.stamp, std::move(r.entry))
                : apply_failed(state_, at->second, r.stamp, std::move(r.error));
}

bool SongLibrary::poll() {
    std::vector<Result> batch;
    std::vector<Found> listed;
    bool relist = false;
    bool again = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(done_);
        if (listed_ready_) {
            listed.swap(listed_);
            listed_ready_ = false;
            relist = true;
            again = std::exchange(rescan_again_, false);
        }
    }
    bool changed = false;
    if (relist) {
        // The folders as the lister found them, under the worker's feet: its results for songs
        // still listed apply as before, those for songs gone are dropped (by name), and a song
        // new to the list is read by the pass that starts once this one has ended.
        changed |= expand_all(batch);
        ensure_index();
        for (Result& r : batch) {
            if (r.kind == Result::Kind::Entry && r.generation == generation_) {
                apply(r);
            }
        }
        batch.clear();
        adopt(std::move(listed));
        relaunch_pending_ = true;
        changed = true;
        if (again) {
            rescan();
        }
    }
    changed |= expand_all(batch);
    ensure_index();
    for (Result& r : batch) {
        if (r.kind == Result::Kind::Entry && r.generation == generation_) {
            changed |= apply(r);
        }
    }
    if (restart_pending_) {
        bool ended = false;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ended = finished_;
        }
        if (ended) {
            // The pass under the old setting is over (its worker only has to return): parse the
            // ABC songs again under the new one.
            if (worker_.joinable()) {
                worker_.join();
            }
            stop_ = false;
            restart_pending_ = false;
            invalidate_abc();
            relaunch_pending_ = false;
            launch_worker();
            changed = true;
        }
    }
    if (relaunch_pending_ && !restart_pending_) {
        bool ended = false;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ended = finished_;
        }
        if (ended) {
            // The songs a new listing brought in are read now that the pass before it has ended.
            if (worker_.joinable()) {
                worker_.join();
            }
            relaunch_pending_ = false;
            launch_worker();
        }
    }
    return changed;
}

void SongLibrary::wait_until_settled() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return finished_; });
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        poll();
        const std::lock_guard<std::mutex> lock(mutex_);
        if (finished_ && done_.empty()) {
            break; // everything is applied, and poll() did not start another pass
        }
    }
}

} // namespace etude::app
