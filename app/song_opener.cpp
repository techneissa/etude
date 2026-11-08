// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_opener.hpp"
#include "crash_thread.hpp"
#include "loop_wake.hpp"

#include <utility>

namespace etude::app {

std::string opening_text(const std::filesystem::path& file, std::string_view tune) {
    std::string text = "Opening " + file.filename().string();
    if (!tune.empty()) {
        text += "#";
        text += tune;
    }
    text += "\xE2\x80\xA6"; // the ellipsis
    return text;
}

SongOpener::SongOpener(LoadFn load, std::chrono::milliseconds delay)
    : load_(std::move(load)), delay_(delay) {
    if (!load_) {
        load_ = [](const std::filesystem::path& path, const SongLoadOptions& options) {
            return load_song_file(path, options);
        };
    }
}

SongOpener::~SongOpener() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        next_.reset();
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SongOpener::request(const std::filesystem::path& path, SongLoadOptions options,
                         std::optional<SongLoadPlan> plan) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        latest_ = ++ticket_;
        done_.reset();
        next_ = Job{latest_, path, std::move(options), std::move(plan)};
        if (!thread_.joinable()) {
            thread_ = spawn_thread([this] { run(); });
        }
    }
    cv_.notify_one();
}

std::optional<SongOpener::Opened> SongOpener::take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!done_) {
        return std::nullopt;
    }
    std::optional<Opened> out = std::move(done_);
    done_.reset();
    latest_ = 0;
    return out;
}

bool SongOpener::busy() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return latest_ != 0;
}

void SongOpener::cancel() {
    const std::lock_guard<std::mutex> lock(mutex_);
    latest_ = 0;
    next_.reset();
    done_.reset();
}

std::uint64_t SongOpener::started() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return started_;
}

std::uint64_t SongOpener::finished() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return finished_;
}

void SongOpener::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        cv_.wait(lock, [this] { return stop_ || next_.has_value(); });
        if (stop_) {
            return;
        }
        Job job = std::move(*next_);
        next_.reset();
        if (delay_.count() > 0) {
            // Ends early on quit, or when a newer request has taken this one's place.
            cv_.wait_for(lock, delay_, [&] { return stop_ || job.ticket != latest_; });
            if (stop_) {
                return;
            }
            if (job.ticket != latest_) {
                continue; // replaced while waiting: never read
            }
        }
        ++started_;
        lock.unlock();
        Opened opened;
        opened.path = job.path;
        opened.tune = job.options.tune;
        try {
            std::error_code ec;
            opened.found = std::filesystem::is_regular_file(job.path, ec);
            opened.load = load_(job.path, job.options);
            if (opened.load.ok) {
                opened.load.song.fingerprint = song_fingerprint(opened.load.song.song);
                if (job.plan) {
                    opened.prepared = SongController::prepare(
                        opened.load.song.song, *job.plan, opened.load.song.fingerprint);
                }
            }
        } catch (...) { // a file too big for the memory left, say
            opened.load = SongFileLoad{};
            opened.prepared = PreparedSong{};
            opened.load.error = "Can't open " + job.path.filename().string() + ": out of memory";
        }
        lock.lock();
        ++finished_;
        if (job.ticket == latest_) { // not replaced or cancelled while it loaded
            done_ = std::move(opened);
            wake_loop(WakeSource::SongOpen);
        }
    }
}

void SavedSongRestore::begin(SongOpener& opener, Request request) {
    request_ = std::move(request);
    library_ = Candidate{};
    absolute_ = Candidate{};
    library_.song = request_.library;
    absolute_.song = request_.absolute;
    stage_ = Stage::Library;
    if (library_.song) {
        ask(opener, *library_.song);
    }
}

void SavedSongRestore::ask(SongOpener& opener, const SongPath& song) {
    opener.request(song.path, request_.options ? request_.options(song) : SongLoadOptions{},
                   request_.plan);
}

bool SavedSongRestore::good(const Candidate& c) const {
    return c.probe.found && c.probe.loaded && c.probe.hash_matches;
}

std::optional<SavedSongRestore::Result> SavedSongRestore::poll(SongOpener& opener) {
    if (stage_ == Stage::Idle) {
        return std::nullopt;
    }
    if (stage_ == Stage::Library && library_.song) {
        std::optional<SongOpener::Opened> opened = opener.take();
        if (!opened) {
            return std::nullopt;
        }
        library_.probe.found = opened->found;
        library_.probe.loaded = opened->load.ok;
        library_.probe.hash_matches =
            opened->load.ok &&
            (request_.saved.song_hash == 0 || opened->load.song.hash == request_.saved.song_hash);
        library_.load = std::move(opened->load);
        library_.prepared = std::move(opened->prepared);
    }
    if (stage_ == Stage::Library) {
        // The library's candidate is in (or there was none). The absolute path is tried only when
        // one was saved and the library's is not good.
        if (!request_.saved.song_abs_path.empty() && absolute_.song && !good(library_)) {
            stage_ = Stage::Absolute;
            ask(opener, *absolute_.song);
            return std::nullopt;
        }
        return decide();
    }
    std::optional<SongOpener::Opened> opened = opener.take();
    if (!opened) {
        return std::nullopt;
    }
    absolute_.probe.found = opened->found;
    absolute_.probe.loaded = opened->load.ok;
    absolute_.probe.hash_matches =
        opened->load.ok &&
        (request_.saved.song_hash == 0 || opened->load.song.hash == request_.saved.song_hash);
    absolute_.load = std::move(opened->load);
    absolute_.prepared = std::move(opened->prepared);
    return decide();
}

std::optional<SavedSongRestore::Result> SavedSongRestore::decide() {
    stage_ = Stage::Idle;
    Result r;
    r.source = choose_song_source(request_.saved, library_.probe, absolute_.probe);
    Candidate& chosen = r.source == SongSource::Library ? library_ : absolute_;
    r.song = std::move(chosen.song);
    r.probe = chosen.probe;
    r.load = std::move(chosen.load);
    r.prepared = std::move(chosen.prepared);
    return r;
}

} // namespace etude::app
