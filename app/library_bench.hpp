// SPDX-License-Identifier: GPL-3.0-or-later
// A made-up library for the benches: thousands of songs with names, composers, folders, lengths
// and levels like a real collection's, so the search and the library screen can be timed without
// a folder of files. `--bench-search` types queries into it a character at a time; `--bench
// --panel library` draws the library screen over it.
#pragma once

#include "library_catalogue.hpp"
#include "ui/library_port.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace etude::app {

class BenchLibrary final : public ui::LibraryPort {
  public:
    // `songs` songs, the same ones every time; every catalogue fact filled in at once.
    explicit BenchLibrary(std::size_t songs);

    LibraryCatalogue& catalogue() override { return catalogue_; }
    std::optional<std::size_t> current() const override { return std::nullopt; }
    std::int32_t today() const override { return kToday; }
    void open(std::size_t) override {}
    void open_at_section(std::size_t, double, double, int) override {}
    void request_progress(int) override {}
    std::shared_ptr<const ProgressView> progress() override { return nullptr; }
    bool progress_failed() const override { return false; }
    const ProfileSet& profiles() const override { return profiles_; }
    std::int64_t active_profile() const override { return kDefaultProfileId; }
    bool open_song_hash(std::uint64_t) override { return false; }
    bool practise_bars(std::uint64_t, int, int) override { return false; }
    // Every song has the same made-up practice: the left hand alone at 70 %.
    const SongPractice& practice(std::size_t) const override {
        static const SongPractice made_up = [] {
            SongPractice p;
            p.hands = HandFilter::Left;
            p.speed_percent = 70;
            return p;
        }();
        return made_up;
    }
    // The content hash of the i-th song, for seeding the sections a bench shows.
    static std::uint64_t hash_of(std::size_t i) { return 0x9e3779b97f4a7c15ULL * (i + 1); }
    void choose_version(std::size_t) override {}
    void split_version(std::size_t) override {}
    JoinOutcome join_versions(std::size_t, std::size_t) override { return JoinOutcome::Joined; }
    void reset_versions(std::size_t) override {}
    VersionSaid version_said(std::size_t) const override { return VersionSaid::Auto; }
    void set_favourite(std::size_t, bool) override {}
    void rename(std::size_t, std::string_view) override {}
    void set_composer(std::size_t, std::string_view) override {}
    std::vector<BestEntry> bests(std::size_t, std::size_t) const override { return {}; }
    bool history_loading() const override { return false; }
    void set_favourites(std::span<const std::size_t>, bool) override {}
    const LibraryGroups& groups() const override { return groups_; }
    std::uint64_t create_group(std::string_view) override { return 0; }
    void rename_group(std::uint64_t, std::string_view) override {}
    void remove_group(std::uint64_t) override {}
    void move_group(std::uint64_t, int) override {}
    void set_in_group(std::uint64_t, std::span<const std::size_t>, bool) override {}
    std::string_view why_not_movable(std::size_t) const override { return "A bench"; }
    std::vector<std::string> folders() const override { return {}; }
    void move_songs(std::span<const std::size_t>, std::string_view) override {}
    void remove_songs(std::span<const std::size_t>) override {}
    bool files_busy() const override { return false; }
    std::optional<std::string> take_files_notice() override { return std::nullopt; }
    void request_preview(std::size_t) override {}
    void stop_preview() override {}
    const SongOpening* preview() const override { return nullptr; }
    bool preview_failed() const override { return false; }
    void preview_note(std::uint8_t, std::uint8_t) override {}
    std::shared_ptr<const LoadedSong> preview_song() const override { return nullptr; }
    KeyRange preview_keys() const override { return kRange88; }
    const CatalogueSnapshot* catalogue_songs() const override { return nullptr; }
    std::span<const CatalogueHit> catalogue_hits() const override { return {}; }
    std::uint64_t catalogue_hits_serial() const override { return 0; }
    std::string_view catalogue_hits_query() const override { return {}; }
    const CatalogueIndexState& catalogue_state(CatalogueId) const override { return state_; }
    void refresh_catalogues() override {}
    CatalogueFile catalogue_file(std::uint32_t) const override { return {}; }
    std::string_view catalogue_file_reason(std::uint32_t) const override { return {}; }
    void download(std::uint32_t) override {}
    void cancel_download(std::uint32_t) override {}
    std::optional<std::size_t> downloaded_song(std::uint32_t) const override { return std::nullopt; }
    void request_catalogue_preview(std::uint32_t) override {}
    const SongOrigin* origin(std::size_t) const override { return nullptr; }

    static constexpr std::int32_t kToday = 20'000;
    // The made-up groups: this many, each of a share of the songs.
    static constexpr int kGroups = 6;

  private:
    LibraryState library_;
    LibraryCatalogue::Prefs prefs_;
    LibraryGroups groups_;
    ProfileSet profiles_;
    LibraryCatalogue catalogue_;
    CatalogueIndexState state_;
};

// --bench-search: builds a library of `songs` songs, then types each of a list of queries into its
// search a character at a time and deletes it again, timing each keystroke (the search and the
// new order together) and counting its heap allocations after a warm-up pass. Prints the times
// and returns the process exit code: 1 when a keystroke allocated or the slowest took 5 ms or more.
int run_search_bench(std::size_t songs);

// --bench-scan DIR: scans the song files under DIR the way the library does (listed by name, then
// parsed on its worker thread, a hand split for every file that has no hand names), prints how
// long that took and how many hand splits it ran, then opens every song the way PageDown does (read
// and parsed again, off the frame thread) and prints the time and the splits that took. No window.
// Returns 1 when DIR has no song or a file failed to load.
int run_scan_bench(const std::string& dir);

} // namespace etude::app
