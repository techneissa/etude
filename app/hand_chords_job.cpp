// SPDX-License-Identifier: GPL-3.0-or-later
#include "hand_chords_job.hpp"
#include "crash_thread.hpp"
#include "loop_wake.hpp"

#include <utility>

namespace etude::app {

HandChordsJob::~HandChordsJob() {
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

void HandChordsJob::request(HandChordsRequest request) {
    {
        const std::lock_guard lock(mutex_);
        waiting_ = std::move(request);
        if (!thread_.joinable()) {
            thread_ = spawn_thread([this] { run(); });
        }
    }
    wake_.notify_one();
}

std::optional<HandChordsResult> HandChordsJob::take() {
    const std::lock_guard lock(mutex_);
    std::optional<HandChordsResult> out;
    out.swap(done_);
    return out;
}

void HandChordsJob::cancel() {
    const std::lock_guard lock(mutex_);
    waiting_.reset();
    done_.reset();
    ++epoch_;
}

bool HandChordsJob::busy() const {
    const std::lock_guard lock(mutex_);
    return waiting_.has_value() || running_;
}

void HandChordsJob::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return !waiting_.has_value() && !running_; });
}

std::uint64_t HandChordsJob::analyses() const {
    const std::lock_guard lock(mutex_);
    return analyses_;
}

void HandChordsJob::run() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stop_ || waiting_.has_value(); });
        if (stop_) {
            return;
        }
        HandChordsRequest request = std::move(*waiting_);
        waiting_.reset();
        running_ = true;
        const std::uint64_t epoch = epoch_;
        lock.unlock();

        // The notes the same table holds and the same keys: the harmony stands, only the names
        // are made again (a transposition, the flats setting).
        const bool same = request.notes != nullptr && request.notes == kept_notes_ &&
                          request.measures == kept_measures_ && request.keys == kept_keys_;
        bool analysed = false;
        if (!same) {
            // Only the notes a hand plays are copied, in the song's order.
            Song song;
            if (request.notes != nullptr) {
                for (const Note& n : *request.notes) {
                    if (!n.drum && !n.accompaniment &&
                        (n.hand == Hand::Left || n.hand == Hand::Right)) {
                        song.notes.push_back(n);
                    }
                }
            }
            if (request.measures != nullptr) {
                song.measures = *request.measures;
            }
            song.tempo = request.tempo;
            song.keys = request.keys;
            song.end_tick = request.end_tick;
            song.duration_s = request.duration_s;
            kept_harmony_ = build_hand_harmony(song);
            kept_notes_ = request.notes;
            kept_measures_ = request.measures;
            kept_keys_ = request.keys;
            analysed = true;
        }
        HandChordsResult result;
        result.serial = request.serial;
        result.chords =
            hand_chords_of(kept_harmony_, request.keys, request.semitones, request.prefer_flats);

        lock.lock();
        running_ = false;
        analyses_ += analysed ? 1 : 0;
        if (epoch == epoch_ && !stop_ && !waiting_.has_value()) {
            done_ = std::move(result); // a newer request waiting makes this one stale
            wake_loop(WakeSource::HandChords);
        }
        if (!waiting_.has_value()) {
            idle_.notify_all();
        }
    }
}

void HandChordsFeed::take_answer() {
    if (std::optional<HandChordsResult> answer = job_.take()) {
        if (answer->serial == asked_) {
            chords_ = std::move(answer->chords);
            have_chords_ = true;
            ++serial_;
        }
    }
}

void HandChordsFeed::drop(const HandChordsKey& key) {
    job_.cancel();
    key_ = key;
    have_key_ = true;
    ++asked_; // anything still in flight is for a song that is gone
    if (have_chords_) {
        chords_ = {};
        have_chords_ = false;
        ++serial_;
    }
}

void HandChordsFeed::ask(const HandChordsKey& key, HandChordsRequest request) {
    // Labels of the same notes (a new transposition) stay on screen until the new names are in;
    // labels of other notes (another song, another split of the hands) go now.
    const bool same_notes = have_key_ && key.load_serial == key_.load_serial &&
                            key.notes_serial == key_.notes_serial && key_.has_song;
    key_ = key;
    have_key_ = true;
    if (!same_notes && have_chords_) {
        chords_ = {};
        have_chords_ = false;
        ++serial_;
    }
    request.serial = ++asked_;
    job_.request(std::move(request));
}

} // namespace etude::app
