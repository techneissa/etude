// SPDX-License-Identifier: GPL-3.0-or-later
// What the Input settings page needs from the microphone: a read-only view of it and a few
// actions. The page edits the chosen source, device and tuning mode in the UiModel; this port
// carries the live part (the meter, the calibration, the device list). app/mic_source.hpp binds
// it to the real microphone.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace etude::app::ui {

struct InputView {
    // The source is Microphone or Both, so capture is wanted.
    bool mic_wanted = false;
    // Capture is running (or a recording is being fed in).
    bool capturing = false;
    // One line: what is listening, or why nothing is.
    std::string status;
    // Input level for the meter: 0..1, with a short peak hold.
    float level = 0.0f;
    // "Calibrate noise" is running; `calibration` is its progress, 0..1.
    bool calibrating = false;
    float calibration = 0.0f;
    // The room's calibrated noise floor on the meter's scale, or 0 when not calibrated.
    float floor_level = 0.0f;
    // The tuning the detector is using, A4 in Hz.
    float a4_hz = 440.0f;
};

class InputPort {
  public:
    virtual ~InputPort() = default;

    virtual const InputView& view() const = 0;
    // The recording devices, by name. Looks them up again when the list may have changed.
    virtual const std::vector<std::string>& devices() = 0;
    // Listens to the room for 3 s and keeps its level as the noise floor. Does nothing unless
    // capture is running.
    virtual void calibrate_noise(std::int64_t now_ns) = 0;
    // Back to A4 = 440 Hz.
    virtual void reset_tuning() = 0;
};

// A port with no microphone behind it, for a UI that runs without one (the bench).
class NullInputPort final : public InputPort {
  public:
    const InputView& view() const override { return view_; }
    const std::vector<std::string>& devices() override { return devices_; }
    void calibrate_noise(std::int64_t) override {}
    void reset_tuning() override {}

  private:
    InputView view_;
    std::vector<std::string> devices_;
};

} // namespace etude::app::ui
