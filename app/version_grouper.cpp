// SPDX-License-Identifier: GPL-3.0-or-later
#include "version_grouper.hpp"

#include <utility>

namespace etude::app {

std::shared_ptr<const PieceMap> piece_map(const VersionIndex& index) {
    auto pieces = std::make_shared<PieceMap>();
    for (const std::vector<VersionIndex::Member>& group : index.groups) {
        std::uint64_t key = ~std::uint64_t{0};
        for (const VersionIndex::Member& member : group) {
            key = std::min(key, member.hash);
        }
        for (const VersionIndex::Member& member : group) {
            (*pieces)[member.hash] = key;
        }
    }
    return pieces;
}

std::shared_ptr<const VersionIndex> group_by_hash(std::vector<VersionInput> songs,
                                                  const VersionOverrides& overrides) {
    // One song for each hash.
    {
        std::unordered_map<std::uint64_t, bool> seen;
        seen.reserve(songs.size());
        std::size_t kept = 0;
        for (std::size_t i = 0; i < songs.size(); ++i) {
            if (songs[i].hash != 0 && seen.emplace(songs[i].hash, true).second) {
                if (kept != i) {
                    songs[kept] = std::move(songs[i]);
                }
                ++kept;
            }
        }
        songs.resize(kept);
    }
    std::vector<VersionSong> bare;
    bare.reserve(songs.size());
    for (VersionInput& s : songs) {
        bare.push_back(std::move(s.song));
    }
    auto index = std::make_shared<VersionIndex>();
    // The corrections as places in the list.
    VersionLinks links;
    if (!overrides.empty()) {
        std::unordered_map<std::uint64_t, std::uint32_t> place;
        place.reserve(songs.size());
        for (std::uint32_t i = 0; i < songs.size(); ++i) {
            place.emplace(songs[i].hash, i);
        }
        for (const std::uint64_t h : overrides.splits) {
            if (const auto it = place.find(h); it != place.end()) {
                links.splits.push_back(it->second);
            }
        }
        for (const auto& [a, b] : overrides.joins) {
            const auto ia = place.find(a);
            const auto ib = place.find(b);
            if (ia != place.end() && ib != place.end()) {
                links.joins.emplace_back(ia->second, ib->second);
            }
        }
    }
    const std::vector<VersionGroup> groups = group_versions(bare, links);
    index->groups.reserve(groups.size());
    for (const VersionGroup& g : groups) {
        std::vector<VersionIndex::Member>& out = index->groups.emplace_back();
        for (const VersionMember& m : g.members) {
            out.push_back({songs[m.song].hash, m.name, m.labelled});
            index->where[songs[m.song].hash] = {
                static_cast<std::uint32_t>(index->groups.size() - 1),
                static_cast<std::uint32_t>(out.size() - 1)};
        }
    }
    return index;
}

VersionGrouper::VersionGrouper() : thread_([this] { run(); }) {}

VersionGrouper::~VersionGrouper() {
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

void VersionGrouper::request(std::vector<VersionInput> songs, VersionOverrides overrides) {
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(songs);
        pending_overrides_ = std::move(overrides);
        has_pending_ = true;
    }
    cv_.notify_one();
}

std::shared_ptr<const VersionIndex> VersionGrouper::take() {
    std::lock_guard lock(mutex_);
    return std::exchange(done_, nullptr);
}

bool VersionGrouper::busy() const {
    std::lock_guard lock(mutex_);
    return has_pending_ || running_;
}

void VersionGrouper::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_cv_.wait(lock, [this] { return !has_pending_ && !running_; });
}

void VersionGrouper::run() {
    std::unique_lock lock(mutex_);
    for (;;) {
        cv_.wait(lock, [this] { return quit_ || has_pending_; });
        if (quit_) {
            return;
        }
        std::vector<VersionInput> songs = std::move(pending_);
        VersionOverrides overrides = std::move(pending_overrides_);
        pending_.clear();
        pending_overrides_ = {};
        has_pending_ = false;
        running_ = true;
        lock.unlock();
        std::shared_ptr<const VersionIndex> index = group_by_hash(std::move(songs), overrides);
        lock.lock();
        done_ = std::move(index);
        running_ = false;
        idle_cv_.notify_all();
    }
}

} // namespace etude::app
