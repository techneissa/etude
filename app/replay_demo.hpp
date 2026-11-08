// SPDX-License-Identifier: GPL-3.0-or-later
// An invented run over a song, for the developer's shots, and the icon audit: its
// record and its replay, built the way the history thread builds a stored run's (core
// `build_run_replay`), so every screen of #78 can be seen without playing a run first.
#pragma once

#include "etude/run_record.hpp"
#include "etude/run_summary.hpp"
#include "etude/song.hpp"
#include "etude/transpose.hpp"

#include <memory>

namespace etude::app {

struct DemoRun {
    RunRecord record;
    std::shared_ptr<const RunReplay> replay;
};

// Both hands, Flow Mode at full speed, over at most `max_notes` of the song's played notes: most
// Perfect, some Good, Early and Late, a few missed and a few wrong presses, deterministic. Notes
// of the song `song`, played through `transposition`.
DemoRun make_demo_run(const Song& song, const Transposition& transposition,
                      std::uint64_t song_hash, std::size_t max_notes = 600);

} // namespace etude::app
