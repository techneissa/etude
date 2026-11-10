// SPDX-License-Identifier: GPL-3.0-or-later
// Splits an open song's hands again on a thread of its own when the player moves the split point,
// and builds the tables the practice needs for the new hands (the same ones the opener builds for
// a song it loads), so the frame thread only takes them over. The newest request wins: a marker
// dragged across many keys makes many requests and the frame sees the latest answer that is ready.
// std only (no SDL), so the tests use it.
#pragma once

#include "lent_song.hpp"
#include "prepared_song.hpp"

#include "etude/difficulty.hpp"
#include "etude/hand_split.hpp"
#include "etude/song.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace etude::app {

// The song to split again comes one of two ways. A caller that holds it as its file gave it hands
// it over in `unsplit`. The controller, which keeps only the hands the file gave
// (LoadedSong::file_hands), lends the open song through `lent` instead: the worker copies it,
// puts `file_hands` back on the copy and compares its song_fingerprint with the file's. A song
// replaced since the request (`generation`), cancelled, or whose fingerprint differs makes no
// answer. The fingerprint does not cover every field (track, finger and ticks are left out); those
// are copied from the open song, which only a load replaces and that changes `generation`.
struct HandSplitRequest {
    std::shared_ptr<const Song> unsplit; // the song as its file gave it
    std::shared_ptr<const HandSplit> file_hands;
    std::shared_ptr<LentSong> lent;
    std::uint64_t generation = 0; // of `lent` when the request was made
    std::uint64_t hash = 0;       // of the file, for the split cache
    bool abc_chords = false;
    int split_point = kAutoSplit; // the player's choice (hand_split.hpp)
    SongLoadPlan plan;            // the hands and transposition the tables are made for
    std::uint64_t serial = 0;     // the caller's, handed back with the answer
};

struct HandSplitResult {
    std::uint64_t serial = 0;
    int split_point = kAutoSplit;
    Song song;                                        // the song with the split applied
    PreparedSong prepared;                            // the tables of `song`
    std::shared_ptr<const SongDifficulty> difficulty; // the bars rated again, for the new hands
    int suggested = kMiddleC; // where a straight line best divides the hands now
    double seconds = 0.0;     // how long the worker took
};

class HandSplitJob {
  public:
    HandSplitJob() = default;
    ~HandSplitJob();
    HandSplitJob(const HandSplitJob&) = delete;
    HandSplitJob& operator=(const HandSplitJob&) = delete;

    // Starts on `request` as soon as the worker is free; one waiting request is kept, a newer one
    // replaces it.
    void request(HandSplitRequest request);
    // The finished answer, if there is one (the newest the worker made), and clears it.
    std::optional<HandSplitResult> take();
    // Drops what waits and what is finished; a running split ends, its answer is not kept.
    void cancel();
    // A request is waiting or running.
    bool busy() const;
    // Blocks until nothing waits or runs (tests, and quitting).
    void wait_idle();

  private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::optional<HandSplitRequest> waiting_;
    std::optional<HandSplitResult> done_;
    bool running_ = false;
    bool stop_ = false;
    // Up by one with every cancel(): an answer made before is dropped, and a copy not yet begun
    // is skipped. Read by the worker while it holds the lent song, so atomic.
    std::atomic<std::uint64_t> epoch_{0};
    std::thread thread_;
};

} // namespace etude::app
