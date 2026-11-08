// SPDX-License-Identifier: GPL-3.0-or-later
#include "midi_input.hpp"

#include "loop_wake.hpp"
#include "midi_port_identity.hpp"

#include <libremidi/libremidi.hpp>

#include <SDL3/SDL_log.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

namespace etude::app {

namespace {

// Ports the app never opens: its own, loopbacks that are not a keyboard, and nameless ones
// (CoreMIDI briefly lists a source that is going away with an empty name).
bool skip_port(const libremidi::input_port& port) {
    const std::string_view name = port.port_name;
    const std::string_view device = port.device_name;
    return name.empty() || name.starts_with("etude") || device.starts_with("etude") ||
           name.find("Midi Through") != std::string_view::npos ||
           device.find("Midi Through") != std::string_view::npos;
}

// MIDI thread. Splits the bytes (state carries over from the last callback), decodes, keeps only
// what the trainer uses, and pushes; no allocation, lock, or logging. The one exception: when the
// frame loop is idle waiting for an event, wake_loop() pushes one SDL event to wake it.
template <class Stream>
void feed_stream(Stream& stream, std::span<const std::uint8_t> bytes, libremidi::timestamp time_ns) {
    stream.splitter.feed(bytes);
    ChannelMessage msg;
    bool pushed = false;
    while (stream.splitter.next(msg)) {
        const MidiEvent ev = decode_midi(msg.bytes.data(), msg.size, time_ns);
        if (is_trainer_event(ev)) {
            stream.queue.push(ev);
            pushed = true;
        }
    }
    if (pushed) {
        wake_loop(WakeSource::Midi);
    }
}

std::string label(const libremidi::input_port& port) {
    return port.display_name.empty() ? port.port_name : port.display_name;
}

// A port that failed to open is retried after 0.5 s, then 1, 2, 4 and every 8 s, for as long as it
// stays listed. Opening can fail while a device is still registering (an ALSA connect, for one).
constexpr std::int64_t kRetryFirstNs = 500'000'000;
constexpr std::int64_t kRetryMaxNs = 8'000'000'000;

std::int64_t retry_delay_ns(int attempts) {
    std::int64_t delay = kRetryFirstNs;
    for (int i = 1; i < attempts && delay < kRetryMaxNs; ++i) {
        delay *= 2;
    }
    return std::min(delay, kRetryMaxNs);
}

libremidi::observer_configuration port_list_config() {
    libremidi::observer_configuration conf;
    conf.track_hardware = true;
    // Virtual sources too, so `midi-demo` and software keyboards work; skip_port() drops our own.
    conf.track_virtual = true;
    conf.notify_in_constructor = false;
    return conf;
}

} // namespace

std::atomic<std::int64_t> g_fake_clock_ns{-1};

std::int64_t midi_clock_ns() {
    using namespace std::chrono;
    if (const std::int64_t fake = g_fake_clock_ns.load(std::memory_order_relaxed); fake >= 0) {
        return fake;
    }
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void set_fake_clock_ns(std::int64_t ns) { g_fake_clock_ns.store(ns, std::memory_order_relaxed); }

// What the two threads share. The watcher thread's lister holds it too, so a watcher left behind by
// a hung CoreMIDI call (see PortWatcher) never touches freed memory.
struct MidiInput::Shared {
    Handover<OpenPorts> ports;
    std::shared_ptr<MidiPorts> choice = std::make_shared<MidiPorts>(); // the player's pick of ports and the lists back
    std::mutex tasks_mutex;
    std::vector<MidiInput::PortTask> tasks;
};

// Everything the watcher thread does with ports: lists them, opens the new ones, closes the gone
// ones, retries the failed ones and publishes the open set. Its members are used from that thread
// only, and it is destroyed there, which closes the ports there.
class MidiInput::Opener {
  public:
    explicit Opener(std::shared_ptr<Shared> shared, std::function<void()> changed)
        : shared_(std::move(shared)), notified_(std::make_shared<std::atomic<std::uint32_t>>(0)) {
        auto conf = port_list_config();
        const auto notified = notified_;
        const auto bump = [notified, changed](const libremidi::input_port&) {
            notified->fetch_add(1, std::memory_order_release);
            changed();
        };
        shared_->choice->set_poke(changed);
        conf.input_added = bump;
        conf.input_removed = bump;
        // One observer reports changes, the other lists and is the one ports are opened through.
        notifier_ = std::make_unique<libremidi::observer>(conf);
        lister_ = std::make_unique<libremidi::observer>(port_list_config());
    }

    // The port tasks hold the output Scanner (its port and observer). The opener is the last holder
    // of `Shared` once MidiInput has let go, so they are released here, on the watcher thread, and
    // not in ~MidiInput on the frame thread: a close stuck in CoreMIDI must not block quitting.
    ~Opener() {
        std::vector<MidiInput::PortTask> tasks;
        {
            const std::lock_guard lock(shared_->tasks_mutex);
            tasks.swap(shared_->tasks);
        }
    }
    Opener(const Opener&) = delete;
    Opener& operator=(const Opener&) = delete;

    // One turn of the watcher thread: list, rescan when the list moved (or a notification came, or
    // a failed open is due), publish, run the port tasks. Returns the listing for the watcher's
    // own change count.
    std::vector<PortWatcher::Entry> tick() {
        const auto ports = lister_->get_input_ports();
        std::vector<PortIdentity> identities;
        identities.reserve(ports.size());
        std::vector<PortWatcher::Entry> entries;
        for (const auto& port : ports) {
            identities.push_back(identify_port(port));
            // The instance catches an unplug and replug between two listings, which leaves the
            // handle and name unchanged but the old connection dead.
            entries.emplace_back(port.port, label(port), identities.back().instance);
        }
        std::vector<PortWatcher::Entry> sorted = entries;
        std::sort(sorted.begin(), sorted.end());
        const std::uint32_t notified = notified_->load(std::memory_order_acquire);
        const std::uint32_t chosen = shared_->choice->choice_serial();
        if (!scanned_ || sorted != last_ || notified != seen_notified_ || chosen != seen_choice_ ||
            retry_due(midi_clock_ns())) {
            seen_notified_ = notified;
            seen_choice_ = chosen;
            last_ = std::move(sorted);
            const bool first = !scanned_;
            scanned_ = true;
            if (rescan(ports, identities) || first) {
                publish();
            }
            publish_names(ports);
        }
        run_tasks();
        return entries;
    }

  private:
    struct Slot {
        std::unique_ptr<libremidi::midi_in> in; // null when the slot is free
        OpenPort view;                          // what the frame thread is given
        std::uint64_t port = 0;                 // libremidi port handle
        std::string instance;                   // USB instance when known; a change means a replug
    };
    // A listed port that failed to open, retried with backoff.
    struct FailedOpen {
        std::uint64_t port = 0;
        std::string name;
        int attempts = 0;
        std::int64_t retry_at_ns = 0;
    };

    bool retry_due(std::int64_t now_ns) const {
        for (const auto& failed : failed_) {
            if (now_ns >= failed.retry_at_ns) {
                return true;
            }
        }
        return false;
    }

    void publish() {
        auto set = std::make_shared<OpenPorts>();
        for (std::size_t i = 0; i < kMaxPorts; ++i) {
            if (slots_[i].in) {
                set->slots[i] = slots_[i].view;
            }
        }
        shared_->ports.publish(std::move(set));
    }

    // The names the settings page offers: every listed input the app would open.
    void publish_names(const std::vector<libremidi::input_port>& ports) {
        std::vector<std::string> names;
        for (const auto& port : ports) {
            if (!skip_port(port)) {
                names.push_back(label(port));
            }
        }
        shared_->choice->publish_inputs(port_keys(names));
    }

    // With a named input that is listed, the handle of the first port of that name.
    std::optional<std::uint64_t> pinned_port(const std::vector<libremidi::input_port>& ports) const {
        std::vector<std::string> keys;
        std::vector<std::uint64_t> handles;
        for (const auto& port : ports) {
            if (!skip_port(port)) {
                keys.push_back(port_key(label(port)));
                handles.push_back(port.port);
            }
        }
        const int at = pinned_index(port_key(shared_->choice->input_choice()), keys);
        if (at < 0) {
            return std::nullopt;
        }
        return handles[static_cast<std::size_t>(at)];
    }

    void run_tasks() {
        std::vector<MidiInput::PortTask> tasks;
        {
            const std::lock_guard lock(shared_->tasks_mutex);
            tasks = shared_->tasks;
        }
        if (tasks.empty()) {
            return;
        }
        std::vector<std::string> names;
        for (const auto& slot : slots_) {
            if (slot.in) {
                names.push_back(slot.view.name);
            }
        }
        for (const auto& task : tasks) {
            task(names);
        }
    }

    void close_slot(Slot& slot) {
        // Closing the port stops its MIDI thread's callbacks. The queued events go with the stream
        // once the frame thread has let go of the set that holds it.
        slot.in.reset();
        slot.view = {};
        slot.instance.clear();
        slot.port = 0;
    }

    // Closes the inputs that left, were replugged, or are not the chosen one. True when it closed
    // any.
    bool close_stale(const std::vector<libremidi::input_port>& ports,
                     const std::vector<PortIdentity>& identities,
                     const std::optional<std::uint64_t>& pin) {
        bool changed = false;
        // Close inputs whose port is gone. Ports are matched by handle, name and USB instance: the
        // handle alone can be reused by a different device on some backends, and a replug between
        // two listings keeps handle and name but gets a new instance where the platform reports
        // one.
        for (auto& slot : slots_) {
            if (!slot.in) {
                continue;
            }
            bool present = false;
            bool replugged = false;
            for (std::size_t i = 0; i < ports.size(); ++i) {
                if (ports[i].port == slot.port && label(ports[i]) == slot.view.name) {
                    present = identities[i].instance == slot.instance;
                    replugged = !present;
                    break;
                }
            }
            if (!present) {
                SDL_Log("MIDI input %s: %s", replugged ? "replugged, reopening" : "removed",
                        slot.view.name.c_str());
                close_slot(slot);
                changed = true;
            } else if (pin && slot.port != *pin) {
                SDL_Log("MIDI input closed, another is chosen: %s", slot.view.name.c_str());
                close_slot(slot);
                changed = true;
            }
        }
        return changed;
    }

    // Opens what is new and closes what is gone. True when the set of open inputs changed.
    bool rescan(const std::vector<libremidi::input_port>& ports,
                const std::vector<PortIdentity>& identities) {
        // A named input that is listed is the only one held open (the first port of that name);
        // with Auto, or a name that is gone, every input is.
        const std::optional<std::uint64_t> pin = pinned_port(ports);
        bool changed = close_stale(ports, identities, pin);

        // Forget failed opens whose port is no longer listed.
        std::erase_if(failed_, [&ports](const FailedOpen& failed) {
            return std::none_of(ports.begin(), ports.end(), [&failed](const auto& port) {
                return port.port == failed.port && label(port) == failed.name;
            });
        });

        // Open ports that are new, and failed ones that are due for a retry.
        const std::int64_t now = midi_clock_ns();
        for (std::size_t n = 0; n < ports.size(); ++n) {
            const auto& port = ports[n];
            if (skip_port(port)) {
                continue;
            }
            const std::string name = label(port);
            if (pin && port.port != *pin) {
                continue;
            }
            bool open = false;
            for (const auto& slot : slots_) {
                if (slot.in && slot.port == port.port && slot.view.name == name) {
                    open = true;
                    break;
                }
            }
            if (open) {
                continue;
            }
            auto failed = std::find_if(failed_.begin(), failed_.end(), [&](const FailedOpen& f) {
                return f.port == port.port && f.name == name;
            });
            if (failed != failed_.end() && now < failed->retry_at_ns) {
                continue;
            }

            Slot* free = nullptr;
            for (std::size_t i = 0; i < kMaxPorts; ++i) {
                Slot& candidate = slots_[(next_slot_ + i) % kMaxPorts];
                if (!candidate.in) {
                    free = &candidate;
                    next_slot_ = (next_slot_ + i + 1) % kMaxPorts;
                    break;
                }
            }
            if (free == nullptr) {
                SDL_Log("MIDI input ignored, %zu already open: %s", kMaxPorts, name.c_str());
                continue;
            }

            auto stream = std::make_shared<PortStream>();
            libremidi::input_configuration conf;
            conf.on_raw_data = [stream](std::span<const std::uint8_t> bytes,
                                        libremidi::timestamp time_ns) {
                feed_stream(*stream, bytes, time_ns);
            };
            conf.ignore_sysex = true;
            conf.ignore_timing = true;
            conf.ignore_sensing = true;
            // steady_clock, read by libremidi on the MIDI thread when it processes the message;
            // see midi_clock_ns(). "Absolute" is not one clock across ports: on ALSA its epoch is
            // each port's queue creation. On CoreMIDI it is host time, which agreed with
            // steady_clock within 0.5 us when measured on macOS, but that is not guaranteed on
            // every platform. SystemMonotonic is one clock everywhere.
            conf.timestamps = libremidi::timestamp_mode::SystemMonotonic;

            auto in = std::make_unique<libremidi::midi_in>(
                conf, libremidi::midi_in_configuration_for(*lister_));
            if (const auto err = in->open_port(port, "etude in"); err != stdx::error{}) {
                if (failed == failed_.end()) {
                    failed_.push_back({port.port, name, 0, 0});
                    failed = std::prev(failed_.end());
                    SDL_Log("MIDI input failed to open, will retry: %s", name.c_str());
                }
                ++failed->attempts;
                failed->retry_at_ns = now + retry_delay_ns(failed->attempts);
                continue;
            }
            if (failed != failed_.end()) {
                SDL_Log("MIDI input opened after %d failed attempts: %s", failed->attempts,
                        name.c_str());
                failed_.erase(failed);
            }
            free->in = std::move(in);
            free->view.stream = std::move(stream);
            free->view.name = name;
            free->view.match = match_keyboard(identities[n].identity);
            free->port = port.port;
            free->instance = identities[n].instance;
            changed = true;
            const auto reason = describe(free->view.match.reason);
            if (free->view.match.profile != nullptr) {
                SDL_Log("MIDI input opened: %s (%.*s, %.*s)", name.c_str(),
                        static_cast<int>(free->view.match.profile->name.size()),
                        free->view.match.profile->name.data(), static_cast<int>(reason.size()),
                        reason.data());
            } else {
                SDL_Log("MIDI input opened: %s (%.*s)", name.c_str(),
                        static_cast<int>(reason.size()), reason.data());
            }
        }
        return changed;
    }

    std::shared_ptr<Shared> shared_;
    std::shared_ptr<std::atomic<std::uint32_t>> notified_; // observer callbacks, any thread
    std::uint32_t seen_notified_ = 0;
    std::uint32_t seen_choice_ = 0; // the choice the last rescan was made for
    bool scanned_ = false;
    std::vector<PortWatcher::Entry> last_; // the listing the last rescan was made for, sorted
    std::size_t next_slot_ = 0;            // free slots are reused round-robin
    std::vector<FailedOpen> failed_;
    // Destroyed in reverse order: the ports close first (slots_), then the notifier stops
    // reporting, then the lister they were opened through goes.
    std::unique_ptr<libremidi::observer> lister_;
    std::unique_ptr<libremidi::observer> notifier_;
    std::array<Slot, kMaxPorts> slots_;
};

MidiInput::MidiInput() : shared_(std::make_shared<Shared>()) {
    // The watcher thread creates its own observers and opens the ports: constructing the first
    // observer is cheap but the first listing is not, and none of it may block the frame thread.
    watcher_ = std::make_unique<PortWatcher>(
        [shared = shared_](std::function<void()> changed) -> PortWatcher::ListFn {
            auto opener = std::make_shared<Opener>(shared, std::move(changed));
            return [opener] { return opener->tick(); };
        });
}

MidiInput::~MidiInput() {
    // The watcher thread closes the ports as it ends; one stuck in CoreMIDI is left behind after
    // kWatchJoinTimeout (see PortWatcher), so quitting never hangs on it. The streams the frame
    // thread's set holds outlive any late callback.
    watcher_.reset();
}

std::shared_ptr<MidiPorts> MidiInput::ports() { return shared_->choice; }

void MidiInput::add_port_task(PortTask task) {
    const std::lock_guard lock(shared_->tasks_mutex);
    shared_->tasks.push_back(std::move(task));
}

void MidiInput::pump_system_events() {
#if defined(__APPLE__)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);
#endif
}

PortChanges MidiInput::update() {
    PortChanges changes;
    std::shared_ptr<const OpenPorts> next = shared_->ports.poll();
    if (next == nullptr) {
        return changes;
    }
    const auto holds = [](const OpenPorts* set, const PortStream* stream) {
        if (set == nullptr) {
            return false;
        }
        return std::any_of(set->slots.begin(), set->slots.end(),
                           [stream](const OpenPort& p) { return p.stream.get() == stream; });
    };
    for (const OpenPort& port : next->slots) {
        if (port.stream && !holds(view_.get(), port.stream.get())) {
            ++changes.opened;
        }
    }
    if (view_ != nullptr) {
        for (const OpenPort& port : view_->slots) {
            if (port.stream && !holds(next.get(), port.stream.get())) {
                ++changes.closed;
                closed_dropped_ += port.stream->queue.dropped();
                closed_resyncs_ += port.stream->queue.resyncs();
            }
        }
    }
    view_ = std::move(next);
    scanned_ = true;
    return changes;
}

std::uint64_t MidiInput::dropped() const {
    std::uint64_t total = closed_dropped_;
    if (view_ != nullptr) {
        for (const auto& port : view_->slots) {
            if (port.stream) {
                total += port.stream->queue.dropped();
            }
        }
    }
    return total;
}

std::uint64_t MidiInput::resyncs() const {
    std::uint64_t total = closed_resyncs_;
    if (view_ != nullptr) {
        for (const auto& port : view_->slots) {
            if (port.stream) {
                total += port.stream->queue.resyncs();
            }
        }
    }
    return total;
}

const KeyboardProfile* MidiInput::profile() const {
    if (view_ != nullptr) {
        for (const auto& port : view_->slots) {
            if (port.stream && port.match.profile != nullptr) {
                return port.match.profile;
            }
        }
    }
    return nullptr;
}

int MidiInput::open_count() const {
    int count = 0;
    if (view_ != nullptr) {
        for (const auto& port : view_->slots) {
            count += port.stream ? 1 : 0;
        }
    }
    return count;
}

std::vector<std::string> MidiInput::open_names() const {
    std::vector<std::string> names;
    if (view_ != nullptr) {
        for (const auto& port : view_->slots) {
            if (port.stream) {
                names.push_back(port.name);
            }
        }
    }
    return names;
}

std::vector<MidiInput::InputInfo> MidiInput::inputs() const {
    std::vector<InputInfo> out;
    if (view_ != nullptr) {
        for (std::size_t i = 0; i < view_->slots.size(); ++i) {
            const OpenPort& port = view_->slots[i];
            if (port.stream) {
                out.push_back({i, port.name, port.match.profile});
            }
        }
    }
    return out;
}

} // namespace etude::app
