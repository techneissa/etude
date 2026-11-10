// SPDX-License-Identifier: GPL-3.0-or-later
// A thread that lists the MIDI ports every kWatchInterval, and at once when the platform reports a
// change, and counts the times the list changed. What the thread does with each listing (opening
// and closing the ports, finding the piano's output) is the lister's own business and runs on the
// thread too: everything that can block (the first CoreMIDI listing takes hundreds of
// milliseconds, a listing can hang, so can opening or closing a port) stays off the frame thread. Everything the thread uses is in a State it shares
// with its owner, so the owner can leave a hung thread behind: the destructor waits for the thread
// for kWatchJoinTimeout, then detaches it, and the thread keeps the State (and the lister it owns)
// alive until it returns. No libremidi or SDL here, so it is tested with a lister that hangs.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace etude::app {

// How often the thread lists the ports to catch changes the platform did not report.
inline constexpr std::chrono::milliseconds kWatchInterval{500};
// How long the destructor waits for a thread that is still inside a listing before it detaches.
inline constexpr std::chrono::milliseconds kWatchJoinTimeout{500};

class PortWatcher {
  public:
    // (port handle, name, USB instance); the instance catches an unplug and replug between two
    // listings, which leaves the handle and name unchanged.
    using Entry = std::tuple<std::uint64_t, std::string, std::string>;
    using ListFn = std::function<std::vector<Entry>()>;
    // Runs first, on the watcher thread, and returns the function that lists the ports; whatever
    // it creates (an observer, the open ports) belongs to that thread. `changed` may be called from
    // any thread, for as long as the returned function lives; it counts as a change of the port
    // list and wakes the thread to list again now.
    using Factory = std::function<ListFn(std::function<void()> changed)>;

    explicit PortWatcher(Factory factory, std::chrono::milliseconds interval = kWatchInterval,
                         std::chrono::milliseconds join_timeout = kWatchJoinTimeout);
    ~PortWatcher();
    PortWatcher(const PortWatcher&) = delete;
    PortWatcher& operator=(const PortWatcher&) = delete;

    // Changes seen so far: the list differed from the one before, or `changed` was called.
    std::uint32_t changes() const { return state_->changes.load(std::memory_order_acquire); }
    // True once the thread has listed the ports once.
    bool listed_once() const { return state_->listed_once.load(std::memory_order_acquire); }

  private:
    struct State {
        std::atomic<std::uint32_t> changes{0};
        std::atomic<bool> listed_once{false};
        std::mutex mutex;             // guards stop and finished
        std::condition_variable wake; // the thread sleeps on it between listings
        std::condition_variable finished_cv;
        bool stop = false;
        bool poked = false;    // `changed` was called since the thread last woke
        bool finished = false; // the thread has returned from its loop
    };

    static void run(const std::shared_ptr<State>& state, const Factory& factory,
                    std::chrono::milliseconds interval);

    std::shared_ptr<State> state_;
    std::chrono::milliseconds join_timeout_;
    std::thread thread_;
    bool detached_ = false;
};

// One value handed from a thread that makes it to a thread that must never wait for it: the
// publisher swaps in a new immutable value under a lock it holds for a pointer swap, and the reader
// takes it with a try-lock, so a reader that finds the lock busy simply tries again next frame and
// is never blocked. The serial counts the publications; poll() delivers each value at most once,
// and only the latest of several published between two polls.
template <class T> class Handover {
  public:
    // Makes `value` the one the next poll() returns.
    // The value it replaces is released after the lock is let go, on the publisher's thread, so a
    // slow release never holds the reader off.
    void publish(std::shared_ptr<const T> value) {
        std::shared_ptr<const T> old;
        {
            const std::lock_guard lock(mutex_);
            old = std::move(value_);
            value_ = std::move(value);
            serial_.fetch_add(1, std::memory_order_release);
        }
    }
    // The value published since the last successful poll, or null (nothing new, or the lock was
    // busy: ask again later). Never waits.
    std::shared_ptr<const T> poll() {
        if (serial_.load(std::memory_order_acquire) == seen_) {
            return nullptr;
        }
        const std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return nullptr;
        }
        seen_ = serial_.load(std::memory_order_relaxed);
        return value_;
    }
    // Publications so far.
    std::uint32_t serial() const { return serial_.load(std::memory_order_acquire); }
    // The publication poll() last took (the reader's thread only).
    std::uint32_t seen() const { return seen_; }

  private:
    std::mutex mutex_;
    std::shared_ptr<const T> value_;
    std::atomic<std::uint32_t> serial_{0};
    std::uint32_t seen_ = 0; // the reader's thread only
};

} // namespace etude::app
