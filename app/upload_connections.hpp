// SPDX-License-Identifier: GPL-3.0-or-later
// A guard for the upload server's connections: no peer holds more than a few at once (so one
// machine cannot take every worker thread), and no connection lives past a total deadline however
// slowly its client trickles bytes (the HTTP library's own timeouts are per read, so a byte every
// nine seconds would hold a worker for ever). The library does not say which sockets it has
// accepted, so the guard looks for them: every so often it walks this process's descriptors,
// picks out the connected sockets whose local port is one of the server's, remembers when it first
// saw each (its address pair is its identity, so a descriptor the system hands to another
// connection starts again) and shuts down the ones past the deadline and the ones beyond a peer's
// share, the newest first. A shut-down socket ends its request at once. No HTTP and no SDL.
#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace etude::app {

class ConnectionLimiter {
  public:
    struct Options {
        std::size_t max_per_peer = 3;
        std::chrono::milliseconds deadline{180'000};
    };
    explicit ConnectionLimiter(Options options) : options_(options) {}

    // The ports the server listens on (the connections it looks for).
    void listen_on(std::vector<int> ports);

    struct Cut {
        std::size_t too_slow = 0; // older than the deadline
        std::size_t too_many = 0; // beyond its peer's share
    };
    // One look at the descriptors. Cheap (a few hundred system calls); called a few times a second.
    Cut sweep(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // Shuts down every connection to the server's ports, at once (a quit must not wait for clients
    // that are idle or slow). The listening sockets are not touched. Returns how many it found.
    std::size_t close_all();

    // Connections seen at the last sweep.
    std::size_t open() const;

  private:
    struct Seen {
        std::chrono::steady_clock::time_point first;
        std::string peer;
        bool cut = false;
    };

    // A connected socket of the server's: its identity, its descriptor and its peer's address.
    struct Found {
        std::string key;
        int fd = -1;
        std::string peer;
    };
    // Every connected socket on the server's ports now (mutex_ held).
    std::vector<Found> scan() const;

    Options options_;
    mutable std::mutex mutex_;
    std::vector<int> ports_;
    std::map<std::string, Seen> seen_; // by "fd local-port peer-address:port"
};

} // namespace etude::app
