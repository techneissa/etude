// SPDX-License-Identifier: GPL-3.0-or-later
// Live MIDI input from every connected keyboard, with hot-plug.
#pragma once

#include "midi_input_info.hpp"
#include "midi_ports.hpp"
#include "port_watcher.hpp"

#include "etude/keyboard.hpp"
#include "etude/keyboard_identity.hpp"
#include "etude/midi_event.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace etude::app {

// The clock every MidiEvent::time_ns is on: std::chrono::steady_clock in nanoseconds. libremidi
// stamps with it on the MIDI thread in timestamp_mode::SystemMonotonic, and the frame loop reads it
// here to compare against those stamps.
std::int64_t midi_clock_ns();
// Developer (--fake-clock-hz): the clock stands still and the frame loop moves it a fixed step per
// frame, so a scripted headless run is the same every time. Never used by a window run.
void set_fake_clock_ns(std::int64_t ns);

// What changed during one update().
struct PortChanges {
    int opened = 0;
    int closed = 0; // inputs that disappeared; the caller clears held keys
};

// Opens every MIDI input port except the app's own and software loopbacks, and reopens them as
// devices come and go.
//
// Threads: the watcher thread (PortWatcher) does everything that can block. It lists the ports
// every 500 ms and when libremidi's observer reports a change (CoreMIDI on macOS 27 delivered no
// observer notifications in testing, hence the listing), opens the new inputs and closes the
// vanished ones, retries a port that failed to open, runs the port tasks (the piano's output port,
// see add_port_task) and publishes what is open as one immutable set. It owns the libremidi
// objects, observers and open ports alike, so quitting waits at most kWatchJoinTimeout for it even
// when CoreMIDI hangs. The list includes each port's USB instance where the platform gives one
// (Linux), so a quick unplug and replug is seen as a change too.
//
// The frame thread's update() takes the latest published set without ever waiting (Handover) and
// reads the queues of the ports in it; every other member is used from the frame loop's thread
// only. libremidi calls each port's message callback on its own MIDI thread. That callback splits
// and decodes the bytes (the port's splitter keeps running status and a partial message between
// callbacks), keeps only the events the trainer uses, and pushes them into the port's own
// single-producer queue, so several keyboards never share a producer. Each open gets a new
// splitter and queue, so a reused slot never inherits state; a closed port's queue leaves the
// published set, so nothing from an unplugged keyboard arrives after update() reported it.
class MidiInput {
  public:
    static constexpr std::size_t kMaxPorts = 8;
    // Per port. Sized so a backlog never builds up in practice: a MIDI 1.0 cable carries at most
    // 31250 / 10 / 2 = 1562 messages a second (two bytes each with running status), which fills
    // 4096 slots in 2.6 s. Dense playing is far below that: ten-note chords sixteen times a
    // second with a continuous pedal is 10 * 2 * 16 + about 100 pedal CCs = 420 events a second,
    // or 7 per 60 FPS frame, so the queue holds about 10 s of it, or a 600-frame stall. A full
    // queue still never leaves a key lit: see MidiEventQueue. 4096 * 16 bytes = 64 KiB per port.
    static constexpr std::size_t kQueueCapacity = 4096;

    MidiInput();
    ~MidiInput();
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    // Takes the set of open inputs the watcher thread published since the last call, if there is
    // one, and says what changed. Never waits and opens nothing itself. A closed input's queued
    // events are discarded, so nothing from an unplugged keyboard arrives after the caller clears
    // its keys.
    PortChanges update();

    // False until update() has taken the first set: the watcher thread has listed the ports and
    // opened what it found. The first listing in a process can take hundreds of milliseconds and
    // blocks every thread that asks CoreMIDI meanwhile; the frame thread leaves MIDI alone until
    // this is true (the "connect a keyboard" prompt, for one, waits for it).
    bool ready() const { return scanned_; }

    // The player's choice of ports going to the watcher thread and the lists coming back. With a
    // named input that is listed, only that input is held open; the rest are closed, and opened
    // again when the choice goes back to Auto or the name is gone.
    std::shared_ptr<MidiPorts> ports();

    // Work for the watcher thread to do after each scan of the inputs, given the names of the
    // open ones: the piano's output port (MidiOutput::port_task). May be called from the frame
    // thread at any time; the task runs on the watcher thread, so it must not touch the frame
    // thread's data, and it lives as long as the watcher thread does.
    using PortTask = std::function<void(std::span<const std::string> open_input_names)>;
    void add_port_task(PortTask task);

    // Runs the main thread's run loop once without waiting. On macOS CoreMIDI updates the port list
    // through it, so a loop without SDL's event pump (the headless --midi-log) calls this each
    // turn. Does nothing elsewhere.
    static void pump_system_events();

    // Pops every queued event from every open input and calls `fn(const MidiEvent&)` for each, in
    // time order across inputs (equal times in slot order). `fn` may take the input's slot as a
    // second argument. Closed slots are skipped. When an
    // input's queue overflowed, its events include a synthetic All Notes Off at the gap.
    template <typename Fn> void drain(Fn&& fn) {
        merge_by_time<kMaxPorts>(
            [this](std::size_t i, MidiEvent& ev) {
                if (view_ == nullptr) {
                    return false;
                }
                const OpenPort& port = view_->slots[i];
                return port.stream != nullptr && port.stream->queue.pop(ev);
            },
            fn, kQueueCapacity);
    }

    // Events dropped because a queue was full, over all ports since start.
    std::uint64_t dropped() const;
    // Synthetic All Notes Off events inserted after drops, over all ports since start.
    std::uint64_t resyncs() const;

    // The first open input that matches a keyboard profile, or nullptr when none does.
    const KeyboardProfile* profile() const;
    int open_count() const;
    // Names of the open inputs, in slot order.
    std::vector<std::string> open_names() const;
    // The open inputs with their slot (the index `drain` passes to a callback that takes one) and
    // the profile each matched (nullptr: unknown).
    using InputInfo = MidiInputInfo;
    std::vector<InputInfo> inputs() const;

  private:
    // State the MIDI thread owns while the port is open; shared with its callback.
    struct PortStream {
        MidiStreamSplitter splitter;          // MIDI thread only
        MidiEventQueue<kQueueCapacity> queue; // MIDI thread -> frame thread
    };
    // What the frame thread sees of an open input (the libremidi port itself stays on the watcher).
    struct OpenPort {
        std::shared_ptr<PortStream> stream; // null when the slot is free; new for every open
        std::string name;
        KeyboardMatch match;
    };
    // The open inputs by slot, published whole and never changed after.
    struct OpenPorts {
        std::array<OpenPort, kMaxPorts> slots;
    };
    struct Shared;  // what the frame thread and the watcher thread share
    class Opener;   // the watcher thread's: libremidi observers, the open ports, the retries

    std::shared_ptr<Shared> shared_;
    std::shared_ptr<const OpenPorts> view_; // the set update() last took; frame thread only
    bool scanned_ = false;
    std::uint64_t closed_dropped_ = 0; // from ports already closed
    std::uint64_t closed_resyncs_ = 0;
    std::unique_ptr<PortWatcher> watcher_; // the thread; last, so it ends first
};

} // namespace etude::app
