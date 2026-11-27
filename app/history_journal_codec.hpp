// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The journal's byte codec: the record kinds, the writers of one field, the reader of a payload and
// the frame check. Shared by the encoder and the decoder.

#include "history_journal.hpp"

#include "etude/content_hash.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace etude::app::journal_codec {

inline constexpr char kKindRun = 'R';
inline constexpr char kKindPractice = 'P';
inline constexpr char kKindPrefs = 'S';
inline constexpr char kKindGroups = 'G';
inline constexpr char kKindOrigins = 'O';
inline constexpr char kKindVersions = 'V';
inline constexpr char kKindPin = 'N';
inline constexpr char kKindProfiles = 'F';
inline constexpr char kKindProfileSettings = 'T';
inline constexpr char kKindProfileDelete = 'D';

inline void put_u32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
}

inline void put_u64(std::string& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
}

inline void put_varint(std::string& out, std::uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<char>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

inline void put_zigzag(std::string& out, std::int64_t v) {
    put_varint(out, (static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63));
}

inline void put_bytes(std::string& out, std::string_view bytes) {
    put_varint(out, bytes.size());
    out += bytes;
}

// A text cut to kMaxJournalText at a character boundary.
inline void put_text(std::string& out, std::string_view text) {
    if (text.size() > kMaxJournalText) {
        std::size_t end = kMaxJournalText;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end; // not inside a UTF-8 sequence
        }
        text = text.substr(0, end);
    }
    put_bytes(out, text);
}

inline std::uint32_t check_of(std::string_view payload) {
    return static_cast<std::uint32_t>(content_hash(payload) & 0xFFFFFFFFu);
}

// Reads a payload; every read says whether the bytes were there.
class Reader {
  public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}
    bool done() const { return at_ >= bytes_.size(); }

    bool byte(std::uint8_t& out) {
        if (done()) {
            return false;
        }
        out = static_cast<std::uint8_t>(bytes_[at_++]);
        return true;
    }
    bool u64(std::uint64_t& out) {
        if (bytes_.size() - at_ < 8) {
            return false;
        }
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes_[at_++])) << (8 * i);
        }
        return true;
    }
    bool varint(std::uint64_t& out) {
        out = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            std::uint8_t b = 0;
            if (!byte(b)) {
                return false;
            }
            if (shift == 63 && (b & 0x7E) != 0) {
                return false; // more than 64 bits
            }
            out |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }
    bool zigzag(std::int64_t& out) {
        std::uint64_t v = 0;
        if (!varint(v)) {
            return false;
        }
        out = static_cast<std::int64_t>(v >> 1) ^ -static_cast<std::int64_t>(v & 1);
        return true;
    }
    // A zigzag that has to fit an int.
    bool integer(int& out) {
        std::int64_t v = 0;
        if (!zigzag(v) || v < -2'000'000'000 || v > 2'000'000'000) {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    }
    bool flag(bool& out) {
        std::uint8_t b = 0;
        if (!byte(b) || b > 1) {
            return false;
        }
        out = b == 1;
        return true;
    }
    bool bytes(std::string_view& out) {
        std::uint64_t n = 0;
        if (!varint(n) || n > bytes_.size() - at_) {
            return false;
        }
        out = bytes_.substr(at_, static_cast<std::size_t>(n));
        at_ += static_cast<std::size_t>(n);
        return true;
    }

  private:
    std::string_view bytes_;
    std::size_t at_ = 0;
};

inline std::uint32_t get_u32(std::string_view bytes, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(
                 static_cast<unsigned char>(bytes[at + static_cast<std::size_t>(i)]))
             << (8 * i);
    }
    return v;
}

} // namespace etude::app::journal_codec
