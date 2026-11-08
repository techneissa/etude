// SPDX-License-Identifier: GPL-3.0-or-later
// Finds which songs of the library are versions of one piece (core song_versions.hpp), on a thread
// of its own: the frame thread hands over a snapshot of the songs when the library loaded or
// changed, and takes the answer when it is ready. Nothing here is per frame; a pass over thousands
// of songs takes a fraction of a second, off the frame thread, and a newer request replaces one
// that has not started. The answer is by content hash, not by place in the list, so it stays good
// while songs come and go: a song it does not know is simply on its own until the next pass.
// No SDL, no files: std only, so the tests use it.
#pragma once

#include "etude/medal_eval.hpp"
#include "etude/song_versions.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace etude::app {

// The groups of a pass, by content hash.
struct VersionIndex {
    struct Member {
        std::uint64_t hash = 0;
        std::string name; // the chip's word
        bool labelled = false;
    };
    // Each group's members, easiest first.
    std::vector<std::vector<Member>> groups;

    // Where a song is: its group and its place in it.
    struct Place {
        std::uint32_t group = 0;
        std::uint32_t member = 0;
    };
    // Null for a song in no group.
    const Place* find(std::uint64_t hash) const {
        const auto it = where.find(hash);
        return it == where.end() ? nullptr : &it->second;
    }
    std::unordered_map<std::uint64_t, Place> where;
};

// One song of a snapshot: its content hash and what grouping reads. Songs with the same hash are
// one song (the first counts).
struct VersionInput {
    std::uint64_t hash = 0;
    VersionSong song;
};

// The groups of `songs`, synchronously (the thread runs this; tests call it). `overrides` are the
// player's corrections by content hash, laid over the automatic grouping.
std::shared_ptr<const VersionIndex> group_by_hash(std::vector<VersionInput> songs,
                                                  const VersionOverrides& overrides = {});

// Which songs are versions of one piece, for the medals (core PieceMap): each member of a group to the
// group's lowest hash, so every version of a piece counts as the one song. Songs in no group are not in it.
std::shared_ptr<const PieceMap> piece_map(const VersionIndex& index);

class VersionGrouper {
  public:
    VersionGrouper();
    ~VersionGrouper();
    VersionGrouper(const VersionGrouper&) = delete;
    VersionGrouper& operator=(const VersionGrouper&) = delete;

    // Asks for a pass over `songs` with the player's corrections; one not started yet is replaced.
    // Never waits.
    void request(std::vector<VersionInput> songs, VersionOverrides overrides = {});
    // The latest finished pass, once; null when none finished since the last call. Never waits.
    std::shared_ptr<const VersionIndex> take();
    // A pass is waiting or running.
    bool busy() const;
    // Waits until nothing is waiting or running (tests).
    void wait_idle();

  private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;
    std::vector<VersionInput> pending_;
    VersionOverrides pending_overrides_;
    bool has_pending_ = false;
    bool running_ = false;
    bool quit_ = false;
    std::shared_ptr<const VersionIndex> done_;
    std::thread thread_;
};

} // namespace etude::app
