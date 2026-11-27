// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_catalogue.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace etude::app {

void LibraryCatalogue::set_group_versions(bool on) {
    if (on == group_versions_) {
        return;
    }
    group_versions_ = on;
    ++versions_serial_;
    versions_dirty_ = true;
    view_dirty_ = true;
}

void LibraryCatalogue::set_versions(std::shared_ptr<const VersionIndex> index) {
    versions_ = std::move(index);
    ++versions_serial_;
    versions_dirty_ = true;
    view_dirty_ = true;
}

std::span<const std::uint32_t> LibraryCatalogue::versions_of(std::size_t i) const {
    if (i >= ver_group_.size() || ver_group_[i] == kNoGroup) {
        return {};
    }
    const std::uint32_t g = ver_group_[i];
    return {ver_songs_.data() + ver_offsets_[g], ver_offsets_[g + 1] - ver_offsets_[g]};
}

std::string_view LibraryCatalogue::version_name(std::size_t i) const {
    return i < ver_group_.size() && ver_group_[i] != kNoGroup && i < ver_name_.size()
               ? std::string_view(ver_name_[i])
               : std::string_view();
}

std::string_view LibraryCatalogue::version_scored(std::size_t i) const {
    return i < ver_group_.size() && ver_group_[i] != kNoGroup && i < ver_scored_.size()
               ? std::string_view(ver_scored_[i])
               : std::string_view();
}

std::string_view LibraryCatalogue::card_title(std::size_t i) const {
    if (i < ver_group_.size() && ver_group_[i] != kNoGroup && ver_group_[i] < ver_piece_.size() &&
        !ver_piece_[ver_group_[i]].empty()) {
        return ver_piece_[ver_group_[i]];
    }
    return songs_[i].title;
}

void LibraryCatalogue::piece_songs(std::size_t i, std::vector<std::size_t>& out) const {
    out.clear();
    const std::span<const std::uint32_t> versions = versions_of(i);
    if (versions.empty()) {
        out.push_back(i);
        return;
    }
    out.assign(versions.begin(), versions.end());
}

std::size_t LibraryCatalogue::card_of(std::size_t i) const {
    if (i >= ver_group_.size() || ver_group_[i] == kNoGroup || ver_card_[ver_group_[i]] == kNoGroup) {
        return i;
    }
    return ver_card_[ver_group_[i]];
}

void LibraryCatalogue::rebuild_versions() {
    versions_dirty_ = false;
    version_words_dirty_ = true;
    versions_built_for_ = songs_version_;
    const std::size_t n = songs_.size();
    has_versions_ = false;
    ver_group_.assign(n, kNoGroup);
    ver_member_.assign(n, 0);
    if (!group_versions_ || versions_ == nullptr || versions_->groups.empty()) {
        ver_offsets_.assign(1, 0);
        ver_songs_.clear();
        ver_card_.clear();
        return;
    }
    const std::vector<std::vector<VersionIndex::Member>>& groups = versions_->groups;
    // A file in the library twice (the same bytes under two names) is on its own, whatever the
    // pass said: a folder shows the copy that is in it, so neither can stand for the other.
    std::unordered_map<std::uint64_t, std::uint32_t> copies;
    for (const CatalogueSong& song : songs_) {
        if (song.hash != 0) {
            ++copies[song.hash];
        }
    }
    std::vector<std::uint32_t> base(groups.size() + 1, 0);
    for (std::size_t g = 0; g < groups.size(); ++g) {
        base[g + 1] = base[g] + static_cast<std::uint32_t>(groups[g].size());
    }
    std::vector<std::uint32_t> first(base.back(), kNoGroup);
    for (std::uint32_t i = 0; i < n; ++i) {
        if (songs_[i].hash == 0 || songs_[i].state != EntryState::Loaded ||
            copies[songs_[i].hash] != 1) {
            continue;
        }
        const VersionIndex::Place* place = versions_->find(songs_[i].hash);
        if (place == nullptr) {
            continue;
        }
        ver_group_[i] = place->group;
        ver_member_[i] = place->member;
        first[base[place->group] + place->member] = i;
    }
    ver_offsets_.assign(1, 0);
    ver_songs_.clear();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (std::uint32_t m = 0; m < groups[g].size(); ++m) {
            if (first[base[g] + m] != kNoGroup) {
                ver_songs_.push_back(first[base[g] + m]);
            }
        }
        ver_offsets_.push_back(static_cast<std::uint32_t>(ver_songs_.size()));
    }
    // A piece with fewer than two versions in the library is on its own.
    for (std::uint32_t i = 0; i < n; ++i) {
        if (ver_group_[i] != kNoGroup) {
            const std::uint32_t g = ver_group_[i];
            if (ver_offsets_[g + 1] - ver_offsets_[g] < 2) {
                ver_group_[i] = kNoGroup;
            } else {
                has_versions_ = true;
            }
        }
    }
    ver_card_.assign(groups.size(), kNoGroup);
    ver_mark_.assign(n, 0);
    ver_emitted_.assign(groups.size(), 0);
    ver_stamp_.assign(n, 0);
    collapsed_.clear();
    collapsed_.reserve(n);
    view_offsets_.clear();
    view_offsets_.reserve(group_offsets_.size() + 1);
    view_songs_.clear();
    view_songs_.reserve(group_songs_.size() + n);
}

// The words the version chips and the cards show, made once for each song when the versions or a
// score changed: the chip's name, the name with the best score, and each piece's name.
void LibraryCatalogue::rebuild_version_words() {
    version_words_dirty_ = false;
    ++version_words_version_;
    const std::size_t n = songs_.size();
    ver_name_.resize(n);
    ver_scored_.resize(n);
    ver_piece_.assign(ver_offsets_.empty() ? 0 : ver_offsets_.size() - 1, std::string());
    if (!has_versions_) {
        return;
    }
    for (std::uint32_t g = 0; g + 1 < ver_offsets_.size(); ++g) {
        std::string piece;
        for (std::uint32_t k = ver_offsets_[g]; k < ver_offsets_[g + 1]; ++k) {
            const std::uint32_t i = ver_songs_[k];
            ver_name_[i].assign(versions_->groups[g][ver_member_[i]].name);
            ver_scored_[i] = ver_name_[i];
            if (songs_[i].best_permille >= 0) {
                ver_scored_[i] += " " + std::to_string((songs_[i].best_permille + 5) / 10) + "%";
            }
            std::string name = piece_name(songs_[i].title);
            if (piece.empty() || name.size() < piece.size()) {
                piece = std::move(name);
            }
        }
        ver_piece_[g] = std::move(piece);
    }
}

// Each group's card among its versions that pass the filters: the one chosen last, else the one
// played last, else the easiest. With `prune`, every other version of a piece is out of the list.
void LibraryCatalogue::pick_cards(bool prune) {
    for (std::uint32_t g = 0; g + 1 < ver_offsets_.size(); ++g) {
        std::uint32_t best = kNoGroup;
        for (std::uint32_t k = ver_offsets_[g]; k < ver_offsets_[g + 1]; ++k) {
            const std::uint32_t j = ver_songs_[k];
            if (include_[j] == 0) {
                continue;
            }
            if (best == kNoGroup) {
                best = j;
                continue;
            }
            const CatalogueSong& a = songs_[j];
            const CatalogueSong& b = songs_[best];
            if (a.chosen_s != b.chosen_s ? a.chosen_s > b.chosen_s : a.recency > b.recency) {
                best = j;
            }
        }
        ver_card_[g] = best;
    }
    if (prune) {
        for (std::uint32_t i = 0; i < songs_.size(); ++i) {
            if (ver_group_[i] != kNoGroup && ver_card_[ver_group_[i]] != i) {
                include_[i] = 0;
            }
        }
    }
}

// A piece is in a group when any of its versions is, and shows there as its card (the one chosen
// last that passes the filters), once.
void LibraryCatalogue::card_groups() {
    view_offsets_.clear();
    view_offsets_.push_back(0);
    view_songs_.clear();
    for (std::uint32_t g = 0; g + 1 < group_offsets_.size(); ++g) {
        for (std::uint32_t m = group_offsets_[g]; m < group_offsets_[g + 1]; ++m) {
            std::uint32_t song = group_songs_[m];
            if (ver_group_[song] != kNoGroup) {
                song = ver_card_[ver_group_[song]];
                if (song == kNoGroup) {
                    continue; // no version of the piece passes the filters
                }
            }
            if (ver_stamp_[song] != g + 1) {
                ver_stamp_[song] = g + 1;
                view_songs_.push_back(song);
            }
        }
        view_offsets_.push_back(static_cast<std::uint32_t>(view_songs_.size()));
    }
    // The stamps are for this pass only.
    for (const std::uint32_t song : view_songs_) {
        ver_stamp_[song] = 0;
    }
}

// A search's hits with each piece once, at the place of its best hit: its card when that is a hit
// too, else the best-ranked version that is.
void LibraryCatalogue::collapse_hits(const std::vector<SearchHit>& hits) {
    collapsed_.clear();
    for (const SearchHit& h : hits) {
        ver_mark_[h.song] = 1;
    }
    for (const SearchHit& h : hits) {
        const std::uint32_t g = ver_group_[h.song];
        if (g == kNoGroup) {
            collapsed_.push_back(h);
        } else if (ver_emitted_[g] == 0) {
            ver_emitted_[g] = 1;
            SearchHit hit = h;
            const std::uint32_t card = ver_card_[g];
            if (card != kNoGroup && ver_mark_[card] != 0) {
                hit.song = card;
            }
            collapsed_.push_back(hit);
        }
    }
    for (const SearchHit& h : hits) {
        ver_mark_[h.song] = 0;
        if (ver_group_[h.song] != kNoGroup) {
            ver_emitted_[ver_group_[h.song]] = 0;
        }
    }
}

} // namespace etude::app
