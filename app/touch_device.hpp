// SPDX-License-Identifier: GPL-3.0-or-later
// Which touch devices are the screen. SDL names the kind of each: a touch screen is DIRECT, a
// trackpad INDIRECT_ABSOLUTE or INDIRECT_RELATIVE, and a device whose driver says nothing (a Pi's
// panel may) is INVALID. A trackpad's fingers are not on the screen, and its pointer events cover
// it; every other finger is on the screen, a device of unknown kind included (core `touch_kind.hpp`
// holds the rule and the log's bookkeeping). Every place that reads finger events asks here, so the
// virtual keys, the section editor, the bar's swipes and the score cannot disagree about what a
// finger is.
#pragma once

#include "log_ring.hpp"
#include "etude/touch_kind.hpp"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_touch.h>

#include <cstdint>
#include <cstdio>

namespace etude::app {

inline TouchKind touch_kind_of(SDL_TouchDeviceType type) {
    switch (type) {
    case SDL_TOUCH_DEVICE_DIRECT:
        return TouchKind::Direct;
    case SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE:
        return TouchKind::IndirectAbsolute;
    case SDL_TOUCH_DEVICE_INDIRECT_RELATIVE:
        return TouchKind::IndirectRelative;
    case SDL_TOUCH_DEVICE_INVALID:
        break;
    }
    return TouchKind::Unknown;
}

inline bool is_screen_touch(SDL_TouchID id) {
    return etude::is_screen_touch(touch_kind_of(SDL_GetTouchDeviceType(id)));
}

inline const char* touch_kind_name(TouchKind kind) {
    switch (kind) {
    case TouchKind::Direct:
        return "direct (a touch screen)";
    case TouchKind::IndirectAbsolute:
        return "indirect, absolute (a trackpad: its fingers are not on the screen)";
    case TouchKind::IndirectRelative:
        return "indirect, relative (a trackpad: its fingers are not on the screen)";
    case TouchKind::Unknown:
        break;
    }
    return "unknown (taken as a touch screen)";
}

// How many fingers SDL itself says are down on the screen's touch devices (trackpads left out).
inline int real_fingers_down() {
    int devices = 0;
    SDL_TouchID* ids = SDL_GetTouchDevices(&devices);
    if (ids == nullptr) {
        return 0;
    }
    int total = 0;
    for (int i = 0; i < devices; ++i) {
        if (!is_screen_touch(ids[i])) {
            continue;
        }
        int fingers = 0;
        SDL_Finger** list = SDL_GetTouchFingers(ids[i], &fingers);
        if (list != nullptr) {
            total += fingers;
            SDL_free(list);
        }
    }
    SDL_free(ids);
    return total;
}

// Prints the touch devices SDL knows to stderr, one line each, so a panel that reports an odd type
// can be told from the log. Nothing is printed when there are none.
inline void log_touch_devices() {
    int count = 0;
    SDL_TouchID* ids = SDL_GetTouchDevices(&count);
    if (ids == nullptr) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetTouchDeviceName(ids[i]);
        log_printf( "etude: touch device %llu \"%s\": %s\n",
                     static_cast<unsigned long long>(ids[i]), name != nullptr ? name : "",
                     touch_kind_name(touch_kind_of(SDL_GetTouchDeviceType(ids[i]))));
    }
    SDL_free(ids);
}

// A finger came from device `id`: the first time a device is seen, its type is printed, which
// covers one plugged in after the start; past the eighth device one line says so and the rest stay
// quiet. The touch the mouse stands in for is not a device (the dev flags' scripted fingers are
// the caller's to leave out).
inline void note_touch_device(SDL_TouchID id) {
    static TouchDeviceLog log;
    if (id == SDL_MOUSE_TOUCHID) {
        return;
    }
    switch (log.observe(id)) {
    case TouchDeviceLog::Say::Device: {
        const char* name = SDL_GetTouchDeviceName(id);
        log_printf( "etude: first finger from touch device %llu \"%s\": %s\n",
                     static_cast<unsigned long long>(id), name != nullptr ? name : "",
                     touch_kind_name(touch_kind_of(SDL_GetTouchDeviceType(id))));
        break;
    }
    case TouchDeviceLog::Say::TooMany:
        log_printf( "etude: more than %zu touch devices: the rest are not logged\n",
                     TouchDeviceLog::kMaxDevices);
        break;
    case TouchDeviceLog::Say::Nothing:
        break;
    }
}

} // namespace etude::app
