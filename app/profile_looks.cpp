// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_looks.hpp"

#include "history_store.hpp"

#include "etude/profile_settings.hpp"

namespace etude::app {

std::span<const ProfileLook> ProfileLookCache::refresh(const HistoryStore& history,
                                                       std::int64_t active) {
    bool changed = false;
    std::size_t n = 0;
    for (const Profile& p : history.profiles().all()) {
        if (p.id == active) {
            continue;
        }
        if (n >= seen_.size() || seen_[n].id != p.id ||
            seen_[n].text != history.profile_settings(p.id)) {
            changed = true;
            break;
        }
        ++n;
    }
    if (changed || n != seen_.size()) {
        seen_.clear();
        looks_.clear();
        for (const Profile& p : history.profiles().all()) {
            if (p.id == active) {
                continue;
            }
            const std::string& text = history.profile_settings(p.id);
            seen_.push_back({p.id, text});
            looks_.push_back({p.id, profile_look(text)});
        }
    }
    return looks_;
}

} // namespace etude::app
