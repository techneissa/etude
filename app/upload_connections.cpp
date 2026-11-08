// SPDX-License-Identifier: GPL-3.0-or-later
#include "upload_connections.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace etude::app {

namespace {

// Descriptors past this are not looked at (a Pi's soft limit is 1024).
constexpr rlim_t kMostDescriptors = 8192;

std::string address_text(const sockaddr_storage& a, int* port) {
    char text[INET6_ADDRSTRLEN] = {};
    if (a.ss_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(&a);
        ::inet_ntop(AF_INET, &in->sin_addr, text, sizeof text);
        *port = ntohs(in->sin_port);
    } else if (a.ss_family == AF_INET6) {
        const auto* in = reinterpret_cast<const sockaddr_in6*>(&a);
        ::inet_ntop(AF_INET6, &in->sin6_addr, text, sizeof text);
        *port = ntohs(in->sin6_port);
    }
    return text;
}

} // namespace

void ConnectionLimiter::listen_on(std::vector<int> ports) {
    const std::lock_guard<std::mutex> lock(mutex_);
    ports_ = std::move(ports);
    seen_.clear();
}

std::vector<ConnectionLimiter::Found> ConnectionLimiter::scan() const {
    std::vector<Found> found;
    if (ports_.empty()) {
        return found;
    }
    rlimit limit{};
    ::getrlimit(RLIMIT_NOFILE, &limit);
    const int last = static_cast<int>(std::min(limit.rlim_cur, kMostDescriptors));
    for (int fd = 3; fd < last; ++fd) {
        sockaddr_storage local{};
        socklen_t length = sizeof local;
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
            continue; // not a socket, or not open
        }
        int local_port = 0;
        address_text(local, &local_port);
        if (std::find(ports_.begin(), ports_.end(), local_port) == ports_.end()) {
            continue;
        }
        int listening = 0;
        socklen_t size = sizeof listening;
        if (::getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &listening, &size) == 0 && listening != 0) {
            continue; // the server's own listening socket
        }
        sockaddr_storage remote{};
        length = sizeof remote;
        if (::getpeername(fd, reinterpret_cast<sockaddr*>(&remote), &length) != 0) {
            continue; // already gone
        }
        int remote_port = 0;
        const std::string peer = address_text(remote, &remote_port);
        found.push_back({std::to_string(fd) + " " + std::to_string(local_port) + " " + peer + ":" +
                             std::to_string(remote_port),
                         fd, peer});
    }
    return found;
}

std::size_t ConnectionLimiter::close_all() {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::vector<Found> found = scan();
    for (const Found& f : found) {
        ::shutdown(f.fd, SHUT_RDWR);
    }
    return found.size();
}

ConnectionLimiter::Cut ConnectionLimiter::sweep(std::chrono::steady_clock::time_point now) {
    Cut cut;
    const std::lock_guard<std::mutex> lock(mutex_);
    if (ports_.empty()) {
        return cut;
    }
    // What is open now, by identity; the entries left over at the end are connections that ended.
    std::map<std::string, int> live; // key -> descriptor
    for (const Found& f : scan()) {
        live[f.key] = f.fd;
        if (seen_.find(f.key) == seen_.end()) {
            seen_[f.key] = {now, f.peer, false};
        }
    }
    for (auto it = seen_.begin(); it != seen_.end();) {
        it = live.count(it->first) == 0 ? seen_.erase(it) : std::next(it);
    }
    // The deadline.
    for (auto& [key, seen] : seen_) {
        if (!seen.cut && now - seen.first >= options_.deadline) {
            ::shutdown(live[key], SHUT_RDWR);
            seen.cut = true;
            ++cut.too_slow;
        }
    }
    // A peer's share: the oldest connections keep it, the newer ones beyond it are cut.
    std::map<std::string, std::vector<std::string>> by_peer;
    for (const auto& [key, seen] : seen_) {
        if (!seen.cut) {
            by_peer[seen.peer].push_back(key);
        }
    }
    for (auto& [peer, keys] : by_peer) {
        if (keys.size() <= options_.max_per_peer) {
            continue;
        }
        std::sort(keys.begin(), keys.end(), [&](const std::string& a, const std::string& b) {
            return seen_[a].first < seen_[b].first;
        });
        for (std::size_t i = options_.max_per_peer; i < keys.size(); ++i) {
            ::shutdown(live[keys[i]], SHUT_RDWR);
            seen_[keys[i]].cut = true;
            ++cut.too_many;
        }
    }
    return cut;
}

std::size_t ConnectionLimiter::open() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return seen_.size();
}

} // namespace etude::app
