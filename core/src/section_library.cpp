// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sections.hpp"

#include "etude/content_hash.hpp"

#include <algorithm>
#include <limits>

namespace etude {

// ---- SectionLibrary

const SongSections* SectionLibrary::find(std::uint64_t song_hash) const {
    for (const SongSections& s : songs_) {
        if (s.song_hash == song_hash) {
            return &s;
        }
    }
    return nullptr;
}

SongSections* SectionLibrary::edit(std::uint64_t song_hash) {
    if (song_hash == 0) {
        return nullptr;
    }
    if (clock_ == std::numeric_limits<std::uint32_t>::max()) {
        // Renumber in use order so the counter never wraps.
        std::stable_sort(
            songs_.begin(), songs_.end(),
            [](const SongSections& a, const SongSections& b) { return a.last_used < b.last_used; });
        clock_ = 0;
        for (SongSections& s : songs_) {
            s.last_used = ++clock_;
        }
    }
    auto matches = [&](const SongSections& s) { return s.song_hash == song_hash; };
    auto it = std::find_if(songs_.begin(), songs_.end(), matches);
    if (it == songs_.end()) {
        songs_.emplace_back();
        songs_.back().song_hash = song_hash;
    }
    std::find_if(songs_.begin(), songs_.end(), matches)->last_used = ++clock_;
    trim(); // never removes the entry just used: its counter is the highest
    return &*std::find_if(songs_.begin(), songs_.end(), matches);
}

void SectionLibrary::put(SongSections entry) {
    if (entry.song_hash == 0) {
        return;
    }
    if (entry.sections.empty() && entry.plan.empty()) {
        erase(entry.song_hash);
        return;
    }
    clock_ = std::max(clock_, entry.last_used);
    for (SongSections& s : songs_) {
        if (s.song_hash == entry.song_hash) {
            s = std::move(entry);
            return;
        }
    }
    songs_.push_back(std::move(entry));
    trim();
}

void SectionLibrary::erase(std::uint64_t song_hash) {
    auto it = std::find_if(songs_.begin(), songs_.end(),
                           [&](const SongSections& s) { return s.song_hash == song_hash; });
    if (it == songs_.end()) {
        return;
    }
    stale_.push_back(std::string(kSectionsSectionPrefix) + content_hash_hex(song_hash));
    songs_.erase(it);
}

void SectionLibrary::trim() {
    while (songs_.size() > kMaxSongsWithSections) {
        auto oldest = std::min_element(
            songs_.begin(), songs_.end(),
            [](const SongSections& a, const SongSections& b) { return a.last_used < b.last_used; });
        stale_.push_back(std::string(kSectionsSectionPrefix) + content_hash_hex(oldest->song_hash));
        songs_.erase(oldest);
    }
}


} // namespace etude
