// SPDX-License-Identifier: GPL-3.0-or-later
#include "folder_events.hpp"

#if defined(__APPLE__)

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include <system_error>
#include <utility>

namespace etude::app {

namespace {

// Events are coalesced by the system for this long, so a copy of fifty files is a few callbacks.
constexpr CFTimeInterval kLatencyS = 0.25;

class FsEvents final : public FolderEvents {
  public:
    ~FsEvents() override { stop(); }

    bool start(const std::filesystem::path& root, std::function<void()> on_change) override {
        stop();
        std::error_code ec;
        // FSEvents reports real paths; a symlinked folder (/var is /private/var) is resolved here.
        const std::filesystem::path real = std::filesystem::canonical(root, ec);
        if (ec) {
            return false;
        }
        on_change_ = std::move(on_change);
        CFStringRef path = CFStringCreateWithCString(nullptr, real.c_str(), kCFStringEncodingUTF8);
        if (path == nullptr) {
            return false;
        }
        const void* values[] = {path};
        CFArrayRef paths = CFArrayCreate(nullptr, values, 1, &kCFTypeArrayCallBacks);
        CFRelease(path);
        if (paths == nullptr) {
            return false;
        }
        FSEventStreamContext context{0, this, nullptr, nullptr, nullptr};
        stream_ = FSEventStreamCreate(nullptr, &FsEvents::called, &context, paths,
                                      kFSEventStreamEventIdSinceNow, kLatencyS,
                                      kFSEventStreamCreateFlagWatchRoot);
        CFRelease(paths);
        if (stream_ == nullptr) {
            return false;
        }
        queue_ = dispatch_queue_create("etude.folder-events", DISPATCH_QUEUE_SERIAL);
        FSEventStreamSetDispatchQueue(stream_, queue_);
        if (!FSEventStreamStart(stream_)) {
            stop();
            return false;
        }
        return true;
    }

    void stop() override {
        if (stream_ != nullptr) {
            FSEventStreamStop(stream_);
            FSEventStreamInvalidate(stream_);
            FSEventStreamRelease(stream_);
            stream_ = nullptr;
        }
        if (queue_ != nullptr) {
            // A callback already queued has run when this returns.
            dispatch_sync_f(queue_, nullptr, [](void*) {});
            dispatch_release(queue_);
            queue_ = nullptr;
        }
        on_change_ = nullptr;
    }

    const char* name() const override { return "fsevents"; }

  private:
    static void called(ConstFSEventStreamRef, void* info, size_t, void*,
                       const FSEventStreamEventFlags*, const FSEventStreamEventId*) {
        auto* self = static_cast<FsEvents*>(info);
        if (self->on_change_) {
            self->on_change_();
        }
    }

    FSEventStreamRef stream_ = nullptr;
    dispatch_queue_t queue_ = nullptr;
    std::function<void()> on_change_;
};

} // namespace

std::unique_ptr<FolderEvents> make_fsevents() { return std::make_unique<FsEvents>(); }

} // namespace etude::app

#else

namespace etude::app {
std::unique_ptr<FolderEvents> make_fsevents() { return nullptr; }
} // namespace etude::app

#endif
