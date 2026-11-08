// SPDX-License-Identifier: GPL-3.0-or-later
// What the touch UI needs from the song transport: a read-only view of it and a few actions. The
// UI holds no song state of its own; app/song_transport.hpp binds it to the SongController, so the
// bar's buttons, the section band and the resume countdown always show and change the real thing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "etude/shift_fit.hpp"

namespace etude::app::ui {

// What you hear of the song. Your own playing always sounds.
enum class SongSound : std::uint8_t { Mute, OtherHand, BothHands };

// How the song is practised: Flow plays on and grades each note; Wait holds at each chord until it
// is played.
enum class PlayMode : std::uint8_t { Flow, Wait };

// Whether the song's accompaniment parts sound: all of them, some, none.
enum class AccompState : std::uint8_t { On, Partial, Off };

// One accompaniment part, for the menu of the accompaniment button.
struct AccompPart {
    std::string name;
    bool muted = false;
};

struct SongView {
    bool has_song = false; // free play: no transport, no section band, no countdown
    std::string title;
    double position_s = 0.0;
    double duration_s = 0.0;
    bool playing = false; // the clock moves, or is about to (a count-in)
    bool loop_enabled = false;
    double loop_start_s = 0.0;
    double loop_end_s = 0.0;
    // Start of every bar, in seconds, for the faint bar lines on the section band. May be empty.
    std::span<const double> bar_starts_s;
    // How hard each bar is, 1..5 stars, one per entry of `bar_starts_s` (the loader rated them);
    // empty when the song has no ratings.
    std::span<const float> bar_difficulty;
    SongSound sound = SongSound::Mute;
    // The accompaniment: the song's parts that are not a practice hand. The button shows only when
    // the song has some; it shows a voice glyph when every one is a voice, an ensemble glyph
    // otherwise.
    bool has_accompaniment = false;
    bool accompaniment_voice = false;
    AccompState accompaniment = AccompState::On;
    std::span<const AccompPart> accompaniment_parts;
    PlayMode mode = PlayMode::Flow;
    // The transposition in force (-12..+12 semitones) and what each shift would leave off the
    // keyboard (null without a song or when the song has no notes to play).
    int transpose = 0;
    const etude::ShiftFits* shift_fits = nullptr;
    // The octave-shift offer. From the song's notes outside the keyboard: the total transposition
    // proposed (`target`) and the one in force (`from`). From the player's presses (the keyboard's
    // own octave button): the total shift of the keyboard's notes proposed and in force; accepting
    // moves the app's model of the keyboard, not the song.
    bool shift_offer = false;
    int shift_offer_target = 0;
    int shift_offer_from = 0;
    bool shift_offer_from_keys = false;
    // The hands being practised: both on is both hands; at least one is always on.
    bool left_hand = true;
    bool right_hand = true;
    // The hands that have notes in the song, or in the looped section. A hand with none is
    // unavailable: its toggle is grey and only says why, the other hand cannot be turned off,
    // "both" is not a choice and the sound button skips "the other hand".
    bool left_available = true;
    bool right_available = true;
    bool hands_in_section = false; // the availability is read from a loop, not the whole song
    // The split of the notes between the hands (hand_split.hpp), for Settings > Practice's Hands
    // row. `split_available`: some notes of the song have no hand in the file, so the player can
    // place the split; a song whose file names the hands has no row. `split_fixed`: the player
    // placed it (otherwise the hands come from the sheet's staves). `split_key` is the key of the
    // keyboard the split stands at (notes from it up are the right hand), with the transposition
    // in force; while the hands come from the sheet it is about where they divide.
    // `split_by_staves`: what decided them: the tracks' staves (true), or a split of one staff's
    // notes at `split_key` (false). `split_flats`: the key names are spelled with flats (the
    // song's key at the playhead has flats). `song_serial` changes with every song opened.
    bool split_available = false;
    bool split_fixed = false;
    int split_key = 60;
    bool split_by_staves = false;
    bool split_flats = false;
    std::uint32_t song_serial = 0;
    int skip_back_s = 5;           // AppSettings::skip_back_seconds
    int skip_forward_s = 5;
    // The resume countdown after the settings close: seconds left (fractional, 0 when none runs)
    // and how long it was.
    double countdown_left_s = 0.0;
    int countdown_total_s = 3;
    // Tempo: the speed in percent (a whole percent), the tempo at the playhead times the speed (beats
    // a minute, what the tempo chip shows) and the song's main tempo in the same beat, which a
    // typed tempo is measured against. The beat is a quarter note, or a dotted quarter
    // (`beat_dotted`) in 6/8, 9/8 and 12/8, where the metronome, the count-in and the chip count
    // it. Zero without a song.
    int speed_percent = 100;
    double tempo_bpm = 0.0;
    double main_bpm = 0.0;
    bool beat_dotted = false;
    // The count-in: bars chosen (0 is off), the beat number to show while it counts (0 when none
    // runs), the beats in all and the share counted (0..1).
    int count_in_bars = 1;
    int count_in_beat = 0;
    int count_in_beats = 0;
    int count_in_bar_beats = 0; // the beats in a bar of the count-in: one dot each
    double count_in_progress = 0.0;
    // The beat the metronome (or the count-in) last sounded: how long ago and whether it was beat
    // one; inactive when neither runs.
    bool beat_active = false;
    double beat_age_s = 0.0;
    bool beat_accent = false;
};

class SongPort {
  public:
    virtual ~SongPort() = default;

    virtual const SongView& view() const = 0;

    // Play or pause. During the resume countdown, stops it (paused).
    virtual void toggle_play(std::int64_t now_ns) = 0;
    // From the loop start when a loop is on, else from the start, and play.
    virtual void replay(std::int64_t now_ns) = 0;
    // One step of skip_back_s / skip_forward_s. A held button calls these again on each repeat.
    virtual void skip_back(std::int64_t now_ns) = 0;
    virtual void skip_forward(std::int64_t now_ns) = 0;
    // A drag or tap on the scrubber. A time outside the loop ends the loop, so the markers drawn
    // match what plays.
    virtual void seek(double song_s, std::int64_t now_ns) = 0;
    // Ends the loop, whatever made it (a section, or a loop set at the keys).
    virtual void clear_loop(std::int64_t now_ns) = 0;
    virtual void set_sound(SongSound sound, std::int64_t now_ns) = 0;
    // The accompaniment button was tapped: all parts sound, or none do (any part sounding mutes
    // them all). Each part is muted on its own too; neither touches the song sound.
    virtual void tap_accompaniment(std::int64_t now_ns) = 0;
    virtual void toggle_accompaniment_part(std::size_t index, std::int64_t now_ns) = 0;
    // Flow or Wait. A change ends the run in progress and starts again from the playhead.
    virtual void set_mode(PlayMode mode, std::int64_t now_ns) = 0;
    // The speed in percent (25..200, in steps of 5), and the speed that plays a typed tempo at the
    // song's main tempo. A change while playing is smooth.
    virtual void set_speed(int percent, std::int64_t now_ns) = 0;
    virtual void set_target_bpm(double bpm, std::int64_t now_ns) = 0;
    // Bars of count-in before the song moves: 0 (off), 1 or 2.
    virtual void set_count_in_bars(int bars, std::int64_t now_ns) = 0;
    // Practise the left hand, the right hand, or both. Neither is ignored.
    virtual void set_hands(bool left, bool right, std::int64_t now_ns) = 0;
    // Places the split on `key` (a key of the keyboard, notes from it up the right hand), or back
    // to the sheet's staves. They do nothing for a song with no split to place.
    virtual void set_split_key(int /*key*/, std::int64_t /*now_ns*/) {}
    virtual void reset_split(std::int64_t /*now_ns*/) {}
    // The player's own transposition, -12..+12 semitones (kept with the song). It ends the offer.
    virtual void set_transpose(int semitones, std::int64_t now_ns) = 0;
    // The octave-shift offer's three answers: take the proposed transposition; hide the card until
    // the song or keyboard is new; and never offer a shift for this song again.
    virtual void accept_shift_offer(std::int64_t now_ns) = 0;
    virtual void dismiss_shift_offer(std::int64_t now_ns) = 0;
    virtual void never_offer_shift(std::int64_t now_ns) = 0;
};

} // namespace etude::app::ui
