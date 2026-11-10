// SPDX-License-Identifier: GPL-3.0-or-later
// Song files on disk: reading one into a LoadedSong (MIDI, MusicXML, .mxl, .mscz, ABC, told apart
// by their bytes), and a library of the songs in a few folders, for next/previous song and for the
// session's song path, which is kept relative to the library. A future song browser would replace
// the minimal part. No SDL: std::filesystem only, so the tests use it.
#pragma once

#include "loop_wake.hpp"
#include "loaded_song.hpp"

#include "etude/abc.hpp"
#include "etude/library.hpp"
#include "etude/library_state.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace etude::app {

// Larger files are not songs (the zip loaders have their own, larger, limits inside).
inline constexpr std::uintmax_t kMaxSongFileBytes = 64u * 1024 * 1024;

// The path as an absolute '/' separated text, for the session's fallback path; empty when it
// can't be made absolute.
std::string absolute_path_text(const std::filesystem::path& path);

// An ABC tune the library has already found in its file: where its text is and what its identity
// is, so opening it reads that text alone (and the file header) and neither lists nor hashes the
// file again. It is good while the file still has the size and time it had when it was listed and
// the bytes read for it hash to the listing's hash (else the file is read whole).
struct AbcKnown {
    AbcTune tune;
    bool first = false; // the first tune of the file, which an empty SongLoadOptions::tune names
    std::size_t tune_count = 1; // tunes in the file
    std::uint64_t file_size = 0;
    std::int64_t file_mtime = 0; // as LibraryFile::mtime
};

// What the library knows of an ABC file and one of its tunes.
enum class AbcStatus : std::uint8_t {
    NotInLibrary, // the library has no entry for the file (outside its folders, or no such file)
    Pending,      // the file is listed but the worker has not read it yet
    Listed,       // the worker read the file and it has that tune
    NoSuchTune,   // the worker read the file and it has no such tune
};

// What start-up does with a saved ABC tune that may not be listed yet.
enum class AbcRestoreStep : std::uint8_t {
    Restore,  // bring the song back now (reads it through the listing, or from the file itself)
    Wait,     // the worker is still reading the file: ask again after the next poll()
    NotFound, // the file or the tune is not there: say so and carry on with no song
    Cancel,   // another song was opened meanwhile: the saved one is not restored
};
struct AbcRestoreState {
    bool abc = false;              // the saved song is an ABC file
    bool scripted = false;         // a scripted run restores at once
    bool cancelled = false;        // another song was opened while waiting
    bool library_pending = false;  // the library still has songs to parse
    AbcStatus status = AbcStatus::NotInLibrary;
};
// Waiting is only for a file the library has and the worker has not read, and only while the worker
// has work left: otherwise nothing would ever end the wait.
AbcRestoreStep decide_abc_restore(const AbcRestoreState& state);

// What to load from a file beyond its bytes.
struct SongLoadOptions {
    // ABC: the tune to open, as AbcTune::address ("X=3"); empty opens the file's first tune. Other
    // formats ignore it.
    std::string tune;
    // ABC: chord symbols become a left-hand part (the `abc_chords` setting).
    bool abc_chords = false;
    // ABC: the tune as the library listed it (SongLibrary::abc_known). load_song_file() uses it
    // when the file is unchanged since, else reads and lists the whole file.
    std::optional<AbcKnown> known;
};

struct SongFileLoad {
    bool ok = false;
    LoadedSong song;                   // library_path empty: the caller sets it
    SongEntry entry;                   // title, length, range, difficulty
    std::string error;                 // one line for the status row when !ok
    std::vector<std::string> warnings; // what the loader had to guess (a score without divisions)
    SongFileFormat format = SongFileFormat::Unknown; // what the bytes are, by content
};

// The hand split of `unsplit` (a song as its file gave it: the open song with
// LoadedSong::file_hands put back) under `options`: the answer of an earlier call for the same file
// and options when the cache holds one, else made now and kept. Safe from any thread. Put it on a
// song with apply_hand_split(song, *split, true).
std::shared_ptr<const HandSplit> hand_split_for(const Song& unsplit, std::uint64_t hash,
                                                bool abc_chords, const HandSplitOptions& options);

// Loads a song from its bytes; `file_name` gives the title when the file has none (for an ABC tune
// the T: line wins). Notes with no hand get the smart hand split. For an ABC file the song's hash
// is the tune's (AbcTune::hash). Never throws.
SongFileLoad load_song_bytes(std::span<const std::uint8_t> bytes, std::string_view file_name,
                             const SongLoadOptions& options = {});
// Reads the file and loads it (for an ABC tune in `options.known`, only the tune's text and the
// file header). A path that ends in an ABC address ("folk.abc#X=3") and names no
// file as written opens that tune of the file. Never throws.
SongFileLoad load_song_file(const std::filesystem::path& path, const SongLoadOptions& options = {});

// A song named as the player typed it or a session saved it: a file, and for an ABC file the tune
// in it. A text that names a file as written is that file; otherwise an address after the last '#'
// (split_abc_location) is cut off.
struct SongPath {
    std::filesystem::path path;
    std::string tune; // empty for the file's first tune, or any other format
};
SongPath split_song_path(std::string_view text);
// The whole file, or false (missing, unreadable, too large) with a short reason.
bool read_song_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out,
                     std::string* error);

// True for the extensions a song file may have (.mid .midi .kar .smf .rmi .musicxml .xml .mxl .mscz
// .mscx .abc, any case).
bool is_song_file_name(const std::filesystem::path& path);

// The library lists its folders first (names, sizes and times only: no file is opened) and parses
// the songs afterwards on a worker thread, so the first frame never waits for a song file. The list
// is in name order from the start; `poll()` brings finished parses into `state()`.
//
// An ABC file is one entry until the worker has read it. A file of several tunes is then replaced,
// in place, by one entry per tune (`folk.abc#X=3`, in tune order), published through `poll()`
// like any parse; the order of the list stays the order of the names. Indices into the list are
// therefore valid until a `poll()` returns true: the caller looks its song up again by path and
// tune (`index_of`) after that. The worker reads, hashes and lists each file once; opening an ABC
// tune (`abc_known`) needs none of that again.
//
// Threads: the frame thread calls everything except the worker's own functions. `scan()`,
// `start_loading()`, `load_all_now()` and `wait_until_settled()` may wait for the worker and are
// for start-up and tools; `poll()`, `set_abc_chords()` and the lookups never wait for it.
class SongLibrary {
  public:
    // The folders, first first: a relative path is looked up in each in this order.
    explicit SongLibrary(std::vector<std::filesystem::path> roots);
    ~SongLibrary();
    SongLibrary(const SongLibrary&) = delete;
    SongLibrary& operator=(const SongLibrary&) = delete;

    // Lists every song file in the folders and their subfolders (missing folders are skipped; the
    // same relative path in an earlier folder wins) from the directory alone: no file is opened.
    // Every song is Pending, or keeps its result when its name, size and time are unchanged since
    // the last scan (an ABC file already split into tunes stays split). Returns the number of
    // listed songs. Stops a running worker; call start_loading() afterwards.
    std::size_t scan();
    // The same listing on a thread of its own (the app moved, removed or added files): the new
    // list comes in at a later poll(), which then has the worker read the songs new to it. Never
    // waits; a call while a listing runs lists again after it.
    void rescan();
    // A rescan has not come in yet.
    bool rescanning() const;
    // Parses the Pending songs on a worker thread, in list order.
    void start_loading();
    // Parses them on this thread (tests, tools). Returns when none is Pending.
    void load_all_now();
    // Applies the worker's finished parses and file listings; true when the state changed or the
    // list was rearranged. Cheap when there is nothing. Restarts the worker after
    // set_abc_chords() once the old pass has finished.
    bool poll();
    // Blocks until every song is parsed or failed and applied (tests, tools).
    void wait_until_settled();

    const LibraryState& state() const { return state_; }
    std::size_t size() const { return state_.items.size(); }
    // The file of item `i` (absolute), and the tune in it ("" for a file that is not a collection
    // of ABC tunes).
    const std::filesystem::path& path_at(std::size_t i) const { return paths_[i]; }
    const std::string& tune_at(std::size_t i) const { return tunes_[i]; }
    // ABC chord symbols as a left-hand part (the setting): the ABC songs are parsed again when it
    // changes, so their hands and difficulty match what opens. Never waits: the running pass is
    // told to stop and poll() starts the new one once it has.
    void set_abc_chords(bool on);
    bool abc_chords() const { return abc_chords_.load(); }
    int unreadable() const { return static_cast<int>(state_.failed()); }
    const std::vector<std::filesystem::path>& roots() const { return roots_; }
    // Asks the worker to read this file next (a song to restore); no effect when it has no job
    // for it. Never waits.
    void prioritize(const std::filesystem::path& file);
    // One-line notices the listing produced (a file with more tunes than are listed), each
    // returned once.
    std::vector<std::string> take_notices();

    // The tune as the worker listed it, for SongLoadOptions::known; empty until the worker has
    // read that file. `tune` empty is the file's first tune.
    std::optional<AbcKnown> abc_known(const std::filesystem::path& file,
                                      std::string_view tune = {}) const;
    // The same for item `i`, by the key its listing made (no path is made canonical).
    std::optional<AbcKnown> abc_known_at(std::size_t i) const;
    // What the library knows of that file and tune, without reading anything.
    AbcStatus abc_status(const std::filesystem::path& file, std::string_view tune = {}) const;

    // `file` relative to the first folder that holds it, '/' separated; empty when none does. With
    // `tune` (an ABC address) it is the listed song's path when the file is listed per tune
    // (`folk.abc#X=3`), else the file's.
    std::string relative_path(const std::filesystem::path& file, std::string_view tune = {}) const;
    // The file a saved relative path names, in the first folder that has it. An ABC address after
    // the file name is left off (resolve_song gives it).
    std::optional<std::filesystem::path> resolve(std::string_view relative) const;
    // The same with the address.
    std::optional<SongPath> resolve_song(std::string_view relative) const;
    // Position of a song in the list, by path and tune; a tune the list does not have (yet) for
    // that file gives the file's first song. One map lookup after the path is made canonical.
    std::optional<std::size_t> index_of(const std::filesystem::path& file,
                                        std::string_view tune = {}) const;
    // The song after (step 1) or before (step -1) `current`, wrapping and passing over songs that
    // failed to parse; the first song when `current` is not in the library. Empty when there is
    // none.
    std::optional<std::size_t> step(std::optional<std::size_t> current, int step) const;

  private:
    struct Job {
        std::string relative; // the item's name in the list
        std::uint64_t stamp = 0;
        std::filesystem::path path;
        std::string tune;
    };
    // What the worker hands over. An Expand lists a file's tunes (the entry for the file is
    // replaced by one per tune); the Entry results that follow are for those. A result carries the
    // file's listing too when it is an ABC file, for abc_known().
    struct Result {
        enum class Kind : std::uint8_t { Entry, Expand };
        Kind kind = Kind::Entry;
        std::string relative;
        std::uint64_t stamp = 0;
        std::uint64_t generation = 0;
        bool ok = false;
        SongEntry entry;
        std::string error;
        bool has_listing = false;
        std::vector<AbcTune> tunes;
        std::uint64_t file_size = 0;
        std::int64_t file_mtime = 0;
    };
    // The file the last jobs read, so the tunes of a big ABC file are read, hashed and listed once.
    struct FileCache {
        std::filesystem::path path;
        bool valid = false;
        std::vector<std::uint8_t> bytes;
        std::string error;
        std::uint64_t hash = 0;
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        SongFileFormat format = SongFileFormat::Unknown;
        std::vector<AbcTune> tunes;
        bool listing_sent = false; // the listing went with an earlier result
    };
    // An ABC file as the worker listed it.
    struct AbcFile {
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        std::vector<AbcTune> tunes;
    };
    // The ABC files whose tunes became their own entries, to keep them across a scan.
    struct Expanded {
        std::uint64_t stamp = 0;
        std::vector<std::string> addresses;
    };
    // A song file the folders hold.
    struct Found {
        std::filesystem::path path;
        LibraryFile file;
        std::string tune; // ABC: the tune's address when the file is listed per tune
        std::string key;  // the file, canonical
    };
    // Lists the folders (any thread): the files only, before any ABC file is split.
    static std::vector<Found> list_folders(const std::vector<std::filesystem::path>& roots,
                                           const std::vector<std::filesystem::path>& root_keys);
    // Makes that listing the list, keeping what is known of the songs in it.
    void adopt(std::vector<Found> found);
    void stop_lister();
    // The setting and generation a pass of the worker runs under.
    struct JobContext {
        std::uint64_t generation = 0;
        bool abc_chords = false;
    };
    // Runs one job, handing each result to `emit` (worker thread, or the caller's in load_all_now).
    template <class Emit>
    void run_job(const Job& job, FileCache& file, const JobContext& ctx, Emit&& emit);
    Result parse_entry(const std::string& relative, std::uint64_t stamp, const std::string& tune,
                       FileCache& file, const JobContext& ctx);
    static void load_file(const std::filesystem::path& path, FileCache& file);
    void invalidate_abc();
    std::vector<Job> pending_jobs() const;
    void launch_worker();
    void stop_worker();
    bool apply(Result& r);
    bool expand_all(std::vector<Result>& batch);
    void ensure_index() const;
    std::string canonical_key(const std::filesystem::path& file) const;
    std::optional<AbcKnown> abc_known_by_key(const std::string& key, std::string_view tune) const;
    std::optional<std::size_t> index_by_key(const std::string& key, std::string_view tune) const;

    std::vector<std::filesystem::path> roots_;
    std::vector<std::filesystem::path> root_keys_; // canonical, parallel to roots_
    std::vector<std::filesystem::path> paths_;     // parallel to state_.items
    std::vector<std::string> tunes_;     // parallel to state_.items: an ABC address, or ""
    std::vector<std::string> file_keys_; // parallel to state_.items: the file, canonical
    LibraryState state_;
    std::atomic<bool> abc_chords_{false};
    std::uint64_t generation_ = 1; // counts the ABC setting's changes; results of an older one go
    bool restart_pending_ = false; // set_abc_chords() ran: poll() restarts the worker
    std::map<std::string, Expanded> expanded_; // by relative path of the file
    std::map<std::string, AbcFile> abc_files_; // by canonical path
    std::vector<std::string> notices_;
    // Lookups, rebuilt when the list changed (the frame thread only).
    mutable bool index_dirty_ = true;
    mutable std::unordered_map<std::string, std::size_t> by_relative_;
    mutable std::unordered_map<std::string, std::size_t> by_song_; // file key + '#' + tune
    mutable std::unordered_map<std::string, std::size_t> by_file_; // file key: its first song

    mutable std::mutex mutex_; // guards done_, priority_, cache_, finished_ and the lister's
    std::thread lister_;
    std::vector<Found> listed_; // a rescan's listing, until poll() takes it
    bool listed_ready_ = false;
    bool lister_running_ = false;
    bool rescan_again_ = false;
    bool relaunch_pending_ = false; // a listing came in: the worker reads its new songs next
    std::vector<Result> done_;
    std::vector<std::filesystem::path> priority_; // files to read next, oldest first
    EntryCache cache_;
    bool finished_ = true; // the worker has handed over every job it was given
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
};

} // namespace etude::app
