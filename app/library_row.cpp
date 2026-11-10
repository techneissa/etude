// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_row.hpp"

#include "etude/library_browse.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <unordered_set>
#include <utility>

namespace etude::app {

namespace {

// How a song ranks for a place in its step, best first: not yet passed, a favourite, then the
// library's folded title, then its place.
struct Ranked {
    std::size_t index = 0;
    bool passed = false;
    bool favourite = false;
};

// Where each entry of the added list is in the library.
struct Found {
    std::vector<std::optional<std::size_t>> index; // by entry: the catalogue's place, none for hidden
    std::vector<LibraryRowFollow> followed;
};

// Finds each entry by its hash in one pass over the songs (a sorted list of the entries' hashes is
// searched for each), then the rest by their path.
Found find_added(const LibraryCatalogue& cat, const MelodyAdded& added) {
    Found found;
    found.index.assign(added.size(), std::nullopt);
    std::vector<std::pair<std::uint64_t, std::size_t>> keys;
    keys.reserve(added.size());
    for (std::size_t e = 0; e < added.size(); ++e) {
        keys.emplace_back(added.at(e).hash, e);
    }
    std::sort(keys.begin(), keys.end());
    if (!keys.empty()) {
        for (std::size_t i = 0; i < cat.size(); ++i) {
            const CatalogueSong& s = cat.song(i);
            if (s.state != EntryState::Loaded || s.hash == 0) {
                continue;
            }
            const auto it = std::lower_bound(keys.begin(), keys.end(), std::pair{s.hash, std::size_t{0}});
            if (it != keys.end() && it->first == s.hash && !found.index[it->second]) {
                found.index[it->second] = i;
            }
        }
    }
    for (std::size_t e = 0; e < added.size(); ++e) {
        const MelodyAddedSong& a = added.at(e);
        if (found.index[e] || a.path.empty()) {
            continue;
        }
        // An edited file has a new hash and the same path: the entry follows it.
        if (const std::optional<std::size_t> at = cat.index_of(a.path)) {
            const CatalogueSong& s = cat.song(*at);
            if (s.state == EntryState::Loaded && s.hash != 0) {
                found.index[e] = *at;
                found.followed.push_back({a.hash, s.hash, a.path});
            }
        }
    }
    return found;
}

// Whether song `i` is the one that stands for its piece: the version the player added (any member of
// the piece's group in `shown`, sorted), else the one practised most, ties to the easiest (versions
// come easiest first).
bool stands_for_piece(const LibraryCatalogue& cat, std::size_t i, const std::vector<std::size_t>& shown) {
    const std::span<const std::uint32_t> versions = cat.versions_of(i);
    if (versions.empty()) {
        return true;
    }
    for (const std::uint32_t v : versions) {
        if (std::binary_search(shown.begin(), shown.end(), static_cast<std::size_t>(v))) {
            return false; // the player's own version of this piece has its tile
        }
    }
    // Among the versions that fit a step: the most practised, the easiest on a tie.
    std::optional<std::uint32_t> best;
    for (const std::uint32_t v : versions) {
        if (suggest_melody_step(step_facts(cat.song(v))) == kMelodyNoStep) {
            continue;
        }
        if (!best || cat.song(v).runs > cat.song(*best).runs) {
            best = v;
        }
    }
    return best && *best == i;
}

} // namespace

MelodyStepFacts step_facts(const CatalogueSong& s) {
    return {s.half_stars, s.left, s.right, s.left_keys, s.right_keys, s.bars, s.notes};
}

std::vector<std::uint64_t> hidden_added(const LibraryCatalogue& catalogue, const MelodyAdded& added) {
    const Found found = find_added(catalogue, added);
    std::vector<std::uint64_t> hidden;
    for (std::size_t e = 0; e < added.size(); ++e) {
        if (!found.index[e]) {
            hidden.push_back(added.at(e).hash);
        }
    }
    return hidden;
}

void build_library_row(const LibraryCatalogue& catalogue, const MelodyAdded& added, LibraryRow& out) {
    out.songs.clear();
    out.sorted = 0;
    out.hidden.clear();
    Found found = find_added(catalogue, added);
    out.followed = std::move(found.followed);
    // The player's own: each at the step they chose, in the order they added them.
    std::array<std::vector<LibraryRowSong>, kMelodyLastStep + 1> own;
    std::vector<std::size_t> shown; // their places, sorted
    std::unordered_set<std::uint64_t> seen;
    for (std::size_t e = 0; e < added.size(); ++e) {
        if (!found.index[e]) {
            out.hidden.push_back(added.at(e).hash);
            continue;
        }
        const CatalogueSong& s = catalogue.song(*found.index[e]);
        if (seen.insert(s.hash).second) {
            own[static_cast<std::size_t>(added.at(e).step)].push_back({*found.index[e], s.hash, added.at(e).step, true});
            shown.push_back(*found.index[e]);
            ++out.sorted;
        }
    }
    std::sort(shown.begin(), shown.end());
    std::array<std::vector<Ranked>, kMelodyLastStep + 1> by_step;
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        const CatalogueSong& s = catalogue.song(i);
        if (s.state != EntryState::Loaded || s.hash == 0 || seen.count(s.hash) != 0) {
            continue;
        }
        const int step = suggest_melody_step(step_facts(s));
        if (step == kMelodyNoStep || !stands_for_piece(catalogue, i, shown) || !seen.insert(s.hash).second) {
            continue;
        }
        ++out.sorted;
        by_step[static_cast<std::size_t>(step)].push_back({i, s.best_permille >= kMasteredPermille, s.favourite});
    }
    for (int step = kMelodyFirstStep; step <= kMelodyLastStep; ++step) {
        const auto at = static_cast<std::size_t>(step);
        out.songs.insert(out.songs.end(), own[at].begin(), own[at].end());
        std::vector<Ranked>& rows = by_step[at];
        const auto before = [&](const Ranked& a, const Ranked& b) {
            if (a.passed != b.passed) {
                return !a.passed;
            }
            if (a.favourite != b.favourite) {
                return a.favourite;
            }
            const std::string& ka = catalogue.title_key(a.index);
            const std::string& kb = catalogue.title_key(b.index);
            return ka != kb ? ka < kb : a.index < b.index;
        };
        const std::size_t take = std::min(kLibraryRowPerStep, rows.size());
        std::partial_sort(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(take), rows.end(), before);
        for (std::size_t r = 0; r < take; ++r) {
            out.songs.push_back({rows[r].index, catalogue.song(rows[r].index).hash, step, false});
        }
    }
}

} // namespace etude::app
