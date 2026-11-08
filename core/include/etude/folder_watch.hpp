// SPDX-License-Identifier: GPL-3.0-or-later
// What changed in the Songs folder between two looks at it, and when a changed file is finished:
// the app lists the folder (on a worker thread, when the system says something moved or a timer
// runs) and hands the listing here. A file that is new or changed is held back until its size and
// time stop changing, so a copy still arriving over the network is never read half done. Pure:
// the listing and the time come in, a report goes out.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// One file of the listing: its path under the watched folder ('/' separated), size and time.
struct WatchEntry {
    std::string path;
    std::uint64_t size = 0;
    std::int64_t mtime = 0; // any stamp that changes when the file does
};

// A file is finished when its size and time have stayed as they are for this long.
inline constexpr std::int64_t kWatchSettleMs = 1500;
// A file that vanishes and appears again with the same size and time within this long was moved or
// renamed, not added.
inline constexpr std::int64_t kWatchMoveMs = 30'000;

// True for a file the library cares about: a song, a PDF or an image, in no hidden folder and not
// itself hidden (so ".incoming" uploads, "._song.mid" from a Mac and ".DS_Store" are left alone).
bool is_watched_path(std::string_view relative);

struct WatchReport {
    std::vector<std::string> added;   // finished new files
    std::vector<std::string> changed; // finished changes to files already there
    std::vector<std::string> removed;
    std::size_t moved = 0;     // files that changed folder or name, which are not new
    std::size_t new_songs = 0; // the song files among `added`
    bool empty() const { return added.empty() && changed.empty() && removed.empty() && moved == 0; }
};

class WatchTracker {
  public:
    explicit WatchTracker(std::int64_t settle_ms = kWatchSettleMs) : settle_ms_(settle_ms) {}

    // Takes a listing of the whole folder at `now_ms` (any steady clock). The first listing is
    // what was already there and reports nothing.
    WatchReport observe(std::span<const WatchEntry> listing, std::int64_t now_ms);
    // True while some file waits for its size to stop changing: look again soon.
    bool settling() const { return !settling_.empty(); }
    // Files known (finished) now.
    std::size_t known() const { return known_.size(); }

  private:
    struct Stamp {
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        bool operator==(const Stamp&) const = default;
    };
    struct Waiting {
        Stamp stamp;
        std::int64_t last_change_ms = 0;
        bool was_known = false;
    };
    struct Gone {
        Stamp stamp;
        std::int64_t at_ms = 0;
    };

    std::int64_t settle_ms_;
    bool baseline_ = false;
    std::map<std::string, Stamp> known_;
    std::map<std::string, Waiting> settling_;
    std::vector<Gone> gone_;
};

// The songs the app downloaded itself, which already had their notice: the watcher sees those files
// too, and they are taken off its count so that a song from elsewhere (the phone) is still told,
// whenever it comes. Downloads the watcher never reports (a file replaced) are forgotten with
// `forget`, once the watcher has had its time to settle.
class OwnDownloads {
  public:
    void landed(std::size_t count) { unreported_ += count; }
    void forget() { unreported_ = 0; }
    // `new_songs` as the watcher counted them, less the app's own downloads among them.
    std::size_t take(std::size_t new_songs) {
        const std::size_t own = new_songs < unreported_ ? new_songs : unreported_;
        unreported_ -= own;
        return new_songs - own;
    }

  private:
    std::size_t unreported_ = 0;
};

// "1 new song", "3 new songs", or empty for none.
std::string new_songs_text(std::size_t count);

} // namespace etude
