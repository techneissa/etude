// SPDX-License-Identifier: GPL-3.0-or-later
// Names each hand's chords on a thread of its own, so the frame thread only takes the answer: the
// harmony of a long song takes tens of milliseconds to work out (core harmony.hpp), and a new song,
// a new split of the hands or a transposition must not stall a frame for it. The request carries
// the notes the evaluators already hold (shared, never copied) and the little else the naming
// reads; the answer carries the serial of the request it was made for, and the feed uses it only
// while it is still the one asked for. The newest request wins. A transposition or a spelling that
// changes over the same notes names the chords again from the harmony the worker kept, without
// analysing the notes a second time. std only (no SDL), so the tests use it.
#pragma once

#include "etude/chord_label.hpp"
#include "etude/evaluator.hpp"
#include "etude/harmony.hpp"
#include "etude/key_signature.hpp"
#include "etude/song.hpp"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace etude::app {

struct HandChordsRequest {
    std::uint64_t serial = 0; // the caller's, handed back with the answer
    FlowEvaluator::NoteTable notes;
    FlowEvaluator::MeasureTable measures;
    TempoMap tempo;
    std::vector<KeyChange> keys;
    std::int64_t end_tick = 0;
    double duration_s = 0.0;
    int semitones = 0;         // the transposition the names follow
    bool prefer_flats = false; // spell a key with as many flats as sharps with flats
};

struct HandChordsResult {
    std::uint64_t serial = 0;
    HandChords chords;
};

class HandChordsJob {
  public:
    HandChordsJob() = default;
    ~HandChordsJob();
    HandChordsJob(const HandChordsJob&) = delete;
    HandChordsJob& operator=(const HandChordsJob&) = delete;

    // Starts on `request` as soon as the worker is free; one waiting request is kept, a newer one
    // replaces it.
    void request(HandChordsRequest request);
    // The finished answer, if there is one (the newest the worker made), and clears it.
    std::optional<HandChordsResult> take();
    // Drops what waits and what is finished; a running build's answer is not kept.
    void cancel();
    bool busy() const;
    // Blocks until nothing waits or runs (tests, and quitting).
    void wait_idle();

  private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::optional<HandChordsRequest> waiting_;
    std::optional<HandChordsResult> done_;
    bool running_ = false;
    bool stop_ = false;
    std::uint64_t epoch_ = 0; // up by one with every cancel(): an answer made before is dropped
    std::uint64_t analyses_ = 0;
    // The harmony of the last notes the worker analysed, kept for a request over the same notes.
    FlowEvaluator::NoteTable kept_notes_;
    FlowEvaluator::MeasureTable kept_measures_;
    std::vector<KeyChange> kept_keys_;
    HarmonyTimeline kept_harmony_;
    std::thread thread_;
};

// What the frame thread keeps of it: the labels of the song on screen. `update` is called once a
// frame with what the labels depend on; it asks the worker when that changed, takes the answer in
// when it is there, and drops labels made for other notes at once.
struct HandChordsKey {
    bool has_song = false;
    std::uint32_t load_serial = 0;  // another song
    std::uint32_t notes_serial = 0; // another split of the hands
    int semitones = 0;
    bool prefer_flats = false;

    friend bool operator==(const HandChordsKey&, const HandChordsKey&) = default;
};

class HandChordsFeed {
  public:
    // `make` builds the request (its serial is set here) and runs only when `key` changed.
    template <class Make> void update(const HandChordsKey& key, Make&& make) {
        take_answer();
        if (have_key_ && key == key_) {
            return;
        }
        if (!key.has_song) {
            drop(key);
            return;
        }
        HandChordsRequest request = make();
        ask(key, std::move(request));
    }

    // The labels of the song and hands on screen; null before the first answer and without a song.
    const HandChords* chords() const { return have_chords_ ? &chords_ : nullptr; }
    // Changes whenever `chords()` becomes another timeline: a view's fades start over.
    std::uint32_t serial() const { return serial_; }
    // Blocks until the worker has nothing left to do, then takes its answer (tests).
    void wait_idle() {
        job_.wait_idle();
        take_answer();
    }
    const HandChordsJob& job() const { return job_; }

  private:
    void take_answer();
    void drop(const HandChordsKey& key);
    void ask(const HandChordsKey& key, HandChordsRequest request);

    HandChordsJob job_;
    HandChordsKey key_;
    bool have_key_ = false;
    std::uint64_t asked_ = 0;
    HandChords chords_;
    bool have_chords_ = false;
    std::uint32_t serial_ = 0;
};

} // namespace etude::app
