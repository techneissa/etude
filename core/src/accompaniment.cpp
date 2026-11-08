// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/accompaniment.hpp"

#include "etude/instrument.hpp"

#include <algorithm>

namespace etude {

namespace {

std::string trimmed(const std::string& text) {
    std::size_t a = 0;
    std::size_t b = text.size();
    while (a < b && static_cast<unsigned char>(text[a]) <= 0x20) {
        ++a;
    }
    while (b > a && static_cast<unsigned char>(text[b - 1]) <= 0x20) {
        --b;
    }
    return text.substr(a, b - a);
}

} // namespace

std::vector<AccompanimentPart> accompaniment_parts(const Song& song) {
    std::vector<AccompanimentPart> out;
    for (std::size_t i = 0; i < song.parts.size() && out.size() < kMaxAccompanimentParts; ++i) {
        const Part& p = song.parts[i];
        if (!p.accompaniment || p.drum) {
            continue;
        }
        AccompanimentPart a;
        a.index = i;
        a.key = part_key(p.track, p.channel);
        a.name = trimmed(p.name);
        if (a.name.empty()) {
            a.name = trimmed(p.instrument_name);
        }
        if (a.name.empty()) {
            a.name = "Part " + std::to_string(out.size() + 1);
        }
        a.voice = is_voice_part(p);
        a.notes = p.note_count;
        out.push_back(std::move(a));
    }
    // A repeated name is numbered against every name in the list, so no two rows end up alike
    // ("Violin", "Violin", "Violin 2" read "Violin", "Violin 3", "Violin 2").
    std::vector<std::string> base;
    for (const AccompanimentPart& a : out) {
        base.push_back(a.name);
    }
    std::vector<std::string> taken;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto in_use = [&](const std::string& name) {
            if (std::find(taken.begin(), taken.end(), name) != taken.end()) {
                return true;
            }
            return std::find(base.begin() + static_cast<std::ptrdiff_t>(i) + 1, base.end(), name) !=
                   base.end();
        };
        std::string name = base[i];
        if (std::find(taken.begin(), taken.end(), name) != taken.end()) {
            int n = 2;
            while (in_use(base[i] + " " + std::to_string(n))) {
                ++n;
            }
            name = base[i] + " " + std::to_string(n);
        }
        out[i].name = name;
        taken.push_back(name);
    }
    return out;
}

AccompanimentState accompaniment_state(std::size_t parts, std::size_t muted) {
    if (parts == 0 || muted == 0) {
        return AccompanimentState::On;
    }
    return muted >= parts ? AccompanimentState::Off : AccompanimentState::Partial;
}

std::vector<std::uint32_t> muted_keys_in(const std::vector<AccompanimentPart>& parts,
                                         const std::vector<std::uint32_t>& keys) {
    std::vector<std::uint32_t> out;
    for (const std::uint32_t key : keys) {
        if (std::any_of(parts.begin(), parts.end(),
                        [key](const AccompanimentPart& p) { return p.key == key; })) {
            out.push_back(key);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

AccompanimentState accompaniment_state(const std::vector<AccompanimentPart>& parts,
                                       const std::vector<std::uint32_t>& muted) {
    return accompaniment_state(parts.size(), muted_keys_in(parts, muted).size());
}

std::vector<std::uint32_t> after_button_tap(const std::vector<AccompanimentPart>& parts,
                                            const std::vector<std::uint32_t>& muted) {
    return accompaniment_state(parts, muted) == AccompanimentState::On ? all_keys(parts)
                                                                      : std::vector<std::uint32_t>{};
}

std::vector<std::uint32_t> after_part_toggle(const std::vector<std::uint32_t>& muted,
                                             std::uint32_t key) {
    std::vector<std::uint32_t> out = muted;
    const auto it = std::find(out.begin(), out.end(), key);
    if (it != out.end()) {
        out.erase(it);
    } else {
        out.push_back(key);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::uint32_t> all_keys(const std::vector<AccompanimentPart>& parts) {
    std::vector<std::uint32_t> out;
    for (const AccompanimentPart& p : parts) {
        out.push_back(p.key);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace etude
