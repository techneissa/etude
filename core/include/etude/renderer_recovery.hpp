// SPDX-License-Identifier: GPL-3.0-or-later
// When the renderer is replaced after start-up, and with which path. Plain values only: the app
// feeds it what SDL reported and does the replacing.
//
// A lost or reset device is a request at once. A present that fails is a request after
// `kPresentFailures` in a row, since one refused present (a window being moved, a mode change in
// flight) is not a lost device. A request is served at a frame boundary: `begin` names the paths to
// try, in order. The first recovery tries the path in use, then `basic`. A recovery that follows
// another within `kRepeatFrames` frames means that path keeps failing, so it tries `basic` only; a
// loss that repeats more than `kMaxRepeats` times in a row gives up (a run that cannot draw ends
// rather than loops).
#pragma once

#include "etude/renderer_path.hpp"

#include <array>
#include <cstdint>

namespace etude {

enum class LossCause : std::uint8_t {
    DeviceLost,    // SDL_EVENT_RENDER_DEVICE_LOST
    DeviceReset,   // SDL_EVENT_RENDER_DEVICE_RESET
    TargetsReset,  // SDL_EVENT_RENDER_TARGETS_RESET
    PresentFailed, // the present kept failing
    Simulated,     // the developer's --device-lost
};

inline constexpr const char* loss_cause_name(LossCause cause) {
    switch (cause) {
    case LossCause::DeviceLost:
        return "device lost";
    case LossCause::DeviceReset:
        return "device reset";
    case LossCause::TargetsReset:
        return "render targets reset";
    case LossCause::PresentFailed:
        return "present failed";
    case LossCause::Simulated:
        break;
    }
    return "simulated device loss";
}

// The paths one recovery tries, first to last, and whether to stop trying at all.
struct RecoveryPlan {
    std::array<RendererPath, 2> tries{RendererPath::Basic, RendererPath::Basic};
    std::uint8_t count = 0;
    bool give_up = false;
};

class RendererRecovery {
  public:
    static constexpr int kPresentFailures = 3; // in a row
    static constexpr long kRepeatFrames = 90;  // a loss this soon after a recovery is a repeat
    static constexpr int kMaxRepeats = 2;      // repeats before the run gives up
    static constexpr long kSettleFrames = 2;   // events this soon after a recovery are its echo

    explicit RendererRecovery(RendererPath path = RendererPath::Basic) : path_(path) {}

    // The path now drawing.
    RendererPath path() const { return path_; }

    // The device was reported lost or reset, at `frame`. An echo of a recovery that just ran (SDL
    // sends a reset after a lost) asks for nothing; a simulated loss is never an echo.
    void note_loss(LossCause cause, long frame) {
        if (cause != LossCause::Simulated && recovered_ && frame - last_frame_ <= kSettleFrames) {
            return;
        }
        request(cause);
    }

    // The result of a present. A failure streak that reaches `kPresentFailures` is a request.
    // `can_fail` false (a hidden or minimised window, which may refuse a present) counts nothing.
    void note_present(bool ok, bool can_fail = true) {
        if (ok) {
            failures_ = 0;
        } else if (can_fail && ++failures_ >= kPresentFailures) {
            failures_ = 0;
            request(LossCause::PresentFailed);
        }
    }

    bool pending() const { return pending_; }
    LossCause cause() const { return cause_; }

    // Serves the request at `frame`: the paths to try. Clears the request.
    RecoveryPlan begin(long frame) {
        RecoveryPlan plan;
        pending_ = false;
        failures_ = 0;
        repeats_ = recovered_ && frame - last_frame_ <= kRepeatFrames ? repeats_ + 1 : 0;
        last_frame_ = frame;
        recovered_ = true;
        if (repeats_ > kMaxRepeats) {
            plan.give_up = true;
            return plan;
        }
        if (repeats_ == 0 && path_ != RendererPath::Basic) {
            plan.tries = {path_, RendererPath::Basic};
            plan.count = 2;
        } else {
            plan.tries = {RendererPath::Basic, RendererPath::Basic};
            plan.count = 1;
        }
        return plan;
    }

    // The path the recovery ended on (it may be `basic` after a fallback).
    void finish(RendererPath used) { path_ = used; }

  private:
    void request(LossCause cause) {
        // The first cause stands: a lost event beats the present failures it brings.
        if (!pending_) {
            cause_ = cause;
        }
        pending_ = true;
    }

    RendererPath path_;
    bool pending_ = false;
    LossCause cause_ = LossCause::DeviceLost;
    int failures_ = 0;
    int repeats_ = 0;
    bool recovered_ = false;
    long last_frame_ = 0;
};

} // namespace etude
