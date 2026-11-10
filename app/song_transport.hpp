// SPDX-License-Identifier: GPL-3.0-or-later
// Binds the UI's SongPort (app/ui/song_port.hpp) to the SongController: the bar's buttons, the
// section band and the resume countdown act on it and show its state. The skip steps and the
// countdown length come from the controller's options (AppSettings).
#pragma once

#include "loaded_song.hpp"
#include "ui/song_port.hpp"

#include <cstdint>
#include <vector>

namespace etude::app {

class SongController;

class SongTransport final : public ui::SongPort {
  public:
    // `songs` must outlive this object.
    explicit SongTransport(SongController& songs);

    // Refreshes view() from the controller. Once per frame, after SongController::update.
    void update(std::int64_t now_ns);

    const ui::SongView& view() const override { return view_; }
    void toggle_play(std::int64_t now_ns) override;
    void replay(std::int64_t now_ns) override;
    void skip_back(std::int64_t now_ns) override;
    void skip_forward(std::int64_t now_ns) override;
    void seek(double song_s, std::int64_t now_ns) override;
    void clear_loop(std::int64_t now_ns) override;
    void set_sound(ui::SongSound sound, std::int64_t now_ns) override;
    void set_mode(ui::PlayMode mode, std::int64_t now_ns) override;
    void set_speed(int percent, std::int64_t now_ns) override;
    void set_target_bpm(double bpm, std::int64_t now_ns) override;
    void set_count_in_bars(int bars, std::int64_t now_ns) override;
    void set_hands(bool left, bool right, std::int64_t now_ns) override;
    void set_split_key(int key, std::int64_t now_ns) override;
    void reset_split(std::int64_t now_ns) override;
    void tap_accompaniment(std::int64_t now_ns) override;
    void toggle_accompaniment_part(std::size_t index, std::int64_t now_ns) override;
    void set_transpose(int semitones, std::int64_t now_ns) override;
    void accept_shift_offer(std::int64_t now_ns) override;
    void dismiss_shift_offer(std::int64_t now_ns) override;
    void never_offer_shift(std::int64_t now_ns) override;

  private:
    SongController& songs_;
    std::vector<double> bar_starts_;
    const LoadedSong* bars_for_ = nullptr; // the song bar_starts_ was built from
    std::uint64_t bars_hash_ = 0;
    std::vector<ui::AccompPart> accompaniment_;
    std::vector<std::uint32_t> accompaniment_muted_; // what accompaniment_ was last built for
    const LoadedSong* accompaniment_for_ = nullptr;
    std::uint64_t accompaniment_hash_ = 0;
    // The controller counts whole seconds; the ring empties smoothly from when it started.
    std::int64_t countdown_start_ns_ = 0;
    int countdown_total_s_ = 0;
    ui::SongView view_;
};

} // namespace etude::app
