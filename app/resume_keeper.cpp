// SPDX-License-Identifier: GPL-3.0-or-later
#include "resume_keeper.hpp"

#include "crash_thread.hpp"
#include "update_files.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>

namespace etude::app {

std::vector<std::string> resume_song_roots(const std::vector<std::filesystem::path>& folders) {
    std::vector<std::string> roots;
    const auto add = [&roots](const std::filesystem::path& folder) {
        std::string text = folder.generic_string();
        if (!text.empty() && std::find(roots.begin(), roots.end(), text) == roots.end()) {
            roots.push_back(std::move(text));
        }
    };
    for (const std::filesystem::path& folder : folders) {
        if (folder.empty()) {
            continue;
        }
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::absolute(folder, ec);
        if (ec) {
            continue;
        }
        add(absolute.lexically_normal());
        const std::filesystem::path real = std::filesystem::weakly_canonical(absolute, ec);
        if (!ec) {
            add(real);
        }
    }
    return roots;
}

std::optional<ResumeParse> read_resume_file(const std::filesystem::path& file) {
    const int fd = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return std::nullopt;
    }
    std::string text;
    struct stat st{};
    bool plain = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
                 static_cast<std::size_t>(st.st_size) <= kMaxResumeBytes;
    if (plain) {
        text.resize(kMaxResumeBytes + 1);
        std::size_t have = 0;
        while (have < text.size()) {
            const ssize_t n = ::read(fd, text.data() + have, text.size() - have);
            if (n <= 0) {
                break;
            }
            have += static_cast<std::size_t>(n);
        }
        text.resize(have);
    }
    ::close(fd);
    if (!plain) {
        ResumeParse not_a_file;
        not_a_file.error = "not a regular file of a size that fits";
        return not_a_file;
    }
    return etude::parse_resume_point(text);
}

ResumeKeeper::~ResumeKeeper() { stop(); }

void ResumeKeeper::stop() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ResumeKeeper::begin(const std::filesystem::path& file, bool enabled, const ResumePoint& first,
                         std::int64_t now_ms) {
    if (!enabled || active_) {
        return;
    }
    active_ = true;
    file_ = file;
    started_ms_ = now_ms;
    last_session_ = first.session;
    last_screen_ = first.screen;
    thread_ = spawn_thread([this] { run(); });
    pacer_.due(now_ms, false, false); // the start: the pacer counts the first write as made
    ResumePoint point = first;
    point.ran_ms = 0;
    point.clean = false;
    queue(point);
}

void ResumeKeeper::note(const SessionState& session, const std::string& screen, std::int64_t now_ms) {
    if (!active_) {
        return;
    }
    const bool screen_changed = screen != last_screen_;
    const bool changed = screen_changed || !(session == last_session_);
    if (!pacer_.due(now_ms, changed, screen_changed)) {
        return;
    }
    last_session_ = session;
    last_screen_ = screen;
    ResumePoint point;
    point.ran_ms = ran_ms(now_ms);
    point.screen = screen;
    point.session = session;
    queue(point);
}

std::int64_t ResumeKeeper::ran_ms(std::int64_t now_ms) const { return std::max<std::int64_t>(0, now_ms - started_ms_); }

void ResumeKeeper::close(const SessionState& session, const std::string& screen, std::int64_t now_ms) {
    if (!active_) {
        return;
    }
    ResumePoint point;
    point.ran_ms = ran_ms(now_ms);
    point.clean = true;
    point.screen = screen;
    point.session = session;
    queue(point);
    stop(); // the thread writes what is queued before it ends
    active_ = false;
}

void ResumeKeeper::queue(const ResumePoint& point) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        pending_ = etude::format_resume_point(point); // the newest replaces one not yet written
    }
    wake_.notify_all();
}

void ResumeKeeper::run() {
    for (;;) {
        std::string text;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return pending_.has_value() || stop_; });
            if (!pending_) {
                return; // stopped with nothing left to write
            }
            text = std::move(*pending_);
            pending_.reset();
        }
        write_file_atomic(file_, text); // a failed write leaves the old file; the next one tries again
    }
}

} // namespace etude::app
