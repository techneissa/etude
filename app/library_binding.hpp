// SPDX-License-Identifier: GPL-3.0-or-later
// Binds the library screen (ui::LibraryPort) to the song library, the practice history and the
// catalogue between them. The frame loop calls `update` once a frame after the library's poll and
// takes the song the player chose to open (`take_open`).
#pragma once

#include "catalogue_service.hpp"
#include "history_store.hpp"
#include "library_catalogue.hpp"
#include "library_files.hpp"
#include "song_library.hpp"
#include "song_opener.hpp"
#include "ui/library_port.hpp"
#include "version_grouper.hpp"

#include <bitset>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <utility>

namespace etude::app {

class LibraryBinding final : public ui::LibraryPort {
  public:
    // `removed`: the folder Delete moves songs into ("Removed songs" in the settings folder).
    LibraryBinding(SongLibrary& library, HistoryStore& history, std::filesystem::path removed);

    // Once a frame: tells the catalogue what changed (`library_changed`: the library's poll said
    // so) and lets it catch up within its time budget; stores a date added for the songs read for
    // the first time once the history is loaded. `now_utc_s` is the wall clock.
    void update(bool library_changed, std::int64_t now_utc_s);
    // The song the player chose, once.
    std::optional<std::size_t> take_open();
    // A section the song just taken with take_open() opens on: its content hash, loop and section.
    struct SectionOpen {
        std::uint64_t hash = 0;
        double start_s = 0.0;
        double end_s = 0.0;
        int section_id = 0;
    };
    std::optional<SectionOpen> take_section_open() { return std::exchange(section_open_, std::nullopt); }
    // The bars the song just taken with take_open() opens on (Progress's "Practise today").
    struct BarsOpen {
        std::uint64_t hash = 0;
        int first = 1;
        int last = 1;
    };
    std::optional<BarsOpen> take_bars_open() { return std::exchange(bars_open_, std::nullopt); }
    // The song that is open now (a catalogue index), for the screen to mark and scroll to.
    void set_current(std::optional<std::size_t> index) { current_ = index; }
    void set_today(std::int32_t day) { today_ = day; }
    // Seconds east of UTC here: the medals count local days.
    void set_utc_offset_s(std::int32_t seconds) { utc_offset_s_ = seconds; }
    // The player's keyboard, as the main view shows it; none when each song sets its own range
    // (the preview then takes the song's).
    void set_keyboard(std::optional<KeyRange> keys) { keyboard_ = keys; }
    // "Get songs": the catalogues (owned by the caller; none: the screen shows no catalogue).
    void set_catalogues(CatalogueService* catalogues) { catalogues_ = catalogues; }

    // ui::LibraryPort
    LibraryCatalogue& catalogue() override { return catalogue_; }
    std::optional<std::size_t> current() const override { return current_; }
    std::int32_t today() const override { return today_; }
    void open(std::size_t i) override;
    void open_at_section(std::size_t i, double start_s, double end_s, int section_id) override;
    void request_progress(int goal_minutes) override;
    std::shared_ptr<const ProgressView> progress() override;
    bool progress_failed() const override { return progress_failed_; }
    const ProfileSet& profiles() const override { return history_.profiles(); }
    std::int64_t active_profile() const override { return history_.active_profile(); }
    bool open_song_hash(std::uint64_t hash) override;
    bool practise_bars(std::uint64_t hash, int first, int last) override;
    const SongPractice& practice(std::size_t i) const override;
    void choose_version(std::size_t i) override;
    void split_version(std::size_t i) override;
    ui::LibraryPort::JoinOutcome join_versions(std::size_t i, std::size_t with) override;
    void reset_versions(std::size_t i) override;
    ui::LibraryPort::VersionSaid version_said(std::size_t i) const override;
    void set_favourite(std::size_t i, bool on) override;
    void rename(std::size_t i, std::string_view title) override;
    void set_composer(std::size_t i, std::string_view composer) override;
    std::vector<BestEntry> bests(std::size_t i, std::size_t max) const override;
    bool history_loading() const override { return history_.loading(); }
    void set_favourites(std::span<const std::size_t> songs, bool on) override;
    const LibraryGroups& groups() const override { return history_.groups(); }
    std::uint64_t create_group(std::string_view name) override;
    void rename_group(std::uint64_t id, std::string_view name) override;
    void remove_group(std::uint64_t id) override;
    void move_group(std::uint64_t id, int step) override;
    void set_in_group(std::uint64_t id, std::span<const std::size_t> songs, bool in) override;
    std::string_view why_not_movable(std::size_t i) const override;
    std::vector<std::string> folders() const override;
    void move_songs(std::span<const std::size_t> songs, std::string_view folder) override;
    void remove_songs(std::span<const std::size_t> songs) override;
    bool files_busy() const override { return files_.busy() || library_.rescanning(); }
    std::optional<std::string> take_files_notice() override;
    // Songs the catalogue service downloaded and this binding took in so far.
    std::size_t downloads_landed() const { return downloads_landed_; }
    void request_preview(std::size_t i) override;
    void stop_preview() override;
    const SongOpening* preview() const override { return preview_ ? &*preview_ : nullptr; }
    bool preview_failed() const override { return preview_failed_; }
    void preview_note(std::uint8_t key, std::uint8_t velocity) override;
    std::shared_ptr<const LoadedSong> preview_song() const override { return preview_song_; }
    KeyRange preview_keys() const override;
    const CatalogueSnapshot* catalogue_songs() const override;
    std::span<const CatalogueHit> catalogue_hits() const override;
    std::uint64_t catalogue_hits_serial() const override {
        return catalogues_ ? catalogues_->hits_serial() : 0;
    }
    std::string_view catalogue_hits_query() const override;
    const CatalogueIndexState& catalogue_state(CatalogueId id) const override;
    void refresh_catalogues() override;
    CatalogueFile catalogue_file(std::uint32_t entry) const override;
    std::string_view catalogue_file_reason(std::uint32_t entry) const override;
    void download(std::uint32_t entry) override;
    void cancel_download(std::uint32_t entry) override;
    std::optional<std::size_t> downloaded_song(std::uint32_t entry) const override;
    void request_catalogue_preview(std::uint32_t entry) override;
    const SongOrigin* origin(std::size_t i) const override;

    // The preview's notes to sound since the last call: (key, velocity), 0 a release. The frame
    // loop plays them on the piano the player hears.
    std::span<const std::pair<std::uint8_t, std::uint8_t>> preview_sound() const {
        return preview_sound_;
    }
    void clear_preview_sound() { preview_sound_.clear(); }

    // The opening a preview plays, in seconds from the first note.
    static constexpr double kPreviewS = 8.0;

    // A frame's share of the catalogue's work on changed songs.
    static constexpr std::int64_t kSyncBudgetNs = 2'000'000;
    // While songs are still being read, a new pass of the version grouping at most this often (s).
    static constexpr std::int64_t kVersionsEveryS = 2;

  private:
    // Changes one field of song `i`'s settings, keeping the rest; while the history is still
    // being read the edit is held (and shown) and goes over the stored settings once they are in.
    void edit_prefs(std::size_t i, SongPrefEdit edit);
    // The files of `songs` that can move, each once: a tune only with all of its file's tunes.
    // `skipped` counts the songs left out.
    std::vector<std::size_t> movable_files(std::span<const std::size_t> songs,
                                           std::size_t& skipped) const;
    std::uint64_t new_group_id() const;
    void release_preview_keys();
    // Takes in the catalogues' news: finished downloads (their source kept, into their
    // catalogue's group), a preview's file fetched.
    void update_catalogues();
    // The group a catalogue's downloads go in, made when there is none.
    std::uint64_t catalogue_group(CatalogueId id);
    // The library's songs by the catalogue entry they were downloaded as, made again when the
    // library or the sources change.
    void index_downloaded();
    // Takes in a finished pass of the version grouping and asks for a new one when what it reads
    // changed (songs read, renamed or re-rated), now and then while the library is still filling
    // in.
    void update_versions();
    // Takes in the Progress answers that are in (the one asked for last replaces the page's).
    void take_progress_answers();

    SongLibrary& library_;
    HistoryStore& history_;
    LibraryCatalogue catalogue_;
    std::optional<std::size_t> current_;
    std::optional<std::size_t> open_;
    std::optional<SectionOpen> section_open_;
    std::optional<BarsOpen> bars_open_;
    std::uint64_t progress_serial_ = 0; // the request whose answer is wanted
    bool progress_failed_ = false;      // it was refused, or answered with nothing
    std::shared_ptr<const ProgressView> progress_;
    std::int32_t today_ = 0;
    std::int32_t utc_offset_s_ = 0;
    // Which songs are versions of one piece, by content hash, from the latest grouping pass (the key is the
    // piece's lowest hash); the medals count a piece once.
    std::shared_ptr<const PieceMap> pieces_;
    std::size_t runs_seen_ = 0;
    bool history_was_loading_ = true;
    std::vector<SongPrefEdit> held_;       // edits made while the history was being read
    HistoryStore::SongPrefsMap held_view_; // and what they show meanwhile
    std::uint64_t groups_seen_ = ~std::uint64_t{0};
    std::int64_t now_utc_s_ = 0;

    VersionGrouper grouper_;
    std::uint64_t versions_asked_for_ = ~std::uint64_t{0}; // the facts_version of the last request
    std::uint64_t overrides_asked_for_ = ~std::uint64_t{0}; // and the overrides' serial
    bool versions_were_on_ = false;
    std::int64_t versions_asked_at_s_ = 0;
    std::int64_t chosen_clock_ = 0; // the time of the latest choice of a version

    std::filesystem::path removed_;
    LibraryFiles files_;
    std::vector<std::string> notices_;
    std::size_t downloads_landed_ = 0;

    SongOpener preview_opener_;
    std::string preview_relative_; // the song asked for
    std::optional<SongOpening> preview_;
    std::shared_ptr<const LoadedSong> preview_song_;
    std::optional<KeyRange> keyboard_;
    KeyRange preview_song_keys_{48, 72}; // the preview song's whole range, in whole octaves
    bool preview_failed_ = false;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> preview_sound_;
    std::bitset<128> preview_held_;

    CatalogueService* catalogues_ = nullptr;
    std::uint64_t preview_entry_key_ = 0; // a catalogue entry whose file the preview waits for
    std::unordered_map<std::uint64_t, std::size_t> downloaded_; // entry key -> library song
    std::size_t downloaded_origins_ = ~std::size_t{0};
    std::size_t downloaded_songs_ = ~std::size_t{0};
    std::uint64_t downloaded_rows_ = ~std::uint64_t{0};
};

} // namespace etude::app
