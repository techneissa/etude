// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue_service.hpp"
#include "loop_wake.hpp"

#include "library_files.hpp"

#include "etude/content_hash.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

bool read_file(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return static_cast<bool>(in) || in.eof();
}

// Writes beside the file, then renames over it, so a reader never sees half a file.
bool write_file(const fs::path& path, std::string_view bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".part";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// The file looks like what its extension says: a song the library can read.
bool looks_like(std::string_view ext, std::string_view bytes) {
    if (ext == ".mid" || ext == ".midi") {
        return bytes.substr(0, 4) == "MThd";
    }
    if (ext == ".mscz" || ext == ".mxl") {
        return bytes.substr(0, 2) == "PK";
    }
    if (ext == ".mscx" || ext == ".musicxml" || ext == ".xml") {
        const std::size_t lt = bytes.find('<');
        return lt != std::string_view::npos && lt < 8;
    }
    return false;
}

} // namespace

// ---- The network thread

void CatalogueService::read_caches() {
    for (const CatalogueInfo& info : catalogue_infos()) {
        if (!info.enabled) {
            continue;
        }
        std::string text;
        std::vector<CatalogueEntry> entries;
        std::int64_t fetched = 0;
        if (read_file(index_file(info.id), text) &&
            read_catalogue_cache(text, info.id, fetched, entries)) {
            publish(info.id, std::move(entries), fetched, CatalogueIndexState::Kind::Ready, {});
        } else {
            publish(info.id, {}, 0, CatalogueIndexState::Kind::None, {});
        }
    }
}

void CatalogueService::publish(CatalogueId id, std::vector<CatalogueEntry> entries,
                               std::int64_t fetched, CatalogueIndexState::Kind kind,
                               std::string reason) {
    // Only this thread (the network's) touches the indexes: the new snapshot is built without the
    // lock, which the frame thread takes every frame.
    const auto i = static_cast<std::size_t>(id);
    const bool keep_entries = kind == CatalogueIndexState::Kind::Failed && entries.empty();
    if (!keep_entries) {
        indexes_[i] = std::move(entries);
    }
    auto snap = std::make_shared<CatalogueSnapshot>();
    std::size_t total = 0;
    for (const auto& list : indexes_) {
        total += list.size();
    }
    snap->entries.reserve(total);
    snap->keys.reserve(total);
    for (const auto& list : indexes_) {
        for (const CatalogueEntry& e : list) {
            snap->entries.push_back(e);
            snap->keys.push_back(catalogue_entry_key(e.source, e.id));
        }
    }
    snap->versions = group_catalogue_versions(snap->entries);
    {
        std::lock_guard lock(mutex_);
        CatalogueIndexState& st = shared_states_[i];
        st.kind = kind;
        st.songs = indexes_[i].size();
        if (fetched > 0) {
            st.fetched_utc_s = fetched;
        }
        st.reason = std::move(reason);
        snap->version = ++snapshot_serial_;
        shared_snapshot_ = std::move(snap);
        ++files_serial_; // the states are read against the new snapshot
    }
    search_wake_.notify_all();
    wake_loop(WakeSource::Catalogue);
}

bool CatalogueService::pop_file(std::size_t& at) {
    while (!file_queue_.empty()) {
        at = file_queue_.front();
        file_queue_.pop_front();
        Slot& s = slots_[at];
        if (s.key == 0 || s.file.state != CatalogueFile::State::Queued) {
            continue;
        }
        // At work from here: cancel() flags it, and queue() never takes the slot.
        s.file.state = CatalogueFile::State::Fetching;
        ++files_serial_;
        return true;
    }
    return false;
}

bool CatalogueService::run_file(std::size_t at) {
    CatalogueEntry entry;
    std::uint64_t key = 0;
    bool keep = false;
    {
        std::lock_guard lock(mutex_);
        Slot& s = slots_[at];
        if (s.cancel || s.key == 0) {
            s.file = {};
            s.key = 0;
            s.cancel = false;
            ++files_serial_;
            return false;
        }
        entry = s.entry;
        key = s.key;
        keep = s.file.keep;
        ++files_serial_;
    }
    const CatalogueInfo& info = catalogue_info(entry.source);
    const fs::path cached = cache_file(entry);
    std::string bytes;
    std::string reason;
    bool ok = false;
    bool cancelled = false;
    std::error_code ec;
    if (!catalogue_file_allowed(entry)) {
        reason = "Not a file this catalogue offers";
    } else if (fs::is_regular_file(cached, ec) && read_file(cached, bytes) && !bytes.empty()) {
        ok = true;
        fs::last_write_time(cached, fs::file_time_type::clock::now(), ec); // used: kept longest
    } else if (!online_) {
        reason = "Offline";
    } else {
        fetches_.fetch_add(1);
        int last_percent = -1;
        FetchResult r = options_.fetcher->fetch(
            entry.url, info.hosts, kMaxFileBytes, [&](std::uint64_t got, std::uint64_t total) {
                std::lock_guard lock(mutex_);
                Slot& s = slots_[at];
                if (s.cancel || stop_ || !online_) {
                    return false;
                }
                const int percent =
                    total > 0 ? static_cast<int>(100 * std::min(got, total) / total) : -1;
                if (percent != last_percent) {
                    last_percent = percent;
                    s.file.progress = percent < 0 ? -1.0f : static_cast<float>(percent) / 100.0f;
                    ++files_serial_;
                }
                return true;
            });
        if (r.ok) {
            if (!looks_like(catalogue_file_extension(entry), r.body)) {
                reason = "The file is not a song";
            } else if (!write_file(cached, r.body)) {
                reason = "The file could not be saved";
            } else {
                bytes = std::move(r.body);
                ok = true;
                sweep_cache();
            }
        } else {
            cancelled = r.cancelled;
            reason = r.reason;
        }
    }
    fs::path kept;
    if (ok) {
        std::lock_guard lock(mutex_);
        keep = slots_[at].file.keep && !slots_[at].cancel;
    }
    if (ok && keep) {
        // Copied under its final name beside the cache, then moved into the catalogue's folder
        // (numbered when the name is taken there; never over a file).
        const fs::path staging = options_.cache_dir / "keep" / catalogue_file_name(entry);
        fs::create_directories(staging.parent_path(), ec);
        fs::remove(staging, ec);
        if (!write_file(staging, bytes)) {
            ok = false;
            reason = "The file could not be saved";
        } else {
            std::string error;
            const MoveDone done = move_into(staging, options_.songs_dir / std::string(info.group),
                                            &error);
            if (done.outcome == MoveOutcome::Failed) {
                fs::remove(staging, ec);
                ok = false;
                reason = "The song could not be put in the Songs folder";
            } else {
                kept = done.now;
            }
        }
    }
    std::lock_guard lock(mutex_);
    Slot& s = slots_[at];
    if (s.key != key) {
        return ok;
    }
    if (cancelled || (s.cancel && !ok)) {
        s.file = {};
        s.key = 0;
    } else if (!ok) {
        s.file.state = CatalogueFile::State::Failed;
        s.reason = reason;
        s.file.keep = false;
    } else if (keep) {
        s.file.state = CatalogueFile::State::Kept;
        s.path = kept;
        downloads_.push_back({entry, kept, content_hash(bytes)}); wake_loop(WakeSource::Catalogue);
    } else {
        s.file.state = CatalogueFile::State::Ready;
        s.path = cached;
    }
    s.cancel = false;
    s.file.progress = ok ? 1.0f : -1.0f;
    ++files_serial_;
    return ok;
}

void CatalogueService::sweep_cache() {
    struct Cached {
        fs::path path;
        std::uint64_t bytes = 0;
        fs::file_time_type used;
    };
    std::vector<fs::path> held;
    {
        std::lock_guard lock(mutex_);
        for (const Slot& s : slots_) {
            if (s.key != 0) {
                held.push_back(cache_file(s.entry));
            }
        }
    }
    std::vector<Cached> files;
    std::uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(options_.cache_dir / "files", ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) {
            continue;
        }
        Cached c{it->path(), it->file_size(e2), it->last_write_time(e2)};
        if (e2) {
            continue;
        }
        total += c.bytes;
        if (std::find(held.begin(), held.end(), c.path) == held.end()) {
            files.push_back(std::move(c));
        }
    }
    if (total <= options_.cache_cap_bytes) {
        return;
    }
    std::sort(files.begin(), files.end(),
              [](const Cached& a, const Cached& b) { return a.used < b.used; });
    for (const Cached& c : files) {
        if (total <= options_.cache_cap_bytes) {
            break;
        }
        if (fs::remove(c.path, ec)) {
            total -= c.bytes;
        }
    }
}

void CatalogueService::net_main() {
    read_caches();
    sweep_cache();
    std::unique_lock lock(mutex_);
    caches_read_ = true;
    busy_ = false;
    while (!stop_) {
        // What is due: a file, else the first stale index whose retry time has come.
        std::optional<CatalogueId> due;
        const std::int64_t now = options_.now_utc_s();
        if (online_ && file_queue_.empty()) {
            for (const CatalogueInfo& info : catalogue_infos()) {
                if (!info.enabled) {
                    continue;
                }
                const auto i = static_cast<std::size_t>(info.id);
                const bool stale = refresh_all_ || shared_states_[i].kind ==
                                                       CatalogueIndexState::Kind::None ||
                                   catalogue_stale(shared_states_[i].fetched_utc_s, now);
                if (stale && now >= next_try_utc_s_[i]) {
                    due = info.id;
                    break;
                }
            }
            if (!due) {
                refresh_all_ = false;
            }
        }
        if (file_queue_.empty() && !due) {
            net_wake_.wait_for(lock, std::chrono::seconds(30));
            continue;
        }
        busy_ = true;
        if (std::size_t slot = 0; pop_file(slot)) {
            lock.unlock();
            run_file(slot);
            lock.lock();
            busy_ = false;
            continue;
        }
        if (!due) {
            busy_ = false;
            continue; // the queue held only cancelled files
        }
        const CatalogueId id = *due;
        const auto i = static_cast<std::size_t>(id);
        // The next try waits, whatever this one gives (a success is fresh for the refresh age).
        next_try_utc_s_[i] = now + kCatalogueRetryS;
        shared_states_[i].kind = CatalogueIndexState::Kind::Fetching;
        ++files_serial_;
        lock.unlock();
        std::vector<CatalogueEntry> entries;
        std::string reason;
        if (fetch_index(id, entries, reason)) {
            const std::int64_t fetched = options_.now_utc_s();
            write_file(index_file(id), write_catalogue_cache(id, fetched, entries));
            publish(id, std::move(entries), fetched, CatalogueIndexState::Kind::Ready, {});
        } else {
            publish(id, {}, 0, CatalogueIndexState::Kind::Failed, std::move(reason));
        }
        lock.lock();
        busy_ = false;
    }
}

} // namespace etude::app
