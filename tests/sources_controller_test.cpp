// SPDX-License-Identifier: GPL-3.0-or-later
// The Add songs page's network folder and USB stick as the frame thread sees them: a stick
// plugged in and pulled out shows within a poll, a copy runs on its own thread with progress, the
// views never wait for a scan, and the page's own code touches no file.
#include "../app/loop_wake.hpp"
#include "../app/sources_controller.hpp"
#include "timing_factor.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace etude::app;
using namespace upload_test;

namespace {

struct Rig {
    TempDir root;
    SourcesOptions options;
    Rig() {
        options.roots.pretend = root.path / "STICK";
        options.songs_folder = root.path / "Songs";
        options.share.forced = ui::ShareState::Shared;
        options.poll_ms = 20;
    }
    fs::path stick() const { return options.roots.pretend; }
};

template <typename Pred> bool wait_for(SourcesController& c, Pred&& done, int ms = 4000) {
    for (int waited = 0; waited < ms; waited += 5) {
        if (done(c.usb())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool ready(const ui::UsbView& v) { return v.present && !v.scanning && !v.importing; }

} // namespace

TEST_CASE("sources: a stick plugged in shows with its songs, pulled out it goes") {
    Rig rig;
    SourcesController c(rig.options);
    CHECK_FALSE(c.usb().present);
    // Made beside and moved into place whole, so a poll never sees half a stick.
    const fs::path made = rig.root.path / "made";
    write_file(made / "a.mid", midi_bytes("1"));
    write_file(made / "Lessons/b.mid", midi_bytes("2"));
    write_file(made / "notes.txt", "x");
    fs::rename(made, rig.stick());
    REQUIRE(wait_for(c, ready));
    CHECK(c.usb().label == "STICK");
    CHECK(c.usb().songs == 2);
    REQUIRE(c.usb().list.size() == 2);
    CHECK(c.usb().list[0].name == "Lessons/b.mid");
    fs::remove_all(rig.stick());
    REQUIRE(wait_for(c, [](const ui::UsbView& v) { return !v.present; }));
    CHECK(c.usb().list.empty());
    CHECK(c.usb().songs == 0);
}

TEST_CASE("sources: a change wakes the frame loop, a quiet stick does not") {
    Rig rig;
    write_file(rig.stick() / "a.mid", midi_bytes());
    SourcesController c(rig.options);
    REQUIRE(wait_for(c, ready));
    const WakeCounts before = wake_counts();
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // ten polls
    const auto mine = static_cast<std::size_t>(WakeSource::Sources);
    CHECK(wake_counts()[mine] == before[mine]);
    write_file(rig.stick() / "b.mid", midi_bytes("2"));
    c.recheck();
    REQUIRE(wait_for(c, [](const ui::UsbView& v) { return v.present && v.songs == 2; }));
    CHECK(wake_counts()[mine] > before[mine]);
}

TEST_CASE("sources: import all runs on a thread, with progress, into the Inbox") {
    Rig rig;
    write_file(rig.stick() / "a.mid", midi_bytes("1"));
    write_file(rig.stick() / "b.mid", midi_bytes("2"));
    SourcesController c(rig.options);
    REQUIRE(wait_for(c, ready));
    c.import_all();
    REQUIRE(wait_for(c, [](const ui::UsbView& v) { return !v.importing && v.copied == 2; }));
    CHECK(c.usb().ended == ui::ImportEnd::Done);
    CHECK(c.usb().import_total == 2);
    CHECK(c.usb().list[0].mark == ui::SongMark::Copied);
    CHECK(c.usb().list[1].mark == ui::SongMark::Copied);
    CHECK(tree(rig.options.songs_folder / "Inbox") == ".incoming/\na.mid\nb.mid\n");
    CHECK_FALSE(c.take_notice()); // the folder watch says "2 new songs"
    // Again: everything is there already, and the player is told.
    c.import_all();
    REQUIRE(wait_for(c, [](const ui::UsbView& v) { return !v.importing && v.duplicates == 2; }));
    const auto told = c.take_notice();
    REQUIRE(told);
    CHECK(told->find("already in your library") != std::string::npos);
    CHECK_FALSE(c.take_notice()); // once
}

TEST_CASE("sources: one song at a time") {
    Rig rig;
    write_file(rig.stick() / "a.mid", midi_bytes("1"));
    write_file(rig.stick() / "b.mid", midi_bytes("2"));
    SourcesController c(rig.options);
    REQUIRE(wait_for(c, ready));
    c.import_one(1);
    REQUIRE(wait_for(c, [](const ui::UsbView& v) { return !v.importing && v.copied == 1; }));
    CHECK(c.usb().list[0].mark == ui::SongMark::None);
    CHECK(c.usb().list[1].mark == ui::SongMark::Copied);
    CHECK(tree(rig.options.songs_folder / "Inbox") == ".incoming/\nb.mid\n");
    c.import_one(7); // no such song: nothing happens
    CHECK_FALSE(c.usb().importing);
}

TEST_CASE("sources: the stick pulled out mid-copy stops with a notice") {
    Rig rig;
    write_file(rig.stick() / "a.mid", midi_bytes(std::string(60'000, 'a')));
    write_file(rig.stick() / "b.mid", midi_bytes("2"));
    rig.options.import_chunk = 4096;
    int chunks = 0;
    rig.options.chunk_hook = [&](bool copying) {
        if (copying && ++chunks == 3) { // the third chunk written to the Inbox
            std::error_code ec;
            fs::remove_all(rig.stick(), ec);
        }
    };
    SourcesController c(rig.options);
    REQUIRE(wait_for(c, ready));
    c.import_all();
    REQUIRE(wait_for(c, [](const ui::UsbView& v) { return !v.importing && !v.present; }));
    std::optional<std::string> told;
    for (int i = 0; i < 200 && !told; ++i) {
        told = c.take_notice();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(told);
    CHECK(told->find("USB stick removed") != std::string::npos);
    CHECK(tree(rig.options.songs_folder / "Inbox").find(".mid") ==
          std::string::npos); // none half done
    CHECK(tree(rig.options.songs_folder / "Inbox/.incoming").empty()); // nor a temporary file
}

TEST_CASE("sources: cancel stops the copy") {
    Rig rig;
    write_file(rig.stick() / "a.mid", midi_bytes(std::string(200'000, 'a')));
    write_file(rig.stick() / "b.mid", midi_bytes("2"));
    rig.options.import_chunk = 1024;
    std::promise<void> reached;
    std::once_flag once;
    SourcesController* self = nullptr;
    rig.options.chunk_hook = [&](bool copying) {
        if (copying) {
            std::call_once(once, [&] {
                reached.set_value();
                self->cancel_import();
            });
        }
    };
    SourcesController c(rig.options);
    self = &c;
    REQUIRE(wait_for(c, ready));
    c.import_all();
    reached.get_future().wait();
    REQUIRE(wait_for(c, [](const ui::UsbView& v) { return !v.importing; }));
    CHECK(c.usb().ended == ui::ImportEnd::None);
    CHECK(c.usb().present);
    CHECK(tree(rig.options.songs_folder / "Inbox").find(".mid") == std::string::npos);
}

TEST_CASE("sources: the views do not wait for a scan") {
    Rig rig;
    write_file(rig.stick() / "a.mid", midi_bytes());
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    rig.options.scan_hook = [gate] { gate.wait(); }; // the watcher stands in the scan
    SourcesController c(rig.options);
    REQUIRE(wait_for(c, [](const ui::UsbView& v) { return v.present && v.scanning; }));
    const auto began = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        (void)c.usb();
        (void)c.network();
        c.recheck();
        c.import_all(); // refused while scanning
    }
    const auto took = std::chrono::steady_clock::now() - began;
    CHECK(took < std::chrono::seconds(10));
    CHECK_TIMING(took < std::chrono::milliseconds(500));
    CHECK(c.usb().scanning);
    release.set_value();
    REQUIRE(wait_for(c, ready));
    CHECK(c.usb().songs == 1);
}

TEST_CASE("sources: the network folder is read when asked") {
    Rig rig;
    SourcesController c(rig.options);
    CHECK(c.network().state == ui::ShareState::Checking);
    c.recheck();
    for (int i = 0; i < 400 && c.network().state == ui::ShareState::Checking; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(c.network().state == ui::ShareState::Shared);
}

TEST_CASE("sources: the page's own code names no file system call") {
    for (const char* name : {"app/ui/pages_add_songs.cpp", "app/ui/pages_add_songs_sources.cpp",
                             "app/ui/add_songs_sources.hpp", "app/ui/sources_port.hpp"}) {
        const std::string text = read_file(fs::path(ETUDE_SOURCE_DIR) / name);
        REQUIRE_FALSE(text.empty());
        for (const char* banned :
             {"filesystem", "fopen", "ifstream", "ofstream", "opendir", "stat(", "statvfs",
              "exists(", "std::system", "popen", "fstream"}) {
            INFO(name << " uses " << banned);
            CHECK(text.find(banned) == std::string::npos);
        }
    }
}

TEST_CASE("sources: quitting does not wait for a stick that never answers") {
    Rig rig;
    write_file(rig.stick() / "a.mid", midi_bytes());
    auto stuck = std::make_shared<std::promise<void>>();
    const std::shared_future<void> gate = stuck->get_future().share();
    auto in_scan = std::make_shared<std::atomic<bool>>(false);
    rig.options.scan_hook = [gate, in_scan] {
        *in_scan = true;
        gate.wait(); // a read that does not return
    };
    rig.options.quit_wait_ms = 100;
    const auto began = std::chrono::steady_clock::now();
    {
        SourcesController c(rig.options);
        for (int i = 0; i < 400 && !*in_scan; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(*in_scan);
    } // the destructor gives up on the watcher after 100 ms
    const auto quit_took = std::chrono::steady_clock::now() - began;
    CHECK(quit_took < std::chrono::seconds(10));
    CHECK_TIMING(quit_took < std::chrono::milliseconds(1500));
    stuck->set_value(); // the thread left behind ends on its own, on state it still holds
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
