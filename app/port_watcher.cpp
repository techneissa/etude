// SPDX-License-Identifier: GPL-3.0-or-later
#include "port_watcher.hpp"
#include "crash_thread.hpp"
#include "loop_wake.hpp"

#include <algorithm>
#include <utility>

namespace etude::app {

PortWatcher::PortWatcher(Factory factory, std::chrono::milliseconds interval,
                         std::chrono::milliseconds join_timeout)
    : state_(std::make_shared<State>()), join_timeout_(join_timeout) {
    // The thread gets its own references: the State, the factory, nothing of this object.
    thread_ = spawn_thread([state = state_, factory = std::move(factory), interval] {
        run(state, factory, interval);
    });
}

PortWatcher::~PortWatcher() {
    {
        const std::lock_guard lock(state_->mutex);
        state_->stop = true;
    }
    state_->wake.notify_all();
    std::unique_lock lock(state_->mutex);
    if (state_->finished_cv.wait_for(lock, join_timeout_, [this] { return state_->finished; })) {
        lock.unlock();
        thread_.join();
        return;
    }
    // Still inside a listing that does not return. It only touches the State and what the factory
    // gave it, all of which it holds itself, so it can finish whenever it likes.
    detached_ = true;
    lock.unlock();
    thread_.detach();
}

void PortWatcher::run(const std::shared_ptr<State>& state, const Factory& factory,
                      std::chrono::milliseconds interval) {
    {
        const ListFn list = factory([state] {
            state->changes.fetch_add(1, std::memory_order_release);
            wake_loop(WakeSource::Ports);
            {
                const std::lock_guard lock(state->mutex);
                state->poked = true;
            }
            state->wake.notify_all();
        });
        std::vector<Entry> last;
        std::vector<Entry> now;
        std::unique_lock lock(state->mutex);
        while (!state->stop) {
            lock.unlock();
            now = list();
            std::sort(now.begin(), now.end());
            if (now != last) {
                last.swap(now);
                state->changes.fetch_add(1, std::memory_order_release);
                wake_loop(WakeSource::Ports);
            }
            state->listed_once.store(true, std::memory_order_release);
            lock.lock();
            // Sleeps until the next listing is due, a change is reported or the owner leaves.
            state->wake.wait_for(lock, interval, [&state] { return state->stop || state->poked; });
            state->poked = false;
        }
    }
    // The lister and what it owns are gone; say so.
    {
        const std::lock_guard lock(state->mutex);
        state->finished = true;
    }
    state->finished_cv.notify_all();
}

} // namespace etude::app
