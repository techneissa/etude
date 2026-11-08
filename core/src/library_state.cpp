// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_state.hpp"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace etude {

std::uint64_t file_stamp(std::uint64_t size, std::int64_t mtime) {
    // FNV-1a over the eight bytes of each number.
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFFu;
            h *= 1099511628211ull;
        }
    };
    mix(size);
    mix(static_cast<std::uint64_t>(mtime));
    return h;
}

std::size_t LibraryState::pending() const {
    return static_cast<std::size_t>(
        std::count_if(items.begin(), items.end(),
                      [](const LibraryItem& i) { return i.state == EntryState::Pending; }));
}
std::size_t LibraryState::loaded() const {
    return static_cast<std::size_t>(
        std::count_if(items.begin(), items.end(),
                      [](const LibraryItem& i) { return i.state == EntryState::Loaded; }));
}
std::size_t LibraryState::failed() const {
    return static_cast<std::size_t>(
        std::count_if(items.begin(), items.end(),
                      [](const LibraryItem& i) { return i.state == EntryState::Failed; }));
}

namespace {

// "X=3" or "X=3.2" as the pair (3, 2); (0, 0) for an empty address.
std::pair<long long, long long> tune_order(std::string_view tune) {
    long long number = 0;
    long long nth = tune.empty() ? 0 : 1;
    std::size_t i = 2;
    while (i < tune.size() && tune[i] >= '0' && tune[i] <= '9') {
        number = std::min(number * 10 + (tune[i] - '0'), 1'000'000'000LL);
        ++i;
    }
    if (i < tune.size() && tune[i] == '.') {
        nth = 0;
        for (++i; i < tune.size() && tune[i] >= '0' && tune[i] <= '9'; ++i) {
            nth = std::min(nth * 10 + (tune[i] - '0'), 1'000'000'000LL);
        }
    }
    return {number, nth};
}

} // namespace

LibraryState list_state(std::span<const LibraryFile> files, const LibraryState* previous) {
    // The tunes of one ABC file ("a.abc#X=2") sit together after the file's name, in tune order
    // (X=2 before X=10).
    struct Keyed {
        std::string folded;
        std::string file;
        std::pair<long long, long long> tune;
        const LibraryFile* source;
    };
    std::vector<Keyed> keyed;
    keyed.reserve(files.size());
    for (const LibraryFile& f : files) {
        const AbcLocation where = split_abc_location(f.relative);
        keyed.push_back(
            {fold_for_search(where.file), std::string(where.file), tune_order(where.tune), &f});
    }
    std::sort(keyed.begin(), keyed.end(), [](const Keyed& a, const Keyed& b) {
        if (a.folded != b.folded) {
            return a.folded < b.folded;
        }
        if (a.file != b.file) {
            return a.file < b.file;
        }
        if (a.tune != b.tune) {
            return a.tune < b.tune;
        }
        return a.source->relative < b.source->relative;
    });

    std::map<std::string, const LibraryItem*> before;
    if (previous != nullptr) {
        for (const LibraryItem& i : previous->items) {
            before[i.relative] = &i;
        }
    }
    LibraryState out;
    out.items.reserve(keyed.size());
    for (const Keyed& k : keyed) {
        LibraryItem item;
        item.relative = k.source->relative;
        item.stamp = file_stamp(k.source->size, k.source->mtime);
        const auto it = before.find(item.relative);
        if (it != before.end() && it->second->stamp == item.stamp &&
            it->second->state != EntryState::Pending) {
            item = *it->second;
        }
        out.items.push_back(std::move(item));
    }
    return out;
}

std::optional<std::size_t> next_pending(const LibraryState& state, std::size_t from) {
    for (std::size_t i = from; i < state.items.size(); ++i) {
        if (state.items[i].state == EntryState::Pending) {
            return i;
        }
    }
    return std::nullopt;
}

namespace {
LibraryItem* current_item(LibraryState& state, std::size_t index, std::uint64_t stamp) {
    if (index >= state.items.size() || state.items[index].stamp != stamp) {
        return nullptr;
    }
    return &state.items[index];
}
} // namespace

bool apply_loaded(LibraryState& state, std::size_t index, std::uint64_t stamp, SongEntry entry) {
    LibraryItem* item = current_item(state, index, stamp);
    if (item == nullptr) {
        return false;
    }
    item->state = EntryState::Loaded;
    item->entry = std::move(entry);
    item->error.clear();
    return true;
}

bool apply_failed(LibraryState& state, std::size_t index, std::uint64_t stamp, std::string error) {
    LibraryItem* item = current_item(state, index, stamp);
    if (item == nullptr) {
        return false;
    }
    item->state = EntryState::Failed;
    item->entry = SongEntry{};
    item->error = std::move(error);
    return true;
}

std::optional<std::size_t> step_item(const LibraryState& state, std::optional<std::size_t> current,
                                     int step) {
    const std::size_t n = state.items.size();
    const auto usable = [&](std::size_t i) { return state.items[i].state != EntryState::Failed; };
    if (n == 0) {
        return std::nullopt;
    }
    if (!current || *current >= n) {
        for (std::size_t i = 0; i < n; ++i) {
            if (usable(i)) {
                return i;
            }
        }
        return std::nullopt;
    }
    const long dir = step < 0 ? -1 : 1;
    long at = static_cast<long>(*current);
    const long len = static_cast<long>(n);
    // `step` songs, wrapping, passing over failed ones; a step of 0 stays put.
    for (int moved = 0; moved < std::abs(step); ++moved) {
        bool found = false;
        for (long tries = 0; tries < len; ++tries) {
            at = ((at + dir) % len + len) % len;
            if (usable(static_cast<std::size_t>(at))) {
                found = true;
                break;
            }
        }
        if (!found) {
            return usable(*current) ? current : std::nullopt;
        }
    }
    return static_cast<std::size_t>(at);
}

const SongEntry* EntryCache::find(std::uint64_t content_hash, std::string_view file_name) const {
    const auto it = entries_.find(content_hash);
    return it == entries_.end() || it->second.file_name != file_name ? nullptr : &it->second.entry;
}

void EntryCache::insert(std::uint64_t content_hash, std::string_view file_name,
                        const SongEntry& entry, std::string_view source) {
    if (!source.empty()) {
        // A source whose bytes changed leaves its old parse behind under the old hash: drop it.
        const auto owned = owners_.find(std::string(source));
        if (owned != owners_.end()) {
            if (owned->second != content_hash) {
                entries_.erase(owned->second);
            }
            owned->second = content_hash;
        } else {
            owners_.emplace(std::string(source), content_hash);
        }
    }
    if (entries_.size() >= kMaxEntries && entries_.find(content_hash) == entries_.end()) {
        // A library past the cap re-parses what it needs; nothing grows without end.
        entries_.clear();
        owners_.clear();
        if (!source.empty()) {
            owners_.emplace(std::string(source), content_hash);
        }
    }
    entries_[content_hash] = Slot{std::string(file_name), entry};
}

} // namespace etude
