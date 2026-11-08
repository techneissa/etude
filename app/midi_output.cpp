// SPDX-License-Identifier: GPL-3.0-or-later
#include "midi_output.hpp"

#include "port_watcher.hpp"

#include <libremidi/libremidi.hpp>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>
#include <string_view>
#include <vector>

namespace etude::app {

namespace {

constexpr std::uint8_t kStatusNoteOff = 0x80;
constexpr std::uint8_t kStatusNoteOn = 0x90;
constexpr std::uint8_t kStatusControl = 0xB0;

std::string label(const libremidi::output_port& port) {
    return port.display_name.empty() ? port.port_name : port.display_name;
}

// Ports the settings page offers: not the app's own, a loopback or a nameless one.
bool offered(const libremidi::output_port& port) {
    const std::string_view name = port.port_name;
    const std::string_view device = port.device_name;
    return !name.empty() && !name.starts_with("etude") && !device.starts_with("etude") &&
           name.find("Midi Through") == std::string_view::npos &&
           device.find("Midi Through") == std::string_view::npos;
}

} // namespace

// An open output port. The watcher thread makes it and holds a share until this object has let go
// of it, so the last share (and so the close) is the watcher thread's.
struct MidiOutput::Port {
    std::unique_ptr<libremidi::midi_out> out;
    std::string name;
};

// The port the watcher thread wants used, or none; published whole.
struct MidiOutput::Link {
    struct Offer {
        std::shared_ptr<Port> port;
    };
    Handover<Offer> offers;
    std::atomic<std::uint32_t> acked{0}; // the offer update() has taken, so the watcher may let go
};

class MidiOutput::Scanner {
  public:
    Scanner(std::shared_ptr<Link> link, std::shared_ptr<MidiPorts> choice)
        : link_(std::move(link)), choice_(std::move(choice)) {}

    // Watcher thread, after each scan of the inputs.
    void scan(std::span<const std::string> open_input_names) {
        if (!observer_) {
            libremidi::observer_configuration conf;
            conf.track_hardware = true;
            conf.track_virtual = true;
            conf.notify_in_constructor = false;
            observer_ = std::make_unique<libremidi::observer>(conf);
        }
        // A port the frame thread has let go of is closed here, not there.
        std::erase_if(retired_, [this](const Retired& r) {
            return link_->acked.load(std::memory_order_acquire) >= r.serial;
        });
        const auto ports = observer_->get_output_ports();
        // The player's pick wins when it is listed; a name that is gone means Auto, and so does one
        // that would not open (not tried again until the choice or the list changes).
        std::string want;
        std::vector<std::string> keys;
        if (choice_ != nullptr) {
            std::vector<std::string> names;
            for (const auto& p : ports) {
                if (offered(p)) {
                    names.push_back(label(p));
                }
            }
            keys = port_keys(names);
            const std::string chosen = port_key(choice_->output_choice());
            if (port_listed(keys, chosen)) {
                want = chosen;
            }
        }
        if (failed_want_ != want) {
            failed_want_.clear();
        }
        std::string failed_report;
        if (!want.empty() && want == failed_want_) {
            failed_report = want;
            want.clear();
        }
        if (current_ != nullptr) {
            const bool present = std::any_of(ports.begin(), ports.end(), [&](const auto& p) {
                return p.port == handle_ && label(p) == current_->name;
            });
            if (present && (want.empty() ? !named_ : port_key(current_->name) == want)) {
                if (choice_ != nullptr) {
                    choice_->publish_outputs(std::move(keys), std::move(failed_report));
                }
                return;
            }
        }
        std::shared_ptr<Port> old = std::move(current_);
        current_.reset();
        if (!open_matching(ports, want, open_input_names) && !want.empty()) {
            failed_want_ = want;
            failed_report = want;
            SDL_Log("MIDI output %s would not open: using the piano's", want.c_str());
            open_matching(ports, "", open_input_names);
        }
        if (choice_ != nullptr) {
            choice_->publish_outputs(std::move(keys), std::move(failed_report));
        }
        if (old == nullptr && current_ == nullptr) {
            return; // none before, none now
        }
        link_->offers.publish(std::make_shared<const Link::Offer>(Link::Offer{current_}));
        if (old != nullptr) {
            retired_.push_back({std::move(old), link_->offers.serial()});
        }
    }

  private:
    // Opens the first port that matches: the piano's when `want` is empty, else the port of that
    // key. True when one is open (current_).
    bool open_matching(const std::vector<libremidi::output_port>& ports, const std::string& want,
                       std::span<const std::string> open_input_names) {
        for (const auto& port : ports) {
            const std::string name = label(port);
            if (want.empty() ? !is_piano_output(name, open_input_names) &&
                                   !is_piano_output(port.port_name, open_input_names)
                             : port_key(name) != want) {
                continue;
            }
            auto out = std::make_unique<libremidi::midi_out>(
                libremidi::output_configuration{},
                libremidi::midi_out_configuration_for(*observer_));
            if (const auto err = out->open_port(port, "etude out"); err != stdx::error{}) {
                if (failed_ != port.port) {
                    failed_ = port.port;
                    SDL_Log("MIDI output failed to open: %s", name.c_str());
                }
                continue;
            }
            failed_ = 0;
            current_ = std::make_shared<Port>();
            current_->out = std::move(out);
            current_->name = name;
            handle_ = port.port;
            named_ = !want.empty();
            return true;
        }
        return false;
    }

    struct Retired {
        std::shared_ptr<Port> port;
        std::uint32_t serial = 0; // the offer that took it away
    };
    std::shared_ptr<Link> link_;
    std::shared_ptr<MidiPorts> choice_; // may be null: always Auto
    std::unique_ptr<libremidi::observer> observer_;
    std::shared_ptr<Port> current_;
    std::uint64_t handle_ = 0;
    std::string failed_want_;  // the chosen output that would not open
    bool named_ = false;       // current_ is the player's pick, not the piano's port
    std::uint64_t failed_ = 0; // the port that last failed to open, so it is logged once
    std::vector<Retired> retired_;
};

MidiOutput::MidiOutput() : link_(std::make_shared<Link>()) {}

MidiOutput::~MidiOutput() {
    all_off();
    // Let go of the port: the watcher thread holds the other share and closes it.
    const std::lock_guard<std::mutex> lock(mutex_);
    out_.reset();
}

std::function<void(std::span<const std::string>)>
MidiOutput::port_task(std::shared_ptr<MidiPorts> ports) {
    auto scanner = std::make_shared<Scanner>(link_, std::move(ports));
    return [scanner](std::span<const std::string> names) { scanner->scan(names); };
}

void MidiOutput::update() {
    const std::shared_ptr<const Link::Offer> offer = link_->offers.poll();
    if (offer == nullptr) {
        return;
    }
    const std::uint32_t taken = link_->offers.seen();
    {
        std::shared_ptr<Port> let_go; // dropped after the lock: ours is not the last share
        const std::lock_guard<std::mutex> lock(mutex_);
        if (out_ != nullptr && out_ != offer->port) {
            SDL_Log("MIDI output removed: %s", name_.c_str());
            // A port still there (the player chose another output) is told to stop what it was
            // playing; one that is gone just drops the send.
            reset_port_locked();
            let_go = std::move(out_);
            out_.reset();
            sounding_ = {};
            pedal_.reset();
            used_.reset();
            reconnect_.lost(name_);
            name_.clear();
        }
        if (offer->port != nullptr && out_ == nullptr) {
            const bool again = reconnect_.reconnected(offer->port->name);
            out_ = offer->port;
            name_ = offer->port->name;
            if (again) {
                reset_port_locked(); // before anything else reaches the piano
            }
            SDL_Log("MIDI output %s, virtual presses play on: %s", again ? "back" : "opened",
                    name_.c_str());
        }
    }
    link_->acked.store(taken, std::memory_order_release);
}

void MidiOutput::reset_port_locked() {
    for (const PlaybackMessage& m : etude::port_reset_messages()) {
        send_locked(static_cast<std::uint8_t>(kStatusControl | m.channel), m.data,
                    static_cast<std::uint8_t>(m.value));
    }
}

void MidiOutput::send_locked(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) {
    if (out_ != nullptr) {
        // A failed send has nobody to report to; the next note will try again.
        (void)out_->out->send_message(status, data1, data2);
    }
}

void MidiOutput::note_on(int channel, int note, int velocity) {
    const std::lock_guard<std::mutex> lock(mutex_);
    note_on_locked(channel, note, velocity);
}

void MidiOutput::note_on_locked(int channel, int note, int velocity) {
    if (out_ == nullptr || channel < 0 || channel > 15 || note < 0 || note > 127) {
        return;
    }
    const auto ch = static_cast<std::size_t>(channel);
    sounding_[ch].set(static_cast<std::size_t>(note));
    used_.set(ch);
    send_locked(static_cast<std::uint8_t>(kStatusNoteOn | channel), static_cast<std::uint8_t>(note),
                static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)));
}

void MidiOutput::note_off(int channel, int note) {
    const std::lock_guard<std::mutex> lock(mutex_);
    note_off_locked(channel, note);
}

void MidiOutput::note_off_locked(int channel, int note) {
    if (out_ == nullptr || channel < 0 || channel > 15 || note < 0 || note > 127) {
        return;
    }
    const auto ch = static_cast<std::size_t>(channel);
    if (!sounding_[ch].test(static_cast<std::size_t>(note))) {
        return; // never started here
    }
    sounding_[ch].reset(static_cast<std::size_t>(note));
    send_locked(static_cast<std::uint8_t>(kStatusNoteOff | channel),
                static_cast<std::uint8_t>(note), 0);
}

void MidiOutput::sustain(int channel, bool down) {
    const std::lock_guard<std::mutex> lock(mutex_);
    sustain_locked(channel, down);
}

void MidiOutput::sustain_locked(int channel, bool down) {
    if (out_ == nullptr || channel < 0 || channel > 15) {
        return;
    }
    const auto ch = static_cast<std::size_t>(channel);
    pedal_.set(ch, down);
    used_.set(ch);
    send_locked(static_cast<std::uint8_t>(kStatusControl | channel), kCcSustain, down ? 127 : 0);
}

void MidiOutput::all_off() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (out_ == nullptr) {
        return;
    }
    for (std::size_t ch = 0; ch < 16; ++ch) {
        if (!used_.test(ch)) {
            continue;
        }
        for (std::size_t note = 0; note < 128; ++note) {
            if (sounding_[ch].test(note)) {
                send_locked(static_cast<std::uint8_t>(kStatusNoteOff | ch),
                            static_cast<std::uint8_t>(note), 0);
            }
        }
        sounding_[ch].reset();
        if (pedal_.test(ch)) {
            send_locked(static_cast<std::uint8_t>(kStatusControl | ch), kCcSustain, 0);
            pedal_.reset(ch);
        }
        send_locked(static_cast<std::uint8_t>(kStatusControl | ch), kCcAllNotesOff, 0);
    }
    used_.reset();
}

void MidiOutput::play(const PlaybackMessage& m) {
    const std::lock_guard<std::mutex> lock(mutex_);
    play_locked(m);
}

void MidiOutput::play_locked(const PlaybackMessage& m) {
    if (out_ == nullptr || m.channel > 15) {
        return;
    }
    const int ch = m.channel;
    switch (m.kind) {
    case PlaybackMessage::Kind::NoteOn:
        if (m.value == 0) {
            note_off_locked(ch, m.data);
        } else {
            note_on_locked(ch, m.data, m.value);
        }
        break;
    case PlaybackMessage::Kind::NoteOff:
        note_off_locked(ch, m.data);
        break;
    case PlaybackMessage::Kind::ControlChange:
        if (m.data == kCcSustain) {
            sustain_locked(ch, m.value >= 64);
            break;
        }
        used_.set(static_cast<std::size_t>(ch));
        send_locked(static_cast<std::uint8_t>(kStatusControl | ch),
                    static_cast<std::uint8_t>(m.data & 0x7F),
                    static_cast<std::uint8_t>(m.value & 0x7F));
        if (m.data == kCcAllNotesOff || m.data == 120) {
            sounding_[static_cast<std::size_t>(ch)].reset();
        }
        break;
    case PlaybackMessage::Kind::ProgramChange:
        used_.set(static_cast<std::size_t>(ch));
        (void)out_->out->send_message(static_cast<unsigned char>(0xC0 | ch),
                                      static_cast<unsigned char>(m.data & 0x7F));
        break;
    case PlaybackMessage::Kind::PitchBend:
        used_.set(static_cast<std::size_t>(ch));
        send_locked(static_cast<std::uint8_t>(0xE0 | ch), static_cast<std::uint8_t>(m.value & 0x7F),
                    static_cast<std::uint8_t>((m.value >> 7) & 0x7F));
        break;
    }
}

int MidiOutput::sounding() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    int n = 0;
    for (const auto& ch : sounding_) {
        n += static_cast<int>(ch.count());
    }
    return n;
}

} // namespace etude::app
