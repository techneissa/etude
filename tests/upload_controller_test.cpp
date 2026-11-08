// SPDX-License-Identifier: GPL-3.0-or-later
// The Add songs page's end of the upload server: the switch starts and stops it on a thread of its
// own, and the view the page reads follows the server. Bound to loopback only.
#include "../app/folder_watcher.hpp"
#include "../app/loop_wake.hpp"
#include "../app/open_song_box.hpp"
#include "../app/upload_controller.hpp"
#include "../app/upload_demo.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include "timing_factor.hpp"
#include "etude/folder_watch.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

// A server on loopback shows that address only when it is asked to (the app's own never does).
constexpr bool kShowLoopback = true;

int port_of(const ui::UploadView& v) {
    return std::stoi(v.number_address.substr(v.number_address.find(':') + 1));
}

// A client that connects to 127.0.0.1:port and stays: silent, or (`halfway`) in the middle of an
// upload that never ends, so the server's worker is waiting on it inside a request.
struct IdleClient {
    int fd = -1;
    explicit IdleClient(int port, bool halfway = false) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        REQUIRE(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
        if (halfway) {
            const std::string head =
                "POST /api/login HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                "\r\nContent-Type: application/x-www-form-urlencoded\r\n"
                "Content-Length: 100\r\n\r\npin=12";
            REQUIRE(::send(fd, head.data(), head.size(), 0) == static_cast<ssize_t>(head.size()));
        }
    }
    ~IdleClient() { ::close(fd); }
    IdleClient(const IdleClient&) = delete;
    IdleClient& operator=(const IdleClient&) = delete;
};

UploadServerOptions loopback(const TempDir& root) {
    UploadServerOptions o;
    o.songs = root.path / "Songs";
    o.removed = root.path / "Removed songs";
    o.port = 0;
    o.bind = {"127.0.0.1"};
    return o;
}

// Calls `ok` with the port's current view until it holds, for at most a few seconds.
bool wait_for(ui::UploadPort& port, const std::function<bool(const ui::UploadView&)>& ok) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end) {
        if (ok(port.view())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

} // namespace

TEST_CASE("upload controller: nothing runs until the switch goes on") {
    TempDir root;
    UploadController controller(loopback(root), kShowLoopback);
    const ui::UploadView& v = controller.view();
    CHECK_FALSE(v.on);
    CHECK_FALSE(v.ready);
    CHECK(v.pin.empty());
    CHECK(v.note.empty());
}

TEST_CASE("upload controller: the switch starts the server, the view has its address and PIN") {
    TempDir root;
    UploadController controller(loopback(root), kShowLoopback);
    controller.set_on(true);
    CHECK(controller.view().on); // the switch follows the tap at once
    REQUIRE(wait_for(controller, [](const ui::UploadView& v) { return v.ready; }));
    const ui::UploadView& v = controller.view();
    CHECK(v.on);
    CHECK_FALSE(v.receiving);
    CHECK(v.pin.size() == 4);
    CHECK(v.name_address.rfind("etude.local:", 0) == 0);
    CHECK(v.number_address.rfind("127.0.0.1:", 0) == 0);
    CHECK(v.url == "http://" + v.number_address + "/");
    CHECK(v.remaining_s > 590);
    CHECK(v.remaining_s <= 600);
    controller.set_on(false);
    CHECK_FALSE(controller.view().on);
    REQUIRE(wait_for(controller, [](const ui::UploadView& v2) { return !v2.ready; }));
    CHECK(controller.view().pin.empty());
}

TEST_CASE("upload controller: the ticking countdown does not wake a still screen") {
    TempDir root;
    UploadController controller(loopback(root), kShowLoopback);
    controller.set_on(true);
    REQUIRE(wait_for(controller, [](const ui::UploadView& v) { return v.ready; }));
    const int before = controller.view().remaining_s;
    const WakeCounts woke = wake_counts();
    std::this_thread::sleep_for(std::chrono::milliseconds(2300));
    const int after = controller.view().remaining_s;
    CHECK(after < before); // the page reads the new count at its own tick
    CHECK(wake_report(woke, wake_counts()) == "none");
}

TEST_CASE("upload controller: a port that is taken turns the switch off with the reason") {
    TempDir root;
    UploadController first(loopback(root), kShowLoopback);
    first.set_on(true);
    REQUIRE(wait_for(first, [](const ui::UploadView& v) { return v.ready; }));
    const int port =
        std::stoi(first.view().number_address.substr(first.view().number_address.find(':') + 1));
    UploadServerOptions taken = loopback(root);
    taken.port = port;
    UploadController second(taken, kShowLoopback);
    second.set_on(true);
    REQUIRE(wait_for(second, [](const ui::UploadView& v) { return !v.on && !v.note.empty(); }));
    CHECK_FALSE(second.view().ready);
    CHECK(second.view().note.find("taken") != std::string::npos);
    // Turning it on again after the port is free works.
    first.set_on(false);
    REQUIRE(wait_for(first, [](const ui::UploadView& v) { return !v.ready; }));
    second.set_on(true);
    CHECK(wait_for(second, [](const ui::UploadView& v) { return v.ready; }));
}

TEST_CASE("upload controller: it ends with the app, switch on or not, and lets go of the port") {
    TempDir root;
    int port = 0;
    {
        UploadController controller(loopback(root), kShowLoopback);
        controller.set_on(true);
        REQUIRE(wait_for(controller, [](const ui::UploadView& v) { return v.ready; }));
        port = port_of(controller.view());
    } // destroyed while on: the server stops and the thread is joined
    REQUIRE(port > 0);
    // The port is free again: a new server binds that very port, and nothing answers in between.
    UploadServerOptions again = loopback(root);
    again.port = port;
    UploadController other(again, kShowLoopback);
    CHECK_FALSE(other.view().on);
    other.set_on(true);
    REQUIRE(wait_for(other, [](const ui::UploadView& v) { return v.ready || !v.note.empty(); }));
    CHECK(other.view().ready);
    CHECK(other.view().note.empty());
    CHECK(port_of(other.view()) == port);
}

TEST_CASE("upload controller: a quit with a phone still connected does not wait for it") {
    TempDir root;
    // The clients outlive the controller: they are still connected, and silent, when it is
    // destroyed.
    std::unique_ptr<IdleClient> a;
    std::unique_ptr<IdleClient> b;
    std::unique_ptr<IdleClient> c;
    std::chrono::steady_clock::time_point began;
    {
        UploadController controller(loopback(root), kShowLoopback);
        controller.set_on(true);
        REQUIRE(wait_for(controller, [](const ui::UploadView& v) { return v.ready; }));
        // One that has sent nothing, and two that stopped in the middle of a request: the library
        // would read for its own timeout (10 s) in each worker before it let go.
        const int port = port_of(controller.view());
        a = std::make_unique<IdleClient>(port);
        b = std::make_unique<IdleClient>(port, true); // in the middle of a request
        c = std::make_unique<IdleClient>(port, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // the workers have them
        began = std::chrono::steady_clock::now();
    }
    const auto took = std::chrono::steady_clock::now() - began;
    INFO("quit took " << std::chrono::duration_cast<std::chrono::milliseconds>(took).count()
                      << " ms");
    CHECK(took < std::chrono::seconds(8)); // the library would read for 10 s in each worker
    CHECK_TIMING(took < std::chrono::seconds(1));
}

TEST_CASE("upload controller: the switch the player left is the switch shown") {
    TempDir root;
    UploadController controller(loopback(root), kShowLoopback);
    // On, then off at once, many times: whatever the server was in the middle of, the view that
    // follows the last tap never shows the switch on.
    for (int round = 0; round < 12; ++round) {
        controller.set_on(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(round * 7));
        controller.set_on(false);
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
        while (std::chrono::steady_clock::now() < end) {
            REQUIRE_FALSE(controller.view().on);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    REQUIRE(wait_for(controller, [](const ui::UploadView& v) { return !v.ready; }));
    CHECK_FALSE(controller.view().on);
    // And the other way: off then on settles on.
    controller.set_on(true);
    controller.set_on(false);
    controller.set_on(true);
    REQUIRE(wait_for(controller, [](const ui::UploadView& v) { return v.ready; }));
    CHECK(controller.view().on);
}

TEST_CASE("upload controller: the address shown is a home-network one, the usual one first") {
    using V = std::vector<std::string>;
    CHECK(shown_address(V{"10.0.0.5", "192.168.1.20", "172.16.0.9"}, false) == "192.168.1.20");
    CHECK(shown_address(V{"172.20.1.1", "10.0.0.5"}, false) == "10.0.0.5");
    CHECK(shown_address(V{"172.31.255.1"}, false) == "172.31.255.1");
    // Never one a phone cannot use: link-local, loopback, public, outside 172.16/12, not IPv4.
    CHECK(shown_address(V{"169.254.3.4"}, false).empty());
    CHECK(shown_address(V{"169.254.3.4", "192.168.1.20"}, false) == "192.168.1.20");
    CHECK(shown_address(V{"127.0.0.1"}, false).empty());
    CHECK(shown_address(V{"8.8.8.8", "172.32.0.1", "172.15.0.1", "0.0.0.0"}, false).empty());
    CHECK(shown_address(V{"fe80::1", "::1", "not an address", ""}, false).empty());
    CHECK(shown_address(V{}, false).empty());
    // Loopback only when the server was bound to it by name, and a real address still wins.
    CHECK(shown_address(V{"127.0.0.1"}, true) == "127.0.0.1");
    CHECK(shown_address(V{"127.0.0.1", "10.1.1.1"}, true) == "10.1.1.1");
    CHECK(shown_address(V{"169.254.3.4"}, true).empty());
}

TEST_CASE("upload controller: with no address on a network there is no code, only the state") {
    TempDir root;
    UploadController controller(loopback(root)); // loopback only, and not asked to show it
    controller.set_on(true);
    REQUIRE(wait_for(controller, [](const ui::UploadView& v) { return v.ready; }));
    const ui::UploadView& v = controller.view();
    CHECK(v.on);
    CHECK(v.number_address.empty());
    CHECK(v.name_address.empty());
    CHECK(v.url.empty());
    CHECK(v.pin.size() == 4);
}

TEST_CASE("upload controller: a song that arrives from the phone is reported as one new song") {
    TempDir root;
    const fs::path songs = root.path / "Songs";
    fs::create_directories(songs);
    FolderWatcher::Options options;
    options.root = songs;
    options.native = false;
    options.settle_ms = 150;
    options.poll_ms = 40;
    options.settling_ms = 20;
    options.min_gap_ms = 10;
    options.safety_poll_ms = 40;
    FolderWatcher watcher(options);
    for (int i = 0; i < 400 && watcher.listings() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // The upload, as the server's request thread does it: through the API's sink into Songs/Inbox.
    UploadApi api(UploadApi::Options{songs, root.path / "Removed songs", [] { return UploadMs(1); },
                                     [] { return std::uint32_t(7); }, nullptr, nullptr});
    api.start();
    ServiceError error;
    std::unique_ptr<UploadSink> sink = api.begin_upload("Phone song.mid", &error);
    REQUIRE(sink);
    const std::string bytes = midi_bytes(std::string(300, 'n'));
    REQUIRE(sink->write(bytes.data(), bytes.size()));
    REQUIRE(sink->finish());
    WatchReport all;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < end && all.new_songs == 0) {
        if (auto r = watcher.take()) {
            all.added.insert(all.added.end(), r->added.begin(), r->added.end());
            all.new_songs += r->new_songs;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(all.new_songs == 1);
    REQUIRE(all.added.size() == 1);
    CHECK(all.added[0] == "Inbox/Phone song.mid"); // never the hidden temporary file
    CHECK(new_songs_text(all.new_songs) == "1 new song");
}

TEST_CASE("upload controller: the open song box is read from any thread") {
    OpenSongBox box;
    CHECK(box.get().empty());
    box.set("/songs/a.mid");
    std::string seen;
    std::thread reader([&] { seen = box.get(); });
    reader.join();
    CHECK(seen == "/songs/a.mid");
    box.set("");
    CHECK(box.get().empty());
}

TEST_CASE(
    "upload controller: the demo port shows its fixed view and the switch turns it off and on") {
    DemoUploadPort receiving("receiving");
    CHECK(receiving.view().receiving);
    CHECK(receiving.view().percent == 62);
    receiving.set_on(false);
    CHECK_FALSE(receiving.view().on);
    CHECK(receiving.view().pin.empty());
    receiving.set_on(true);
    CHECK(receiving.view().pin == "4821");
    DemoUploadPort off("off");
    CHECK_FALSE(off.view().on);
    DemoUploadPort done("done");
    CHECK(done.view().received == 3);
    CHECK_FALSE(done.view().receiving);
}
