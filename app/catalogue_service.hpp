// SPDX-License-Identifier: GPL-3.0-or-later
// "Get songs": the free song catalogues' indexes, their search and their downloads, all off the
// frame thread. No SDL: std only, so the tests drive it with a FolderFetcher.
//
//   indexes   each catalogue's index is read from its cache in the cache folder at start (works
//             offline), and fetched again when it is older than kCatalogueRefreshS while the
//             player has the catalogues online; a page at a time, pacing the site, the songs
//             found joining the search as each page comes in.
//   search    the library's search text, typed on the frame thread, is searched by a thread of
//             its own once the text has been still for `debounce` (core SearchIndex over every
//             catalogue's songs at once); the frame thread only hands the text over and takes
//             the hits, allocating nothing.
//   files     a song's file is fetched into the cache folder for its preview; Download fetches it
//             the same way if it is not there yet and copies it into the Songs folder, in a folder
//             named after its catalogue. One file at a time, before the index pages; each with
//             its progress, a cancel, and the reason when it fails.
#pragma once

#include "net_fetch.hpp"

#include "etude/library_search.hpp"
#include "etude/song_catalogue.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace etude::app {

// Hooks for fetch_catalogue_index: asked before each request (false stops it), during one (false
// cancels it), and given the songs found so far every few pages of a long listing.
struct IndexHooks {
    std::function<bool()> before_request;
    std::function<bool()> still_wanted;
    std::function<void(const std::vector<CatalogueEntry>&)> partial;
};
// Fetches a catalogue's whole listing, a request at a time. False, with the player's words in
// `reason`, when a request fails or the listing has no songs.
bool fetch_catalogue_index(Fetcher& fetcher, CatalogueId id, std::vector<CatalogueEntry>& out,
                           std::string& reason, const IndexHooks& hooks);
// --get-songs-check: fetches each catalogue's listing over the network once and prints what came;
// the process exit code (1 when any failed).
int run_get_songs_check();

// Every catalogue's songs as last read: shared, never changed (a new index is a new snapshot).
struct CatalogueSnapshot {
    std::vector<CatalogueEntry> entries;
    std::vector<std::uint64_t> keys; // catalogue_entry_key of each entry
    // The entries that are one piece (core group_catalogue_versions), worked out on the network
    // thread with the snapshot, so the frame thread only reads it.
    CatalogueVersions versions;
    std::uint64_t version = 0;
};

// A song's identity across snapshots: its catalogue and its id there.
std::uint64_t catalogue_entry_key(CatalogueId source, std::string_view id);

struct CatalogueHit {
    std::uint32_t entry = 0; // in the snapshot the hits were found in
    MatchRank rank = MatchRank::TitleWord;
};

// How a catalogue's index stands.
struct CatalogueIndexState {
    enum class Kind : std::uint8_t {
        None,      // no cache and not fetched (offline, or not asked yet)
        Reading,   // the cache is being read
        Ready,     // songs to search
        Fetching,  // being fetched (the songs found so far are searched)
        Failed,    // the last fetch failed (`reason`); the cache, if any, is still searched
    };
    Kind kind = Kind::None;
    std::size_t songs = 0;
    std::int64_t fetched_utc_s = 0;
    std::string reason;
};

// A song's file, for its preview or to keep.
struct CatalogueFile {
    enum class State : std::uint8_t { None, Queued, Fetching, Failed, Ready, Kept };
    State state = State::None;
    float progress = -1.0f; // 0..1 while fetching; below 0 when the size is not known
    bool keep = false;      // Download was asked: it goes into the Songs folder
};

// A finished download, for the library to take in.
struct CatalogueDownload {
    CatalogueEntry entry;
    std::filesystem::path path; // in the Songs folder
    std::uint64_t content_hash = 0;
};

class CatalogueService {
  public:
    struct Options {
        std::filesystem::path cache_dir; // the indexes and the previews' files
        std::filesystem::path songs_dir; // where downloads go, in a folder per catalogue
        std::unique_ptr<Fetcher> fetcher;
        std::chrono::milliseconds debounce{120};
        std::function<std::int64_t()> now_utc_s; // the wall clock (tests set it)
        std::size_t max_hits = 40;
        // The previews' files are kept up to this many bytes; past it the least recently used go
        // (never one a slot holds). Downloads in the Songs folder are not part of it.
        std::uint64_t cache_cap_bytes = 64u << 20;
    };

    explicit CatalogueService(Options options);
    ~CatalogueService();
    CatalogueService(const CatalogueService&) = delete;
    CatalogueService& operator=(const CatalogueService&) = delete;

    // Reads the caches (off the frame thread); the network is not touched until set_online(true).
    void start();
    // The player's choice: the catalogues may be fetched (indexes and files). Off cancels what is
    // under way; the caches stay searchable.
    void set_online(bool on);
    bool online() const { return online_; }
    // Fetches every index again now, whatever its age (still only while online).
    void refresh();

    // The search text, each frame or as it changes. Allocates nothing once warm (texts up to
    // `kMaxQueryBytes`; longer ones are cut).
    void search(std::string_view query);
    static constexpr std::size_t kMaxQueryBytes = 256;

    // Once a frame: takes what the threads finished (a new snapshot, new hits, files' states).
    // True when something the screen shows changed. Allocates nothing unless something did.
    bool poll();

    // The snapshot the hits index (null before any index is read).
    const CatalogueSnapshot* snapshot() const { return snapshot_.get(); }
    std::span<const CatalogueHit> hits() const { return hits_; }
    // Changes whenever poll() took new hits (and with them the snapshot they index).
    std::uint64_t hits_serial() const { return taken_hits_; }
    // The text the hits are for; differs from the latest search() while one is under way.
    std::string_view hits_query() const { return hits_query_; }
    bool searching() const;
    const CatalogueIndexState& index_state(CatalogueId id) const {
        return states_[static_cast<std::size_t>(id)];
    }
    // The time the latest search took on its thread, and the most it took.
    std::int64_t last_search_ns() const { return last_search_ns_; }

    // Files, by entry of the current snapshot.
    void preview(std::uint32_t entry);
    void download(std::uint32_t entry);
    void cancel(std::uint32_t entry);
    CatalogueFile file(std::uint32_t entry) const;
    // Why it failed (empty unless Failed).
    std::string_view file_reason(std::uint32_t entry) const;
    // The previewed file, once fetched (empty until Ready).
    std::filesystem::path file_path(std::uint32_t entry) const;
    // Downloads finished since the last call, oldest first.
    std::optional<CatalogueDownload> take_download();
    // Forgets a kept file's state (the song was deleted from the library: it can be downloaded
    // again).
    void forget(std::uint64_t key);

    // Tests: waits until no work is queued or under way, up to `timeout`.
    bool wait_idle(std::chrono::milliseconds timeout);
    // Fetches made so far (tests).
    int fetches() const { return fetches_.load(); }

    // The largest file and index page fetched.
    static constexpr std::uint64_t kMaxFileBytes = 8u << 20;
    static constexpr std::uint64_t kMaxPageBytes = 4u << 20;

  private:
    struct Slot {
        std::uint64_t key = 0;
        CatalogueEntry entry;
        CatalogueFile file;
        std::string reason;
        std::filesystem::path path;
        bool cancel = false;
        std::uint64_t used = 0; // when it was last asked for, for the eviction
    };
    static constexpr std::size_t kSlots = 16;

    void queue(std::uint32_t entry, bool keep);
    // mutex_ held: the next queued file, marked Fetching; false when none is left.
    bool pop_file(std::size_t& at);
    void search_main();
    void net_main();
    void read_caches();
    void publish(CatalogueId id, std::vector<CatalogueEntry> entries, std::int64_t fetched,
                 CatalogueIndexState::Kind kind, std::string reason);
    bool fetch_index(CatalogueId id, std::vector<CatalogueEntry>& out, std::string& reason);
    bool run_file(std::size_t slot);
    // Removes the least recently used preview files past the cap.
    void sweep_cache();
    std::filesystem::path cache_file(const CatalogueEntry& e) const;
    std::filesystem::path index_file(CatalogueId id) const;
    Slot* slot_of(std::uint64_t key);
    const Slot* slot_of(std::uint64_t key) const;
    std::uint64_t key_of(std::uint32_t entry) const;

    Options options_;
    std::atomic<bool> online_{false};
    std::atomic<int> fetches_{0};

    // The frame thread's view.
    std::shared_ptr<const CatalogueSnapshot> snapshot_;
    std::vector<CatalogueHit> hits_;
    std::string hits_query_;
    std::array<CatalogueIndexState, kCatalogueCount> states_{};
    std::uint64_t taken_hits_ = 0;
    std::uint64_t taken_files_ = 0;
    std::int64_t last_search_ns_ = 0;
    std::array<Slot, kSlots> view_slots_{}; // the files' states as last taken

    // Shared with the threads, under mutex_.
    mutable std::mutex mutex_;
    std::condition_variable search_wake_;
    std::condition_variable net_wake_;
    bool stop_ = false;
    std::string query_;            // the latest text
    std::uint64_t query_serial_ = 0;
    std::chrono::steady_clock::time_point query_at_{};
    std::shared_ptr<const CatalogueSnapshot> shared_snapshot_;
    std::uint64_t snapshot_serial_ = 0;
    std::vector<CatalogueHit> shared_hits_;
    std::shared_ptr<const CatalogueSnapshot> shared_hits_snapshot_;
    std::shared_ptr<const CatalogueSnapshot> retired_; // the frame's old one, let go by the search thread
    std::string shared_hits_query_;
    std::uint64_t hits_serial_ = 0;
    std::int64_t shared_search_ns_ = 0;
    bool searching_ = false;
    std::array<CatalogueIndexState, kCatalogueCount> shared_states_{};
    // Per catalogue; the network thread's alone (not under mutex_).
    std::array<std::vector<CatalogueEntry>, kCatalogueCount> indexes_;
    std::array<Slot, kSlots> slots_{};
    std::uint64_t files_serial_ = 0;
    std::uint64_t use_clock_ = 0;
    std::deque<std::size_t> file_queue_; // slots waiting
    bool busy_ = false;                  // the network thread is working
    bool refresh_all_ = false;
    std::array<std::int64_t, kCatalogueCount> next_try_utc_s_{};
    std::deque<CatalogueDownload> downloads_;
    bool caches_read_ = false;

    std::thread search_thread_;
    std::thread net_thread_;
};

} // namespace etude::app
