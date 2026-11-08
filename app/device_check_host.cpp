// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_check_host.hpp"

#include <algorithm>
#include <cctype>

namespace etude::app {

namespace {

DeviceCheckKeyboard describe(std::string_view port_name, const KeyboardProfile* profile) {
    DeviceCheckKeyboard kb;
    kb.id = keyboard_id(port_name, profile);
    kb.name = port_key(port_name);
    if (profile != nullptr) {
        kb.profile = std::string(profile->name);
        kb.range = profile->range;
    } else {
        kb.range = kRange88; // what the app takes an unknown keyboard to have
    }
    return kb;
}

} // namespace

std::string keyboard_id(std::string_view port_name, const KeyboardProfile* profile) {
    if (profile != nullptr) {
        return std::string(profile->name);
    }
    return port_key(port_name);
}

void DeviceCheckHost::load(const AppSettings& app, bool quiet) {
    memory_ = device_check_memory(app.device_check_done, app.device_check_seen);
    saved_ = memory_;
    quiet_ = quiet;
}

void DeviceCheckHost::write_back(AppSettings& app) {
    if (memory_.first_run_done == saved_.first_run_done && memory_.seen == saved_.seen) {
        return;
    }
    app.device_check_done = memory_.first_run_done;
    app.device_check_seen = device_check_seen_text(memory_, kMaxDeviceCheckSeenBytes);
    saved_ = memory_;
}

std::vector<DeviceCheckHost::Keyboard>
DeviceCheckHost::connected(std::span<const MidiInputInfo> inputs) const {
    std::vector<Keyboard> out;
    for (const MidiInputInfo& in : inputs) {
        out.push_back({describe(in.name, in.profile), false, in.name});
    }
    for (const std::string& name : pretend_) {
        out.push_back({describe(name, find_profile(name)), true, name});
    }
    // The MIDI inputs were narrowed to the pick by the app; the pretend ones are narrowed here, and
    // a real keyboard is never hidden.
    const std::string pick = port_key(pick_);
    if (!pick.empty() && std::any_of(out.begin(), out.end(), [&](const Keyboard& kb) {
            return port_key(kb.port) == pick;
        })) {
        std::erase_if(out, [&](const Keyboard& kb) { return kb.pretend && port_key(kb.port) != pick; });
    }
    return out;
}

void DeviceCheckHost::set_input_pick(const std::string& name) {
    if (name != pick_) {
        pick_ = name;
        pretend_changed_ = true; // the keyboards seen are worked out again
    }
}

// Notes the keyboard the card follows now.
void DeviceCheckHost::follow() {
    const DeviceCheckKeyboard* kb = check_.keyboard();
    if (kb != nullptr && std::ranges::find(followed_, kb->id) == followed_.end()) {
        followed_.push_back(kb->id);
    }
}

void DeviceCheckHost::drop_note_ons(std::vector<MidiEvent>& events, bool card_up) const {
    if (open_ && card_up) {
        std::erase_if(events, [](const MidiEvent& e) { return e.type == MidiEventType::NoteOn; });
    }
}

void DeviceCheckHost::begin_run() {
    check_.begin();
    followed_.clear();
    for (const Keyboard& kb : now_) {
        check_.keyboard_connected(kb.info);
    }
    follow();
    open_ = true;
    want_ = false;
}

// The check ended: nothing offers itself again for the keyboards it followed (one it never
// followed is still new), and the first start is over.
void DeviceCheckHost::end_run() {
    open_ = false;
    follow();
    remember_device_check(memory_, "");
    for (const std::string& id : followed_) {
        remember_device_check(memory_, id);
    }
}

void DeviceCheckHost::update(std::span<const MidiInputInfo> inputs, bool ready, bool changed,
                             bool idle) {
    if (changed) {
        for (std::string& id : slot_id_) {
            id.clear();
        }
        for (const MidiInputInfo& in : inputs) {
            if (in.slot < slot_id_.size()) {
                slot_id_[in.slot] = keyboard_id(in.name, in.profile);
            }
        }
    }
    if (!ready || (!changed && scanned_ && !want_ && !pretend_changed_)) {
        return;
    }
    pretend_changed_ = false;
    std::vector<Keyboard> next = connected(inputs);
    if (!scanned_) {
        scanned_ = true;
        want_ = !quiet_ && should_offer_device_check(memory_, DeviceCheckTrigger::Start, "");
    }
    for (const Keyboard& kb : next) {
        const bool known = std::any_of(now_.begin(), now_.end(), [&](const Keyboard& old) {
            return old.info.id == kb.info.id;
        });
        if (known) {
            continue;
        }
        if (open_) {
            check_.keyboard_connected(kb.info);
            follow();
        } else if ((!quiet_ || kb.pretend) &&
                   should_offer_device_check(memory_, DeviceCheckTrigger::KeyboardPlugged,
                                             kb.info.id)) {
            want_ = true;
        }
    }
    for (const Keyboard& old : now_) {
        const bool still = std::any_of(next.begin(), next.end(), [&](const Keyboard& kb) {
            return old.info.id == kb.info.id;
        });
        if (!still && open_) {
            check_.keyboard_removed(old.info.id);
        }
    }
    now_ = std::move(next);
    if (want_ && !open_ && idle) {
        begin_run();
    }
}

void DeviceCheckHost::note_on(std::size_t slot, int pitch) {
    if (open_ && slot < slot_id_.size()) {
        check_.note_on(slot_id_[slot], pitch);
        follow();
    }
}

std::optional<DeviceCheckHost::Played> DeviceCheckHost::dev_event(const DevKeyboardEvent& event) {
    using Kind = DevKeyboardEvent::Kind;
    switch (event.kind) {
    case Kind::OpenCheck:
        begin_run();
        return std::nullopt;
    case Kind::Plug:
        if (std::ranges::find(pretend_, event.name) == pretend_.end()) {
            pretend_.push_back(event.name);
            pretend_changed_ = true;
        }
        return std::nullopt;
    case Kind::Unplug:
        pretend_changed_ = std::erase(pretend_, event.name) > 0;
        return std::nullopt;
    case Kind::NoteOn:
    case Kind::NoteOff:
        if (std::ranges::find(pretend_, event.name) == pretend_.end()) {
            return std::nullopt;
        }
        if (event.kind == Kind::NoteOn && open_) {
            check_.note_on(keyboard_id(event.name, find_profile(event.name)), event.pitch);
            follow();
        }
        return Played{event.pitch, event.kind == Kind::NoteOn};
    }
    return std::nullopt;
}

void DeviceCheckHost::answer(DeviceCheckChoice choice) {
    switch (choice) {
    case DeviceCheckChoice::None:
        return;
    case DeviceCheckChoice::Run:
        begin_run();
        return;
    case DeviceCheckChoice::Skip:
        check_.skip();
        break;
    case DeviceCheckChoice::Done:
        check_.finish();
        break;
    }
    if (open_ && check_.finished()) {
        end_run();
    }
}

} // namespace etude::app
