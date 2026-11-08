// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_files.hpp"
#include "log_ring.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio> // renamex_np, renameat2
#include <string_view>
#include <system_error>

#if defined(__linux__)
#include <fcntl.h> // AT_FDCWD
#endif

namespace etude::app {

namespace fs = std::filesystem;

namespace {

enum class Step : unsigned char { Done, Taken, OtherDisk, Failed };

// Renames `from` to `to` unless `to` exists (then Taken, and nothing changes): one call that
// cannot replace a file made meanwhile. A file system with no such rename gets a hard link, then
// the old name goes. OtherDisk: no rename across disks.
Step rename_no_replace(const fs::path& from, const fs::path& to, std::error_code& ec) {
    ec.clear();
#if defined(__APPLE__)
    if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0) {
        return Step::Done;
    }
    const int e = errno;
    if (e == EEXIST) {
        return Step::Taken;
    }
    if (e == EXDEV) {
        return Step::OtherDisk;
    }
    if (e != ENOTSUP && e != EINVAL) {
        ec.assign(e, std::generic_category());
        return Step::Failed;
    }
#elif defined(__linux__)
    if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) {
        return Step::Done;
    }
    const int e = errno;
    if (e == EEXIST) {
        return Step::Taken;
    }
    if (e == EXDEV) {
        return Step::OtherDisk;
    }
    if (e != EINVAL && e != ENOSYS) {
        ec.assign(e, std::generic_category());
        return Step::Failed;
    }
#endif
    fs::create_hard_link(from, to, ec);
    if (ec == std::errc::file_exists) {
        return Step::Taken;
    }
    if (ec) {
        // No links here (a memory stick's file system) or another disk: a copy.
        ec.clear();
        return Step::OtherDisk;
    }
    fs::remove(from, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(to, ignored); // the link this call made; the file keeps its old name
        return Step::Failed;
    }
    return Step::Done;
}

// Copies `from` to `to` unless `to` exists (Taken), then removes `from`. A copy this call made is
// removed again when the move cannot finish; a file that was there before is never touched.
Step copy_then_remove(const fs::path& from, const fs::path& to, std::error_code& ec) {
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::none, ec);
    if (ec == std::errc::file_exists) {
        return Step::Taken;
    }
    std::error_code ignored;
    if (ec) {
        // It failed after making the file (a full disk): what is there is this call's.
        if (ec != std::errc::file_exists && fs::exists(to, ignored)) {
            fs::remove(to, ignored);
        }
        return Step::Failed;
    }
    fs::remove(from, ec);
    if (ec) {
        fs::remove(to, ignored);
        return Step::Failed;
    }
    return Step::Done;
}

} // namespace

MoveDone move_into(const fs::path& file, const fs::path& to, std::string* error,
                   std::string_view name) {
    const auto fail = [&](std::string why) {
        if (error != nullptr) {
            *error = std::move(why);
        }
        return MoveDone{MoveOutcome::Failed, {}};
    };
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return fail("The file is not there any more");
    }
    fs::create_directories(to, ec);
    if (!fs::is_directory(to, ec)) {
        return fail("Could not make the folder " + to.filename().string());
    }
    if (fs::equivalent(file.parent_path(), to, ec)) {
        return {MoveOutcome::AlreadyThere, file};
    }
    const fs::path own_name = name.empty() ? file.filename() : fs::path(std::string(name));
    const std::string stem = own_name.stem().string();
    const std::string ext = own_name.extension().string();
    bool other_disk = false;
    for (int n = 1; n < kMaxNameTries; ++n) {
        const fs::path target =
            n == 1 ? to / own_name : to / (stem + " (" + std::to_string(n) + ")" + ext);
        Step step = other_disk ? Step::OtherDisk : rename_no_replace(file, target, ec);
        if (step == Step::OtherDisk) {
            other_disk = true;
            step = copy_then_remove(file, target, ec);
        }
        switch (step) {
        case Step::Done:
            return {MoveOutcome::Moved, target};
        case Step::Taken:
            continue; // the next name
        case Step::Failed:
            return fail("Could not move " + file.filename().string() + ": " + ec.message());
        case Step::OtherDisk:
            break;
        }
    }
    return fail("Too many files of that name in " + to.filename().string());
}

bool rename_in_place(const fs::path& file, const std::string& new_name, std::string* error) {
    const auto fail = [&](std::string why) {
        if (error != nullptr) {
            *error = std::move(why);
        }
        return false;
    };
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return fail("The file is not there any more");
    }
    const fs::path target = file.parent_path() / new_name;
    switch (rename_no_replace(file, target, ec)) {
    case Step::Done:
        return true;
    case Step::Taken:
        return fail("A file with that name is already there");
    case Step::OtherDisk:
    case Step::Failed:
        break;
    }
    return fail("Could not rename " + file.filename().string() + (ec ? ": " + ec.message() : ""));
}

LibraryFiles::LibraryFiles() : thread_([this] { run(); }) {}

LibraryFiles::~LibraryFiles() {
    {
        // The moves asked for are finished first (a rename each, so this is quick), up to
        // kDrainLimit; what is left then is said on the way out and stays where it was.
        const std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
        drain_until_ = std::chrono::steady_clock::now() + kDrainLimit;
    }
    cv_.notify_all();
    thread_.join();
    std::size_t left = 0;
    for (const FileJob& job : jobs_) {
        left += job.files.size();
    }
    if (left > 0) {
        log_printf( "library: %zu song file(s) were not moved before quitting\n", left);
    }
}

void LibraryFiles::request(FileJob job) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
        ++open_;
    }
    cv_.notify_all();
}

std::optional<FileResult> LibraryFiles::take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (results_.empty()) {
        return std::nullopt;
    }
    FileResult r = std::move(results_.front());
    results_.erase(results_.begin());
    --open_;
    return r;
}

bool LibraryFiles::busy() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return open_ > 0;
}

void LibraryFiles::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return jobs_.empty() && !working_; });
}

void LibraryFiles::run() {
    for (;;) {
        FileJob job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return quit_ || !jobs_.empty(); });
            if (jobs_.empty() || (quit_ && std::chrono::steady_clock::now() >= drain_until_)) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.erase(jobs_.begin());
            working_ = true;
        }
        FileResult r;
        r.kind = job.kind;
        for (const auto& [file, to] : job.files) {
            std::string error;
            const MoveDone done = move_into(file, to, &error);
            switch (done.outcome) {
            case MoveOutcome::Moved:
                ++r.moved;
                r.done.emplace_back(file, done.now);
                break;
            case MoveOutcome::AlreadyThere:
                ++r.skipped;
                break;
            case MoveOutcome::Failed:
                ++r.failed;
                if (r.error.empty()) {
                    r.error = std::move(error);
                }
                break;
            }
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            results_.push_back(std::move(r));
            working_ = false;
        }
        cv_.notify_all();
    }
}

} // namespace etude::app
