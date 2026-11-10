// SPDX-License-Identifier: GPL-3.0-or-later
// The sheet worker: one thread that owns the engraver (Verovio is not thread-safe) and does all of
// sheet music's slow work, so the frame thread never waits for it. It reads the song's score again
// (sheet_job.hpp), lays it out, matches it to the song (core sheet_map.hpp) and draws the systems
// it is asked for. The frame thread hands in jobs and takes finished results; a newer job makes
// the older one's results stale (`generation`), and they are dropped.
#pragma once

#include "sheet_engraver.hpp"
#include "loaded_song.hpp"

#include "etude/sheet_map.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace etude::app {

// Songs with more practised notes than this get no sheet music: the engraver keeps a few kB per
// note while it works.
inline constexpr int kMaxSheetNotes = 10000;

struct SheetJob {
    std::uint64_t generation = 0;
    std::uint32_t song_serial = 0; // SongController::load_serial: the score is read once per song
    std::shared_ptr<const LoadedSong> song;
    EngraveOptions options;
};

struct SheetLayoutReady {
    std::uint64_t generation = 0;
    bool ok = false;
    std::string reason; // why there is no sheet music, when !ok
    EngraveOptions options;
    SheetLayout layout;
    SheetMap map;
    double source_ms = 0.0;
    double engrave_ms = 0.0;
};

struct SheetPictureReady {
    std::uint64_t generation = 0;
    int system = 0;
    SystemPicture picture;
    double ms = 0.0;
};

class SheetWorker {
  public:
    SheetWorker();
    ~SheetWorker();
    SheetWorker(const SheetWorker&) = delete;
    SheetWorker& operator=(const SheetWorker&) = delete;

    // Lays a song out; replaces the job not started and every picture asked for before.
    void engrave(SheetJob job);
    // The systems the view lacks from the layout of `generation`, most wanted first; replaces the
    // list asked for before. One whose picture is still waiting to be taken is not drawn again.
    void want_pictures(std::uint64_t generation, std::span<const int> systems);
    // Finished work, oldest first; false when there is none. A picture that could not be drawn
    // comes back empty (width 0); it is drawn again only when asked for again.
    bool take_layout(SheetLayoutReady& out);
    bool take_picture(SheetPictureReady& out);

  private:
    void run();

    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::unique_ptr<SheetJob> job_;      // waiting to start
    std::uint64_t picture_generation_ = 0;
    std::vector<int> wanted_;            // for picture_generation_
    std::vector<int> drawn_;             // drawn for picture_generation_
    std::deque<SheetLayoutReady> layouts_;
    std::deque<SheetPictureReady> pictures_;
    std::thread thread_;                 // started once the rest is built
};

} // namespace etude::app
