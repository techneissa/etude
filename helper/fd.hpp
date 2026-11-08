// SPDX-License-Identifier: GPL-3.0-or-later
// A file descriptor that closes itself. Part of the root update helper (see helper/main.cpp).
#pragma once

#include <unistd.h>

#include <utility>

namespace etude::helper {

class Fd {
  public:
    Fd() = default;
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() { reset(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : fd_(other.release()) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    int get() const { return fd_; }
    bool ok() const { return fd_ >= 0; }
    int release() { return std::exchange(fd_, -1); }
    void reset(int fd = -1) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_ = -1;
};

} // namespace etude::helper
