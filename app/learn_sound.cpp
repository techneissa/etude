// SPDX-License-Identifier: GPL-3.0-or-later
#include "learn_sound.hpp"

#include "etude/exercise_sound.hpp"

namespace etude::app {

double LearnSound::listen(const DrillSpec& spec, std::string& why) {
    const Drill d = make_drill(spec, range_);
    if (!d.ok) {
        why = d.message;
        return 0.0;
    }
    // What an earlier drill had not started goes; what sounds is stopped by the same message.
    pending_.clear();
    pending_.push_back({SongSoundEvent::Kind::StopAll, {}});
    const std::vector<SongSoundEvent> events =
        exercise_sound_events(d.song, now_ns_ + kSongSoundLeadNs);
    if (events.empty()) {
        pending_.clear();
        why = "There is nothing to play.";
        return 0.0;
    }
    pending_.insert(pending_.end(), events.begin(), events.end());
    spans_ = exercise_spans(d.song);
    ++started_;
    return exercise_length_s(d.song) + static_cast<double>(kSongSoundLeadNs) / 1e9;
}

bool LearnSound::practise(const DrillSpec& spec, std::string& why) {
    Drill d = make_drill(spec, range_);
    if (!d.ok) {
        why = d.message;
        return false;
    }
    drill_ = DrillRequest{spec, std::move(d)};
    return true;
}

namespace {

// How the Ear card's notes sound: a second a note (60 beats a minute, a quarter each), and a tapped
// key for half a second.
ExerciseSettings ear_play(double bpm, double note_beats) {
    ExerciseSettings play;
    play.bpm = bpm;
    play.note_beats = note_beats;
    return play;
}

} // namespace

double LearnSound::ask_ear(const EarQuestion& question, std::string& why) {
    const Song song = ear_question_song(question, ear_play(60.0, 1.0));
    const std::vector<SongSoundEvent> events =
        exercise_sound_events(song, now_ns_ + kSongSoundLeadNs);
    if (events.empty()) {
        why = "There is nothing to play.";
        return 0.0;
    }
    pending_.clear();
    pending_.push_back({SongSoundEvent::Kind::StopAll, {}});
    pending_.insert(pending_.end(), events.begin(), events.end());
    spans_.clear(); // the question's notes are not lit on the keys
    ++questions_;
    return exercise_length_s(song) + static_cast<double>(kSongSoundLeadNs) / 1e9;
}

void LearnSound::sound_key(int note) {
    EarQuestion tap;
    tap.first = tap.second = note;
    const std::vector<SongSoundEvent> events = exercise_sound_events(
        ear_question_song(tap, ear_play(120.0, 1.0)), now_ns_ + kSongSoundLeadNs);
    pending_.insert(pending_.end(), events.begin(), events.end());
}

LearnSound::MelodyCounts LearnSound::melody_counts() {
    if (!counts_) {
        const std::span<const unsigned char> text = embedded_melody_manifest();
        const MelodyManifest manifest =
            parse_melody_manifest({reinterpret_cast<const char*>(text.data()), text.size()});
        counts_ = MelodyCounts{static_cast<int>(manifest.paths.size()),
                               static_cast<int>(manifest.files.size())};
    }
    return *counts_;
}

const MelodyBook& LearnSound::melodies() {
    if (!book_) {
        book_ = load_melody_book();
        progress_taken_ = false; // its best scores are read from the runs on the next frame
    }
    return *book_;
}

bool LearnSound::play_melody(std::size_t piece, std::string& why) {
    const MelodyBook& book = melodies();
    if (piece >= book.pieces.size()) {
        why = "That piece is not here.";
        return false;
    }
    std::optional<OpenedMelody> song = open_melody(book.pieces[piece], embedded_melody_files());
    if (!song) {
        why = "That piece could not be opened.";
        return false;
    }
    melody_ = std::move(song);
    ++melodies_opened_;
    return true;
}

void LearnSound::update_melody_progress(std::span<const RunRecord> runs, std::int64_t profile) {
    if (!book_ || (progress_taken_ && runs.size() == progress_runs_ && profile == progress_profile_)) {
        return;
    }
    if (demo_scores_) {
        // The first path's first three pieces at 95%, 94% and 96% (passed), its fourth at 56% and the
        // next two with no score; the second path's first at 93% (passed) and its second at 38%.
        const int scores[] = {950, 940, 960, 560, -1, -1, 930, 380};
        std::size_t at = 0;
        for (const MelodyPath& path : book_->paths) {
            for (const std::size_t piece : path.pieces) {
                book_->pieces[piece].best_permille = at < std::size(scores) ? scores[at] : -1;
                ++at;
            }
        }
        progress_runs_ = runs.size();
        progress_profile_ = profile;
        progress_taken_ = true;
        return;
    }
    book_->set_best(summarise_plays(runs));
    progress_runs_ = runs.size();
    progress_profile_ = profile;
    progress_taken_ = true;
}

void LearnSound::stop_sound() {
    pending_.clear();
    pending_.push_back({SongSoundEvent::Kind::StopAll, {}});
}

void LearnSound::take(std::vector<SongSoundEvent>& out) {
    sent_ += static_cast<long>(pending_.size());
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

} // namespace etude::app
