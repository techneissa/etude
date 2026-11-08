// SPDX-License-Identifier: GPL-3.0-or-later
// A Learn drill in the player (#373): Practice makes the scale or arpeggio the card shows as a song
// (core drill.hpp), lends it to the song controller the way a file is, and starts it: notes fall in
// their hands' colours with their finger numbers, graded like any song, and the run ends in the
// drill's "How it went" card. The history knows it by the drill's title and hash, not as a library
// song: it has no path, so the saved session never reopens it.
#include "window_state.hpp"

#include <string>
#include <utility>

namespace etude::app {

bool WindowBase::start_drill(const etude::DrillSpec& spec) {
    return start_drill(spec, etude::make_drill(spec, session.layout.range()));
}

bool WindowBase::start_drill(const etude::DrillSpec& spec, etude::Drill made) {
    if (!made.ok) {
        show_message(made.message.empty() ? "That cannot be played on this keyboard."
                                          : made.message,
                     true);
        return false;
    }
    etude::app::LoadedSong loaded;
    loaded.song = std::move(made.song);
    loaded.title = made.title;
    loaded.display_path = made.title;
    loaded.hash = made.hash;
    const std::int64_t now = midi_clock_ns();
    practice_sync.song_closing(); // the song that goes keeps its last change
    songs.load(std::move(loaded), now);
    practice_sync.drill_opened(now); // the card's tempo at 100% speed, no loop, whatever was saved
    song_index.reset();
    open_song_ref.reset();
    open_song_box.set(std::string{});
    ++songs_opened;
    results_card.reset();
    replay.close_view();
    replay.forget_review();
    drill = etude::DrillSpec(spec);
    drill->bpm = etude::drill_tempo(spec.bpm);
    // Both hands, so every note of the drill is graded, then the count-in and off.
    songs.set_practice_hands(etude::HandFilter::Both, now);
    songs.play(now);
    return true;
}

} // namespace etude::app
