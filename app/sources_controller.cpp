// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources_controller.hpp"
#include "crash_thread.hpp"

#include "loop_wake.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace etude::app {

namespace {

void wake() { wake_loop(WakeSource::Sources); }

std::string plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// What to tell the player when a copy has ended, or empty when the watcher's "N new songs" is all.
std::string ending_text(ui::ImportEnd end, int copied, int duplicates, int failed, int total) {
    std::string text;
    if (end == ui::ImportEnd::Removed) {
        text =
            "USB stick removed. Copied " + std::to_string(copied) + " of " + std::to_string(total);
    } else if (end == ui::ImportEnd::Done && copied == 0 && duplicates > 0 && failed == 0) {
        text = duplicates == 1 ? "That song is already in your library"
                               : "Those songs are already in your library";
    }
    if (failed > 0 && end != ui::ImportEnd::None) {
        text +=
            (text.empty() ? "" : ". ") + plural(failed, "song", "songs") + " could not be copied";
    }
    return text;
}

} // namespace

struct SourcesController::Shared {
    explicit Shared(SourcesOptions o) : options(std::move(o)) {}

    SourcesOptions options;
    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable exited; // a thread has ended
    bool quit = false;
    bool refresh = true; // look again at the volumes now
    std::vector<Volume> volumes;
    Volume chosen;
    bool present = false;
    bool scanning = false;
    StickScan scan;
    std::vector<ui::SongMark> marks; // one for each song of `scan`
    bool importing = false;
    bool cancel = false;
    bool gone = false;       // the stick being copied from is no longer there
    bool look_after = false; // the volumes changed during a copy
    int import_total = 0;
    int import_done = 0;
    std::string current;
    ui::ImportEnd ended = ui::ImportEnd::None;
    int copied = 0;
    int duplicates = 0;
    int failed = 0;
    std::optional<std::string> notice;
    std::uint64_t version = 1;
    int running = 0; // threads started and not ended
    std::thread watcher;
    std::thread importer;

    ui::UsbView make_view() const;
    void watch();
    void rescan(std::unique_lock<std::mutex>& lock);
    void import_run(ImportJob job, std::vector<std::size_t> at);
    void start_import(const std::shared_ptr<Shared>& self, std::vector<std::size_t> at);
    void thread_ended();
};

ui::UsbView SourcesController::Shared::make_view() const {
    ui::UsbView v;
    v.present = present;
    v.scanning = scanning;
    v.label = chosen.label;
    v.songs = static_cast<int>(scan.songs.size());
    v.truncated = scan.truncated;
    v.list.reserve(scan.songs.size());
    for (std::size_t i = 0; i < scan.songs.size(); ++i) {
        v.list.push_back({scan.songs[i].path, scan.songs[i].size, marks[i]});
    }
    v.importing = importing;
    v.import_total = import_total;
    v.import_done = import_done;
    v.current = current;
    v.ended = ended;
    v.copied = copied;
    v.duplicates = duplicates;
    v.failed = failed;
    return v;
}

void SourcesController::Shared::thread_ended() {
    {
        const std::lock_guard<std::mutex> lock(mutex);
        --running;
    }
    exited.notify_all();
}

// The volumes changed or the page asked: the first volume that has songs shows (a stick with two
// partitions has one of them). The lock is held on entry and on return, and not while the folders
// are read.
void SourcesController::Shared::rescan(std::unique_lock<std::mutex>& lock) {
    present = !volumes.empty();
    scanning = present;
    scan = StickScan();
    marks.clear();
    ended = ui::ImportEnd::None;
    chosen = present ? volumes.front() : Volume();
    ++version;
    const std::vector<Volume> look = volumes;
    lock.unlock();
    wake();
    if (options.scan_hook) {
        options.scan_hook();
    }
    Volume pick = look.empty() ? Volume() : look.front();
    StickScan found;
    for (std::size_t i = 0; i < look.size() && i < 4 && found.songs.empty(); ++i) {
        StickScan one = scan_stick(look[i].path);
        if (i == 0 || !one.songs.empty()) {
            pick = look[i];
            found = std::move(one);
        }
    }
    lock.lock();
    if (!importing && present) {
        chosen = pick;
        scan = std::move(found);
        marks.assign(scan.songs.size(), ui::SongMark::None);
        scanning = false;
        ++version;
        lock.unlock();
        wake();
        lock.lock();
    }
}

void SourcesController::Shared::watch() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!quit) {
        lock.unlock();
        std::vector<Volume> now = list_volumes(options.roots);
        lock.lock();
        const bool changed = now != volumes;
        const bool look = changed || refresh;
        refresh = false;
        volumes = std::move(now);
        if (importing && std::find(volumes.begin(), volumes.end(), chosen) == volumes.end()) {
            gone = true; // the importer stops between two reads
        }
        look_after = look_after || (look && importing);
        if (look && !importing) {
            rescan(lock);
        }
        cv.wait_for(lock, std::chrono::milliseconds(options.poll_ms),
                    [this] { return quit || refresh; });
    }
}

void SourcesController::Shared::start_import(const std::shared_ptr<Shared>& self,
                                             std::vector<std::size_t> at) {
    ImportJob job;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (importing || !present || scanning || at.empty() || quit) {
            return;
        }
        for (const std::size_t i : at) {
            if (i >= scan.songs.size()) {
                return;
            }
        }
        job.stick = chosen.path;
        job.songs_folder = options.songs_folder;
        job.chunk = options.import_chunk;
        for (const std::size_t i : at) {
            job.songs.push_back(scan.songs[i].path);
            marks[i] = ui::SongMark::None;
        }
        importing = true;
        cancel = false;
        gone = false;
        ended = ui::ImportEnd::None;
        import_total = static_cast<int>(at.size());
        import_done = 0;
        copied = duplicates = failed = 0;
        current.clear();
        ++version;
        ++running;
    }
    if (importer.joinable()) {
        importer.join(); // the last job is over (importing was false)
    }
    importer = spawn_thread([self, job = std::move(job), at = std::move(at)]() mutable {
        self->import_run(std::move(job), std::move(at));
        self->thread_ended();
    });
    wake();
}

void SourcesController::Shared::import_run(ImportJob job, std::vector<std::size_t> at) {
    ImportHooks hooks;
    hooks.on_song = [&](std::size_t i, ui::SongMark mark) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (mark == ui::SongMark::None) {
                current = job.songs[i];
                import_done = static_cast<int>(i);
            } else {
                marks[at[i]] = mark;
                import_done = static_cast<int>(i) + 1;
                copied += mark == ui::SongMark::Copied ? 1 : 0;
                duplicates += mark == ui::SongMark::Duplicate ? 1 : 0;
                failed += mark == ui::SongMark::Failed ? 1 : 0;
            }
            ++version;
        }
        wake();
    };
    hooks.stop = [this] {
        const std::lock_guard<std::mutex> lock(mutex);
        return cancel || gone || quit;
    };
    hooks.on_chunk = options.chunk_hook;
    const ImportResult result = import_songs(job, hooks);
    {
        const std::lock_guard<std::mutex> lock(mutex);
        importing = false;
        current.clear();
        ended = result.end;
        if (look_after || gone) {
            look_after = false;
            refresh = true; // the stick changed while it was being read: look again
            cv.notify_all();
        }
        if (!quit) {
            const std::string text = ending_text(result.end, result.copied, result.duplicates,
                                                 result.failed, static_cast<int>(job.songs.size()));
            if (!text.empty()) {
                notice = text;
            }
        }
        ++version;
    }
    wake();
}

SourcesController::SourcesController(SourcesOptions options)
    : shared_(std::make_shared<Shared>(std::move(options))), share_([&] {
          ShareProbeOptions share = shared_->options.share;
          share.on_change = wake;
          return share;
      }()) {
    shared_->running = 1;
    shared_->watcher = spawn_thread([sh = shared_] {
        sh->watch();
        sh->thread_ended();
    });
}

SourcesController::~SourcesController() {
    Shared& s = *shared_;
    std::unique_lock<std::mutex> lock(s.mutex);
    s.quit = true;
    s.cancel = true;
    s.cv.notify_all();
    // A stick that stopped answering holds a thread in a read for as long as it likes: wait a
    // little, then leave the threads behind (they hold the shared state, so nothing dangles).
    const bool clean = s.exited.wait_for(lock, std::chrono::milliseconds(s.options.quit_wait_ms),
                                         [&s] { return s.running == 0; });
    lock.unlock();
    for (std::thread* t : {&s.watcher, &s.importer}) {
        if (t->joinable()) {
            clean ? t->join() : t->detach();
        }
    }
}

const ui::NetworkView& SourcesController::network() {
    network_shown_.state = share_.state();
    return network_shown_;
}

const ui::UsbView& SourcesController::usb() {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shown_version_ != shared_->version) {
        usb_shown_ = shared_->make_view();
        shown_version_ = shared_->version;
    }
    return usb_shown_;
}

void SourcesController::recheck() {
    share_.recheck();
    {
        const std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->refresh = true;
    }
    shared_->cv.notify_all();
}

std::optional<std::string> SourcesController::take_notice() {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    std::optional<std::string> out;
    out.swap(shared_->notice);
    return out;
}

void SourcesController::import_all() {
    std::vector<std::size_t> at;
    {
        const std::lock_guard<std::mutex> lock(shared_->mutex);
        for (std::size_t i = 0; i < shared_->scan.songs.size(); ++i) {
            at.push_back(i);
        }
    }
    shared_->start_import(shared_, std::move(at));
}

void SourcesController::import_one(std::size_t index) { shared_->start_import(shared_, {index}); }

void SourcesController::cancel_import() {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->cancel = true;
}

} // namespace etude::app
