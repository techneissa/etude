// SPDX-License-Identifier: GPL-3.0-or-later
#include "mic_capture.hpp"

#include <SDL3/SDL.h>

#include <chrono>

namespace etude::app {
namespace {

std::int64_t steady_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

std::vector<MicDevice> list_mic_devices() {
    std::vector<MicDevice> out;
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count);
    if (ids == nullptr) {
        return out;
    }
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        out.push_back({ids[i], name != nullptr ? name : "Microphone"});
    }
    SDL_free(ids);
    return out;
}

// Runs on SDL's capture thread, with the stream's lock held by SDL. Copies, stamps and returns: no
// allocation of its own, no locks of its own, no logging.
void SDLCALL MicCapture::callback(void* userdata, SDL_AudioStream* stream,
                                  int /*additional_amount*/, int /*total_amount*/) {
    auto* self = static_cast<MicCapture*>(userdata);
    const int frame_bytes = static_cast<int>(sizeof(float)) * self->device_channels_;
    // Whole frames only; the buffer holds at least one for up to eight channels.
    const int capacity =
        static_cast<int>(self->buffer_.size() * sizeof(float)) / frame_bytes * frame_bytes;
    for (;;) {
        const int got = SDL_GetAudioStreamData(stream, self->buffer_.data(), capacity);
        if (got <= 0) {
            break;
        }
        self->pipeline_->push(self->buffer_.data(), got / frame_bytes, steady_ns());
    }
}

bool MicCapture::device_format(SDL_AudioDeviceID device_id, int* rate, int* channels,
                               std::string* error) {
    SDL_AudioSpec spec{};
    int frames = 0;
    if (!SDL_GetAudioDeviceFormat(device_id != 0 ? device_id : SDL_AUDIO_DEVICE_DEFAULT_RECORDING,
                                  &spec, &frames) ||
        spec.freq <= 0 || spec.channels <= 0) {
        if (error != nullptr) {
            *error = SDL_GetError();
        }
        return false;
    }
    *rate = spec.freq;
    *channels = spec.channels;
    return true;
}

bool MicCapture::open(MicPipeline& pipeline, SDL_AudioDeviceID device_id, std::string* error) {
    close();
    pipeline_ = &pipeline;
    device_channels_ = pipeline.input_channels();
    // A small device buffer keeps the capture delay short; the backend may give another size.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    const SDL_AudioSpec spec{SDL_AUDIO_F32, pipeline.input_channels(), pipeline.sample_rate()};
    stream_ = SDL_OpenAudioDeviceStream(
        device_id != 0 ? device_id : SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, &callback, this);
    if (stream_ == nullptr) {
        if (error != nullptr) {
            *error = SDL_GetError();
        }
        return false;
    }
    const SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(stream_);
    SDL_AudioSpec device_spec{};
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(dev, &device_spec, &frames)) {
        device_rate_ = device_spec.freq;
        device_frames_ = frames;
    }
    const char* name = SDL_GetAudioDeviceName(dev);
    device_name_ = name != nullptr ? name : "Microphone";
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        if (error != nullptr) {
            *error = SDL_GetError();
        }
        close();
        return false;
    }
    return true;
}

void MicCapture::close() {
    if (stream_ != nullptr) {
        // Waits for a running callback to finish.
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    pipeline_ = nullptr;
}

} // namespace etude::app
