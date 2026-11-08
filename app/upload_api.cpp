// SPDX-License-Identifier: GPL-3.0-or-later
#include "upload_api.hpp"

#include "etude/upload_network.hpp"
#include "etude/upload_rules.hpp"

#include <array>
#include <utility>

namespace etude::app {

namespace {

constexpr std::uint32_t kPinRange = 10000;
// The largest multiple of 10000 below 2^32: a random number at or past it is drawn again, so
// every PIN is equally likely.
constexpr std::uint32_t kPinLimit = 4'294'960'000u;

std::string json_string(std::string_view text) { return "\"" + json_escape(text) + "\""; }

ApiReply error_reply(int status, std::string_view message) {
    ApiReply r;
    r.status = status;
    r.body = "{\"error\":" + json_string(message) + "}";
    return r;
}

std::string session_cookie(const std::string& token) {
    // Not Secure: the page is served over plain HTTP on the home network. Not readable by scripts,
    // and never sent with a request from another site.
    return std::string(kSessionCookie) + "=" + token +
           "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + std::to_string(kSessionIdleMs / 1000);
}

} // namespace

UploadApi::UploadApi(Options options)
    : options_(std::move(options)),
      service_(options_.songs, options_.removed, options_.open_song, options_.song_length),
      auth_("") {}

void UploadApi::start() {
    std::uint32_t r = options_.random32();
    while (r >= kPinLimit) {
        r = options_.random32();
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    auth_ = UploadAuth(pin_from_random(r % kPinRange));
    life_.start(now());
    progress_ = UploadProgress();
}

void UploadApi::stop() {
    const std::lock_guard<std::mutex> lock(mutex_);
    auth_.clear();
    life_.stop();
}

bool UploadApi::tick() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!life_.tick(now())) {
        return false;
    }
    auth_.clear();
    return true;
}

std::string UploadApi::pin() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return auth_.pin();
}

UploadStatus UploadApi::status() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return life_.status();
}

UploadMs UploadApi::remaining_ms() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return life_.remaining_ms(now());
}

std::string UploadApi::reason() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return life_.reason();
}

void UploadApi::fail(std::string reason) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auth_.clear();
    life_.fail(std::move(reason));
}

std::string UploadApi::make_token() {
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        const std::uint32_t r = options_.random32();
        for (std::size_t k = 0; k < 4; ++k) {
            bytes[i + k] = static_cast<std::uint8_t>(r >> (8 * k));
        }
    }
    return token_from_bytes(bytes);
}

bool UploadApi::authorize(std::string_view cookie_header) {
    const std::string_view token = cookie_value(cookie_header, kSessionCookie);
    const std::lock_guard<std::mutex> lock(mutex_);
    const UploadMs t = now();
    if (life_.status() == UploadStatus::Off || life_.status() == UploadStatus::Error ||
        !auth_.check(token, t)) {
        return false;
    }
    life_.touch(t);
    return true;
}

ApiReply UploadApi::login(std::string_view pin) {
    const std::string token = make_token();
    const std::lock_guard<std::mutex> lock(mutex_);
    const UploadMs t = now();
    const LoginAnswer answer = auth_.login(pin, token, t);
    switch (answer.result) {
    case LoginResult::Ok: {
        life_.touch(t);
        ApiReply r;
        r.body = "{\"signed_in\":true}";
        r.set_cookie = session_cookie(answer.token);
        return r;
    }
    case LoginResult::Locked:
        return error_reply(429, "Too many tries. Wait " +
                                    std::to_string((answer.retry_ms + 999) / 1000) + " seconds.");
    case LoginResult::BadFormat:
    case LoginResult::WrongPin:
        break;
    }
    return error_reply(403, "That is not the PIN on the Pi's screen");
}

ApiReply UploadApi::logout(std::string_view cookie_header) {
    const std::string_view token = cookie_value(cookie_header, kSessionCookie);
    const std::lock_guard<std::mutex> lock(mutex_);
    auth_.logout(token);
    ApiReply r;
    r.body = "{\"signed_in\":false}";
    r.set_cookie = std::string(kSessionCookie) + "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
    return r;
}

ApiReply UploadApi::session(std::string_view cookie_header) {
    const std::string_view token = cookie_value(cookie_header, kSessionCookie);
    const std::lock_guard<std::mutex> lock(mutex_);
    ApiReply r;
    const UploadMs t = now();
    // Asking is not use: the time left shown to the phone does not restart by being read.
    r.body = auth_.check(token, t) ? "{\"signed_in\":true,\"remaining_s\":" +
                                         std::to_string((life_.remaining_ms(t) + 999) / 1000) + "}"
                                   : "{\"signed_in\":false}";
    return r;
}

ApiReply UploadApi::files() {
    const Listing listing = service_.list();
    ApiReply r;
    r.body = "{\"files\":[";
    for (std::size_t i = 0; i < listing.files.size(); ++i) {
        const ListedFile& f = listing.files[i];
        r.body +=
            (i == 0 ? "" : ",") + std::string("{\"path\":") + json_string(f.path) +
            ",\"size\":" + std::to_string(f.size) + ",\"kind\":" + json_string(f.kind) +
            (f.length_s > 0.0 ? ",\"length\":" + std::to_string(static_cast<long>(f.length_s + 0.5))
                              : std::string()) +
            "}";
    }
    r.body += "],\"folders\":[";
    for (std::size_t i = 0; i < listing.folders.size(); ++i) {
        r.body += (i == 0 ? "" : ",") + json_string(listing.folders[i]);
    }
    r.body += std::string("],\"truncated\":") + (listing.truncated ? "true" : "false") + "}";
    return r;
}

ApiReply UploadApi::result_reply(const ServiceResult& result) {
    if (!result.ok) {
        return refused(result.error);
    }
    ApiReply r;
    r.body = result.path.empty() ? "{\"ok\":true}"
                                 : "{\"ok\":true,\"path\":" + json_string(result.path) + "}";
    return r;
}

ApiReply UploadApi::rename(std::string_view path, std::string_view new_name) {
    return result_reply(service_.rename(path, new_name));
}

ApiReply UploadApi::move(std::string_view path, std::string_view folder) {
    return result_reply(service_.move(path, folder));
}

ApiReply UploadApi::remove(std::string_view path) { return result_reply(service_.remove(path)); }

std::unique_ptr<UploadSink> UploadApi::begin_upload(std::string_view name, ServiceError* error) {
    return service_.begin_upload(name, error);
}

void UploadApi::transfer_begin() {
    const std::lock_guard<std::mutex> lock(mutex_);
    life_.transfer_begin(now());
}

void UploadApi::transfer_end() {
    const std::lock_guard<std::mutex> lock(mutex_);
    life_.transfer_end(now());
}

void UploadApi::touch() {
    const std::lock_guard<std::mutex> lock(mutex_);
    life_.touch(now());
}

void UploadApi::progress_begin(std::string_view name, std::uint64_t total) {
    const std::lock_guard<std::mutex> lock(mutex_);
    progress_.active = true;
    progress_.name = std::string(name);
    progress_.received = 0;
    progress_.total = total;
}

void UploadApi::progress_add(std::size_t bytes) {
    const std::lock_guard<std::mutex> lock(mutex_);
    progress_.received += bytes;
}

void UploadApi::progress_end(bool stored) {
    const std::lock_guard<std::mutex> lock(mutex_);
    progress_.active = false;
    ++(stored ? progress_.completed : progress_.failed);
}

void UploadApi::progress_idle() {
    const std::lock_guard<std::mutex> lock(mutex_);
    progress_.active = false;
}

UploadProgress UploadApi::progress() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

ApiReply UploadApi::uploaded(const std::string& name, const std::string& saved_as,
                             std::size_t ignored) {
    ApiReply r;
    r.body = "{\"ok\":true,\"name\":" + json_string(name) +
             ",\"saved_as\":" + json_string(saved_as) +
             (ignored > 0 ? ",\"ignored\":" + std::to_string(ignored) : std::string()) + "}";
    return r;
}

ApiReply UploadApi::refused(const ServiceError& error) {
    return error_reply(error.status, error.message);
}

} // namespace etude::app
