// SPDX-License-Identifier: GPL-3.0-or-later
#include "upload_controller.hpp"
#include "crash_thread.hpp"

#include "loop_wake.hpp"

#include "etude/upload_network.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

namespace etude::app {

namespace {

constexpr auto kPoll = std::chrono::milliseconds(250);
constexpr const char* kNameHost = "etude.local";

// 0 for the home network's usual 192.168/16, 1 for 10/8, 2 for 172.16/12, 3 for loopback; -1 for
// anything a phone could not use (link-local, public, not an IPv4 address).
int address_rank(const std::string& text) {
    std::uint8_t bytes[16];
    std::size_t length = 0;
    if (!etude::parse_address(text, bytes, length) || length != 4) {
        return -1;
    }
    if (bytes[0] == 192 && bytes[1] == 168) {
        return 0;
    }
    if (bytes[0] == 10) {
        return 1;
    }
    if (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) {
        return 2;
    }
    return bytes[0] == 127 ? 3 : -1;
}

bool same(const ui::UploadView& a, const ui::UploadView& b) {
    return a.on == b.on && a.ready == b.ready && a.receiving == b.receiving &&
           a.name_address == b.name_address && a.number_address == b.number_address &&
           a.url == b.url && a.pin == b.pin && a.remaining_s == b.remaining_s && a.file == b.file &&
           a.percent == b.percent && a.received == b.received && a.note == b.note;
}

} // namespace

std::string shown_address(const std::vector<std::string>& addresses, bool allow_loopback) {
    std::string best;
    int best_rank = 4;
    for (const std::string& address : addresses) {
        const int rank = address_rank(address);
        if (rank >= 0 && (rank < 3 || allow_loopback) && rank < best_rank) {
            best = address;
            best_rank = rank;
        }
    }
    return best;
}

UploadController::UploadController(UploadServerOptions options, bool show_loopback)
    : options_(std::move(options)), show_loopback_(show_loopback) {
    latest_.on = false;
}

UploadController::~UploadController() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

const ui::UploadView& UploadController::view() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (shown_version_ != latest_version_) {
        shown_ = latest_;
        shown_version_ = latest_version_;
    }
    return shown_;
}

void UploadController::set_on(bool on) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (wanted_ == on) {
            return;
        }
        wanted_ = on;
        // The switch follows the tap at once; the rest of the view follows the server.
        latest_.on = on;
        if (on) {
            latest_.note.clear();
        }
        ++latest_version_;
        if (on && !worker_.joinable()) {
            worker_ = spawn_thread([this] { run(); });
        }
    }
    cv_.notify_all();
}

void UploadController::publish(ui::UploadView next) {
    bool clock_only = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        // The switch is what was asked for last, whatever the server was doing when this view was
        // read: a view made before a tap must not turn the tap back.
        next.on = wanted_;
        if (same(latest_, next)) {
            return;
        }
        // The countdown ticks every second whether or not anything else happened: the page shows it
        // at the loop's own tick, and a wake for it would keep a still screen drawing.
        const int clock = latest_.remaining_s;
        latest_.remaining_s = next.remaining_s;
        clock_only = same(latest_, next);
        latest_.remaining_s = clock;
        latest_ = std::move(next);
        ++latest_version_;
    }
    if (!clock_only) {
        wake_loop(WakeSource::Upload);
    }
}

// The view of a running server.
ui::UploadView UploadController::read(const UploadServer& server, bool wanted) const {
    ui::UploadView v;
    v.on = wanted;
    v.ready = server.status() == UploadStatus::Ready || server.status() == UploadStatus::Receiving;
    const UploadProgress progress = server.progress();
    v.receiving = progress.active;
    v.pin = server.pin();
    v.remaining_s = static_cast<int>((server.remaining_ms() + 999) / 1000);
    v.received = static_cast<int>(progress.completed);
    if (progress.active) {
        v.file = progress.name;
        v.percent = progress.total == 0 ? 0
                                        : static_cast<int>(std::min<std::uint64_t>(
                                              99, progress.received * 100 / progress.total));
    }
    const std::string port = ":" + std::to_string(server.port());
    std::vector<std::string> candidates = server.addresses();
    if (std::find(candidates.begin(), candidates.end(), "0.0.0.0") != candidates.end()) {
        candidates = local_ipv4_addresses(); // listening on every interface: the machine's own
    }
    const std::string number = shown_address(candidates, show_loopback_);
    if (number.empty()) {
        return v; // not on a network: no address, no code
    }
    v.number_address = number + port;
    v.name_address = kNameHost + port;
    v.url = "http://" + v.number_address + "/";
    return v;
}

void UploadController::run() {
    std::unique_ptr<UploadServer> server;
    std::unique_lock<std::mutex> lock(mutex_);
    while (!quit_) {
        const bool wanted = wanted_;
        lock.unlock();
        if (wanted && server == nullptr) {
            auto next = std::make_unique<UploadServer>(options_);
            std::string error;
            if (next->start(&error)) {
                server = std::move(next);
            } else {
                ui::UploadView off;
                off.note = error.empty() ? "Could not start" : error;
                lock.lock();
                wanted_ = false;
                lock.unlock();
                publish(std::move(off));
            }
        } else if (!wanted && server != nullptr) {
            server->stop();
            server.reset();
            publish(ui::UploadView());
        }
        if (server != nullptr) {
            const UploadStatus status = server->status();
            if (status == UploadStatus::Off || status == UploadStatus::Error) {
                // It ended itself: ten idle minutes, or a failure.
                ui::UploadView off;
                off.note = server->reason().empty() ? "Turned off" : server->reason();
                server.reset();
                lock.lock();
                wanted_ = false;
                lock.unlock();
                publish(std::move(off));
            } else {
                publish(read(*server, wanted));
            }
        }
        lock.lock();
        cv_.wait_for(lock, kPoll, [this, was = wanted] { return quit_ || wanted_ != was; });
    }
    lock.unlock();
    if (server != nullptr) {
        server->stop();
    }
}

} // namespace etude::app
