// SPDX-License-Identifier: GPL-3.0-or-later
#include "upload_service.hpp"

#include "library_files.hpp"

#include "etude/upload_rules.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>
#include <span>
#include <system_error>
#include <utility>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr const char* kOpenSongText =
    "That song is open on the piano, so it stays where it is. Open another one first.";

const char* kind_text(FileKind kind) {
    switch (kind) {
    case FileKind::Song:
        return "song";
    case FileKind::Document:
        return "document";
    case FileKind::Image:
        return "image";
    }
    return "song";
}

ServiceResult failure(int status, std::string message) {
    ServiceResult r;
    r.error = {status, std::move(message)};
    return r;
}

const char* problem_text(NameProblem problem) {
    switch (problem) {
    case NameProblem::Empty:
        return "The name is empty";
    case NameProblem::TooLong:
        return "The name is too long";
    case NameProblem::BadUtf8:
        return "The name has characters that cannot be read";
    case NameProblem::Control:
        return "The name has characters that cannot be used";
    case NameProblem::Separator:
        return "The name cannot hold a slash";
    case NameProblem::Dots:
        return "The name cannot start or end with a dot";
    case NameProblem::NotAllowed:
        return "That kind of file is not accepted. Songs, PDFs and photos are.";
    case NameProblem::None:
        break;
    }
    return "The name cannot be used";
}

std::string random_hex() {
    std::random_device device;
    char text[17];
    std::snprintf(text, sizeof text, "%08x%08x", device(), device());
    return text;
}

std::string generic(const fs::path& p) { return p.generic_string(); }

} // namespace

UploadService::UploadService(fs::path songs, fs::path removed,
                             std::function<std::string()> open_song,
                             std::function<double(const fs::path&)> song_length)
    : songs_(std::move(songs)), removed_(std::move(removed)), open_song_(std::move(open_song)),
      song_length_(std::move(song_length)) {
    remove_stale_incoming();
}

void UploadService::remove_stale_incoming() const {
    std::error_code ec;
    const fs::path folder = inbox() / kIncomingFolder;
    if (!fs::is_directory(folder, ec)) {
        return;
    }
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(kStaleIncomingHours);
    for (fs::directory_iterator it(folder, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        std::error_code entry_ec;
        if (it->is_regular_file(entry_ec) && it->last_write_time(entry_ec) < limit) {
            fs::remove(it->path(), entry_ec);
        }
    }
}

bool UploadService::is_open_song(const fs::path& full) const {
    if (!open_song_) {
        return false;
    }
    std::string text = open_song_();
    std::error_code ec;
    if (!text.empty() && !fs::exists(fs::path(text), ec) && text.rfind('#') != std::string::npos) {
        text.resize(text.rfind('#')); // an ABC tune: the file is before the address
    }
    return !text.empty() && fs::exists(fs::path(text), ec) &&
           fs::equivalent(fs::path(text), full, ec) && !ec;
}

bool UploadService::inside_songs(const fs::path& path) const {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(songs_, ec);
    if (ec) {
        return false;
    }
    const fs::path real = fs::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }
    const fs::path relative = real.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

bool UploadService::resolve_file(std::string_view relative, fs::path& full,
                                 ServiceError* error) const {
    const auto refuse = [&](int status, std::string message) {
        if (error != nullptr) {
            *error = {status, std::move(message)};
        }
        return false;
    };
    const RelPath path = clean_relative_path(relative, false);
    if (!path.ok()) {
        return refuse(400, problem_text(path.problem));
    }
    if (!kind_of_name(path.parts.back())) {
        return refuse(400, problem_text(NameProblem::NotAllowed));
    }
    full = songs_ / fs::path(path.path);
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(full, ec);
    if (ec || !fs::is_regular_file(status)) {
        return refuse(404, "That file is not there");
    }
    if (!inside_songs(full)) {
        return refuse(403, "That file is outside the Songs folder");
    }
    return true;
}

Listing UploadService::list() const {
    Listing listing;
    std::error_code ec;
    if (!fs::is_directory(songs_, ec)) {
        return listing;
    }
    fs::recursive_directory_iterator it(songs_, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const std::string name = it->path().filename().string();
        std::error_code entry_ec;
        const bool hidden = !name.empty() && name.front() == '.';
        if (it->is_symlink(entry_ec)) {
            if (it->is_directory(entry_ec)) {
                it.disable_recursion_pending(); // a link out of the Songs folder is not followed
            }
            continue;
        }
        if (it->is_directory(entry_ec)) {
            const std::string relative = generic(it->path().lexically_relative(songs_));
            if (hidden || std::count(relative.begin(), relative.end(), '/') + 1 >
                              static_cast<std::ptrdiff_t>(kMaxDepth)) {
                it.disable_recursion_pending();
            } else if (listing.folders.size() < kMaxListed) {
                listing.folders.push_back(relative);
            }
            continue;
        }
        if (hidden || !it->is_regular_file(entry_ec)) {
            continue;
        }
        const std::optional<FileKind> kind = kind_of_name(name);
        if (!kind) {
            continue;
        }
        if (listing.files.size() >= kMaxListed) {
            listing.truncated = true;
            break;
        }
        const std::uintmax_t size = it->file_size(entry_ec);
        const double length =
            *kind == FileKind::Song && song_length_ ? song_length_(it->path()) : -1.0;
        listing.files.push_back({generic(it->path().lexically_relative(songs_)),
                                 entry_ec ? 0 : static_cast<std::uint64_t>(size), kind_text(*kind),
                                 length});
    }
    std::sort(listing.files.begin(), listing.files.end(),
              [](const ListedFile& a, const ListedFile& b) { return a.path < b.path; });
    std::sort(listing.folders.begin(), listing.folders.end());
    return listing;
}

std::unique_ptr<UploadSink> UploadService::begin_upload(std::string_view name,
                                                        ServiceError* error) {
    const auto refuse = [&](int status, std::string message) -> std::unique_ptr<UploadSink> {
        if (error != nullptr) {
            *error = {status, std::move(message)};
        }
        return nullptr;
    };
    const CleanName cleaned = check_upload_type(name);
    if (!cleaned.ok()) {
        return refuse(cleaned.problem == NameProblem::NotAllowed ? 415 : 400,
                      problem_text(cleaned.problem));
    }
    const fs::path incoming = inbox() / kIncomingFolder;
    std::error_code ec;
    // Checked before anything is made: an Inbox that is a link out of the Songs folder must not
    // get even an empty folder.
    if (!inside_songs(inbox())) {
        return refuse(500, "The Inbox folder cannot be made");
    }
    fs::create_directories(incoming, ec);
    if (ec || !inside_songs(incoming)) {
        return refuse(500, "The Inbox folder cannot be made");
    }
    // Opened for new only and never through a link: a name that exists or is a link is refused.
    const fs::path temp = incoming / ("." + random_hex() + ".part");
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        return refuse(500, "The Inbox folder cannot be written to");
    }
    return std::unique_ptr<UploadSink>(
        new UploadSink(*this, cleaned.name, lower_extension(cleaned.name), temp, fd));
}

ServiceResult UploadService::rename(std::string_view path, std::string_view new_name) {
    fs::path full;
    ServiceError error;
    if (!resolve_file(path, full, &error)) {
        return failure(error.status, error.message);
    }
    if (is_open_song(full)) {
        return failure(409, kOpenSongText);
    }
    const CleanName cleaned = check_upload_type(new_name);
    if (!cleaned.ok()) {
        return failure(400, problem_text(cleaned.problem));
    }
    std::string why;
    if (!rename_in_place(full, cleaned.name, &why)) {
        return failure(409, why);
    }
    ServiceResult ok;
    ok.ok = true;
    ok.path = generic((full.parent_path() / cleaned.name).lexically_relative(songs_));
    return ok;
}

ServiceResult UploadService::move(std::string_view path, std::string_view folder) {
    fs::path full;
    ServiceError error;
    if (!resolve_file(path, full, &error)) {
        return failure(error.status, error.message);
    }
    if (is_open_song(full)) {
        return failure(409, kOpenSongText);
    }
    const RelPath target = clean_relative_path(folder, true);
    if (!target.ok()) {
        return failure(400, problem_text(target.problem));
    }
    const fs::path to = songs_ / fs::path(target.path);
    if (!inside_songs(to)) {
        return failure(403, "That folder is outside the Songs folder");
    }
    std::string why;
    const MoveDone done = move_into(full, to, &why);
    if (done.outcome == MoveOutcome::Failed) {
        return failure(500, why);
    }
    ServiceResult ok;
    ok.ok = true;
    ok.path = generic(done.now.lexically_relative(songs_));
    return ok;
}

ServiceResult UploadService::remove(std::string_view path) {
    fs::path full;
    ServiceError error;
    if (!resolve_file(path, full, &error)) {
        return failure(error.status, error.message);
    }
    if (is_open_song(full)) {
        return failure(409, kOpenSongText);
    }
    if (removed_.empty()) {
        return failure(500, "There is no Removed songs folder");
    }
    // Into Removed songs in the folders it had, to be taken back from there: nothing is deleted.
    fs::path folder = full.parent_path().lexically_relative(songs_);
    if (folder == ".") {
        folder.clear();
    }
    const fs::path to = removed_ / folder;
    std::string why;
    if (move_into(full, to, &why).outcome == MoveOutcome::Failed) {
        return failure(500, why);
    }
    ServiceResult ok;
    ok.ok = true;
    return ok;
}

UploadSink::UploadSink(const UploadService& service, std::string name, std::string extension,
                       fs::path temp, int fd)
    : service_(service), name_(std::move(name)), extension_(std::move(extension)),
      temp_(std::move(temp)), fd_(fd) {}

UploadSink::~UploadSink() { abort(); }

void UploadSink::abort() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!done_) {
        std::error_code ec;
        fs::remove(temp_, ec);
        done_ = true;
    }
}

bool UploadSink::fail(int status, std::string message) {
    if (error_.message.empty()) {
        error_ = {status, std::move(message)};
    }
    abort();
    return false;
}

bool UploadSink::check_head(bool whole) {
    if (head_checked_ || (!whole && head_.size() < sniff_window(extension_))) {
        return true;
    }
    head_checked_ = true;
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(head_.data()),
                                              head_.size());
    if (!header_matches(extension_, bytes)) {
        return fail(415, head_.empty() ? "The file is empty"
                                       : "The file is not what its name says it is");
    }
    return true;
}

bool UploadSink::write(const char* data, std::size_t size) {
    if (done_ || fd_ < 0) {
        return false;
    }
    if (bytes_ + size > kMaxUploadBytes) {
        return fail(413, "The file is too big (the limit is 32 MB)");
    }
    if (head_.size() < kSniffBytes) {
        head_.append(data, std::min(size, kSniffBytes - head_.size()));
    }
    if (!check_head(false)) {
        return false;
    }
    std::size_t at = 0;
    while (at < size) {
        const ssize_t n = ::write(fd_, data + at, size - at);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return fail(507, "The Pi has no room for the file");
        }
        at += static_cast<std::size_t>(n);
    }
    bytes_ += size;
    return true;
}

bool UploadSink::finish() {
    if (done_ || fd_ < 0) {
        return false;
    }
    if (!check_head(true)) {
        return false;
    }
    if (::fsync(fd_) != 0) {
        return fail(507, "The file could not be saved");
    }
    ::close(fd_);
    fd_ = -1;
    std::string why;
    const MoveDone moved = move_into(temp_, service_.inbox(), &why, name_);
    if (moved.outcome != MoveOutcome::Moved) {
        return fail(500, why.empty() ? "The file could not be saved" : why);
    }
    done_ = true; // the temporary file is the Inbox file now
    saved_as_ = moved.now.filename().string();
    return true;
}

} // namespace etude::app
