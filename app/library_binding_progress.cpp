// SPDX-License-Identifier: GPL-3.0-or-later
// The Progress page's part of the library binding: asking the history for the numbers, taking the
// answer, and opening a song from a hash (Continue, Practise today).
#include "library_binding.hpp"

namespace etude::app {

void LibraryBinding::request_progress(int goal_minutes) {
    ProgressRequest request;
    request.profile_id = history_.active_profile();
    request.today = today_;
    request.goal_minutes = goal_minutes;
    request.utc_offset_s = utc_offset_s_;
    request.pieces = pieces_;
    progress_serial_ = history_.request_progress(request);
    progress_failed_ = progress_serial_ == 0; // the store takes no more work
}

std::shared_ptr<const ProgressView> LibraryBinding::progress() {
    take_progress_answers();
    return progress_;
}

void LibraryBinding::take_progress_answers() {
    for (ProgressAnswer& answer : history_.take_progress()) {
        if (answer.serial != progress_serial_) {
            continue; // an earlier request's
        }
        if (answer.view != nullptr) {
            progress_ = std::move(answer.view);
            progress_failed_ = false;
        } else {
            progress_failed_ = true; // the store could not read it
        }
    }
}

bool LibraryBinding::open_song_hash(std::uint64_t hash) {
    if (hash == 0) {
        return false;
    }
    for (std::size_t i = 0; i < catalogue_.size(); ++i) {
        if (catalogue_.song(i).hash == hash) {
            open(i);
            return true;
        }
    }
    return false;
}

bool LibraryBinding::practise_bars(std::uint64_t hash, int first, int last) {
    if (!open_song_hash(hash)) {
        return false;
    }
    bars_open_ = BarsOpen{hash, first, std::max(first, last)};
    return true;
}

} // namespace etude::app
