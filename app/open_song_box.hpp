// SPDX-License-Identifier: GPL-3.0-or-later
// The song the piano has open, as a path text that any thread may read: the upload server's request
// threads ask it before they rename, move or delete a file, so the song being played is never taken
// from under the player. The frame thread sets it when a song is installed. No SDL.
#pragma once

#include <mutex>
#include <string>
#include <utility>

namespace etude::app {

class OpenSongBox {
  public:
    // The absolute path of the open song's file, with "#tune" for an ABC tune; empty for none.
    void set(std::string path) {
        const std::lock_guard<std::mutex> lock(mutex_);
        path_ = std::move(path);
    }
    std::string get() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return path_;
    }

  private:
    mutable std::mutex mutex_;
    std::string path_;
};

} // namespace etude::app
