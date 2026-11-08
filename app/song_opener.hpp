// SPDX-License-Identifier: GPL-3.0-or-later
// Opens a song file off the frame thread. The frame thread asks for a song and keeps drawing the
// old one; a loader thread reads and parses the file, and the frame thread takes the finished song
// on a later frame. Asking again before the answer is back replaces the earlier request: a request
// that has not started never starts, and the answer of one that has is dropped. With a plan the
// loader also builds the song's practice tables (SongController::prepare), so taking the song in
// costs the frame thread little. `SavedSongRestore` brings back the saved session's song through
// the opener. No SDL: std only, so the tests use it.
#pragma once

#include "song_library.hpp"

#include "etude/session_restore.hpp"
#include "etude/session_state.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace etude::app {

// An open that takes longer than this shows an "Opening..." state; a quicker one shows nothing.
inline constexpr std::int64_t kOpeningShownNs = 150'000'000;

// True once an open that began `elapsed_ns` ago should show the state (exactly at the limit).
constexpr bool opening_shown(std::int64_t elapsed_ns) { return elapsed_ns >= kOpeningShownNs; }

// The status text of an open in flight: "Opening NAME...".
std::string opening_text(const std::filesystem::path& file, std::string_view tune);

class SongOpener {
  public:
    // How a request is loaded; load_song_file by default. A test passes its own.
    using LoadFn =
        std::function<SongFileLoad(const std::filesystem::path&, const SongLoadOptions&)>;

    // `delay`: the loader waits this long before each load (a developer's slow card); quitting or a
    // newer request ends the wait.
    explicit SongOpener(LoadFn load = {}, std::chrono::milliseconds delay = {});
    ~SongOpener();
    SongOpener(const SongOpener&) = delete;
    SongOpener& operator=(const SongOpener&) = delete;

    // A finished request: the file as asked for and what loading it gave.
    struct Opened {
        std::filesystem::path path;
        std::string tune;
        bool found = false; // the path names a regular file (checked on the loader's thread)
        SongFileLoad load;
        PreparedSong prepared; // valid when the request had a plan and the song loaded
    };

    // Asks for `path` (its tune in `options.tune`, and everything else the load needs, decided by
    // the caller: the loader thread reads no state of the caller's). With a `plan`, the song's
    // practice tables are built after it loads. Never waits. Replaces the request before it, if
    // that has not been taken yet.
    void request(const std::filesystem::path& path, SongLoadOptions options,
                 std::optional<SongLoadPlan> plan = std::nullopt);
    // The answer to the latest request, once; empty while it is still loading, when nothing was
    // asked and when the request was cancelled or replaced. Never waits.
    std::optional<Opened> take();
    // A request was made and its answer not taken (or cancelled) yet.
    bool busy() const;
    // Forgets the latest request: its answer, ready or not, is never returned.
    void cancel();
    // Loads begun, finished and dropped as replaced since the start (tests).
    std::uint64_t started() const;
    std::uint64_t finished() const;

  private:
    struct Job {
        std::uint64_t ticket = 0;
        std::filesystem::path path;
        SongLoadOptions options;
        std::optional<SongLoadPlan> plan;
    };
    void run();

    LoadFn load_;
    std::chrono::milliseconds delay_{};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Job> next_;    // the request the loader has not started
    std::optional<Opened> done_; // the answer to the latest request, until taken
    std::uint64_t latest_ = 0;   // ticket of the latest request; 0: none wanted
    std::uint64_t ticket_ = 0;
    std::uint64_t started_ = 0;
    std::uint64_t finished_ = 0;
    bool stop_ = false;
    std::thread thread_; // started by the first request
};

// The saved session's song, brought back through a SongOpener so no file is read on the frame
// thread. The library path is tried first, then the absolute one, one read at a time; each is
// checked by content hash, and `choose_song_source` picks the answer exactly as it always has: the
// first that is found, loads and has the saved hash, else the library path unless only the
// absolute one found a file.
class SavedSongRestore {
  public:
    struct Request {
        std::optional<SongPath> library;  // where the saved library path resolves, if anywhere
        std::optional<SongPath> absolute; // the saved absolute path; empty when none was saved
        SessionState saved;               // its paths and hash say which candidates count
        // How a candidate is read besides its bytes (the ABC tune the library listed, the chord
        // setting), decided on the frame thread.
        std::function<SongLoadOptions(const SongPath&)> options;
        std::optional<SongLoadPlan> plan;
    };
    struct Result {
        SongSource source = SongSource::Library;
        std::optional<SongPath> song; // the chosen candidate; empty when it had no path to try
        SongProbe probe;              // what was found of it
        SongFileLoad load;            // its load, ok or not
        PreparedSong prepared;
    };

    // Starts a restore (any one still running is forgotten) and asks `opener` for the first
    // candidate. The opener is the restore's alone until it ends: a request made to it meanwhile
    // replaces this one, so the caller calls abandon() with it.
    void begin(SongOpener& opener, Request request);
    // A restore has begun and has no result yet.
    bool active() const { return stage_ != Stage::Idle; }
    // Once a frame: takes the opener's answer, asks for the absolute candidate when the library's
    // is not good, and returns the result once it is decided. Never waits.
    std::optional<Result> poll(SongOpener& opener);
    // Forgets the restore; the answer of a read in flight is not used (the caller's own request
    // has replaced it, or it calls opener.cancel()).
    void abandon() { stage_ = Stage::Idle; }

  private:
    enum class Stage : std::uint8_t { Idle, Library, Absolute };
    struct Candidate {
        std::optional<SongPath> song;
        SongProbe probe;
        SongFileLoad load;
        PreparedSong prepared;
    };
    void ask(SongOpener& opener, const SongPath& song);
    bool good(const Candidate& c) const;
    std::optional<Result> decide();

    Stage stage_ = Stage::Idle;
    Request request_;
    Candidate library_;
    Candidate absolute_;
};

} // namespace etude::app
