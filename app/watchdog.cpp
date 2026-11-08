// SPDX-License-Identifier: GPL-3.0-or-later
#include "watchdog.hpp"

#include <algorithm>
#include <cstdlib>

#if defined(__linux__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#endif

namespace etude::app {

namespace {

constexpr std::int64_t kDefaultIntervalNs = 2'000'000'000;
constexpr std::int64_t kShortestNs = 100'000'000;
constexpr std::int64_t kLongestNs = 5'000'000'000;

} // namespace

std::int64_t watchdog_interval_ns(std::string_view watchdog_usec) {
    if (watchdog_usec.empty() || watchdog_usec.size() > 15) {
        return kDefaultIntervalNs;
    }
    std::int64_t usec = 0;
    for (const char ch : watchdog_usec) {
        if (ch < '0' || ch > '9') {
            return kDefaultIntervalNs;
        }
        usec = usec * 10 + (ch - '0');
    }
    if (usec == 0) {
        return kDefaultIntervalNs;
    }
    return std::clamp<std::int64_t>(usec * 1000 / 4, kShortestNs, kLongestNs);
}

std::string notify_socket_address(std::string_view text) {
    if (text.size() < 2 || (text.front() != '/' && text.front() != '@') || text.size() > 100) {
        return {};
    }
    std::string address(text);
    if (address.front() == '@') {
        address.front() = '\0';
    }
    return address;
}

#if defined(__linux__)

Watchdog::Watchdog() {
    const char* socket = std::getenv("NOTIFY_SOCKET");
    if (socket == nullptr) {
        return;
    }
    const char* usec = std::getenv("WATCHDOG_USEC");
    interval_ns_ = watchdog_interval_ns(usec != nullptr ? usec : "");
    open(socket);
}

Watchdog::Watchdog(std::string_view socket_text, std::int64_t interval_ns) : interval_ns_(interval_ns) {
    open(socket_text);
}

void Watchdog::open(std::string_view socket_text) {
    address_ = notify_socket_address(socket_text);
    if (address_.empty()) {
        return;
    }
    fd_ = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
}

Watchdog::~Watchdog() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void Watchdog::ping(std::int64_t now_ns) {
    if (fd_ < 0 || (pinged_ && now_ns - last_ns_ < interval_ns_)) {
        return;
    }
    sockaddr_un to{};
    to.sun_family = AF_UNIX;
    std::memcpy(to.sun_path, address_.data(), address_.size());
    const socklen_t length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + address_.size());
    constexpr char kMessage[] = "WATCHDOG=1";
    // MSG_NOSIGNAL: a socket whose reader is gone must not raise SIGPIPE.
    const ssize_t n = ::sendto(fd_, kMessage, sizeof kMessage - 1, MSG_NOSIGNAL,
                               reinterpret_cast<const sockaddr*>(&to), length);
    pinged_ = true;
    last_ns_ = now_ns;
    if (n > 0) {
        ++sent_;
    }
}

#else

// No systemd here: nothing to open, nothing to send.
Watchdog::Watchdog() = default;
Watchdog::Watchdog(std::string_view, std::int64_t) {}
Watchdog::~Watchdog() = default;
void Watchdog::open(std::string_view) {}
void Watchdog::ping(std::int64_t) {}

#endif

} // namespace etude::app
