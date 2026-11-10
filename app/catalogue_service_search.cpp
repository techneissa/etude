// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue_service.hpp"
#include "loop_wake.hpp"

#include <chrono>

namespace etude::app {

void CatalogueService::search_main() {
    SearchIndex index;
    std::vector<SearchDoc> docs;
    std::shared_ptr<const CatalogueSnapshot> indexed;
    std::string query;
    query.reserve(kMaxQueryBytes);
    std::uint64_t searched_serial = 0;
    std::uint64_t searched_snapshot = 0;
    std::vector<CatalogueHit> found;
    found.reserve(options_.max_hits);
    std::unique_lock lock(mutex_);
    while (!stop_) {
        search_wake_.wait(lock, [&] {
            return stop_ || query_serial_ != searched_serial ||
                   snapshot_serial_ != searched_snapshot || retired_;
        });
        if (stop_) {
            break;
        }
        if (retired_) {
            std::shared_ptr<const CatalogueSnapshot> old = std::move(retired_);
            lock.unlock();
            old.reset();
            lock.lock();
            continue;
        }
        // The text must be still for the debounce before it is searched (an empty one at once).
        if (!query_.empty() && query_serial_ != searched_serial) {
            const auto ready = query_at_ + options_.debounce;
            if (std::chrono::steady_clock::now() < ready) {
                search_wake_.wait_until(lock, ready, [&] { return stop_; });
                continue; // look again: the text may have changed meanwhile
            }
        }
        const std::uint64_t serial = query_serial_;
        const std::uint64_t snap_serial = snapshot_serial_;
        query.assign(query_);
        std::shared_ptr<const CatalogueSnapshot> snap = shared_snapshot_;
        lock.unlock();

        if (snap != indexed) {
            docs.clear();
            if (snap) {
                docs.reserve(snap->entries.size());
                for (const CatalogueEntry& e : snap->entries) {
                    docs.push_back({e.title, e.composer, catalogue_info(e.source).name, e.detail, {}});
                }
            }
            index.build(docs);
            indexed = snap;
        }
        const auto t0 = std::chrono::steady_clock::now();
        found.clear();
        if (snap && !query.empty()) {
            for (const SearchHit& h : index.search(query)) {
                if (found.size() >= options_.max_hits) {
                    break;
                }
                found.push_back({h.song, h.rank});
            }
        }
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();

        lock.lock();
        searched_serial = serial;
        searched_snapshot = snap_serial;
        shared_hits_.assign(found.begin(), found.end());
        shared_hits_query_.assign(query);
        shared_search_ns_ = ns;
        shared_hits_snapshot_ = std::move(snap);
        ++hits_serial_;
        wake_loop(WakeSource::Catalogue);
        searching_ = query_serial_ != searched_serial;
    }
}

} // namespace etude::app
