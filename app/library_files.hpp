// SPDX-License-Identifier: GPL-3.0-or-later
// Moves song files for the library screen, on a thread of its own: into another folder of the
// Songs folder (Move), or out of the library into "Removed songs" in the settings folder (Delete),
// from where the player can take them back. Nothing is ever deleted. The frame thread asks and
// later takes the answer; it never waits for a file.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace etude::app {

// Moves `file` into the folder `to` (made when missing), under its own name or, when that is
// taken, "name (2).ext" and so on. The rename never replaces a file, even one made while it runs;
// across disks it copies (never over a file) and removes the original, and a copy it made is
// removed again when that fails. A file already in `to` stays (AlreadyThere). `error` says why a
// move failed.
enum class MoveOutcome : unsigned char { Moved, AlreadyThere, Failed };
struct MoveDone {
    MoveOutcome outcome = MoveOutcome::Failed;
    std::filesystem::path now; // where the file is: Moved and AlreadyThere
};
// `name`, when given, is the file's name in `to` (the upload page stores a temporary file under
// the name the player sent); empty keeps the file's own.
MoveDone move_into(const std::filesystem::path& file, const std::filesystem::path& to,
                   std::string* error, std::string_view name = {});
// Renames `file` inside its folder to `new_name`, never over another file: false, with the reason
// in `error`, when the name is taken or the rename fails.
bool rename_in_place(const std::filesystem::path& file, const std::string& new_name,
                     std::string* error);
// The names tried in a folder: the file's own, then "(2)" up to this.
inline constexpr int kMaxNameTries = 1000;

struct FileJob {
    enum class Kind : unsigned char { Move, Remove };
    Kind kind = Kind::Move;
    // Each file and the folder it goes into.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files;
};

struct FileResult {
    FileJob::Kind kind = FileJob::Kind::Move;
    std::size_t moved = 0;
    std::size_t skipped = 0; // already in the folder
    std::size_t failed = 0;
    std::string error; // the first failure's reason
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> done; // from, to
};

// Jobs still queued when it is destroyed (the app quits) are done first, for up to kDrainLimit.
class LibraryFiles {
  public:
    LibraryFiles();
    ~LibraryFiles();
    LibraryFiles(const LibraryFiles&) = delete;
    LibraryFiles& operator=(const LibraryFiles&) = delete;

    // Queues a job. Never waits.
    void request(FileJob job);
    // A finished job's result, oldest first, once. Never waits.
    std::optional<FileResult> take();
    // Jobs asked for and not taken yet.
    bool busy() const;
    // Waits until every job asked for is done (tests).
    void wait_idle();

  private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<FileJob> jobs_;
    std::vector<FileResult> results_;
    std::size_t open_ = 0; // asked for, not taken
    bool working_ = false;
    bool quit_ = false;
    std::chrono::steady_clock::time_point drain_until_{};
    static constexpr std::chrono::seconds kDrainLimit{2};
    std::thread thread_;
};

} // namespace etude::app
