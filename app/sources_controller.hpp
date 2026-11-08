// SPDX-License-Identifier: GPL-3.0-or-later
// The Add songs page's network folder and USB stick, behind ui::SourcesPort. Three threads of its
// own do everything that can block, so the frame thread only reads two finished views (a copy
// under a mutex, taken when they changed) and sets flags:
//  - ShareProbe reads smb.conf once when the page opens;
//  - the watcher lists the mounted volumes every `poll_ms` (a stick plugged in or pulled out shows
//    within that), and scans the songs on the one that has some (usb_scan.hpp, bounded);
//  - the importer copies songs into the Inbox (usb_import.hpp), one job at a time, with progress,
//    and stops at once when the stick goes away.
// A change wakes the frame loop. A stick that stops answering can hold the watcher or the importer
// in a system call that never returns, so quitting waits `kQuitWaitMs` for them and then leaves
// them behind (detached, holding the state they share), as the upload server's quit does. No SDL.
#pragma once

#include "share_probe.hpp"
#include "ui/sources_port.hpp"
#include "usb_import.hpp"
#include "usb_scan.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace etude::app {

// How long quitting waits for the two threads before leaving them behind.
inline constexpr int kQuitWaitMs = 1000;

struct SourcesOptions {
    VolumeRoots roots;
    std::filesystem::path songs_folder;
    ShareProbeOptions share;
    std::int64_t poll_ms = 1500;
    std::size_t import_chunk = kImportChunk;
    // Called on the watcher thread before each scan, and on the importer thread after each chunk
    // (`copying`: in the pass that writes the Inbox): a test blocks or acts there.
    std::function<void()> scan_hook;
    std::function<void(bool copying)> chunk_hook;
    int quit_wait_ms = kQuitWaitMs;
};

class SourcesController final : public ui::SourcesPort {
  public:
    explicit SourcesController(SourcesOptions options);
    ~SourcesController() override;
    SourcesController(const SourcesController&) = delete;
    SourcesController& operator=(const SourcesController&) = delete;

    const ui::NetworkView& network() override;
    const ui::UsbView& usb() override;
    void recheck() override;
    void import_all() override;
    void import_one(std::size_t index) override;
    void cancel_import() override;
    ui::SourcesSheet open() const override { return open_.load(); }
    void set_open(ui::SourcesSheet sheet) override { open_ = sheet; }

    // What the player should be told once (a stick pulled out mid-copy, songs that were all there
    // already), or none. The frame thread asks once a frame; it never waits.
    std::optional<std::string> take_notice();

  private:
    struct Shared; // what the threads use, kept alive by them
    std::shared_ptr<Shared> shared_;
    ShareProbe share_;
    ui::NetworkView network_shown_;
    ui::UsbView usb_shown_;
    std::uint64_t shown_version_ = 0;
    std::atomic<ui::SourcesSheet> open_{ui::SourcesSheet::None};
};

} // namespace etude::app
