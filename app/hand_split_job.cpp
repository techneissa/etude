// SPDX-License-Identifier: GPL-3.0-or-later
#include "hand_split_job.hpp"
#include "crash_thread.hpp"
#include "loop_wake.hpp"

#include "song_library.hpp"

#include "etude/difficulty.hpp"

#include <chrono>
#include <utility>

namespace etude::app {

HandSplitJob::~HandSplitJob() {
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
        waiting_.reset();
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HandSplitJob::request(HandSplitRequest request) {
    {
        const std::lock_guard lock(mutex_);
        waiting_ = std::move(request);
        if (!thread_.joinable()) {
            thread_ = spawn_thread([this] { run(); });
        }
    }
    wake_.notify_one();
}

std::optional<HandSplitResult> HandSplitJob::take() {
    const std::lock_guard lock(mutex_);
    std::optional<HandSplitResult> out;
    out.swap(done_);
    return out;
}

void HandSplitJob::cancel() {
    const std::lock_guard lock(mutex_);
    waiting_.reset();
    done_.reset();
    ++epoch_;
}

bool HandSplitJob::busy() const {
    const std::lock_guard lock(mutex_);
    return waiting_.has_value() || running_;
}

void HandSplitJob::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return !waiting_.has_value() && !running_; });
}

namespace {

// Makes `out` the song as its file gave it, from the request's own copy or from a copy of the lent
// song with the file's hands put back. False when there is none to be had: the request is stale
// (cancelled, or the lent song was replaced), or the copy's fingerprint is not the file's.
template <class Stale>
bool take_unsplit(const HandSplitRequest& request, Song& out, const Stale& stale) {
    if (request.unsplit != nullptr) {
        out = *request.unsplit;
        return true;
    }
    if (request.lent == nullptr || request.file_hands == nullptr) {
        return false;
    }
    {
        const std::lock_guard lock(request.lent->mutex);
        // A cancelled request copies nothing: a load that cancelled it waits for this lock.
        if (stale() || request.lent->song == nullptr ||
            request.lent->generation != request.generation) {
            return false;
        }
        out = *request.lent->song;
    }
    return apply_hand_split(out, *request.file_hands, true) &&
           song_fingerprint(out) == request.file_hands->fingerprint;
}

} // namespace

void HandSplitJob::run() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stop_ || waiting_.has_value(); });
        if (stop_) {
            return;
        }
        HandSplitRequest request = std::move(*waiting_);
        waiting_.reset();
        running_ = true;
        const std::uint64_t epoch = epoch_.load();
        lock.unlock();

        const auto begin = std::chrono::steady_clock::now();
        HandSplitResult result;
        result.serial = request.serial;
        result.split_point = request.split_point;
        bool applied = false;
        Song base; // the song as its file gave it: a copy of it, which the split is applied to
        if (take_unsplit(request, base, [&] { return epoch_.load() != epoch; })) {
            const std::shared_ptr<const HandSplit> split =
                hand_split_for(base, request.hash, request.abc_chords,
                               options_for_split_point(request.split_point));
            result.song = std::move(base);
            applied = apply_hand_split(result.song, *split, true);
        }
        if (applied) {
            result.suggested = suggested_split_point(result.song);
            result.difficulty = std::make_shared<const SongDifficulty>(rate_song(result.song));
            result.prepared = SongController::prepare(result.song, request.plan);
            result.seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        }

        lock.lock();
        running_ = false;
        if (applied && epoch == epoch_ && !stop_) {
            done_ = std::move(result);
            wake_loop(WakeSource::HandSplit);
        }
        if (!waiting_.has_value()) {
            idle_.notify_all();
        }
    }
}

} // namespace etude::app
