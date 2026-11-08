// SPDX-License-Identifier: GPL-3.0-or-later
// What the UI shows of a run's review and what it asks back: the summary page ("How it went") that
// the results card opens, and the replay view that takes the bar's place. Plain values the app
// fills every frame (the UI draws and holds no replay state beyond which page is open) and the
// choices the UI hands back, which the app acts on.
#pragma once

#include "etude/leaderboard.hpp"
#include "etude/profile.hpp"
#include "etude/run_replay.hpp"
#include "etude/run_summary.hpp"
#include "etude/song.hpp"
#include "etude/song_board.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace etude::app {

// The run's pin: its notes kept whatever else is pruned. Not `available` while the run has no notes
// to keep (they were not kept, or the history thread has not found the run); `pending` while a pin
// or an unpin waits for the history thread's answer; `pinned` is what that thread last confirmed.
struct PinInfo {
    bool available = false;
    bool pinned = false;
    bool pending = false;
};

// The summary page's Board tab: the household's best runs of the run's song for each period, and
// who the rows are. The app builds the boards on the frame thread (`SongBoardCache`); a board that
// is null has not been built, and the tab says it is working it out.
struct BoardInfo {
    std::array<const SongBoard*, 3> periods{}; // by `Period`: Week, Month, AllTime
    const ProfileSet* profiles = nullptr;      // names and colours of the rows
    std::int64_t active = kDefaultProfileId;   // the profile in use: its row is marked "(you)"
    std::int32_t today = 0;                    // the local day key, for "Today" and "Oct 17"
    RunsChartView chart_view = RunsChartView::Bars; // how the player's runs are charted (kept per profile)

    const SongBoard* board(Period period) const {
        return periods[static_cast<std::size_t>(period)];
    }
};

// The run the results card is about, for its summary page. `summary` is null until the history
// thread has built it.
struct ReviewInfo {
    // The run has a review: the card offers "How it went". The summary comes some frames later.
    bool offered = false;
    const RunSummary* summary = nullptr;
    // The played notes are kept: a replay can run. A record-only summary has none to watch.
    bool watchable = false;
    PinInfo pin;
    BoardInfo board;
};

// The replay view, while it is up.
struct ReplayInfo {
    const ReplayTimeline* timeline = nullptr;
    const RunSummary* summary = nullptr;
    std::span<const Measure> measures; // the song's bars, for the run bar by bar
    double song_duration_s = 0.0;      // where the last bar ends
    std::int32_t position_ms = 0;
    std::int32_t duration_ms = 0;
    bool playing = false;
    bool sound_on = true; // the player's own sound
    int speed_percent = 100;
    // Which tools apply now: a mistake lies before / after the position, the run has a summary
    // page to go to, and bars to practise.
    bool prev_mistake = false;
    bool next_mistake = false;
    bool summary_offered = false;
    bool practise_offered = false;
    PinInfo pin;
};

enum class ReplayAction : std::uint8_t {
    None,
    Close,      // back to where the replay was opened from
    PlayPause,
    Restart,
    SeekBack,   // five seconds
    SeekForward,
    Seek,       // to `fraction` of the replay (a tap or a drag on the run bar)
    Sound,      // the player's own sound on or off
    Speed,      // the next speed
    PrevMistake,
    NextMistake,
    Summary,    // the summary page
    Practise,   // the worst bars
    Pin,        // keep this run's replay, or let go of it
};

struct ReplayChoice {
    ReplayAction action = ReplayAction::None;
    double fraction = 0.0;
};

enum class ReviewAction : std::uint8_t {
    None,
    Watch,        // from the start (measure 0) or from a bar
    Practise,     // one bar (measure > 0) or every trouble bar (measure 0)
    Done,         // the card is dismissed
    Pin,          // keep this run's replay, or let go of it
    ChartView,    // the Board tab's chart goes to its next view
};

struct ReviewChoice {
    ReviewAction action = ReviewAction::None;
    int measure = 0; // the song's bar number
};

// Seconds the skip buttons of a replay move by (the bar's own default).
inline constexpr double kReplaySkipSeconds = 5.0;

} // namespace etude::app
