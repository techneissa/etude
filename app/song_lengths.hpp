// SPDX-License-Identifier: GPL-3.0-or-later
// How long each song plays, for the phone page's list ("Inbox · 2 KB · 3:12"). The library already
// reads every song once on its worker thread (and again only when the file's size or time changes),
// so the lengths are taken from what it has parsed: the frame thread makes the book from the
// library's finished entries whenever the library changes, and the upload server's request threads
// only look in it. No file is opened for a length, on a request thread or the frame thread, and a
// PDF or photo (never parsed) has none. No SDL.
#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace etude::app {

class SongLibrary;

// Path (as the library lists it, '/' separated, normalised) to seconds.
using SongLengths = std::unordered_map<std::string, double>;

// The length of every song the library has parsed and that is the only song of its file: a file of
// several ABC tunes is left out (no one length is its).
SongLengths song_lengths_of(const SongLibrary& library);

// The book the frame thread fills and the request threads read.
class SongLengthBook {
  public:
    void set(SongLengths lengths) {
        const std::lock_guard<std::mutex> lock(mutex_);
        lengths_ = std::move(lengths);
    }
    // Seconds, or a negative number when the song is not in the book.
    double get(const std::filesystem::path& path) const {
        const std::string key = path.lexically_normal().generic_string();
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = lengths_.find(key);
        return it == lengths_.end() ? -1.0 : it->second;
    }

  private:
    mutable std::mutex mutex_;
    SongLengths lengths_;
};

} // namespace etude::app
