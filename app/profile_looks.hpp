// SPDX-License-Identifier: GPL-3.0-or-later
// The looks of the other profiles, for the Configs page's "Copy from another profile" row: read
// from each profile's stored settings (core `profile_look`), and read again only when that text
// changed.
#pragma once

#include "ui/profile_port.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace etude::app {

class HistoryStore;

class ProfileLookCache {
  public:
    // The looks of every profile but `active`, in the order of the profiles. Frame thread.
    std::span<const ProfileLook> refresh(const HistoryStore& history, std::int64_t active);

  private:
    struct Seen {
        std::int64_t id = 0;
        std::string text;
    };
    std::vector<Seen> seen_;
    std::vector<ProfileLook> looks_;
};

} // namespace etude::app
