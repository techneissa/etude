// SPDX-License-Identifier: GPL-3.0-or-later
// The systemd watchdog ping (app/watchdog): what goes to $NOTIFY_SOCKET, how often, and that nothing
// happens without a socket.
#include "watchdog.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#if defined(__linux__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#endif

using namespace etude::app;

TEST_CASE("watchdog: the ping interval is a quarter of the unit's WATCHDOG_USEC, kept in bounds") {
    CHECK(watchdog_interval_ns("60000000") == 5'000'000'000); // 60 s: capped at five seconds
    CHECK(watchdog_interval_ns("8000000") == 2'000'000'000);  // 8 s
    CHECK(watchdog_interval_ns("1000000") == 250'000'000);    // 1 s
    CHECK(watchdog_interval_ns("100000") == 100'000'000);     // 0.1 s: floor
    CHECK(watchdog_interval_ns("") == 2'000'000'000);         // not set
    CHECK(watchdog_interval_ns("0") == 2'000'000'000);
    CHECK(watchdog_interval_ns("sixty") == 2'000'000'000);
    CHECK(watchdog_interval_ns("99999999999999999999") == 2'000'000'000);
}

TEST_CASE("watchdog: a socket address is a path or an abstract name, and nothing else") {
    CHECK(notify_socket_address("/run/systemd/notify") == "/run/systemd/notify");
    const std::string abstract = notify_socket_address("@abstract");
    REQUIRE(abstract.size() == 9);
    CHECK(abstract.front() == '\0');
    CHECK(abstract.substr(1) == "abstract");
    CHECK(notify_socket_address("").empty());
    CHECK(notify_socket_address("/").empty());
    CHECK(notify_socket_address("relative/path").empty());
    CHECK(notify_socket_address("vsock:2:1234").empty());
    CHECK(notify_socket_address("/" + std::string(200, 'x')).empty());
}

#if defined(__linux__)
namespace {

// A datagram socket bound to `path`, with a short wait on reads.
int listen_on(const std::string& path) {
    ::unlink(path.c_str());
    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    REQUIRE(fd >= 0);
    sockaddr_un at{};
    at.sun_family = AF_UNIX;
    std::strncpy(at.sun_path, path.c_str(), sizeof at.sun_path - 1);
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) == 0);
    timeval wait{0, 100'000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    return fd;
}

// The next datagram, or "" when none came.
std::string receive(int fd) {
    char buffer[64];
    const ssize_t n = ::recv(fd, buffer, sizeof buffer, 0);
    return n > 0 ? std::string(buffer, static_cast<std::size_t>(n)) : std::string();
}

} // namespace

TEST_CASE("watchdog: a ping is WATCHDOG=1 to the socket, once per interval, from the caller's thread") {
    const std::string path = "/tmp/etude-watchdog-test-" + std::to_string(::getpid());
    const int listener = listen_on(path);
    {
        Watchdog dog(path, 1'000'000'000);
        REQUIRE(dog.active());
        dog.ping(5'000'000'000);
        CHECK(receive(listener) == "WATCHDOG=1");
        dog.ping(5'400'000'000); // too soon
        dog.ping(5'999'999'999);
        CHECK(receive(listener).empty());
        dog.ping(6'000'000'000);
        CHECK(receive(listener) == "WATCHDOG=1");
        CHECK(dog.sent() == 2);
        // A loop turn that is late (a long frame, then a ping) still sends one.
        dog.ping(60'000'000'000);
        CHECK(receive(listener) == "WATCHDOG=1");
    }
    ::close(listener);
    ::unlink(path.c_str());
}

TEST_CASE("watchdog: a socket nobody reads, or one that is gone, loses the ping and never blocks or raises") {
    const std::string path = "/tmp/etude-watchdog-gone-" + std::to_string(::getpid());
    const int listener = listen_on(path);
    Watchdog dog(path, 0);
    REQUIRE(dog.active());
    ::close(listener);
    ::unlink(path.c_str());
    for (int i = 0; i < 10; ++i) {
        dog.ping(i); // no reader: the send fails, nothing else happens
    }
    CHECK(dog.sent() == 0);
}

TEST_CASE("watchdog: with no socket, or one that is no address, nothing is opened and nothing is sent") {
    Watchdog none("", 1);
    CHECK_FALSE(none.active());
    none.ping(1'000'000'000);
    CHECK(none.sent() == 0);
    Watchdog relative("not/absolute", 1);
    CHECK_FALSE(relative.active());
    // The environment constructor: no NOTIFY_SOCKET in the test run, so it is off.
    if (std::getenv("NOTIFY_SOCKET") == nullptr) {
        Watchdog from_env;
        CHECK_FALSE(from_env.active());
        from_env.ping(1);
        CHECK(from_env.sent() == 0);
    }
}
#else
TEST_CASE("watchdog: off where there is no systemd") {
    Watchdog dog("/run/systemd/notify", 1);
    CHECK_FALSE(dog.active());
    dog.ping(1);
    CHECK(dog.sent() == 0);
}
#endif

namespace {

std::string source_text(const std::string& relative) {
    std::ifstream in(std::string(ETUDE_SOURCE_DIR) + "/" + relative, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::size_t count_of(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("watchdog: only the frame loop pings, and the watchdog starts no thread of its own") {
    // A ping from a thread of its own would go on while the frame thread hangs, and hide the hang.
    const std::string loop = source_text("app/window_run.cpp");
    CHECK(count_of(loop, "watchdog.ping(") == 1);
    CHECK(loop.find("watchdog.ping(") > loop.find("while (running)"));
    const std::string watchdog = source_text("app/watchdog.cpp");
    for (const char* thread : {"<thread>", "std::thread", "std::jthread", "std::async", "pthread", "spawn_thread", "SDL_CreateThread",
                               "timer_create", "SDL_AddTimer"}) {
        CAPTURE(thread);
        CHECK(watchdog.find(thread) == std::string::npos);
    }
    for (const char* file : {"app/window_state.hpp", "app/window_frame.cpp", "app/window_draw.cpp",
                             "app/window_startup.cpp", "app/window_shutdown.cpp", "app/update_service.cpp"}) {
        CAPTURE(file);
        CHECK(source_text(file).find("Watchdog") == std::string::npos);
    }
}
