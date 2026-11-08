// SPDX-License-Identifier: GPL-3.0-or-later
#include "share_probe.hpp"
#include "crash_thread.hpp"

#include "etude/network_share.hpp"

#include <fstream>
#include <iterator>
#include <string>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxConfBytes = 1 << 20; // a smb.conf is a few KB

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string text;
    if (in) {
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (text.size() > kMaxConfBytes) {
        text.resize(kMaxConfBytes);
    }
    return text;
}

// Whether the pid file names a process that is there.
bool smbd_running(const ShareProbeOptions& options) {
    const std::string text = read_text(options.pid_file);
    long pid = 0;
    try {
        pid = std::stol(text);
    } catch (const std::exception&) {
        return false;
    }
    std::error_code ec;
    return pid > 0 && fs::exists(options.proc / std::to_string(pid), ec);
}

} // namespace

ui::ShareState read_share_state(const ShareProbeOptions& options) {
    if (options.forced) {
        return *options.forced;
    }
    std::string conf = read_text(options.conf);
    // One level of includes: the Pi's setup keeps the share in a file of its own.
    for (const std::string& file : smb_conf_includes(conf)) {
        conf += "\n" + read_text(file);
    }
    const bool shared = smb_conf_has_share(conf, kShareName);
    return shared && smbd_running(options) ? ui::ShareState::Shared : ui::ShareState::NotSetUp;
}

ShareProbe::ShareProbe(ShareProbeOptions options) : options_(std::move(options)) {}

ShareProbe::~ShareProbe() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ShareProbe::recheck() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        wanted_ = true;
        if (!worker_.joinable()) {
            worker_ = spawn_thread([this] { run(); });
        }
    }
    cv_.notify_all();
}

void ShareProbe::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!quit_) {
        cv_.wait(lock, [this] { return quit_ || wanted_; });
        if (quit_) {
            break;
        }
        wanted_ = false;
        lock.unlock();
        const ui::ShareState now = read_share_state(options_);
        if (now != state_.exchange(now) && options_.on_change) {
            options_.on_change();
        }
        lock.lock();
    }
}

} // namespace etude::app
