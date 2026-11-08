// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_browse.hpp"

#include "etude/library.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

BrowseOrder with_sort(BrowseOrder order, BrowseSort sort) {
    if (order.sort == sort) {
        order.reversed = !order.reversed;
    } else {
        order.sort = sort;
        order.reversed = false;
    }
    return order;
}

std::string composer_sort_key(std::string_view composer) {
    std::string folded = fold_for_search(composer);
    if (const std::size_t open = folded.find('('); open != std::string::npos) {
        folded.resize(open);
    }
    std::vector<std::string_view> words;
    std::string_view rest = folded;
    while (!rest.empty()) {
        const std::size_t start = rest.find_first_not_of(" ,.");
        if (start == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(start);
        const std::size_t end = std::min(rest.find_first_of(" ,"), rest.size());
        words.push_back(rest.substr(0, end));
        rest.remove_prefix(end);
    }
    const auto suffix = [](std::string_view w) {
        return w == "jr" || w == "jr." || w == "sr" || w == "sr." || w == "i" || w == "ii" ||
               w == "iii" || w == "iv";
    };
    while (words.size() > 1 && suffix(words.back())) {
        words.pop_back();
    }
    std::string key = words.empty() ? std::string() : std::string(words.back());
    for (const std::string_view w : words) {
        key += ' ';
        key += w;
    }
    return key;
}

Progress progress_of(const BrowseSong& song) {
    if (song.recency < 0 && song.best_permille < 0) {
        return Progress::NotStarted;
    }
    if (song.best_permille >= kMasteredPermille) {
        return Progress::Mastered;
    }
    return Progress::InProgress;
}

bool passes_filter(const BrowseSong& song, BrowseFilter filter, std::int32_t today) {
    switch (filter) {
    case BrowseFilter::All:
        return true;
    case BrowseFilter::Favourites:
        return song.favourite;
    case BrowseFilter::Recent:
        return song.recency >= 0 && today - song.last_day < kRecentDays && song.last_day <= today;
    case BrowseFilter::InProgress:
        return progress_of(song) == Progress::InProgress;
    }
    return true;
}

int facet_count(const BrowseOrder& order) {
    return ((order.levels & kLevelBits) != 0 ? 1 : 0) + (order.hands != BrowseHands::Any ? 1 : 0);
}

bool passes_facets(const BrowseSong& song, const BrowseOrder& order) {
    const int levels = order.levels & kLevelBits;
    if (levels != 0) {
        if (song.difficulty < 1 || song.difficulty > 5 ||
            (levels & (1 << (song.difficulty - 1))) == 0) {
            return false;
        }
    }
    switch (order.hands) {
    case BrowseHands::Any:
        return true;
    case BrowseHands::Both:
        return song.left && song.right;
    case BrowseHands::RightOnly:
        return song.right && !song.left;
    case BrowseHands::LeftOnly:
        return song.left && !song.right;
    }
    return true;
}

namespace {

// The group a song is in, as the section's key and its place among the groups.
struct GroupOf {
    std::uint32_t key = 0;
    std::uint64_t place = 0;
};

GroupOf group_of(const BrowseSong& s, BrowseGroup group) {
    switch (group) {
    case BrowseGroup::None:
        return {};
    case BrowseGroup::Composer:
        return {s.composer_rank, s.composer_rank};
    case BrowseGroup::Genre:
        return {s.genre_rank, s.genre_rank};
    case BrowseGroup::Difficulty:
        // Easy to hard, then the songs not read yet.
        return {s.difficulty, s.difficulty == 0 ? 6u : s.difficulty};
    case BrowseGroup::Progress:
        return {static_cast<std::uint32_t>(progress_of(s)),
                static_cast<std::uint64_t>(progress_of(s))};
    case BrowseGroup::MyGroups:
        return {}; // sectioned by browse() from the group lists
    }
    return {};
}

// The sort's value of a song, bigger later in the natural direction, and whether it has one.
struct SortValue {
    bool missing = false;
    double v = 0.0;
};

SortValue sort_value(const BrowseSong& s, BrowseSort sort) {
    switch (sort) {
    case BrowseSort::Name:
        return {false, static_cast<double>(s.name_rank)};
    case BrowseSort::Composer:
        return {s.composer_rank == kNoRank, static_cast<double>(s.composer_rank)};
    case BrowseSort::Difficulty:
        return {s.difficulty == 0, static_cast<double>(s.difficulty)};
    case BrowseSort::Progress:
        return {s.best_permille < 0, -static_cast<double>(s.best_permille)}; // best first
    case BrowseSort::Length:
        return {!(s.duration_s > 0.0), s.duration_s};
    case BrowseSort::Recent:
        return {s.recency < 0, -static_cast<double>(s.recency)}; // latest first
    case BrowseSort::Added:
        return {s.added_s <= 0, -static_cast<double>(s.added_s)}; // newest first
    }
    return {};
}

} // namespace

void LibraryBrowser::browse(std::span<const BrowseSong> songs, const BrowseOrder& order,
                            std::int32_t today, std::span<const std::uint8_t> include,
                            GroupLists groups) {
    order_.clear();
    sections_.clear();
    order_.reserve(songs.size());
    sections_.reserve(songs.size() + 1);
    const bool mine = order.group == BrowseGroup::MyGroups;
    const BrowseGroup by = mine ? BrowseGroup::None : order.group;
    for (std::size_t i = 0; i < songs.size(); ++i) {
        if (!include.empty() && (i >= include.size() || include[i] == 0)) {
            continue;
        }
        if (passes_filter(songs[i], order.filter, today) && passes_facets(songs[i], order)) {
            order_.push_back(static_cast<std::uint32_t>(i));
        }
    }
    std::sort(order_.begin(), order_.end(), [&](std::uint32_t a, std::uint32_t b) {
        const BrowseSong& x = songs[a];
        const BrowseSong& y = songs[b];
        const GroupOf gx = group_of(x, by);
        const GroupOf gy = group_of(y, by);
        if (gx.place != gy.place) {
            return gx.place < gy.place;
        }
        const SortValue vx = sort_value(x, order.sort);
        const SortValue vy = sort_value(y, order.sort);
        if (vx.missing != vy.missing) {
            return vy.missing;
        }
        if (!vx.missing && vx.v != vy.v) {
            return order.reversed ? vx.v > vy.v : vx.v < vy.v;
        }
        if (x.name_rank != y.name_rank) {
            // A name sort turned round keeps its own direction for the ties it has none of.
            const bool down = order.reversed && order.sort == BrowseSort::Name;
            return down ? x.name_rank > y.name_rank : x.name_rank < y.name_rank;
        }
        return a < b;
    });
    if (mine) {
        // Each group's songs in the order just made, then the songs in no group.
        constexpr std::uint32_t kOut = std::numeric_limits<std::uint32_t>::max();
        place_.assign(songs.size(), kOut);
        grouped_.assign(songs.size(), 0);
        sorted_.assign(order_.begin(), order_.end());
        for (std::uint32_t k = 0; k < sorted_.size(); ++k) {
            place_[sorted_[k]] = k;
        }
        order_.clear();
        const std::size_t n = groups.offsets.empty() ? 0 : groups.offsets.size() - 1;
        for (std::size_t g = 0; g < n; ++g) {
            const auto begin = static_cast<std::uint32_t>(order_.size());
            for (std::uint32_t m = groups.offsets[g]; m < groups.offsets[g + 1]; ++m) {
                const std::uint32_t song = groups.songs[m];
                if (song < songs.size()) {
                    grouped_[song] = 1;
                    if (place_[song] != kOut) {
                        order_.push_back(song);
                    }
                }
            }
            std::sort(order_.begin() + begin, order_.end(),
                      [&](std::uint32_t a, std::uint32_t b) { return place_[a] < place_[b]; });
            order_.erase(std::unique(order_.begin() + begin, order_.end()), order_.end());
            const auto end = static_cast<std::uint32_t>(order_.size());
            if (end > begin) {
                sections_.push_back({static_cast<std::uint32_t>(g), begin, end});
            }
        }
        const auto begin = static_cast<std::uint32_t>(order_.size());
        for (const std::uint32_t song : sorted_) {
            if (grouped_[song] == 0) {
                order_.push_back(song);
            }
        }
        if (order_.size() > begin) {
            sections_.push_back({kNoRank, begin, static_cast<std::uint32_t>(order_.size())});
        }
        return;
    }
    for (std::uint32_t k = 0; k < order_.size(); ++k) {
        const std::uint32_t key = group_of(songs[order_[k]], order.group).key;
        if (sections_.empty() || sections_.back().key != key) {
            sections_.push_back({key, k, k + 1});
        } else {
            sections_.back().end = k + 1;
        }
    }
}

void LibraryBrowser::ranked(std::span<const SearchHit> hits) {
    order_.clear();
    sections_.clear();
    order_.reserve(hits.size());
    for (const SearchHit& h : hits) {
        order_.push_back(h.song);
    }
    if (!order_.empty()) {
        sections_.push_back({0, 0, static_cast<std::uint32_t>(order_.size())});
    }
}

// ---- The grid

float GridColumns::x(int column) const { return x0 + static_cast<float>(column) * (card_w + gap); }

GridColumns grid_columns(float width, float min_card_w, float gap, float pad) {
    GridColumns g;
    g.gap = gap;
    const float inner = std::max(0.0f, width - 2.0f * pad);
    g.x0 = pad;
    if (!(min_card_w > 0.0f) || !(inner > 0.0f)) {
        g.columns = 1;
        g.card_w = inner;
        return g;
    }
    g.columns = std::max(1, static_cast<int>(std::floor((inner + gap) / (min_card_w + gap))));
    g.card_w = (inner - gap * static_cast<float>(g.columns - 1)) / static_cast<float>(g.columns);
    return g;
}

void GridRows::build(std::span<const BrowseSection> sections, int columns, float card_h, float gap,
                     float header_h, bool headers) {
    sections_.clear();
    columns_ = std::max(1, columns);
    card_h_ = card_h;
    gap_ = gap;
    header_h_ = headers ? header_h : 0.0f;
    float y = 0.0f;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const BrowseSection& s = sections[i];
        Section out;
        if (i > 0) {
            y += gap;
        }
        out.top = y;
        out.cards_top = y + header_h_;
        out.begin = s.begin;
        out.end = s.end;
        const auto rows = (s.end - s.begin + static_cast<std::uint32_t>(columns_) - 1) /
                          static_cast<std::uint32_t>(columns_);
        y = out.cards_top + static_cast<float>(rows) * card_h +
            static_cast<float>(rows > 0 ? rows - 1 : 0) * gap;
        sections_.push_back(out);
    }
    height_ = y;
}

std::size_t GridRows::section_of(std::uint32_t k) const {
    const auto it = std::upper_bound(sections_.begin(), sections_.end(), k,
                                     [](std::uint32_t v, const Section& s) { return v < s.begin; });
    return it == sections_.begin() ? 0 : static_cast<std::size_t>(it - sections_.begin() - 1);
}

GridRows::Place GridRows::place(std::uint32_t k) const {
    if (sections_.empty()) {
        return {};
    }
    const Section& s = sections_[section_of(k)];
    const std::uint32_t i = k - std::min(k, s.begin);
    const auto cols = static_cast<std::uint32_t>(columns_);
    return {s.cards_top + static_cast<float>(i / cols) * (card_h_ + gap_),
            static_cast<int>(i % cols)};
}

float GridRows::section_top(std::size_t s) const {
    return s < sections_.size() ? sections_[s].top : height_;
}

GridRows::Range GridRows::visible(float top, float bottom) const {
    Range r;
    if (sections_.empty() || bottom <= top) {
        return r;
    }
    const float pitch = card_h_ + gap_;
    const auto cols = static_cast<std::uint32_t>(columns_);
    bool found = false;
    for (const Section& s : sections_) {
        if (s.end <= s.begin) {
            continue;
        }
        const std::uint32_t rows = (s.end - s.begin + cols - 1) / cols;
        const float last_bottom = s.cards_top + static_cast<float>(rows - 1) * pitch + card_h_;
        if (last_bottom <= top) {
            continue; // wholly above
        }
        if (s.cards_top >= bottom) {
            break; // this and every later one below
        }
        // The rows of this section that reach into [top, bottom).
        const float from = std::max(0.0f, top - s.cards_top);
        auto first_row = static_cast<std::uint32_t>(from / pitch);
        // The gap after a row is not part of it.
        if (from - static_cast<float>(first_row) * pitch >= card_h_) {
            ++first_row;
        }
        const float to = bottom - s.cards_top;
        const auto last_row = std::min<std::uint32_t>(
            rows, static_cast<std::uint32_t>(std::max(0.0f, std::ceil(to / pitch))));
        if (first_row >= last_row) {
            continue;
        }
        const std::uint32_t a = s.begin + first_row * cols;
        const std::uint32_t b = std::min(s.end, s.begin + last_row * cols);
        if (!found) {
            r.first = a;
            found = true;
        }
        r.last = b;
    }
    if (!found) {
        r = {};
    }
    return r;
}

GridRows::Sticky GridRows::sticky(float scroll) const {
    Sticky out;
    if (header_h_ <= 0.0f || sections_.empty() || scroll <= sections_.front().top) {
        return out;
    }
    std::size_t s = 0;
    while (s + 1 < sections_.size() && sections_[s + 1].top <= scroll) {
        ++s;
    }
    if (sections_[s].top >= scroll) {
        return out; // its header is in its own place
    }
    out.section = static_cast<int>(s);
    out.y = scroll;
    if (s + 1 < sections_.size()) {
        out.y = std::min(out.y, sections_[s + 1].top - header_h_);
    }
    return out;
}

} // namespace etude
