// SPDX-License-Identifier: GPL-3.0-or-later
// The system's word that something changed in a folder: FSEvents on the Mac, inotify on Linux. It
// says only "look again" (a callback with no detail); what changed is worked out from a listing by
// the FolderWatcher, so one small interface is all a platform needs. No SDL.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>

namespace etude::app {

class FolderEvents {
  public:
    virtual ~FolderEvents() = default;
    // Starts watching `root` and everything under it. `on_change` may be called from any thread,
    // often and quickly (once per write), and must be cheap. False when the system cannot watch
    // it (no such folder, no watches left): the caller polls instead.
    virtual bool start(const std::filesystem::path& root, std::function<void()> on_change) = 0;
    // Stops; no callback runs after it returns.
    virtual void stop() = 0;
    virtual const char* name() const = 0;
};

// The platform's own, or nothing where there is none.
std::unique_ptr<FolderEvents> make_fsevents();
std::unique_ptr<FolderEvents> make_inotify();
inline std::unique_ptr<FolderEvents> make_native_events() {
#if defined(__APPLE__)
    return make_fsevents();
#elif defined(__linux__)
    return make_inotify();
#else
    return nullptr;
#endif
}

} // namespace etude::app
