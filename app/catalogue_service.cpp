// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue_service.hpp"
#include "crash_thread.hpp"

#include "etude/content_hash.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

std::int64_t system_now_utc_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
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

} // namespace etude::app
