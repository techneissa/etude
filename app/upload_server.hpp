// SPDX-License-Identifier: GPL-3.0-or-later
// The local web server behind "Add songs from phone": off until started, a 4-digit PIN made at each
// start, uploads into Songs/Inbox only, the library's files listed and renamed, moved and deleted
// from the phone, and its own end after ten idle minutes. It runs on threads of its own (the
// listener, a small pool for requests, a watchdog); the frame thread only starts and stops it and
// reads its status. HTTP is cpp-httplib, kept in upload_server.cpp alone. No SDL.
#pragma once

#include "upload_api.hpp"

#include "etude/upload_auth.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace etude::app {

// The port people type after "etude.local:" and the QR code carries.
inline constexpr int kUploadPort = 8080;

struct UploadServerOptions {
    std::filesystem::path songs;   // the Songs folder (uploads go into its Inbox)
    std::filesystem::path removed; // "Removed songs": where Delete puts files
    int port = kUploadPort;        // 0 takes any free port (tests)
    // The addresses to listen on, one listener each. Empty (the default) chooses: the machine's
    // private LAN addresses (10/8, 172.16/12, 192.168/16, link-local), or loopback when it has
    // none, so the server is never reachable from a public or VPN interface. Names an address
    // (such as "127.0.0.1") only when the caller means it.
    std::vector<std::string> bind;
    bool all_interfaces = false; // listen on 0.0.0.0: only when asked for by name
    // The song the piano has open (a path), or empty: it cannot be renamed, moved or deleted from
    // the phone. Called from request threads, so it must be safe from any thread.
    std::function<std::string()> open_song;
    // How long the song at a path plays, in seconds, or a negative number when not known: the
    // listing shows it beside the size. Called from request threads; it must not read the file.
    std::function<double(const std::filesystem::path&)> song_length;
    // No connection lives longer than this, however slowly its client sends; a peer may hold at
    // most `max_per_peer` connections at once.
    std::chrono::milliseconds request_deadline{180'000};
    std::size_t max_per_peer = 3;
    // Names the page may be reached by besides an IP address and "localhost" (the Host header).
    std::vector<std::string> host_names{"etude.local"};
};

class UploadServer {
  public:
    explicit UploadServer(UploadServerOptions options);
    ~UploadServer(); // stops

    // Makes a new PIN and starts listening. False, with `error` set, when the port cannot be
    // bound (status() is then Error); already running is true. Never blocks on a client.
    bool start(std::string* error = nullptr);
    void stop();

    UploadStatus status() const;
    std::string pin() const;                    // the PIN to type on the phone; empty when off
    int port() const;                           // the port actually bound; 0 when off
    std::vector<std::string> addresses() const; // the addresses it listens on; empty when off
    std::string reason() const;                 // why it is off or failed, for the screen
    UploadMs remaining_ms() const;              // until the idle end
    UploadProgress progress() const;            // the file coming in and the counts

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The addresses `start()` would listen on for these options (named ones, else the machine's private
// LAN addresses, else loopback; "0.0.0.0" with `all_interfaces`). Binds nothing.
std::vector<std::string> planned_addresses(const UploadServerOptions& options);

// This machine's IPv4 addresses on the local network (not loopback), for the address on the screen.
std::vector<std::string> local_ipv4_addresses();

} // namespace etude::app
