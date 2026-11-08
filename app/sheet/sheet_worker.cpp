// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_worker.hpp"
#include "../crash_thread.hpp"
#include "../loop_wake.hpp"

#include "sheet_job.hpp"

#include <algorithm>
#include <chrono>

namespace etude::app {
namespace {

double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

int practised_notes(const Song& song) {
    int n = 0;
    for (const Note& note : song.notes) {
        n += note.drum || note.accompaniment ? 0 : 1;
    }
    return n;
}

} // namespace

SheetWorker::SheetWorker() {
    // The lists the frame thread fills on a page turn, sized once.
    wanted_.reserve(16);
    drawn_.reserve(16);
    thread_ = spawn_thread([this] { run(); });
}

SheetWorker::~SheetWorker() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void SheetWorker::engrave(SheetJob job) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        job_ = std::make_unique<SheetJob>(std::move(job));
        wanted_.clear();
    }
    wake_.notify_all();
}

void SheetWorker::want_pictures(std::uint64_t generation, std::span<const int> systems) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        // The list holds only what the view lacks: a system drawn before and since let go is drawn
        // again, unless its picture is still waiting to be taken.
        picture_generation_ = generation;
        drawn_.clear();
        for (const SheetPictureReady& p : pictures_) {
            if (p.generation == generation) {
                drawn_.push_back(p.system);
            }
        }
        wanted_.assign(systems.begin(), systems.end());
    }
    wake_.notify_all();
}

bool SheetWorker::take_layout(SheetLayoutReady& out) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (layouts_.empty()) {
        return false;
    }
    out = std::move(layouts_.front());
    layouts_.pop_front();
    return true;
}

bool SheetWorker::take_picture(SheetPictureReady& out) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (pictures_.empty()) {
        return false;
    }
    out = std::move(pictures_.front());
    pictures_.pop_front();
    return true;
}

void SheetWorker::run() {
    std::unique_ptr<SheetEngraver> engraver;
    std::uint32_t source_serial = 0;
    bool have_source = false;
    SheetSource source;
    std::uint64_t engraved_generation = 0;
    bool engraved = false;
    for (;;) {
        std::unique_ptr<SheetJob> job;
        int system = -1;
        std::uint64_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] {
                if (stop_ || job_ != nullptr) {
                    return true;
                }
                if (!engraved || picture_generation_ != engraved_generation) {
                    return false;
                }
                return std::any_of(wanted_.begin(), wanted_.end(), [&](int s) {
                    return std::find(drawn_.begin(), drawn_.end(), s) == drawn_.end();
                });
            });
            if (stop_) {
                return;
            }
            if (job_ != nullptr) {
                job = std::move(job_);
            } else {
                for (const int s : wanted_) {
                    if (std::find(drawn_.begin(), drawn_.end(), s) == drawn_.end()) {
                        system = s;
                        break;
                    }
                }
                drawn_.push_back(system);
                generation = picture_generation_;
            }
        }
        if (job != nullptr) {
            SheetLayoutReady ready;
            ready.generation = job->generation;
            ready.options = job->options;
            engraved = false;
            const auto t0 = std::chrono::steady_clock::now();
            if (job->song == nullptr) {
                ready.reason = "no song is open";
            } else if (practised_notes(job->song->song) > kMaxSheetNotes) {
                ready.reason = "the song is too long for sheet music";
            } else {
                if (!have_source || source_serial != job->song_serial) {
                    source = make_sheet_source(*job->song);
                    source_serial = job->song_serial;
                    have_source = true;
                }
                ready.source_ms = ms_since(t0);
                if (source.status != SheetSourceStatus::Ok) {
                    ready.reason = source.reason;
                } else {
                    if (engraver == nullptr) {
                        engraver = std::make_unique<SheetEngraver>(sheet_resource_dir());
                    }
                    const auto t1 = std::chrono::steady_clock::now();
                    std::string error;
                    if (!engraver->engrave(source.musicxml, job->options, ready.layout, error)) {
                        ready.reason = error;
                    } else {
                        ready.map = build_sheet_map(ready.layout, job->song->song);
                        ready.engrave_ms = ms_since(t1);
                        if (ready.map.usable()) {
                            ready.ok = true;
                            engraved = true;
                            engraved_generation = job->generation;
                        } else {
                            ready.reason = "the score's notes do not line up with the song's";
                        }
                    }
                }
            }
            const std::lock_guard<std::mutex> lock(mutex_);
            layouts_.push_back(std::move(ready)); wake_loop(WakeSource::Sheet);
            continue;
        }
        if (system < 0 || engraver == nullptr) {
            continue;
        }
        SheetPictureReady ready;
        ready.generation = generation;
        ready.system = system;
        const auto t0 = std::chrono::steady_clock::now();
        if (!engraver->draw_system(system, ready.picture)) {
            ready.picture = SystemPicture{}; // failed: an empty picture tells the view
        }
        ready.ms = ms_since(t0);
        const std::lock_guard<std::mutex> lock(mutex_);
        pictures_.push_back(std::move(ready)); wake_loop(WakeSource::Sheet);
    }
}

} // namespace etude::app
