// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue_service.hpp"
#include "crash_thread.hpp"
#include "loop_wake.hpp"

#include "library_files.hpp"

#include "etude/content_hash.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

std::int64_t system_now_utc_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

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

// A name the file system takes for an id ("2247", "Schubert/op.2/1").
std::string safe_name(std::string_view id) {
    std::string out;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '-' || c == '_' || c == '.';
        out += ok ? c : '_';
    }
    if (out.empty() || out[0] == '.') {
        out.insert(out.begin(), '_');
    }
    return out;
}

} // namespace

std::uint64_t catalogue_entry_key(CatalogueId source, std::string_view id) {
    const auto s = static_cast<std::uint8_t>(source);
    return content_hash_extend(content_hash(&s, 1), id.data(), id.size());
}

CatalogueService::CatalogueService(Options options) : options_(std::move(options)) {
    if (!options_.now_utc_s) {
        options_.now_utc_s = system_now_utc_s;
    }
    hits_.reserve(options_.max_hits);
    hits_query_.reserve(kMaxQueryBytes);
    query_.reserve(kMaxQueryBytes);
    shared_hits_.reserve(options_.max_hits);
    shared_hits_query_.reserve(kMaxQueryBytes);
    for (Slot& s : view_slots_) {
        s.reason.reserve(64);
    }
}

CatalogueService::~CatalogueService() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        for (Slot& s : slots_) {
            s.cancel = true;
        }
    }
    search_wake_.notify_all();
    net_wake_.notify_all();
    if (search_thread_.joinable()) {
        search_thread_.join();
    }
    if (net_thread_.joinable()) {
        net_thread_.join();
    }
}

void CatalogueService::start() {
    if (net_thread_.joinable()) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        for (const CatalogueInfo& info : catalogue_infos()) {
            if (info.enabled) {
                shared_states_[static_cast<std::size_t>(info.id)].kind =
                    CatalogueIndexState::Kind::Reading;
            }
        }
        busy_ = true;
    }
    search_thread_ = spawn_thread([this] { search_main(); });
    net_thread_ = spawn_thread([this] { net_main(); });
}

void CatalogueService::set_online(bool on) {
    if (online_.exchange(on) == on) {
        return;
    }
    if (!on) {
        std::lock_guard lock(mutex_);
        for (Slot& s : slots_) {
            s.cancel = s.file.state == CatalogueFile::State::Queued ||
                       s.file.state == CatalogueFile::State::Fetching;
        }
    }
    net_wake_.notify_all();
}

void CatalogueService::refresh() {
    {
        std::lock_guard lock(mutex_);
        refresh_all_ = true;
        next_try_utc_s_.fill(0);
    }
    net_wake_.notify_all();
}

void CatalogueService::search(std::string_view query) {
    query = query.substr(0, kMaxQueryBytes);
    {
        std::lock_guard lock(mutex_);
        if (query == query_) {
            return;
        }
        query_.assign(query.data(), query.size());
        ++query_serial_;
        query_at_ = std::chrono::steady_clock::now();
        searching_ = true;
    }
    search_wake_.notify_one();
}

bool CatalogueService::searching() const {
    std::lock_guard lock(mutex_);
    return searching_;
}

bool CatalogueService::poll() {
    bool changed = false;
    std::lock_guard lock(mutex_);
    if (hits_serial_ != taken_hits_) {
        taken_hits_ = hits_serial_;
        // The hits index the snapshot they were found in: both are taken together. The old one
        // is let go on the search thread, so its songs are not freed in a frame.
        if (snapshot_ != shared_hits_snapshot_ && snapshot_ && !retired_) {
            retired_ = std::move(snapshot_);
            search_wake_.notify_one();
        }
        snapshot_ = shared_hits_snapshot_;
        hits_.assign(shared_hits_.begin(), shared_hits_.end());
        hits_query_.assign(shared_hits_query_);
        last_search_ns_ = shared_search_ns_;
        changed = true;
    }
    for (std::size_t i = 0; i < kCatalogueCount; ++i) {
        CatalogueIndexState& mine = states_[i];
        const CatalogueIndexState& theirs = shared_states_[i];
        if (mine.kind != theirs.kind || mine.songs != theirs.songs ||
            mine.fetched_utc_s != theirs.fetched_utc_s || mine.reason != theirs.reason) {
            mine.kind = theirs.kind;
            mine.songs = theirs.songs;
            mine.fetched_utc_s = theirs.fetched_utc_s;
            mine.reason = theirs.reason;
            changed = true;
        }
    }
    if (files_serial_ != taken_files_) {
        taken_files_ = files_serial_;
        for (std::size_t i = 0; i < kSlots; ++i) {
            Slot& v = view_slots_[i];
            const Slot& s = slots_[i];
            // The reason and path follow the slot, not only a change of state: a retry that fails
            // again between two polls is Failed both times, with a new reason.
            const bool text_changed =
                v.key != s.key || v.file.state != s.file.state || v.reason != s.reason ||
                v.path != s.path;
            v.key = s.key;
            v.file = s.file;
            if (text_changed) {
                v.reason = s.reason;
                v.path = s.path;
            }
        }
        changed = true;
    }
    return changed;
}

std::uint64_t CatalogueService::key_of(std::uint32_t entry) const {
    if (!snapshot_ || entry >= snapshot_->keys.size()) {
        return 0;
    }
    return snapshot_->keys[entry];
}

CatalogueService::Slot* CatalogueService::slot_of(std::uint64_t key) {
    for (Slot& s : slots_) {
        if (s.key == key && key != 0) {
            return &s;
        }
    }
    return nullptr;
}

const CatalogueService::Slot* CatalogueService::slot_of(std::uint64_t key) const {
    for (const Slot& s : view_slots_) {
        if (s.key == key && key != 0) {
            return &s;
        }
    }
    return nullptr;
}

void CatalogueService::preview(std::uint32_t entry) { queue(entry, false); }

void CatalogueService::download(std::uint32_t entry) { queue(entry, true); }

void CatalogueService::queue(std::uint32_t entry, bool keep) {
    const std::uint64_t key = key_of(entry);
    if (key == 0) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        Slot* s = slot_of(key);
        if (s) {
            s->used = ++use_clock_;
            using State = CatalogueFile::State;
            const State state = s->file.state;
            if (state == State::Kept) {
                return;
            }
            if (keep && !s->file.keep) {
                s->file.keep = true;
                ++files_serial_;
            }
            // Under way: it reads `keep` once the file is in. Fetched for its preview: only the
            // copy is left to do.
            if (state == State::Queued || state == State::Fetching ||
                (state == State::Ready && !keep)) {
                return;
            }
        } else {
            // A slot not in use, else the oldest settled one (never one at work).
            for (Slot& c : slots_) {
                if (c.key == 0) {
                    s = &c;
                    break;
                }
            }
            if (!s) {
                for (Slot& c : slots_) {
                    if ((c.file.state == CatalogueFile::State::Ready ||
                         c.file.state == CatalogueFile::State::Failed ||
                         c.file.state == CatalogueFile::State::Kept) &&
                        (s == nullptr || c.used < s->used)) {
                        s = &c;
                    }
                }
            }
            if (!s) {
                return;
            }
            s->key = key;
            s->entry = snapshot_->entries[entry];
            s->file = {};
            s->file.keep = keep;
        }
        s->used = ++use_clock_;
        s->file.state = CatalogueFile::State::Queued;
        s->file.progress = -1.0f;
        s->reason.clear();
        s->path.clear();
        s->cancel = false;
        file_queue_.push_back(static_cast<std::size_t>(s - slots_.data()));
        ++files_serial_;
    }
    net_wake_.notify_all();
}

void CatalogueService::cancel(std::uint32_t entry) {
    const std::uint64_t key = key_of(entry);
    std::lock_guard lock(mutex_);
    if (Slot* s = slot_of(key)) {
        if (s->file.state == CatalogueFile::State::Queued ||
            s->file.state == CatalogueFile::State::Fetching) {
            s->cancel = true;
            s->file.keep = false;
            if (s->file.state == CatalogueFile::State::Queued) {
                const auto at = static_cast<std::size_t>(s - slots_.data());
                file_queue_.erase(std::remove(file_queue_.begin(), file_queue_.end(), at),
                                  file_queue_.end());
                s->file = {};
                s->key = 0;
            }
            ++files_serial_;
        }
    }
}

CatalogueFile CatalogueService::file(std::uint32_t entry) const {
    const Slot* s = slot_of(key_of(entry));
    return s ? s->file : CatalogueFile{};
}

std::string_view CatalogueService::file_reason(std::uint32_t entry) const {
    const Slot* s = slot_of(key_of(entry));
    return s && s->file.state == CatalogueFile::State::Failed ? std::string_view(s->reason)
                                                              : std::string_view();
}

fs::path CatalogueService::file_path(std::uint32_t entry) const {
    const Slot* s = slot_of(key_of(entry));
    return s && (s->file.state == CatalogueFile::State::Ready ||
                 s->file.state == CatalogueFile::State::Kept)
               ? s->path
               : fs::path();
}

std::optional<CatalogueDownload> CatalogueService::take_download() {
    std::lock_guard lock(mutex_);
    if (downloads_.empty()) {
        return std::nullopt;
    }
    CatalogueDownload d = std::move(downloads_.front());
    downloads_.pop_front();
    return d;
}

void CatalogueService::forget(std::uint64_t key) {
    std::lock_guard lock(mutex_);
    if (Slot* s = slot_of(key); s && s->file.state == CatalogueFile::State::Kept) {
        s->file = {};
        s->key = 0;
        ++files_serial_;
    }
}

bool CatalogueService::wait_idle(std::chrono::milliseconds timeout) {
    const auto until = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < until) {
        {
            std::lock_guard lock(mutex_);
            if (!busy_ && file_queue_.empty() && !searching_) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

fs::path CatalogueService::index_file(CatalogueId id) const {
    return options_.cache_dir / "indexes" / (std::string(catalogue_info(id).key) + ".tsv");
}

fs::path CatalogueService::cache_file(const CatalogueEntry& e) const {
    return options_.cache_dir / "files" / std::string(catalogue_info(e.source).key) /
           (safe_name(e.id) + catalogue_file_extension(e));
}

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

bool fetch_catalogue_index(Fetcher& fetcher, CatalogueId id, std::vector<CatalogueEntry>& out,
                           std::string& reason, const IndexHooks& hooks) {
    const CatalogueInfo& info = catalogue_info(id);
    auto get = [&](const std::string& url, std::string& body) {
        if (hooks.before_request && !hooks.before_request()) {
            reason = "Offline";
            return false;
        }
        FetchResult r = fetcher.fetch(url, info.hosts, CatalogueService::kMaxPageBytes,
                                      [&](std::uint64_t, std::uint64_t) {
                                          return !hooks.still_wanted || hooks.still_wanted();
                                      });
        if (!r.ok) {
            reason = r.reason;
            return false;
        }
        body = std::move(r.body);
        return true;
    };
    out.clear();
    std::string body;
    switch (id) {
    case CatalogueId::Mutopia: {
        constexpr std::size_t kMaxPages = 200;
        for (std::size_t page = 0; page < kMaxPages; ++page) {
            if (!get(mutopia_page_url(page * kMutopiaPageSize), body)) {
                return false;
            }
            const std::size_t before = out.size();
            bool more = false;
            parse_mutopia_page(body, out, more);
            if (!more || (out.size() == before && page > 0)) {
                break;
            }
            if (hooks.partial && page % 5 == 4) {
                hooks.partial(out);
            }
        }
        break;
    }
    case CatalogueId::PianoMidi: {
        if (!get(std::string(piano_midi_start_url()), body)) {
            return false;
        }
        const std::vector<std::string> pages = parse_piano_midi_composers(body);
        for (const std::string& page : pages) {
            if (!get(page, body)) {
                return false;
            }
            parse_piano_midi_composer(body, page, out);
        }
        break;
    }
    case CatalogueId::OpenScoreLieder: {
        if (!get(std::string(openscore_lieder_index_url()), body)) {
            return false;
        }
        parse_openscore_lieder(body, out);
        break;
    }
    }
    if (out.empty()) {
        reason = "The catalogue's listing could not be read";
        return false;
    }
    return true;
}

bool CatalogueService::fetch_index(CatalogueId id, std::vector<CatalogueEntry>& out,
                                   std::string& reason) {
    IndexHooks hooks;
    // Files the player asked for go first: before each page the thread serves them.
    hooks.before_request = [&] {
        while (true) {
            std::size_t slot = 0;
            {
                std::lock_guard lock(mutex_);
                if (stop_ || !pop_file(slot)) {
                    break;
                }
            }
            run_file(slot);
        }
        fetches_.fetch_add(1);
        std::lock_guard lock(mutex_);
        return !stop_ && online_.load();
    };
    hooks.still_wanted = [&] {
        std::lock_guard lock(mutex_);
        return !stop_ && online_.load();
    };
    // While there is no cache, the songs found so far are searched already.
    hooks.partial = [&](const std::vector<CatalogueEntry>& so_far) {
        if (indexes_[static_cast<std::size_t>(id)].size() <= so_far.size()) {
            publish(id, so_far, 0, CatalogueIndexState::Kind::Fetching, {});
        }
    };
    return fetch_catalogue_index(*options_.fetcher, id, out, reason, hooks);
}

int run_get_songs_check() {
    CurlFetcher fetcher;
    int failed = 0;
    for (const CatalogueInfo& info : catalogue_infos()) {
        if (!info.enabled) {
            std::printf("%-17s off\n", std::string(info.name).c_str());
            continue;
        }
        std::vector<CatalogueEntry> entries;
        std::string reason;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = fetch_catalogue_index(fetcher, info.id, entries, reason, {});
        const double s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (ok) {
            const CatalogueEntry& e = entries.front();
            std::printf("%-17s %5zu songs in %5.1f s, first: %s - %s (%s) %s\n",
                        std::string(info.name).c_str(), entries.size(), s, e.composer.c_str(),
                        e.title.c_str(), e.licence.c_str(), e.url.c_str());
        } else {
            ++failed;
            std::printf("%-17s failed after %.1f s: %s\n", std::string(info.name).c_str(), s,
                        reason.c_str());
        }
    }
    return failed == 0 ? 0 : 1;
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
