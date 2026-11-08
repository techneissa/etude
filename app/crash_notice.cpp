// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_notice.hpp"

#include "crash_handler.hpp"

#include "etude/version.hpp"

#include <cxxabi.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <vector>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kReadLimit = 256 * 1024;

// The file's text, "" for anything but a regular file: a link is never followed.
std::string read_limited(const fs::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    std::string text;
    struct stat st{};
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        text.resize(kReadLimit);
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
    return text;
}

// A file replaced whole, so a start cut short leaves the old one.
void write_whole(const fs::path& path, const std::string& text) {
    const fs::path temp = path.string() + ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            return;
        }
    }
    std::error_code ignored;
    fs::rename(temp, path, ignored);
}

// The regular files of the folder named like a report: a link, a folder or a pipe is skipped.
std::vector<std::string> report_names(const fs::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        struct stat st{};
        if (etude::is_crash_report_name(name) && ::lstat(it->path().c_str(), &st) == 0 &&
            S_ISREG(st.st_mode)) {
            names.push_back(name);
        }
    }
    return names;
}

void prune(const fs::path& dir) {
    for (const std::string& old : etude::reports_to_prune(report_names(dir), etude::kCrashReportsKept)) {
        std::error_code ignored;
        fs::remove(dir / old, ignored);
    }
}

// The trace has the compiler's names (`_ZN5etude...`): the readable ones in their place, in the
// lines of "Stack trace:" only.
std::string demangled_trace(const std::string& text) {
    const std::size_t from = text.find("Stack trace:\n");
    if (from == std::string::npos) {
        return text;
    }
    const std::size_t to = std::min(text.find("\n\n", from), text.size());
    std::string out = text.substr(0, from);
    std::size_t at = from;
    while (at < to) {
        const std::size_t mark = text.find("_Z", at);
        if (mark == std::string::npos || mark >= to) {
            break;
        }
        std::size_t end = mark;
        while (end < to && (std::isalnum(static_cast<unsigned char>(text[end])) != 0 || text[end] == '_')) {
            ++end;
        }
        out.append(text, at, mark - at);
        const std::string mangled = text.substr(mark, end - mark);
        int status = 0;
        char* readable = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
        out += readable != nullptr && status == 0 ? std::string(readable) : mangled;
        std::free(readable);
        at = end;
    }
    out.append(text, at, std::string::npos);
    return out;
}

CrashNotice notice_from(std::string name, std::string text) {
    CrashNotice n;
    n.name = std::move(name);
    n.text = std::move(text);
    n.summary = etude::summarize_crash(n.text);
    n.url = etude::crash_issue_url(n.summary, etude::kCrashUrlMax);
    n.qr_url = etude::crash_issue_url(n.summary, etude::kCrashQrUrlMax);
    return n;
}

} // namespace

fs::path install_dir_of(std::string_view base_path) {
    std::string text(base_path);
    while (text.size() > 1 && text.back() == '/') {
        text.pop_back(); // SDL's base path ends in a slash, which would make the folder's name empty
    }
    fs::path dir(text);
    if (dir.filename() == "bin") {
        return dir.parent_path(); // <prefix>/versions/X/bin
    }
    if (dir.filename() == "Resources" && dir.parent_path().filename() == "Contents") {
        return dir.parent_path().parent_path(); // a Mac's X.app/Contents/Resources
    }
    return dir;
}

etude::ScrubRules scrub_rules_for(const fs::path& data_dir, const fs::path& install_dir) {
    etude::ScrubRules rules;
    if (const char* home = std::getenv("HOME")) {
        rules.home = home;
    }
    // A trace names the program by the path it was started with (argv[0]): the shipped units run
    // `<prefix>/current/bin/etude`, and SDL's base path, where the install folder comes from, is the
    // version folder `current` points to. Both stay, or every frame of the program would read <path>.
    std::vector<fs::path> dirs{data_dir, install_dir};
    if (install_dir.parent_path().filename() == "versions") {
        dirs.push_back(install_dir.parent_path().parent_path() / "current");
    }
    for (const fs::path& dir : dirs) {
        std::string text = dir.lexically_normal().string();
        while (text.size() > 1 && text.back() == '/') {
            text.pop_back();
        }
        if (!text.empty()) {
            rules.app_dirs.push_back(std::move(text));
        }
    }
    return rules;
}

namespace {

std::string told_name(const fs::path& dir) {
    std::string seen = read_limited(dir / kCrashSeenFile);
    while (!seen.empty() && (seen.back() == '\n' || seen.back() == '\r' || seen.back() == ' ')) {
        seen.pop_back();
    }
    return seen;
}

// The report `name` of `dir` as the player reads it: every path scrubbed, the trace demangled, and the
// file rewritten the same.
CrashNotice read_notice(const fs::path& dir, const std::string& name, const etude::ScrubRules& rules) {
    const std::string raw = read_limited(dir / name);
    // The trace's module paths came from the system, unscrubbed: now they are, in the file too.
    const std::string text = demangled_trace(etude::scrub_text(raw, rules));
    if (text != raw) {
        write_whole(dir / name, text);
    }
    return notice_from(name, text);
}

} // namespace

std::optional<CrashNotice> start_crash_notice(const fs::path& data_dir, const etude::ScrubRules& rules) {
    const fs::path dir = data_dir / kCrashFolder;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return std::nullopt;
    }
    prune(dir);
    const etude::CrashNoticePlan plan = etude::plan_crash_notice(report_names(dir), told_name(dir));
    if (plan.show.empty()) {
        return std::nullopt;
    }
    CrashNotice notice = read_notice(dir, plan.show, rules);
    write_whole(dir / kCrashSeenFile, plan.seen + "\n");
    return notice;
}

std::optional<CrashNotice> latest_crash_notice(const fs::path& data_dir, const etude::ScrubRules& rules) {
    const fs::path dir = data_dir / kCrashFolder;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return std::nullopt;
    }
    const std::string newest = etude::newest_crash_report(report_names(dir));
    if (newest.empty()) {
        return std::nullopt;
    }
    return read_notice(dir, newest, rules);
}

CrashNotice restore_failure_notice(const std::string& reason, const etude::ScrubRules& rules) {
    std::ostringstream text;
    text << "etude crash report\n"
         << "Version: " << etude::app_version() << "\n"
         << "OS: " << os_description() << "\n"
         << "Screen: crash screen\n"
         << "Signal: the update's data could not be put back\n\n"
         << "Reason: " << etude::scrub_line(reason, rules) << "\n";
    return notice_from("", text.str());
}

void mark_crash_reports_seen(const fs::path& data_dir) {
    const fs::path dir = data_dir / kCrashFolder;
    const std::string newest = etude::newest_crash_report(report_names(dir));
    if (!newest.empty() && newest > told_name(dir)) {
        write_whole(dir / kCrashSeenFile, newest + "\n");
    }
}

bool leave_send_marker(const fs::path& data_dir) {
    const fs::path dir = data_dir / kCrashFolder;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const int fd = ::open((dir / kCrashSendMarker).c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    const bool wrote = ::write(fd, "send\n", 5) == 5;
    return ::close(fd) == 0 && wrote;
}

bool take_send_marker(const fs::path& data_dir) {
    const fs::path marker = data_dir / kCrashFolder / kCrashSendMarker;
    struct stat st{};
    if (::lstat(marker.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    return ::unlink(marker.c_str()) == 0;
}

bool crash_notice_pending(const fs::path& data_dir) {
    const fs::path dir = data_dir / kCrashFolder;
    return !etude::plan_crash_notice(report_names(dir), told_name(dir)).show.empty();
}

void CrashNoticeStart::defer(const fs::path& data_dir, bool send) {
    data_dir_ = data_dir;
    send_ = send;
    waiting_ = true;
}

std::optional<CrashNotice> CrashNoticeStart::poll(bool names_known, const etude::ScrubRules& rules) {
    if (!waiting_ || !names_known) {
        return std::nullopt;
    }
    waiting_ = false;
    std::optional<CrashNotice> notice = start_crash_notice(data_dir_, rules);
    if (!notice && send_) {
        notice = latest_crash_notice(data_dir_, rules);
    }
    return notice;
}

CrashNotice demo_crash_notice(bool mac) {
    std::ostringstream text;
    text << "etude crash report\n"
         << "Version: " << etude::app_version() << "\n"
         << "OS: " << (mac ? "macOS (Darwin 27.0.0) arm64" : "Linux 6.12.25 aarch64") << "\n"
         << "Screen: playing a song\n"
         << "Song: Moonlight Sonata\n"
         << "Signal: SIGSEGV (11)\n"
         << "Time (UTC): 20261020-153012\n\n"
         << "Stack trace:\n";
    if (mac) {
        text << "0   etude   0x0000000104a3c1b8 etude::app::write_crash_report(char const*, int, char const*) + 280\n"
             << "1   etude   0x0000000104a3c4f0 etude_crash_signal + 40\n"
             << "2   libsystem_platform.dylib   0x000000018c1a2a24 _sigtramp + 56\n"
             << "3   etude   0x0000000104b01234 etude::app::Scene::draw(etude::app::Frame const&) + 812\n"
             << "4   etude   0x0000000104b05678 etude::app::WindowState::draw_frame() + 1204\n"
             << "5   etude   0x0000000104b09abc etude::app::WindowState::run() + 396\n"
             << "6   etude   0x0000000104a20110 main + 144\n";
    } else {
        text << "/opt/etude/versions/1.1.0/bin/etude(etude::app::write_crash_report(char const*, int, char const*)+0x1c0) [0x5555a1b3c1b8]\n"
             << "/opt/etude/versions/1.1.0/bin/etude(etude_crash_signal+0x28) [0x5555a1b3c4f0]\n"
             << "linux-vdso.so.1(__kernel_rt_sigreturn+0x0) [0x7f9c3e0a1800]\n"
             << "/opt/etude/versions/1.1.0/bin/etude(etude::app::Scene::draw(etude::app::Frame const&)+0x32c) [0x5555a1c01234]\n"
             << "/opt/etude/versions/1.1.0/bin/etude(etude::app::WindowState::draw_frame()+0x4b4) [0x5555a1c05678]\n"
             << "/opt/etude/versions/1.1.0/bin/etude(etude::app::WindowState::run()+0x18c) [0x5555a1c09abc]\n"
             << "/lib/aarch64-linux-gnu/libc.so.6(+0x273fc) [0x7f9c3dfa73fc]\n";
    }
    text << "\nLog (the last lines, the newest last):\n"
         << "etude: touch device 3 \"Goodix Capacitive TouchScreen\": touchscreen\n"
         << "etude: a song loaded with 2 warning(s)\n"
         << "etude: a song Playback was dropped with 3 notes sounding\n";
    return notice_from("20261020-153012.txt", text.str());
}

} // namespace etude::app
