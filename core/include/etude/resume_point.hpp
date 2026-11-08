// SPDX-License-Identifier: GPL-3.0-or-later
// The resume point: a small file the app keeps while it runs, so that every start, after a crash or a
// normal close, at any time later, reopens exactly where the player was: the same screen, song, bar,
// section, hand and tempo, paused. It is written every few seconds and at every change of screen (the
// app does it atomically), and the crash handler writes nothing.
//
// One guard: a run that was not closed and had run for under `kResumeGuardSeconds` when it last wrote
// probably crashed on what it had just reopened, so the next start opens the song list instead (the
// settings stay) and says so. How long a run had run is measured by the run itself on a steady clock
// and written as milliseconds: a Pi has no clock that survives a power-off, and the time it sets itself
// to after the start would turn any wall-clock difference into nonsense. Pure text and rules, no file
// system.
#pragma once

#include "etude/session_state.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

inline constexpr int kResumeVersion = 2;
inline constexpr int kResumeGuardSeconds = 10;
inline constexpr std::size_t kMaxResumeBytes = 4096;

// The screens a point names: "play" (the practice screen: nothing else open), "library", "learn" and its
// cards ("learn-scales", "learn-chords", "learn-ear", "learn-melodies"), and the settings pages
// ("notes", "effects", "view", "practice", "input", "presets", "add-songs", "about"). Any other name
// is damage.
bool resume_screen_known(std::string_view name);

struct ResumePoint {
    std::int64_t ran_ms = 0;    // how long the run that wrote it had run, by its steady clock, when it wrote this
    bool clean = false;         // the run ended on its own (a normal close), not by a crash or a cut
    std::string screen = "play";
    // The song-bound part of the session (SessionState): the song, where in it, its speed, hands, loop,
    // section and plan item, count-in and transposition. The rest of a SessionState (the keyboard, the
    // auto speed-up policy, the mode) is the player's or the device's and stays in the settings.
    SessionState session;
    friend bool operator==(const ResumePoint&, const ResumePoint&) = default;
};

// The text of a point (a header, one `key=value` line each, an `end` line), at most kMaxResumeBytes.
std::string format_resume_point(const ResumePoint& point);

struct ResumeParse {
    ResumePoint point;
    bool ok = false;
    std::string error; // "line 3: speed is not a number", when not ok
};
// Strict: the header must name this version, every line a known key with a valid value, and the `end`
// line must be there, so a file cut short or written by another version is damage, not a guess.
ResumeParse parse_resume_point(std::string_view text);

enum class ResumeAction : std::uint8_t {
    Fresh,        // no usable point: start as the settings say
    Resume,       // reopen the point
    OpenSongList, // the run before crashed within the guard: the song list, no song, settings kept
    SongRefused,  // the point names a song that is not under a folder the app keeps songs in: the song list
};

// Whether a point's song may be reopened. The file is the app's own, but it is read at every start and
// should never be able to name a file anywhere else: the library-relative path must be relative with no
// `..`, and the absolute one absolute with no `..` and under one of `roots` (the song library's folders
// and the data folder), compared by path components. No song at all is allowed.
bool resume_song_allowed(const SessionState& session, const std::vector<std::string>& roots);

// What a start does. `read` is null when there is no file; a point that did not parse is as good as none.
// A point never expires: a start at any time later reopens it, unless its song is refused (`roots`, see
// `resume_song_allowed`).
ResumeAction plan_resume(const ResumeParse* read, const std::vector<std::string>& roots);

// The settings' session with the point's song-bound part laid over it, clamped. The keyboard, the auto
// speed-up policy and the mode stay the settings'.
SessionState session_with_resume(const SessionState& settings, const ResumePoint& point);
// The same with no song (the guard): everything that belongs to a song back at its default.
SessionState session_without_song(const SessionState& settings);

// When the running app writes its point, as a rule with no clock of its own (the caller passes
// milliseconds): at once at the start, at every change of screen, at least every `kResumeEvery` while
// the state changes, and for the first `kResumeBeat` of the run every `kResumeBeatEvery` whether it
// changed or not, so the point says the run lasted past the guard.
inline constexpr std::int64_t kResumeEveryMs = 3000;
inline constexpr std::int64_t kResumeBeatMs = 15000;
inline constexpr std::int64_t kResumeBeatEveryMs = 2000;
inline constexpr std::int64_t kResumeScreenGapMs = 250; // a screen that flips every frame still writes only so often

class ResumePacer {
  public:
    // True when a point is to be written now; the pacer then counts it as written.
    bool due(std::int64_t now_ms, bool changed, bool screen_changed);

  private:
    bool started_ = false;
    std::int64_t started_ms_ = 0;
    std::int64_t last_ms_ = 0;
};

} // namespace etude
