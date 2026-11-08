// SPDX-License-Identifier: GPL-3.0-or-later
// What the phone upload page does to the library's files, with no sockets and no HTTP in it: store
// an upload into Songs/Inbox, and list, rename, move and delete files of the Songs folder. Every
// name and path from a request goes through the core rules (upload_rules.hpp) before it touches the
// disk, a path must still lie inside the Songs folder after links are followed, an upload is
// written to a hidden temporary file and renamed into place only when whole (so a half file is
// never listed), and nothing is deleted: Delete moves the file into "Removed songs", as the
// library screen does. Safe from several threads at once (the HTTP server's workers). No SDL.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app {

// The folder uploads go into, under the Songs folder, and the hidden folder inside it where an
// upload is written until it is whole.
inline constexpr const char* kInboxFolder = "Inbox";
inline constexpr const char* kIncomingFolder = ".incoming";
// The most files and folders one listing carries.
inline constexpr std::size_t kMaxListed = 5000;
// Partial uploads older than this are removed when the service starts.
inline constexpr int kStaleIncomingHours = 1;

// A refused or failed request: `message` is for the player, `status` the HTTP status that fits it.
struct ServiceError {
    int status = 400;
    std::string message;
};

// The result of rename, move and remove: `path` is where the file is now ('/' separated, under the
// Songs folder; empty for a removal).
struct ServiceResult {
    bool ok = false;
    ServiceError error;
    std::string path;
};

struct ListedFile {
    std::string path; // under the Songs folder, '/' separated
    std::uint64_t size = 0;
    std::string kind; // "song", "document" or "image"
    double length_s = -1.0; // a song's playing time when it is known, else negative
};

struct Listing {
    std::vector<ListedFile> files; // in name order
    std::vector<std::string> folders;
    bool truncated = false; // more than kMaxListed
};

class UploadSink;

class UploadService {
  public:
    // `songs` is the Songs folder; `removed` is where Delete puts files (empty: Delete refuses).
    // `open_song` says which file the piano has open (a path, for an ABC tune "file.abc#X=3", or
    // empty): that song is not renamed, moved or deleted from the phone, as the library screen does
    // not either. It is called from request threads, so it must be safe to call from any thread.
    // `song_length` says how long the song at a path (inside the Songs folder) plays, in seconds,
    // or a negative number when it does not know (yet): the app answers from what its library
    // already parsed, so the listing never opens a file to find out. Called from request threads.
    UploadService(std::filesystem::path songs, std::filesystem::path removed,
                  std::function<std::string()> open_song = nullptr,
                  std::function<double(const std::filesystem::path&)> song_length = nullptr);

    Listing list() const;

    // Starts an upload under the name the player's phone sent. Null, with `error` set, when the
    // name or type is not allowed or the Inbox cannot be made. The sink takes the bytes and the
    // file appears in Songs/Inbox when `finish()` succeeds.
    std::unique_ptr<UploadSink> begin_upload(std::string_view name, ServiceError* error);

    ServiceResult rename(std::string_view path, std::string_view new_name);
    // `folder` is a folder under the Songs folder ("" is the top one); it is made when missing.
    ServiceResult move(std::string_view path, std::string_view folder);
    ServiceResult remove(std::string_view path);

    const std::filesystem::path& songs() const { return songs_; }

  private:
    friend class UploadSink;
    // The file `relative` names, when it is a real file of an allowed type inside the Songs
    // folder; else an error.
    bool resolve_file(std::string_view relative, std::filesystem::path& full,
                      ServiceError* error) const;
    // True when `path` (existing or not) is inside the Songs folder once links are followed.
    bool inside_songs(const std::filesystem::path& path) const;
    // True when `full` is the song the piano has open.
    bool is_open_song(const std::filesystem::path& full) const;
    std::filesystem::path inbox() const { return songs_ / kInboxFolder; }
    void remove_stale_incoming() const;

    std::filesystem::path songs_;
    std::filesystem::path removed_;
    std::function<std::string()> open_song_;
    std::function<double(const std::filesystem::path&)> song_length_;
};

// One upload in progress: bytes in, a finished file out. Destroyed unfinished, it removes its
// temporary file.
class UploadSink {
  public:
    ~UploadSink();
    UploadSink(const UploadSink&) = delete;
    UploadSink& operator=(const UploadSink&) = delete;

    // Appends bytes. False when the upload failed (too big, wrong kind of file, disk full): it
    // is over, `error()` says why, and further calls do nothing.
    bool write(const char* data, std::size_t size);
    // Closes, syncs and moves the file into the Inbox (under another name when that one is
    // taken); the name it got is in `saved_as()`.
    bool finish();
    void abort();
    const ServiceError& error() const { return error_; }
    const std::string& saved_as() const { return saved_as_; }
    std::uint64_t bytes() const { return bytes_; }

  private:
    friend class UploadService;
    UploadSink(const UploadService& service, std::string name, std::string extension,
               std::filesystem::path temp, int fd);
    bool fail(int status, std::string message);
    bool check_head(bool whole);

    const UploadService& service_;
    std::string name_;
    std::string extension_;
    std::filesystem::path temp_;
    int fd_ = -1;
    std::uint64_t bytes_ = 0;
    std::string head_;
    bool head_checked_ = false;
    bool done_ = false;
    ServiceError error_;
    std::string saved_as_;
};

} // namespace etude::app
