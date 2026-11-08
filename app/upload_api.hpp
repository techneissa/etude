// SPDX-License-Identifier: GPL-3.0-or-later
// The upload page's requests, as functions over text: sign in with the PIN, who is signed in, the
// library's file list, rename, move, delete, and the start and end of an upload. The HTTP server
// (upload_server.cpp) turns a request into one of these calls and the reply into HTTP; nothing here
// knows about sockets, so the tests drive it directly with a clock of their own. One lock guards
// the PIN, the sessions and the server's life; file work runs outside it. No SDL.
#pragma once

#include "upload_service.hpp"

#include "etude/upload_auth.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace etude::app {

// The name of the session cookie.
inline constexpr const char* kSessionCookie = "tv_session";

// What the Pi's screen shows of the uploads: the file coming in now and how many have arrived since
// the server started. Counted where the bytes are written, read by the frame thread as a copy.
struct UploadProgress {
    bool active = false;         // a file is being received
    std::string name;            // its name, as the phone sent it
    std::uint64_t received = 0;  // bytes of it so far
    std::uint64_t total = 0;     // its size as the request says (0 when not known)
    std::uint32_t completed = 0; // files stored since the server started
    std::uint32_t failed = 0;    // uploads refused or stopped since the server started
};

struct ApiReply {
    int status = 200;
    std::string body;       // JSON
    std::string set_cookie; // a Set-Cookie value, or empty
};

class UploadApi {
  public:
    struct Options {
        std::filesystem::path songs;
        std::filesystem::path removed;
        std::function<UploadMs()> clock;         // milliseconds, steady
        std::function<std::uint32_t()> random32; // random numbers for the PIN and tokens
        std::function<std::string()> open_song;  // the song the piano has open, if any
        // How long the song at a path plays, in seconds; negative when not known (UploadService).
        std::function<double(const std::filesystem::path&)> song_length;
    };
    explicit UploadApi(Options options);

    // A new PIN, no sessions, and the ten idle minutes start now.
    void start();
    void stop();
    // True once when the server has been idle long enough and should stop.
    bool tick();
    std::string pin() const;
    UploadStatus status() const;
    UploadMs remaining_ms() const;
    std::string reason() const;
    void fail(std::string reason);

    // True when `cookie_header` holds a live session; that is use, which restarts the idle time.
    bool authorize(std::string_view cookie_header);
    ApiReply login(std::string_view pin);
    ApiReply logout(std::string_view cookie_header);
    ApiReply session(std::string_view cookie_header);

    ApiReply files();
    ApiReply rename(std::string_view path, std::string_view new_name);
    ApiReply move(std::string_view path, std::string_view folder);
    ApiReply remove(std::string_view path);

    // An upload: begin it (null with `error`), feed the sink, then `finished` with its outcome.
    // `transfer_begin` and `transfer_end` bracket it so a long upload is use.
    std::unique_ptr<UploadSink> begin_upload(std::string_view name, ServiceError* error);
    void transfer_begin();
    void transfer_end();
    void touch();
    // The upload counters (see UploadProgress): a file starts, bytes arrive, it ends.
    void progress_begin(std::string_view name, std::uint64_t total);
    void progress_add(std::size_t bytes);
    void progress_end(bool stored);
    void progress_idle(); // not receiving, without counting a file
    UploadProgress progress() const;
    // `ignored`: files after the first in the same request, which are not stored.
    static ApiReply uploaded(const std::string& name, const std::string& saved_as,
                             std::size_t ignored = 0);
    static ApiReply refused(const ServiceError& error);

  private:
    UploadMs now() const { return options_.clock(); }
    std::string make_token();
    ApiReply result_reply(const ServiceResult& result);

    Options options_;
    UploadService service_;
    mutable std::mutex mutex_;
    UploadAuth auth_;
    UploadLife life_;
    UploadProgress progress_;
};

} // namespace etude::app
