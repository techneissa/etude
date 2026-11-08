// SPDX-License-Identifier: GPL-3.0-or-later
// The sound of the Learn screen's drills (ui/learn_port.hpp): a drill is made as an exercise (core
// drill.hpp), turned into note messages with their due times (core exercise_sound.hpp) and handed to
// the same sound path as the song's, so it plays on the piano the player hears and a slow frame cannot
// make a note late. The frame loop calls `set_frame` once a frame, takes the messages the Learn screen
// asked for with `take` and sends them on with the song's (`WindowState::send_song`). Built on the
// frame thread when Listen is tapped, never per frame: an idle frame costs nothing. Practice only
// leaves a request here (`take_drill`), which the window answers by opening the drill in the player.
#pragma once

#include "melody_book.hpp"
#include "ui/learn_port.hpp"

#include "etude/drill.hpp"
#include "etude/keyboard.hpp"
#include "etude/song_schedule.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace etude::app {

// What Practice asks of the window: the drill's settings and the drill made from them, once.
struct DrillRequest {
    DrillSpec spec;
    Drill drill;
};

class LearnSound final : public ui::LearnPort {
  public:
    // The clock (steady nanoseconds, the song's) and the keyboard the player has, for the frame about
    // to draw: a scale is placed on those keys, and its first note is due a look-ahead after `now_ns`.
    void set_frame(std::int64_t now_ns, KeyRange range) {
        now_ns_ = now_ns;
        range_ = range;
    }

    double listen(const DrillSpec& spec, std::string& why) override;
    bool practise(const DrillSpec& spec, std::string& why) override;
    double ask_ear(const EarQuestion& question, std::string& why) override;
    void sound_key(int note) override;
    std::span<const ExerciseSpan> listening() const override { return spans_; }
    void stop_sound() override;
    MelodyCounts melody_counts() override;
    const MelodyBook& melodies() override;
    bool play_melody(std::size_t piece, std::string& why) override;

    // Once a frame: takes the best scores of the melodies from the active profile's runs, when the
    // runs or the profile changed since (the runs are the history's, oldest first). Nothing until the
    // melodies have been asked for.
    void update_melody_progress(std::span<const RunRecord> runs, std::int64_t profile);
    // Developer (--melodies-demo): the best scores are invented, not the history's.
    void use_demo_scores() { demo_scores_ = true; }
    // The melody the player tapped since the last call (once): its song, ready to play.
    std::optional<OpenedMelody> take_melody() { return std::exchange(melody_, std::nullopt); }

    // Moves the messages waiting to go out to the end of `out`.
    void take(std::vector<SongSoundEvent>& out);
    // The drill Practice asked for since the last call (once), else none. It is made once, when it is
    // asked for, and the window opens that one.
    std::optional<DrillRequest> take_drill() { return std::exchange(drill_, std::nullopt); }
    KeyRange range() const { return range_; }

    // For the tests: scales started, and the note messages handed over for them so far.
    int scales_started() const { return started_; }
    long messages_sent() const { return sent_; }
    int questions_sounded() const { return questions_; }
    int melodies_opened() const { return melodies_opened_; }

  private:
    std::int64_t now_ns_ = 0;
    KeyRange range_ = kRange61;
    std::vector<SongSoundEvent> pending_;
    std::vector<ExerciseSpan> spans_; // the last scale's notes, for the screen's lit keys
    std::optional<DrillRequest> drill_;
    std::optional<MelodyBook> book_;
    std::optional<MelodyCounts> counts_;
    std::optional<OpenedMelody> melody_;
    // What the melodies' best scores were last taken from: the runs' count and the profile.
    std::size_t progress_runs_ = 0;
    std::int64_t progress_profile_ = -1;
    bool progress_taken_ = false;
    bool demo_scores_ = false;
    int started_ = 0;
    int questions_ = 0;
    int melodies_opened_ = 0;
    long sent_ = 0;
};

} // namespace etude::app
