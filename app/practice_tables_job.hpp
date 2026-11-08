// SPDX-License-Identifier: GPL-3.0-or-later
// Builds the evaluators' tables for another hand choice or another key on a thread of its own, so
// the frame thread only takes them over: the targets (Flow) and chords (Wait) of a long song take
// tens of milliseconds to work out, and a tap on a hand toggle or a transposition button must not
// stall a frame for them. The request carries the notes the evaluators already hold (shared, never
// copied) and the song's fingerprint; the answer carries the hands and transposition it was built
// for, and SongController uses it when its hands are still asked for (a key a few taps behind is
// installed too, the newest request being built next). The newest request wins. std only (no SDL), so the tests use it.
#pragma once

#include "etude/evaluator.hpp"
#include "etude/hand.hpp"
#include "etude/transpose.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace etude::app {

struct TablesRequest {
    std::uint64_t serial = 0; // the caller's, handed back with the answer
    HandFilter hands = HandFilter::Both;
    Transposition transposition;
    std::uint64_t fingerprint = 0; // of the song the notes are
    FlowEvaluator::NoteTable notes;
    FlowEvaluator::MeasureTable measures;
};

struct TablesResult {
    std::uint64_t serial = 0;
    HandFilter hands = HandFilter::Both;
    Transposition transposition;
    FlowEvaluator::Prepared flow;
    WaitEvaluator::Prepared wait;
};

class PracticeTablesJob {
  public:
    PracticeTablesJob() = default;
    ~PracticeTablesJob();
    PracticeTablesJob(const PracticeTablesJob&) = delete;
    PracticeTablesJob& operator=(const PracticeTablesJob&) = delete;

    // Starts on `request` as soon as the worker is free; one waiting request is kept, a newer one
    // replaces it. A build already running finishes and its answer is kept for take(), so a caller
    // that asks again and again still gets an answer each time one build lasts; the newest request
    // is built next.
    void request(TablesRequest request);
    // The finished answer, if there is one (the newest the worker made), and clears it.
    std::optional<TablesResult> take();
    // Drops what waits and what is finished (freed on the worker, not by the caller); a running
    // build stops at its next check and its answer is not kept.
    void cancel();
    // A request is waiting or running.
    bool busy() const;
    // Blocks until nothing waits or runs (tests, and quitting).
    void wait_idle();
    // Frees tables the evaluators replaced (FlowEvaluator::take_retired) on the worker, so that the
    // frame thread does not free a heap vector for each of a long song's notes.
    void discard(FlowEvaluator::Prepared&& flow, WaitEvaluator::Prepared&& wait);
    void discard(std::vector<FlowEvaluator::Prepared>&& flow,
                 std::vector<WaitEvaluator::Prepared>&& wait);
    // Holds the worker before its next build until released (tests: the frames between a tap and
    // its answer are then not a race).
    void hold(bool on);
    // Tests: `hook` runs on the worker at every check of whether to stop, before the answer to it,
    // so a test can park a build mid-way and ask it to stop with nothing left to the clock. Set it
    // before the first request.
    void set_check_hook(std::function<void()> hook);
    // How many builds ended early because they were asked to stop (a cancel, quit), for tests.
    std::uint64_t stopped_builds() const { return stopped_builds_.load(); }

  private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::optional<TablesRequest> waiting_;
    std::optional<TablesResult> done_;
    bool held_ = false;
    std::function<void()> check_hook_;
    std::atomic<std::uint64_t> stopped_builds_{0};
    std::atomic<bool> abort_{false}; // the running build should stop: a cancel, quit
    std::vector<FlowEvaluator::Prepared> flow_garbage_;
    std::vector<WaitEvaluator::Prepared> wait_garbage_;
    bool running_ = false;
    bool stop_ = false;
    std::uint64_t epoch_ = 0; // up by one with every cancel(): an answer made before is dropped
    std::thread thread_;
};

} // namespace etude::app
