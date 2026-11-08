// SPDX-License-Identifier: GPL-3.0-or-later
// The microphone as an SDL3 recording stream feeding a MicPipeline.
#pragma once

#include "mic_pipeline.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace etude::app {

struct MicDevice {
    SDL_AudioDeviceID id = 0;
    std::string name;
};

// The recording devices SDL lists. Needs SDL's audio subsystem initialised. Listing does not open
// a device, so it never triggers a permission prompt.
std::vector<MicDevice> list_mic_devices();

// The device's own format as float: the stream is opened with exactly the channel count and rate
// the device reports, so SDL has no channel or rate conversion to do in the callback. Mixing down
// to mono and resampling happen on the analysis thread (MicPipeline). The callback only copies the
// stream's frames into the pipeline (MicPipeline::push), which is wait-free. SDL itself holds the
// stream's lock around the callback, by design, and the device thread that fills the stream runs
// before it; what the callback does not do is anything that waits on the application.
//
// A device whose native sample format is not float (a 16-bit ALSA card) still has SDL convert
// that, in the callback; the first such conversion may allocate once inside SDL.
class MicCapture {
  public:
    MicCapture() = default;
    ~MicCapture() { close(); }
    MicCapture(const MicCapture&) = delete;
    MicCapture& operator=(const MicCapture&) = delete;

    // The channel count and rate `device_id` (0: the default recording device) runs at. Needs SDL's
    // audio subsystem initialised. Opens nothing, so it never triggers a permission prompt.
    static bool device_format(SDL_AudioDeviceID device_id, int* rate, int* channels,
                              std::string* error);

    // Opens `device_id` (0: the default recording device) in the format device_format() gave and
    // starts it; `pipeline` must have been prepared with that rate and channel count. The first
    // open on macOS makes the system ask for permission.
    bool open(MicPipeline& pipeline, SDL_AudioDeviceID device_id, std::string* error);
    void close();
    bool is_open() const { return stream_ != nullptr; }

    // What the device gave, valid after open().
    int device_channels() const { return device_channels_; }
    int device_rate() const { return device_rate_; }
    int device_frames() const { return device_frames_; }
    const std::string& device_name() const { return device_name_; }

  private:
    static void SDLCALL callback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                                 int total_amount);

    SDL_AudioStream* stream_ = nullptr;
    MicPipeline* pipeline_ = nullptr;
    int device_rate_ = 0;
    int device_channels_ = 0;
    int device_frames_ = 0;
    std::string device_name_;
    // Callback only.
    std::array<float, 4096> buffer_{};
};

} // namespace etude::app
