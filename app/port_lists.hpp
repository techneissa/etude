// SPDX-License-Identifier: GPL-3.0-or-later
// The MIDI ports the watcher thread sees and the rules for choosing one of them. Plain values, no
// MIDI library: the settings page, the window and the tests use it.
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app {

// The names the watcher thread last listed (ports the app never opens left out).
struct PortLists {
    std::vector<std::string> inputs;  // by port_key, each name once
    std::vector<std::string> outputs;
    bool inputs_known = false;  // the watcher thread has listed the inputs / the outputs
    bool outputs_known = false;
    std::string output_failed;  // the chosen output that is listed but would not open, or empty

    bool known(bool input) const { return input ? inputs_known : outputs_known; }

    friend bool operator==(const PortLists&, const PortLists&) = default;
};

// "PSR-E383 MIDI 1 24:0" -> "PSR-E383 MIDI 1": the client and port numbers a Linux port ends with
// change with the order the system found its devices in. Ports are saved and compared by this.
inline std::string port_key(std::string_view name) {
    const auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    const std::size_t colon = name.rfind(':');
    if (colon == std::string_view::npos || colon + 1 >= name.size() ||
        !std::all_of(name.begin() + static_cast<std::ptrdiff_t>(colon) + 1, name.end(), digit)) {
        return std::string(name);
    }
    std::size_t start = colon;
    while (start > 0 && digit(name[start - 1])) {
        --start;
    }
    if (start == colon || start == 0 || name[start - 1] != ' ') {
        return std::string(name);
    }
    return std::string(name.substr(0, start - 1));
}

// `names` with the numbers taken off and each name once, in order.
inline std::vector<std::string> port_keys(const std::vector<std::string>& names) {
    std::vector<std::string> out;
    for (const std::string& name : names) {
        std::string key = port_key(name);
        if (std::find(out.begin(), out.end(), key) == out.end()) {
            out.push_back(std::move(key));
        }
    }
    return out;
}

inline bool port_listed(const std::vector<std::string>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

// A named choice that is not listed is not in force: the port is used as if the choice were Auto.
inline bool port_missing(std::string_view saved, const PortLists& lists, bool input) {
    return lists.known(input) && !saved.empty() && !port_listed(input ? lists.inputs : lists.outputs, saved);
}

// With a named input that is listed, the first port of that name is the only one held open: its
// index among `keys` (the listed ports' keys in listing order), else -1 (Auto, or the name is gone:
// every input is held).
inline int pinned_index(std::string_view chosen, const std::vector<std::string>& keys) {
    if (chosen.empty()) {
        return -1;
    }
    const auto at = std::find(keys.begin(), keys.end(), chosen);
    return at == keys.end() ? -1 : static_cast<int>(at - keys.begin());
}

// The steps of a port row: Auto, then the listed ports, then the saved name when it is not listed
// (so a port that went stays visible and can be left by a tap).
struct PortChoices {
    std::vector<std::string> names; // names[0] is Auto, as an empty name
    int current = 0;                // the saved choice's place in names
};

inline PortChoices port_choices(std::string_view saved, const std::vector<std::string>& listed) {
    PortChoices out;
    out.names.emplace_back();
    for (const std::string& name : listed) {
        out.names.push_back(name);
    }
    if (!saved.empty()) {
        const auto at = std::find(out.names.begin() + 1, out.names.end(), saved);
        out.current = static_cast<int>(at - out.names.begin());
        if (at == out.names.end()) {
            out.names.emplace_back(saved);
        }
    }
    return out;
}

// Says once, when a saved port goes missing, that it did; the next time it goes missing it says so
// again. Call it every frame with the saved name and the latest lists.
class PortFallback {
  public:
    // True on the call that sees `saved` gone.
    bool update(std::string_view saved, const PortLists& lists, bool input) {
        if (!port_missing(saved, lists, input)) {
            if (lists.known(input)) {
                told_.clear();
            }
            return false;
        }
        if (told_ == saved) {
            return false;
        }
        told_ = std::string(saved);
        return true;
    }

  private:
    std::string told_;
};

// Says once that a chosen output is listed but would not open (the piano's is used instead); the
// next name that fails says so again.
class OpenFailure {
  public:
    bool update(std::string_view failed) {
        if (failed.empty()) {
            told_.clear();
            return false;
        }
        if (told_ == failed) {
            return false;
        }
        told_ = std::string(failed);
        return true;
    }

  private:
    std::string told_;
};

} // namespace etude::app
