// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_binding.hpp"

#include <utility>

namespace etude::app {

void LibraryBinding::update_catalogues() {
    if (!catalogues_) {
        return;
    }
    catalogues_->search(catalogue_.query());
    catalogues_->poll();
    // Finished downloads, once the history is read (so the catalogue's group is found, not made
    // twice).
    while (!history_.loading()) {
        std::optional<CatalogueDownload> d = catalogues_->take_download();
        if (!d) {
            break;
        }
        if (d->content_hash != 0) {
            history_.set_song_origin(d->content_hash, song_origin_of(d->entry, now_utc_s_));
            // The catalogue's name and composer for it, unless the player gave it their own.
            SongPrefs prefs;
            if (const auto it = history_.song_prefs().find(d->content_hash);
                it != history_.song_prefs().end()) {
                prefs = it->second;
            }
            if (prefs.title.empty()) {
                prefs.title = d->entry.title;
            }
            if (prefs.composer.empty()) {
                prefs.composer = d->entry.composer;
            }
            prefs.added_utc_s = prefs.added_utc_s > 0 ? prefs.added_utc_s : now_utc_s_;
            history_.set_song_prefs(d->content_hash, prefs);
            GroupEdit e;
            e.kind = GroupEdit::Kind::Add;
            e.group.id = catalogue_group(d->entry.source);
            e.hash = d->content_hash;
            e.at_utc_s = now_utc_s_;
            history_.edit_groups(e);
        }
        ++downloads_landed_;
        notices_.push_back("Downloaded \"" + d->entry.title + "\" into " +
                           std::string(catalogue_info(d->entry.source).group));
        library_.rescan();
    }
    // A catalogue preview's file: read once it is in.
    if (preview_entry_key_ != 0) {
        const CatalogueSnapshot* snap = catalogues_->snapshot();
        std::optional<std::uint32_t> entry;
        if (snap) {
            for (std::uint32_t i = 0; i < snap->keys.size(); ++i) {
                if (snap->keys[i] == preview_entry_key_) {
                    entry = i;
                    break;
                }
            }
        }
        const CatalogueFile f = entry ? catalogues_->file(*entry) : CatalogueFile{};
        if (f.state == CatalogueFile::State::Ready || f.state == CatalogueFile::State::Kept) {
            preview_entry_key_ = 0;
            SongLoadOptions o;
            o.abc_chords = library_.abc_chords();
            preview_opener_.request(catalogues_->file_path(*entry), std::move(o));
        } else if (f.state == CatalogueFile::State::Failed || f.state == CatalogueFile::State::None) {
            preview_entry_key_ = 0;
            preview_failed_ = true;
        }
    }
}

std::uint64_t LibraryBinding::catalogue_group(CatalogueId id) {
    const CatalogueInfo& info = catalogue_info(id);
    for (const SongGroup& g : history_.groups().groups()) {
        if (g.kind == GroupKind::Catalogue && g.source == info.key) {
            return g.id;
        }
    }
    GroupEdit e;
    e.kind = GroupEdit::Kind::Create;
    e.group.id = new_group_id();
    e.group.name = std::string(info.group);
    e.group.kind = GroupKind::Catalogue;
    e.group.source = std::string(info.key);
    e.group.position = history_.groups().next_position();
    e.group.created_utc_s = now_utc_s_;
    history_.edit_groups(e);
    return e.group.id;
}

void LibraryBinding::index_downloaded() {
    const auto& origins = history_.song_origins();
    if (origins.size() == downloaded_origins_ && catalogue_.size() == downloaded_songs_ &&
        catalogue_.songs_version() == downloaded_rows_) {
        return;
    }
    downloaded_origins_ = origins.size();
    downloaded_songs_ = catalogue_.size();
    downloaded_rows_ = catalogue_.songs_version();
    downloaded_.clear();
    if (origins.empty()) {
        return;
    }
    for (std::size_t i = 0; i < catalogue_.size(); ++i) {
        const std::uint64_t hash = catalogue_.song(i).hash;
        if (hash == 0) {
            continue;
        }
        const auto it = origins.find(hash);
        CatalogueId id{};
        if (it != origins.end() && catalogue_from_key(it->second.catalogue, id)) {
            downloaded_.emplace(catalogue_entry_key(id, it->second.id), i);
        }
    }
}

const CatalogueSnapshot* LibraryBinding::catalogue_songs() const {
    return catalogues_ ? catalogues_->snapshot() : nullptr;
}

std::span<const CatalogueHit> LibraryBinding::catalogue_hits() const {
    return catalogues_ ? catalogues_->hits() : std::span<const CatalogueHit>();
}

std::string_view LibraryBinding::catalogue_hits_query() const {
    return catalogues_ ? catalogues_->hits_query() : std::string_view();
}

const CatalogueIndexState& LibraryBinding::catalogue_state(CatalogueId id) const {
    static const CatalogueIndexState kNone;
    return catalogues_ ? catalogues_->index_state(id) : kNone;
}

void LibraryBinding::refresh_catalogues() {
    if (catalogues_) {
        catalogues_->refresh();
    }
}

CatalogueFile LibraryBinding::catalogue_file(std::uint32_t entry) const {
    return catalogues_ ? catalogues_->file(entry) : CatalogueFile{};
}

std::string_view LibraryBinding::catalogue_file_reason(std::uint32_t entry) const {
    return catalogues_ ? catalogues_->file_reason(entry) : std::string_view();
}

void LibraryBinding::download(std::uint32_t entry) {
    if (catalogues_) {
        catalogues_->download(entry);
    }
}

void LibraryBinding::cancel_download(std::uint32_t entry) {
    if (catalogues_) {
        catalogues_->cancel(entry);
    }
}

std::optional<std::size_t> LibraryBinding::downloaded_song(std::uint32_t entry) const {
    const CatalogueSnapshot* snap = catalogue_songs();
    if (!snap || entry >= snap->keys.size()) {
        return std::nullopt;
    }
    const auto it = downloaded_.find(snap->keys[entry]);
    if (it == downloaded_.end() || it->second >= catalogue_.size()) {
        return std::nullopt;
    }
    return it->second;
}

void LibraryBinding::request_catalogue_preview(std::uint32_t entry) {
    const CatalogueSnapshot* snap = catalogue_songs();
    if (!snap || entry >= snap->keys.size()) {
        return;
    }
    const std::string name = "catalogue:" + std::to_string(snap->keys[entry]);
    if (name == preview_relative_ && (preview_ || preview_opener_.busy() || preview_entry_key_)) {
        return;
    }
    release_preview_keys();
    preview_opener_.cancel();
    preview_relative_ = name;
    preview_.reset();
    preview_song_.reset();
    preview_failed_ = false;
    preview_entry_key_ = snap->keys[entry];
    catalogues_->preview(entry);
}

} // namespace etude::app
