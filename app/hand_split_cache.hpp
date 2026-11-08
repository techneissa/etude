// SPDX-License-Identifier: GPL-3.0-or-later
// A cache of hand splits by what a file holds. std only, so the tests use it.
#pragma once

#include "etude/hand_split.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace etude::app {

// The hand splits of the songs loaded so far, by what the file holds (its content hash, and for an
// ABC tune whether chord symbols became a left hand) and the options the split was made with. The
// library's scan splits a file to make its entry and opening it parses it and would split it
// again: the second takes the first's answer. A player who moves the split point gets one split
// per point, made once: going back to a point already tried finds it here.
// The split is put on the new parse only when it was made from the same notes (apply_hand_split
// checks), so a file that changed, or a hash clash, costs a split and never a wrong hand. Bounded
// by the notes it holds; the oldest go first.
class HandSplitCache {
  public:
    // The file's content hash, the ABC chord setting and every option the split depends on (the
    // defaults for Auto): the same file split another way is another entry.
    struct Key {
        std::uint64_t hash = 0;
        bool abc_chords = false;
        SplitMethod method = SplitMethod::Smart;
        int split_point = kMiddleC;
        bool single_lines_whole = true;
        bool staves_decide = true;
        bool by_pitch = false;
        friend auto operator<=>(const Key&, const Key&) = default;
    };
    static Key key_of(std::uint64_t hash, bool abc_chords, const HandSplitOptions& options) {
        return {hash,
                abc_chords,
                options.method,
                options.split_point,
                options.single_lines_whole,
                options.staves_decide,
                options.by_pitch};
    }

    std::shared_ptr<const HandSplit> find(const Key& key) const {
        const std::lock_guard lock(mutex_);
        const auto it = entries_.find(key);
        return it == entries_.end() ? nullptr : it->second;
    }

    void put(const Key& key, std::shared_ptr<const HandSplit> split) {
        const std::lock_guard lock(mutex_);
        // A key that is there holds a split that no longer fits the file (a hash clash, or the
        // fingerprint refused it): the new one takes its place.
        if (const auto it = entries_.find(key); it != entries_.end()) {
            notes_ -= it->second->note_hands.size();
            entries_.erase(it);
            order_.erase(std::find(order_.begin(), order_.end(), key));
        }
        entries_.emplace(key, split);
        order_.push_back(key);
        notes_ += split->note_hands.size();
        while (notes_ > kMaxNotes && order_.size() > 1) {
            const auto old = entries_.find(order_.front());
            notes_ -= old->second->note_hands.size();
            entries_.erase(old);
            order_.erase(order_.begin());
        }
    }

  private:
    // Room for about 2 million notes (a byte for a hand and a bit for the accompaniment flag each):
    // more than a library of a thousand songs.
    static constexpr std::size_t kMaxNotes = 2'000'000;
    mutable std::mutex mutex_;
    std::map<Key, std::shared_ptr<const HandSplit>> entries_;
    std::vector<Key> order_;
    std::size_t notes_ = 0;
};

} // namespace etude::app
