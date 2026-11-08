// SPDX-License-Identifier: GPL-3.0-or-later
// Every thread the app starts goes through `spawn_thread`, which gives it an alternate stack for the
// crash handlers before it runs its function. No SDL.
#pragma once

#include <thread>
#include <utility>

namespace etude::app {

// Defined in crash_handler.cpp.
void crash_thread_init();

template <class Fn> std::thread spawn_thread(Fn&& fn) {
    return std::thread([fn = std::forward<Fn>(fn)]() mutable {
        crash_thread_init();
        fn();
    });
}

} // namespace etude::app
