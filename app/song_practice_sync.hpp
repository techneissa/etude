// SPDX-License-Identifier: GPL-3.0-or-later
// Keeps a song's practice choices (speed, count-in, hands, transposition: song_practice.hpp) in
// the history store, by the song's content hash: opening a song brings back what the player last
// used on it, a change is saved a moment after it stops, and a song never opened has the defaults.
// Frame thread only; the store does the reading and writing on its own thread, so nothing here
// waits for a file.
//
// A song opened before the history has loaded starts on the defaults, and its stored choices are
// applied the frame the history is in, except for each choice the player changed in between: the
// player's change wins. The last-song session restore (settings.conf) is a player's choice too: the
// song it brings back keeps the session's values and the history only learns them.
#pragma once

#include "history_store.hpp"
#include "sections_controller.hpp"
#include "etude/session_restore.hpp"
#include "etude/song_practice.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

namespace etude::app {

class SongController;

class SongPracticeSync {
  public:
    // A change is written once it has stood this long, so a dragged slider writes once.
    static constexpr std::int64_t kSaveDelayNs = 400'000'000;
    // How long quitting waits for a history that has not loaded to take in changes made before it
    // did (the one place the frame thread waits, and only when there are such changes).
    static constexpr std::chrono::milliseconds kQuitWait{2000};

    SongPracticeSync(HistoryStore& history, SongController& songs);

    // The song's section and practice list item are the sections controller's: with it set they are
    // kept and brought back with the loop.
    void set_sections(SectionsController* sections) { sections_ = sections; }
    // The global default for accompaniment (AppSettings): a song the player has not chosen for
    // starts with all its parts sounding, or all muted. Call when it changes.
    void set_accompaniment_default(bool on) { accompaniment_on_ = on; }

    // The defaults of a song never opened: what the controller's options say for the count-in.
    SongPractice defaults() const;

    // Call before a song is replaced (SongController::load) while the old one is
    // still loaded: writes a change that has not been saved yet.
    void song_closing();
    // Call right after a song was loaded. Brings back its choices, or applies the defaults;
    // `from_session` is the saved session's song, whose own values were applied (or are about to
    // be) and stay.
    void song_opened(bool from_session, std::int64_t now_ns);
    // Call right after a Learn drill was loaded instead of song_opened: a drill is played as the
    // card asks (its tempo, 100% speed, no loop, no transposition) whatever practice choices were
    // saved under its hash, and what a player does to it is not saved.
    void drill_opened(std::int64_t now_ns);
    // The song about to be opened (library hash `hash`) opens with its loop on [start_s, end_s],
    // the section `section_id` selected, over what it was left on: the library's section chips.
    // Used by the next song_opened of that song, then forgotten.
    void open_with_loop(std::uint64_t hash, double start_s, double end_s, int section_id);
    // Forgets a loop that no open used (the song failed to load).
    void clear_opening_loop() { opening_loop_.reset(); }
    // Brings the saved session's song in, in the order that keeps each part of it: the session's
    // hands and key first (the song's tables are made for them), then `install(from_session)`,
    // which must close the old song and load the new one through song_closing() and
    // song_opened(from_session, ...) (`from_session` is false for a file that changed since: it
    // takes its own choices like any song opened), then the rest of the session, except for that
    // changed file, where it would write the defaults over the choices just applied.
    void restore_session(const RestorePlan& plan, const SessionState& session,
                         const std::function<void(bool from_session)>& install,
                         std::int64_t now_ns);
    // The song just opened is the saved session's and the session's values were applied to it
    // after the open (a start that names the file): they stay, and the history learns them.
    void session_applied();
    // Call once the options of this run (--speed and the like) are set: they are for this run and
    // are not saved, only a later change is.
    void adopt();
    // Once a frame, after HistoryStore::poll(): the late arrival of the stored choices and the
    // debounced save. `now_ns` is the clock the controller is driven with.
    void update(std::int64_t now_ns);
    // Writes any change at once (quitting).
    void flush();

    // Rows written to the history so far (each queues one write), for tests.
    int saves() const { return saves_; }

  private:
    SongPractice held(std::uint64_t hash) const; // the history's row, or the defaults
    // `p` for the loaded song: with its muted parts worked out where the player never chose.
    SongPractice resolved(SongPractice p) const;
    // The choices of the loaded song now, section and practice list item included.
    const SongPractice& current();
    // Applies `p` to the loaded song, the section and practice list item after the loop.
    void apply(const SongPractice& p, std::int64_t now_ns);
    // `p` as the history keeps it: the parts are only kept once the player chose them.
    SongPractice stored_form(const SongPractice& p) const;
    void close_with(const SongPractice& now);
    void resolve_late(std::int64_t now_ns);
    void save(std::uint64_t hash, const SongPractice& practice);

    HistoryStore& history_;
    SongController& songs_;
    SectionsController* sections_ = nullptr;
    SessionState scratch_; // carries the section and practice list item to and from the sections
    bool accompaniment_on_ = true;      // the global default now
    bool open_default_on_ = true;       // the default in force when the song was opened
    SongPractice cur_;                  // current()'s answer, kept so a frame allocates nothing
    std::uint64_t hash_ = 0; // the song being watched; 0 none (a song without a hash is not kept)
    bool late_ = false;      // the history had not loaded when the song was opened
    bool from_session_ = false;
    bool adopted_ = false;
    SongPractice at_open_;  // the choices when the song was opened (to tell what the player changed)
    SongPractice known_;    // what the history holds for the song, or will once the queue is written
    SongPractice seen_;     // the choices last looked at
    std::int64_t changed_ns_ = 0; // when `seen_` last changed
    // What the player changed on songs left before the history had loaded, to lay over each song's
    // stored row when it arrives.
    std::unordered_map<std::uint64_t, PracticeChanges> late_changes_;
    int saves_ = 0;
    struct OpeningLoop {
        std::uint64_t hash = 0;
        double start_s = 0.0;
        double end_s = 0.0;
        int section_id = 0;
    };
    std::optional<OpeningLoop> opening_loop_;
    // The song opened looping a section chip's section: that loop is for this open only, so the
    // history keeps the loop it held, until the player changes the loop themselves.
    bool one_off_loop_ = false;
    // `now` as the history keeps it: with a one-off loop still as opened, the loop of the row.
    const SongPractice& kept(const SongPractice& now);
    SongPractice kept_; // kept()'s answer, so a frame allocates nothing
};

} // namespace etude::app
