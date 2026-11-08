// SPDX-License-Identifier: GPL-3.0-or-later
// Who may use the phone upload page and for how long: the 4-digit PIN shown on the Pi's screen,
// the session cookie a right PIN earns, the lock after wrong PINs, and the server's own life (off
// by default, off again after ten idle minutes). Pure state with the time and the random numbers
// handed in, so it is tested without a clock, a socket or a random source.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace etude {

// Milliseconds on any steady clock.
using UploadMs = std::int64_t;

inline constexpr std::size_t kPinDigits = 4;
// Five wrong PINs in a row lock the PIN for a minute; a right PIN or the end of the lock starts the
// count again. With a new PIN each time the server starts and ten idle minutes to its end, a
// guesser gets a few dozen tries of ten thousand.
inline constexpr int kMaxPinFailures = 5;
inline constexpr UploadMs kPinLockMs = 60'000;
// A session ends after this long without a request, and the server after this long without
// anything the page did.
inline constexpr UploadMs kSessionIdleMs = 10 * 60'000;
inline constexpr UploadMs kServerIdleMs = 10 * 60'000;
// Phones and tablets in the room: more than this many open sessions drops the oldest.
inline constexpr std::size_t kMaxSessions = 8;
// A session token is 32 lower-case hex digits (128 random bits).
inline constexpr std::size_t kTokenChars = 32;

// The PIN for a random number: four digits, leading zeros kept ("0042").
std::string pin_from_random(std::uint32_t random);
bool is_pin_text(std::string_view text);
bool is_token_text(std::string_view text);
// The token for 16 random bytes.
std::string token_from_bytes(const std::array<std::uint8_t, 16>& bytes);

enum class LoginResult : std::uint8_t {
    Ok,
    WrongPin,
    Locked,    // too many wrong PINs: wait `retry_ms`
    BadFormat, // not four digits (counts as a wrong PIN)
};

struct LoginAnswer {
    LoginResult result = LoginResult::WrongPin;
    std::string token;     // Ok: the session cookie's value
    UploadMs retry_ms = 0; // Locked: how long until another try
};

// The PIN gate and the sessions. One per server run, with the PIN made at start.
class UploadAuth {
  public:
    explicit UploadAuth(std::string pin) : pin_(std::move(pin)) {}

    // A PIN try. `fresh_token` is only used when the PIN is right (random bytes from the caller).
    LoginAnswer login(std::string_view pin, std::string_view fresh_token, UploadMs now);
    // True when `token` is a live session, which it then keeps alive.
    bool check(std::string_view token, UploadMs now);
    void logout(std::string_view token);
    void clear();
    std::size_t sessions() const { return sessions_.size(); }
    const std::string& pin() const { return pin_; }

  private:
    struct Session {
        std::string token;
        UploadMs last = 0;
    };
    void drop_expired(UploadMs now);

    std::string pin_;
    std::vector<Session> sessions_;
    int failures_ = 0;
    UploadMs locked_until_ = 0;
};

// Whether the server is running, and when it ends.
enum class UploadStatus : std::uint8_t {
    Off,       // not running (the default)
    Ready,     // running, nothing arriving
    Receiving, // a file is arriving
    Error,     // could not start or stopped on a failure: `UploadLife::reason`
};

class UploadLife {
  public:
    void start(UploadMs now);
    void stop();
    void fail(std::string reason);
    // Anything the page did that counts as use: a request with a live session, a chunk arriving.
    void touch(UploadMs now) { last_use_ = now; }
    void transfer_begin(UploadMs now);
    void transfer_end(UploadMs now);
    // True once, when the server has been idle for kServerIdleMs (a transfer in progress is use):
    // the caller stops it, and the reason reads "Turned off after 10 minutes unused".
    bool tick(UploadMs now);

    UploadStatus status() const;
    // Milliseconds until the idle end, for a countdown (0 when not running).
    UploadMs remaining_ms(UploadMs now) const;
    const std::string& reason() const { return reason_; }

  private:
    bool running_ = false;
    bool failed_ = false;
    int transfers_ = 0;
    UploadMs last_use_ = 0;
    std::string reason_;
};

} // namespace etude
