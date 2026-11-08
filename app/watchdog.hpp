// SPDX-License-Identifier: GPL-3.0-or-later
// The systemd watchdog, for the Pi: the unit has WatchdogSec= and the frame loop answers it with a
// datagram "WATCHDOG=1" to $NOTIFY_SOCKET (sd_notify without libsystemd). It is sent from the frame
// loop alone, never from a thread of its own, so a frame that hangs stops the pings and systemd
// restarts the app; it goes on while the loop is only throttled (idle, the screen off), because every
// turn of the loop pings, and the idle wait is at most a quarter of a second. Nothing happens when
// $NOTIFY_SOCKET is not set (a run from a shell, the tests, the headless runs), and the Mac build has
// no code for it at all.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace etude::app {

// How often a ping goes out at most: a quarter of the unit's WATCHDOG_USEC, which systemd sets in the
// environment (the manual asks for half), between a tenth of a second and five seconds; two seconds
// when it is not set.
std::int64_t watchdog_interval_ns(std::string_view watchdog_usec);

// The socket address of $NOTIFY_SOCKET as the kernel takes it: a path, or the abstract name that
// starts with "@" (its first byte becomes a zero). Empty when the text is not an address a datagram
// can go to.
std::string notify_socket_address(std::string_view text);

class Watchdog {
  public:
    // Reads NOTIFY_SOCKET and WATCHDOG_USEC from the environment; off when there is no socket.
    Watchdog();
    // For the tests: a socket address and the interval given.
    Watchdog(std::string_view socket_text, std::int64_t interval_ns);
    ~Watchdog();
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    bool active() const { return fd_ >= 0; }
    // Sends WATCHDOG=1 when one is due (`now_ns` from any steady clock). Never blocks: a socket that
    // is full or gone loses the ping, and the next one is tried at the next due time.
    void ping(std::int64_t now_ns);
    // How many pings went out (for the tests).
    int sent() const { return sent_; }

  private:
    void open(std::string_view socket_text);
    int fd_ = -1;
    int sent_ = 0;
#if defined(__linux__)
    std::string address_;
    std::int64_t interval_ns_ = 0;
    std::int64_t last_ns_ = 0;
    bool pinged_ = false;
#endif
};

} // namespace etude::app
