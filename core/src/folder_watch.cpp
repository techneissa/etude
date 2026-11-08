// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/folder_watch.hpp"

#include "etude/upload_rules.hpp"

#include <algorithm>
#include <iterator>

namespace etude {

bool is_watched_path(std::string_view relative) {
    if (relative.empty() || relative.front() == '/') {
        return false;
    }
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = relative.find('/', start);
        const std::string_view part =
            relative.substr(start, slash == std::string_view::npos ? slash : slash - start);
        if (part.empty() || part.front() == '.') {
            return false;
        }
        if (slash == std::string_view::npos) {
            return kind_of_name(part).has_value();
        }
        start = slash + 1;
    }
}

WatchReport WatchTracker::observe(std::span<const WatchEntry> listing, std::int64_t now_ms) {
    WatchReport report;
    std::map<std::string, Stamp> current;
    for (const WatchEntry& e : listing) {
        if (is_watched_path(e.path)) {
            current[e.path] = {e.size, e.mtime};
        }
    }
    if (!baseline_) {
        baseline_ = true;
        known_ = std::move(current);
        return report;
    }
    gone_.erase(std::remove_if(gone_.begin(), gone_.end(),
                               [&](const Gone& g) { return now_ms - g.at_ms > kWatchMoveMs; }),
                gone_.end());
    for (auto it = known_.begin(); it != known_.end();) {
        if (current.count(it->first) == 0) {
            report.removed.push_back(it->first);
            gone_.push_back({it->second, now_ms});
            it = known_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = settling_.begin(); it != settling_.end();) {
        it = current.count(it->first) == 0 ? settling_.erase(it) : std::next(it);
    }
    for (const auto& [path, stamp] : current) {
        const auto known = known_.find(path);
        if (known != known_.end() && known->second == stamp) {
            settling_.erase(path); // changed and changed back
            continue;
        }
        auto waiting = settling_.find(path);
        if (waiting == settling_.end()) {
            if (known == known_.end()) {
                const auto moved = std::find_if(gone_.begin(), gone_.end(),
                                                [&](const Gone& g) { return g.stamp == stamp; });
                if (moved != gone_.end()) {
                    gone_.erase(moved);
                    known_[path] = stamp; // a rename keeps size and time: nothing arrived
                    ++report.moved;
                    continue;
                }
            }
            settling_[path] = {stamp, now_ms, known != known_.end()};
        } else if (!(waiting->second.stamp == stamp)) {
            waiting->second.stamp = stamp; // still growing
            waiting->second.last_change_ms = now_ms;
        } else if (now_ms - waiting->second.last_change_ms >= settle_ms_) {
            (waiting->second.was_known ? report.changed : report.added).push_back(path);
            known_[path] = stamp;
            settling_.erase(waiting);
        }
    }
    for (const std::string& path : report.added) {
        if (is_song_extension(lower_extension(path))) {
            ++report.new_songs;
        }
    }
    return report;
}

std::string new_songs_text(std::size_t count) {
    if (count == 0) {
        return {};
    }
    return std::to_string(count) + (count == 1 ? " new song" : " new songs");
}

} // namespace etude
