// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_catalogue.hpp"

#include <algorithm>

namespace etude::app {

void LibraryCatalogue::build_shelves() {
    shelves_.clear();
    shelf_order_.clear();
    const std::vector<std::uint32_t>& shown = browser_.order();
    shelf_order_.reserve(shown.size() + kShelfSongs * 8);
    if (searching_ || order_.group != BrowseGroup::None) {
        // One shelf per section, as the grid has them.
        shelf_order_.assign(shown.begin(), shown.end());
        for (std::uint32_t k = 0; k < browser_.sections().size(); ++k) {
            const BrowseSection& sec = browser_.sections()[k];
            shelves_.push_back(
                {LibraryShelf::Kind::Section, k, sec.begin, sec.end, sec.end - sec.begin});
        }
        return;
    }
    const auto add = [&](LibraryShelf::Kind kind, std::uint32_t key, auto&& in) {
        LibraryShelf shelf{kind, key, static_cast<std::uint32_t>(shelf_order_.size()), 0, 0};
        for (const std::uint32_t i : shown) {
            if (in(i)) {
                if (shelf.total < kShelfSongs) {
                    shelf_order_.push_back(i);
                }
                ++shelf.total;
            }
        }
        shelf.end = static_cast<std::uint32_t>(shelf_order_.size());
        if (shelf.total > 0) {
            shelves_.push_back(shelf);
        }
    };
    // Recent: the latest played first, whatever the sort.
    {
        shelf_scratch_.clear();
        for (const std::uint32_t i : shown) {
            if (songs_[i].recency >= 0) {
                shelf_scratch_.push_back(i);
            }
        }
        std::sort(shelf_scratch_.begin(), shelf_scratch_.end(),
                  [&](std::uint32_t a, std::uint32_t b) {
                      return songs_[a].recency > songs_[b].recency;
                  });
        LibraryShelf shelf{LibraryShelf::Kind::Recent, 0,
                           static_cast<std::uint32_t>(shelf_order_.size()), 0,
                           static_cast<std::uint32_t>(shelf_scratch_.size())};
        for (std::size_t k = 0; k < shelf_scratch_.size() && k < kShelfSongs; ++k) {
            shelf_order_.push_back(shelf_scratch_[k]);
        }
        shelf.end = static_cast<std::uint32_t>(shelf_order_.size());
        if (shelf.total > 0) {
            shelves_.push_back(shelf);
        }
    }
    add(LibraryShelf::Kind::Favourites, 0, [&](std::uint32_t i) { return songs_[i].piece_favourite; });
    add(LibraryShelf::Kind::InProgress, 0,
        [&](std::uint32_t i) { return progress_of(facts_[i]) == Progress::InProgress; });
    if (scope_.kind != LibraryScope::Kind::Inbox) {
        add(LibraryShelf::Kind::Inbox, 0, [&](std::uint32_t i) { return songs_[i].inbox; });
    }
    for (std::uint32_t c = 0; c < collections_.size(); ++c) {
        const std::string& path = collections_[c].path;
        add(LibraryShelf::Kind::Collection, c,
            [&](std::uint32_t i) { return in_collection(songs_[i].folder, path); });
    }
    // The songs right here, in no collection inside: every song of a group or the Inbox.
    const bool by_folder =
        scope_.kind == LibraryScope::Kind::All || scope_.kind == LibraryScope::Kind::Folder;
    add(LibraryShelf::Kind::Here, 0,
        [&](std::uint32_t i) { return !by_folder || songs_[i].folder == scope_.path; });
    if (scope_.kind != LibraryScope::Kind::Group) {
        const std::vector<std::uint32_t>& offsets = view_groups_cards_ ? view_offsets_ : group_offsets_;
        const std::vector<std::uint32_t>& members = view_groups_cards_ ? view_songs_ : group_songs_;
        shelf_mark_.assign(songs_.size(), 0);
        for (std::uint32_t g = 0; g + 1 < offsets.size(); ++g) {
            // The group's songs marked, its shelf taken in the shown order, the marks cleared.
            for (std::uint32_t m = offsets[g]; m < offsets[g + 1]; ++m) {
                shelf_mark_[members[m]] = 1;
            }
            add(LibraryShelf::Kind::Group, g, [&](std::uint32_t i) { return shelf_mark_[i] != 0; });
            for (std::uint32_t m = offsets[g]; m < offsets[g + 1]; ++m) {
                shelf_mark_[members[m]] = 0;
            }
        }
    }
}

std::string_view LibraryCatalogue::shelf_label(const LibraryShelf& shelf) const {
    switch (shelf.kind) {
    case LibraryShelf::Kind::Recent:
        return "Recent";
    case LibraryShelf::Kind::Favourites:
        return "Favourites";
    case LibraryShelf::Kind::InProgress:
        return "In progress";
    case LibraryShelf::Kind::Inbox:
        return "Inbox";
    case LibraryShelf::Kind::Collection:
        return shelf.key < collections_.size() ? std::string_view(collections_[shelf.key].name)
                                               : std::string_view();
    case LibraryShelf::Kind::Group:
        return shelf.key < group_names_.size() ? std::string_view(group_names_[shelf.key])
                                               : std::string_view();
    case LibraryShelf::Kind::Section:
        return shelf.key < browser_.sections().size()
                   ? section_label(browser_.sections()[shelf.key])
                   : std::string_view();
    case LibraryShelf::Kind::Here:
        switch (scope_.kind) {
        case LibraryScope::Kind::All:
            return "Songs";
        case LibraryScope::Kind::Folder: {
            const std::size_t slash = scope_.path.rfind('/');
            return slash == std::string::npos ? std::string_view(scope_.path)
                                              : std::string_view(scope_.path).substr(slash + 1);
        }
        case LibraryScope::Kind::Inbox:
            return "Inbox";
        case LibraryScope::Kind::Group:
            if (const std::optional<std::size_t> g = group_index(scope_.group)) {
                return group_names_[*g];
            }
            return {};
        }
        return {};
    }
    return {};
}

} // namespace etude::app
