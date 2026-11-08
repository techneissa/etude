// SPDX-License-Identifier: GPL-3.0-or-later
// What the frame thread and the MIDI watcher thread tell each other about the ports: the player's
// choice of input and output (by name, empty for Auto) going to the watcher, and the names the
// watcher listed coming back. The frame thread never waits for the watcher: the lists come through
// a Handover, and a choice is handed over only when it changed.
#pragma once

#include "port_lists.hpp"
#include "port_watcher.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace etude::app {

class MidiPorts {
  public:
    // Frame thread. Cheap when nothing changed: it compares with what it last handed over before
    // it takes the lock.
    void choose(const std::string& input, const std::string& output) {
        if (input == sent_input_ && output == sent_output_) {
            return;
        }
        sent_input_ = input;
        sent_output_ = output;
        std::function<void()> poke;
        {
            const std::lock_guard lock(mutex_);
            input_ = input;
            output_ = output;
            serial_.fetch_add(1, std::memory_order_release);
            poke = poke_;
        }
        if (poke) {
            poke(); // list the ports now, so the new choice shows at once
        }
    }
    // The latest lists the watcher published, or the last ones seen. Frame thread; never waits.
    const PortLists& lists() {
        if (auto next = published_.poll()) {
            latest_ = *next;
        }
        return latest_;
    }

    // How many publications the frame thread has taken (it copies the lists when this moves).
    std::uint32_t lists_seen() const { return published_.seen(); }

    // Watcher thread.
    std::string input_choice() const {
        const std::lock_guard lock(mutex_);
        return input_;
    }
    std::string output_choice() const {
        const std::lock_guard lock(mutex_);
        return output_;
    }
    // Counts the choices handed over: the watcher scans again when it moved.
    std::uint32_t choice_serial() const { return serial_.load(std::memory_order_acquire); }
    // For the watcher thread to be woken by a new choice (PortWatcher's `changed`).
    void set_poke(std::function<void()> poke) {
        const std::lock_guard lock(mutex_);
        poke_ = std::move(poke);
    }
    // Publishes what the watcher listed; nothing is published when it equals the last lists.
    void publish_inputs(std::vector<std::string> names) {
        work_.inputs = std::move(names);
        work_.inputs_known = true;
        publish_work();
    }
    // `failed`: the chosen output that is listed but would not open, or empty.
    void publish_outputs(std::vector<std::string> names, std::string failed = {}) {
        work_.outputs = std::move(names);
        work_.outputs_known = true;
        work_.output_failed = std::move(failed);
        publish_work();
    }

  private:
    void publish_work() {
        if (work_ == last_published_) {
            return;
        }
        last_published_ = work_;
        published_.publish(std::make_shared<const PortLists>(work_));
    }

    mutable std::mutex mutex_; // input_, output_, poke_
    std::string input_;
    std::string output_;
    std::function<void()> poke_;
    std::atomic<std::uint32_t> serial_{0};
    std::string sent_input_;  // frame thread only
    std::string sent_output_;
    Handover<PortLists> published_;
    PortLists latest_;         // frame thread only
    PortLists work_;           // watcher thread only
    PortLists last_published_; // watcher thread only
};

} // namespace etude::app
