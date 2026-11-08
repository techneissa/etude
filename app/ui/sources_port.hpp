// SPDX-License-Identifier: GPL-3.0-or-later
// What the "Add songs" settings page needs from the two other ways songs come in: the network
// folder (Samba on the Pi) and a USB stick. A read-only view of each and a few actions; the page
// draws from the views on the frame thread and never touches a file. app/sources_controller.hpp
// binds the port to the real readers, which run on threads of their own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace etude::app::ui {

enum class ShareState : std::uint8_t {
    Checking, // not read yet
    NotSetUp, // no share, or Samba is not running
    Shared,   // Samba runs and shares the folder
};

struct NetworkView {
    ShareState state = ShareState::Checking;
};

// What happened to a song on the stick in the import that ran last.
enum class SongMark : std::uint8_t { None, Copied, Duplicate, Failed };

struct UsbSong {
    std::string name; // the path on the stick, '/' separated
    std::uint64_t size = 0;
    SongMark mark = SongMark::None;
};

// How the last import ended.
enum class ImportEnd : std::uint8_t { None, Done, Removed, Failed };

struct UsbView {
    bool present = false;  // a stick is plugged in
    bool scanning = false; // and its songs are being looked for
    std::string label;
    int songs = 0;             // found on the stick
    bool truncated = false;    // there are more than the list holds
    std::vector<UsbSong> list; // in name order
    bool importing = false;    // a copy is running
    int import_total = 0;      // songs in it
    int import_done = 0;       // handled so far
    std::string current;       // the one being copied
    ImportEnd ended = ImportEnd::None;
    int copied = 0;
    int duplicates = 0;
    int failed = 0;
};

// Which of the page's own views is open instead of the Phone row and the tiles.
enum class SourcesSheet : std::uint8_t { None, Network, Usb };

class SourcesPort {
  public:
    virtual ~SourcesPort() = default;

    // The latest views. A reference stays valid until the next call of the same function.
    virtual const NetworkView& network() = 0;
    virtual const UsbView& usb() = 0;
    // The page was opened: look at Samba and at the stick again (on the readers' own threads).
    virtual void recheck() = 0;
    // Copy every song on the stick (or the one at `index` of the list) into the Inbox.
    virtual void import_all() = 0;
    virtual void import_one(std::size_t index) = 0;
    virtual void cancel_import() = 0;
    // The view that is open. It lives here so that a developer flag can start with one open.
    virtual SourcesSheet open() const = 0;
    virtual void set_open(SourcesSheet sheet) = 0;
};

// A port with nothing behind it (the bench, the settings check): no share, no stick.
class NullSourcesPort final : public SourcesPort {
  public:
    const NetworkView& network() override { return network_; }
    const UsbView& usb() override { return usb_; }
    void recheck() override {}
    void import_all() override {}
    void import_one(std::size_t) override {}
    void cancel_import() override {}
    SourcesSheet open() const override { return open_; }
    void set_open(SourcesSheet sheet) override { open_ = sheet; }

  private:
    NetworkView network_{ShareState::NotSetUp};
    UsbView usb_;
    SourcesSheet open_ = SourcesSheet::None;
};

} // namespace etude::app::ui
