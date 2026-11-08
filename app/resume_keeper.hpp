// SPDX-License-Identifier: GPL-3.0-or-later
// Keeps the resume point (core resume_point.hpp) on disk while the app runs: read at the start, written
// on a thread of its own every few seconds and at every change of screen (a temp file, fsync, rename:
// `write_file_atomic`), and once more, marked clean, at a normal close. The frame only builds the state
// and asks the pacer; the disk is never touched on the frame thread. No SDL.
#pragma once

#include "etude/resume_point.hpp"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace etude::app {

inline constexpr const char* kResumeFile = "resume.txt";

// The file as the next start finds it: null when there is none; a file that is not a regular file of
// a size that fits (a link, a folder, a pipe, too big) reads as one that did not parse.
std::optional<ResumeParse> read_resume_file(const std::filesystem::path& file);

// The folders a song may be reopened from, as `plan_resume` takes them. A song's path is kept as an absolute,
// lexically normal path, so each folder (the song library's, the data folder) is given the same way, made
// absolute against the working folder with `.` and `..` resolved, and also with its links resolved
// (`weakly_canonical`) when that is another path; a folder given with `.`, `..` or relative would
// otherwise match no song at all and refuse every one.
std::vector<std::string> resume_song_roots(const std::vector<std::filesystem::path>& folders);

class ResumeKeeper {
  public:
    ResumeKeeper() = default;
    ~ResumeKeeper();
    ResumeKeeper(const ResumeKeeper&) = delete;
    ResumeKeeper& operator=(const ResumeKeeper&) = delete;

    // Begins the run: `first` (the point the run is about to reopen, or the defaults) is written at once
    // as this run's own, unclean, having run 0 ms. `now_ms` is a clock that only goes forward (the
    // steady clock), the run's start for the pacer and for how long the run has run; the wall clock is
    // never read. Nothing else happens until `note`. With `enabled` false (a developer run) the keeper
    // does nothing at all.
    void begin(const std::filesystem::path& file, bool enabled, const ResumePoint& first, std::int64_t now_ms);
    bool active() const { return active_; }

    // Every frame once the state is the one to keep: writes when the pacer says so. `now_ms` is the clock
    // of `begin`.
    void note(const SessionState& session, const std::string& screen, std::int64_t now_ms);

    // The normal close: the last point, marked clean, is on disk when this returns.
    void close(const SessionState& session, const std::string& screen, std::int64_t now_ms);

  private:
    std::int64_t ran_ms(std::int64_t now_ms) const;
    void queue(const ResumePoint& point);
    void stop();
    void run();

    bool active_ = false;
    std::filesystem::path file_;
    std::int64_t started_ms_ = 0;
    etude::ResumePacer pacer_;
    SessionState last_session_;
    std::string last_screen_;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<std::string> pending_;
    bool stop_ = false;
    std::thread thread_;
};

} // namespace etude::app
