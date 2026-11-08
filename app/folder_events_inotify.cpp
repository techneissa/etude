// SPDX-License-Identifier: GPL-3.0-or-later
#include "folder_events.hpp"
#include "crash_thread.hpp"

#if defined(__linux__)

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace etude::app {

namespace {

namespace fs = std::filesystem;

// The most folders watched at once; the kernel's own limit is often 8192 for the whole user.
// Past it the folder-wide timer still finds changes, a few seconds later.
constexpr std::size_t kMaxWatches = 4096;

constexpr std::uint32_t kMask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
                                IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF |
                                IN_MOVE_SELF;

class Inotify final : public FolderEvents {
  public:
    ~Inotify() override { stop(); }

    bool start(const fs::path& root, std::function<void()> on_change) override {
        stop();
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            return false;
        }
        fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        stop_fd_ = eventfd(0, EFD_CLOEXEC);
        if (fd_ < 0 || stop_fd_ < 0) {
            close_fds();
            return false;
        }
        on_change_ = std::move(on_change);
        add_tree(root);
        if (watches_.empty()) {
            close_fds();
            return false;
        }
        thread_ = spawn_thread([this] { run(); });
        return true;
    }

    void stop() override {
        if (thread_.joinable()) {
            const std::uint64_t one = 1;
            [[maybe_unused]] const ssize_t written = ::write(stop_fd_, &one, sizeof one);
            thread_.join();
        }
        close_fds();
        watches_.clear();
        on_change_ = nullptr;
    }

    const char* name() const override { return "inotify"; }

  private:
    void close_fds() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        if (stop_fd_ >= 0) {
            ::close(stop_fd_);
            stop_fd_ = -1;
        }
    }

    // Watches `dir` and every folder under it, hidden ones left out (uploads are written in a
    // hidden folder, and its churn is not news).
    void add_tree(const fs::path& dir) {
        if (watches_.size() >= kMaxWatches) {
            return;
        }
        const int wd = inotify_add_watch(fd_, dir.c_str(), kMask | IN_ONLYDIR);
        if (wd < 0) {
            return;
        }
        watches_[wd] = dir;
        std::error_code ec;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::directory_iterator(); it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (!name.empty() && name.front() != '.' && it->is_directory(ec) &&
                !it->is_symlink(ec)) {
                add_tree(it->path());
            }
        }
    }

    void run() {
        alignas(inotify_event) char buffer[16384];
        pollfd fds[2] = {{fd_, POLLIN, 0}, {stop_fd_, POLLIN, 0}};
        for (;;) {
            if (::poll(fds, 2, -1) < 0 && errno != EINTR) {
                return;
            }
            if ((fds[1].revents & POLLIN) != 0) {
                return;
            }
            if ((fds[0].revents & POLLIN) == 0) {
                continue;
            }
            const ssize_t n = ::read(fd_, buffer, sizeof buffer);
            if (n <= 0) {
                continue;
            }
            for (std::size_t at = 0; at < static_cast<std::size_t>(n);) {
                const auto* e = reinterpret_cast<const inotify_event*>(buffer + at);
                at += sizeof(inotify_event) + e->len;
                if ((e->mask & IN_IGNORED) != 0) {
                    watches_.erase(e->wd);
                } else if ((e->mask & (IN_CREATE | IN_MOVED_TO)) != 0 &&
                           (e->mask & IN_ISDIR) != 0 && e->len > 0 && e->name[0] != '.') {
                    const auto parent = watches_.find(e->wd);
                    if (parent != watches_.end()) {
                        add_tree(parent->second / e->name);
                    }
                }
            }
            on_change_();
        }
    }

    int fd_ = -1;
    int stop_fd_ = -1;
    std::map<int, fs::path> watches_; // the thread's after start(), until stop()
    std::function<void()> on_change_;
    std::thread thread_;
};

} // namespace

std::unique_ptr<FolderEvents> make_inotify() { return std::make_unique<Inotify>(); }

} // namespace etude::app

#else

namespace etude::app {
std::unique_ptr<FolderEvents> make_inotify() { return nullptr; }
} // namespace etude::app

#endif
