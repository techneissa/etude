// SPDX-License-Identifier: GPL-3.0-or-later
// Downloads for "Get songs": a file from a free song catalogue, read whole into memory. Only the
// catalogue thread fetches (never the frame thread), one file at a time.
//
// Rules every fetcher keeps: HTTPS only, the server's certificate checked against the system's
// trusted roots and its host name; only the hosts it is given (a redirect to any other host or to
// plain HTTP fails); a time limit to connect, a stall limit while reading and a limit on the whole
// request; at most `max_bytes`;
// a User-Agent that names the app; and at least `kFetchSpacing` between two requests to the same
// host.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace etude::app {

inline constexpr std::chrono::milliseconds kFetchSpacing{1000};
inline constexpr std::chrono::seconds kFetchConnectLimit{10};
// A read that brings nothing new for this long fails.
inline constexpr std::chrono::seconds kFetchStallLimit{20};
// A request that has not finished in this long fails, however it trickles in.
inline constexpr std::chrono::seconds kFetchTotalLimit{120};
inline constexpr int kFetchMaxRedirects = 3;
// The User-Agent sent with every request.
std::string_view fetch_user_agent();

struct FetchResult {
    bool ok = false;
    bool cancelled = false;
    int status = 0;      // the HTTP status, 0 when none came
    std::string body;    // when ok
    std::string reason;  // when not: a few words for the player ("No connection", "Not found")
    // When not: the technical cause, for a message that says what happened ("timed out after 15 s",
    // "could not resolve host"); empty when the fetcher has nothing more than `reason`.
    std::string detail;
    std::string final_url; // after redirects
};

// Called as bytes arrive: (received, total or 0 when unknown). False cancels the fetch.
using FetchProgress = std::function<bool(std::uint64_t received, std::uint64_t total)>;

class Fetcher {
  public:
    virtual ~Fetcher() = default;
    // Blocks until the file is in, fails or is cancelled. `hosts`: the hosts it may reach.
    virtual FetchResult fetch(std::string_view url, std::span<const std::string_view> hosts,
                              std::uint64_t max_bytes, const FetchProgress& progress) = 0;
};

// The host of an https URL ("www.mutopiaproject.org"), lowercased; empty for any other URL (plain
// http, user info, no host, a port other than 443).
std::string https_host(std::string_view url);
// `url` is https and its host is one of `hosts` (exactly, case aside).
bool fetch_allowed(std::string_view url, std::span<const std::string_view> hosts);
// A redirect's Location read against the URL that gave it: an absolute URL as it is, "/path"
// on the same scheme and host; empty for anything else.
std::string redirect_target(std::string_view from, std::string_view location);
// The player's words for an HTTP status that is not a success.
std::string_view http_status_reason(int status);

// The system's HTTPS (libcurl, with the platform's certificates). Thread-safe: the pacing between
// requests is shared.
class CurlFetcher final : public Fetcher {
  public:
    CurlFetcher();
    ~CurlFetcher() override;
    FetchResult fetch(std::string_view url, std::span<const std::string_view> hosts,
                      std::uint64_t max_bytes, const FetchProgress& progress) override;
};

// A developer's stand-in that reads files from a folder instead of the network, for the tests and
// the audit's Get songs screens (--catalogue-sites): "https://host/a/b.mid" is DIR/host/a/b.mid. A
// missing file is a 404. A song file arrives at `bytes_per_s` (0: all at once), in steps, with the
// progress called as for a real fetch; `fail_first` makes the first fetch of each URL fail with "No connection" (to show a retry).
// `hold(true)` keeps a song file half in, with the progress called again and again, until
// `hold(false)` or a cancel: a test then sees the Fetching state however slow the machine.
class FolderFetcher final : public Fetcher {
  public:
    explicit FolderFetcher(std::string folder, std::uint64_t bytes_per_s = 0,
                           bool fail_first = false);
    FetchResult fetch(std::string_view url, std::span<const std::string_view> hosts,
                      std::uint64_t max_bytes, const FetchProgress& progress) override;
    // Fetches asked for so far.
    int fetches() const { return fetches_.load(); }
    void hold(bool on) { hold_.store(on); }

  private:
    std::string folder_;
    std::uint64_t bytes_per_s_ = 0;
    bool fail_first_ = false;
    std::string failed_; // URLs failed once, '\n' separated
    std::atomic<int> fetches_{0};
    std::atomic<bool> hold_{false};
};

} // namespace etude::app
