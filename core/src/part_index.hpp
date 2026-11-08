// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to core: finds a Song part by track and channel in constant time, so loading a song
// with thousands of parts does not search the part list once per note.
#pragma once

#include "etude/song.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace etude {

class PartIndex {
  public:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    PartIndex() = default;
    explicit PartIndex(const std::vector<Part>& parts) {
        index_.reserve(parts.size());
        for (std::size_t i = 0; i < parts.size(); ++i) {
            add(parts[i].track, parts[i].channel, i);
        }
    }

    // Index of the part of this track and channel, or kNone.
    std::size_t find(std::uint16_t track, std::uint8_t channel) const {
        const auto it = index_.find(key(track, channel));
        return it == index_.end() ? kNone : it->second;
    }
    // Records that `parts[at]` is the part of this track and channel (the first one added wins).
    void add(std::uint16_t track, std::uint8_t channel, std::size_t at) {
        index_.emplace(key(track, channel), at);
    }

  private:
    static std::uint32_t key(std::uint16_t track, std::uint8_t channel) {
        return (static_cast<std::uint32_t>(track) << 8) | channel;
    }
    std::unordered_map<std::uint32_t, std::size_t> index_;
};

} // namespace etude
