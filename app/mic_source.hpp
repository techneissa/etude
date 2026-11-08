// SPDX-License-Identifier: GPL-3.0-or-later
// The microphone as the frame loop sees it: opens and closes capture as the chosen input source
// asks, applies the tuning settings, turns pitch events into MidiEvents (tagged
// InputSource::Microphone) and "not sure" markers, and serves the Input settings page. Main thread
// only; the capture callback and the analysis thread are MicPipeline's.
#pragma once

#include "mic_capture.hpp"
#include "mic_inbox.hpp"
#include "mic_pipeline.hpp"
#include "ui/input_port.hpp"
#include "wav_feeder.hpp"

#include "etude/latency_stats.hpp"
#include "etude/mic_input.hpp"
#include "etude/midi_event.hpp"
#include "etude/session_state.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace etude::app {

struct MicSourceOptions {
    std::string wav_path; // --mic-wav: feed this recording instead of the device
    bool log = false;     // --mic-log
};

class MicSource final : public ui::InputPort {
  public:
    // How long "Calibrate noise" listens.
    static constexpr std::int64_t kCalibrationNs = 3'000'000'000;

    explicit MicSource(MicSourceOptions options = {});
    ~MicSource() override;

    // Once a frame: opens or closes capture to match the source, applies the device, tuning and
    // noise-floor settings, runs the calibration timer and the meter.
    void apply(const AppSettings& settings, std::int64_t now_ns);
    // Copies what the microphone learned (A4, the calibrated floor) into `settings`; true if
    // anything changed.
    bool write_back(AppSettings& settings);

    // The notes the detector should check for (Wait Mode's chord), empty for open detection.
    void set_expected(std::span<const int> notes);
    // A MIDI keyboard press, for Both mode.
    void note_midi_press(int note, std::int64_t time_ns);

    // Appends the events the microphone produced since the last call (NoteOn/NoteOff with
    // source Microphone, in time order) and its "not sure" markers. Safe to call at any point of
    // a frame, and the host does: it takes before every action on the song, so a note stamped
    // before the action is in the song's inbox first (see MicInbox).
    void poll(std::int64_t now_ns, std::vector<MidiEvent>& events, std::vector<UnsureMark>& marks);
    // The last note the microphone confirmed, -1 for none.
    int last_note() const { return inbox_.last_note(); }

    // --mic-log. Call right after the frame that drew the events poll() returned is presented (or,
    // with no window, after they were handled): the time from each note's onset to `now_ns` is
    // counted, and a summary is printed every 5 s. `to_screen` words it for the window.
    void log_presented(std::int64_t now_ns, bool to_screen);

    bool capturing() const { return pipeline_ != nullptr; }
    const MicSourceOptions& options() const { return options_; }
    // Prints the recording's name and ends when it has been fed in and heard (for --mic-wav).
    bool wav_finished() const { return feeder_ && feeder_->finished(); }

    // ui::InputPort
    const ui::InputView& view() const override { return view_; }
    const std::vector<std::string>& devices() override;
    void calibrate_noise(std::int64_t now_ns) override;
    void reset_tuning() override;

    // An SDL audio device was added or removed.
    void devices_changed() { devices_dirty_ = true; }

  private:
    void open(const AppSettings& settings, std::int64_t now_ns);
    void close();
    void refresh_devices();

    MicSourceOptions options_;
    std::unique_ptr<MicPipeline> pipeline_;
    MicCapture capture_;
    std::unique_ptr<WavFeeder> feeder_;
    bool audio_subsystem_ = false;
    std::string open_device_; // the setting the capture was opened for
    std::string open_error_;
    std::int64_t retry_at_ns_ = 0;

    MicInbox inbox_;
    std::vector<int> expected_;
    std::vector<std::int8_t> pushed_expected_;

    // Tuning applied to the detector, to notice changes.
    bool learn_applied_ = false;
    int a4_tenths_applied_ = kDefaultMicA4Tenths;
    int a4_tenths_learned_ = kDefaultMicA4Tenths;
    bool a4_dirty_ = false;
    bool reset_a4_ = false;

    // Calibration.
    bool calibrating_ = false;
    std::int64_t calibration_start_ns_ = 0;
    bool awaiting_floor_ = false;
    float floor_before_ = 0.0f;
    std::int64_t awaiting_since_ns_ = 0;
    float new_floor_ = -1.0f;

    ui::InputView view_;
    std::int64_t view_ns_ = 0;
    std::vector<std::string> device_names_;
    std::vector<MicDevice> devices_;
    bool devices_dirty_ = true;
    int mode_ = 0;

    // Logging.
    LatencyStats latency_{256};
    std::int64_t next_report_ns_ = 0;
};

} // namespace etude::app
