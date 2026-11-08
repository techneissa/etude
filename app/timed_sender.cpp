// SPDX-License-Identifier: GPL-3.0-or-later
#include "timed_sender.hpp"
#include "crash_thread.hpp"

#include <chrono>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <pthread/qos.h>
#endif

namespace etude::app {

namespace {

std::int64_t steady_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

TimedSender::TimedSender(Deliver deliver) : deliver_(std::move(deliver)) {
    thread_ = spawn_thread([this] { run(); });
}

TimedSender::~TimedSender() { stop(); }

void TimedSender::submit(const SongSoundEvent& event) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopped_) {
        return;
    }
    switch (event.kind) {
    case SongSoundEvent::Kind::Message:
        queue_.push(event.message);
        if (queue_.lost_note_offs() != seen_lost_offs_) {
            seen_lost_offs_ = queue_.lost_note_offs();
            stop_all_locked(lock);
            return;
        }
        break;
    case SongSoundEvent::Kind::CancelUnstarted:
        queue_.cancel_unstarted(event.message.at_ns);
        break;
    case SongSoundEvent::Kind::StopAll:
        stop_all_locked(lock);
        return;
    }
    lock.unlock();
    wake_.notify_one();
}

// Forgets the queue and puts every channel in a known state. Delivered here, outside the lock,
// since `deliver` may take other locks.
void TimedSender::stop_all_locked(std::unique_lock<std::mutex>& lock) {
    queue_.clear();
    ++epoch_;
    lock.unlock();
    {
        const std::lock_guard<std::mutex> send(send_mutex_);
        for (const PlaybackMessage& m : port_reset_messages()) {
            deliver_(m);
        }
    }
    wake_.notify_one();
}

void TimedSender::stop() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        stopping_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::uint64_t TimedSender::dropped_note_ons() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return queue_.dropped_note_ons();
}

void TimedSender::run() {
#if defined(__APPLE__)
    // Without this macOS coalesces the sleep's wake-up with other timers, a few ms late, and a busy
    // machine keeps the thread off the CPU for several more. A real-time class (as audio threads
    // have) wakes it at once: it runs for well under a millisecond per message.
    mach_timebase_info_data_t base{};
    mach_timebase_info(&base);
    const auto ticks = [&](std::uint64_t ns) {
        return static_cast<std::uint32_t>(ns * base.denom / base.numer);
    };
    thread_time_constraint_policy_data_t policy{};
    policy.period = ticks(1'000'000);
    policy.computation = ticks(200'000);
    policy.constraint = ticks(1'000'000);
    policy.preemptible = 1;
    thread_policy_set(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY,
                      reinterpret_cast<thread_policy_t>(&policy),
                      THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#endif
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        if (queue_.empty()) {
            if (stopping_) {
                return;
            }
            wake_.wait(lock);
            continue;
        }
        const std::int64_t due = queue_.front().at_ns;
        if (!stopping_ && due > steady_ns()) {
            wake_.wait_until(lock,
                             std::chrono::steady_clock::time_point(std::chrono::nanoseconds(due)));
            continue;
        }
        const PlaybackMessage m = queue_.front();
        queue_.pop_front();
        const std::uint64_t popped_in = epoch_;
        lock.unlock();
        {
            // A reset that ran after the pop already silenced everything: sending this message
            // now could start a note nothing will stop.
            const std::lock_guard<std::mutex> send(send_mutex_);
            bool stale = false;
            {
                const std::lock_guard<std::mutex> check(mutex_);
                stale = epoch_ != popped_in;
            }
            if (!stale) {
                deliver_(m);
            }
        }
        lock.lock();
    }
}

} // namespace etude::app
