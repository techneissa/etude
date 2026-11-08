// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/upload_auth.hpp"

#include <algorithm>
#include <cstdio>

namespace etude {

namespace {

// Equal text in time that depends on the length only, so a wrong PIN's first right digits are
// not told by how fast the answer came.
bool same_text(std::string_view a, std::string_view b) {
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        diff = static_cast<unsigned char>(
            diff | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
    }
    return diff == 0;
}

} // namespace

std::string pin_from_random(std::uint32_t random) {
    char text[8];
    std::snprintf(text, sizeof text, "%04u", random % 10000u);
    return text;
}

bool is_pin_text(std::string_view text) {
    return text.size() == kPinDigits &&
           std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool is_token_text(std::string_view text) {
    return text.size() == kTokenChars && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

std::string token_from_bytes(const std::array<std::uint8_t, 16>& bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string token;
    token.reserve(kTokenChars);
    for (const std::uint8_t b : bytes) {
        token.push_back(kDigits[b >> 4]);
        token.push_back(kDigits[b & 0x0F]);
    }
    return token;
}

void UploadAuth::drop_expired(UploadMs now) {
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [now](const Session& s) { return now - s.last >= kSessionIdleMs; }),
        sessions_.end());
}

LoginAnswer UploadAuth::login(std::string_view pin, std::string_view fresh_token, UploadMs now) {
    LoginAnswer answer;
    if (failures_ >= kMaxPinFailures) {
        if (now < locked_until_) {
            answer.result = LoginResult::Locked;
            answer.retry_ms = locked_until_ - now;
            return answer;
        }
        failures_ = 0; // the lock ran out: five more tries
    }
    const bool format_ok = is_pin_text(pin);
    // Compared even when the format is wrong, so every answer takes the same steps.
    const bool match = same_text(format_ok ? pin : std::string_view("----"), pin_);
    if (format_ok && match && !is_token_text(fresh_token)) {
        answer.result = LoginResult::BadFormat; // the caller's token, not the player's PIN
        return answer;
    }
    if (!format_ok || !match) {
        if (++failures_ >= kMaxPinFailures) {
            locked_until_ = now + kPinLockMs;
        }
        answer.result = format_ok ? LoginResult::WrongPin : LoginResult::BadFormat;
        return answer;
    }
    failures_ = 0;
    drop_expired(now);
    if (sessions_.size() >= kMaxSessions) {
        sessions_.erase(sessions_.begin());
    }
    sessions_.push_back({std::string(fresh_token), now});
    answer.result = LoginResult::Ok;
    answer.token = std::string(fresh_token);
    return answer;
}

bool UploadAuth::check(std::string_view token, UploadMs now) {
    drop_expired(now);
    if (!is_token_text(token)) {
        return false;
    }
    for (Session& s : sessions_) {
        if (same_text(s.token, token)) {
            s.last = now;
            return true;
        }
    }
    return false;
}

void UploadAuth::logout(std::string_view token) {
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [&](const Session& s) { return same_text(s.token, token); }),
                    sessions_.end());
}

void UploadAuth::clear() {
    sessions_.clear();
    failures_ = 0;
    locked_until_ = 0;
}

void UploadLife::start(UploadMs now) {
    running_ = true;
    failed_ = false;
    transfers_ = 0;
    last_use_ = now;
    reason_.clear();
}

void UploadLife::stop() {
    running_ = false;
    transfers_ = 0;
}

void UploadLife::fail(std::string reason) {
    running_ = false;
    failed_ = true;
    transfers_ = 0;
    reason_ = std::move(reason);
}

void UploadLife::transfer_begin(UploadMs now) {
    ++transfers_;
    last_use_ = now;
}

void UploadLife::transfer_end(UploadMs now) {
    transfers_ = std::max(0, transfers_ - 1);
    last_use_ = now;
}

bool UploadLife::tick(UploadMs now) {
    if (!running_ || transfers_ > 0 || now - last_use_ < kServerIdleMs) {
        return false;
    }
    running_ = false;
    reason_ = "Turned off after 10 minutes unused";
    return true;
}

UploadStatus UploadLife::status() const {
    if (failed_) {
        return UploadStatus::Error;
    }
    if (!running_) {
        return UploadStatus::Off;
    }
    return transfers_ > 0 ? UploadStatus::Receiving : UploadStatus::Ready;
}

UploadMs UploadLife::remaining_ms(UploadMs now) const {
    if (!running_) {
        return 0;
    }
    return std::max<UploadMs>(0, kServerIdleMs - (now - last_use_));
}

} // namespace etude
