// SPDX-License-Identifier: GPL-3.0-or-later
// Text for the per-frame UI without heap allocation: `FitText` is a line in a fixed buffer and
// `FitCache` remembers, per (font, size, width, text), the line cut to the width with an ellipsis,
// so a label is measured only when one of those changes. Plain C++ with the measuring function
// passed in, so it is tested without a font.
#pragma once

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace etude::app::ui {

// The longest line the UI shows whole, bytes without the terminator. Longer text is cut to this
// (at a character boundary) and ends in an ellipsis.
inline constexpr std::size_t kFitMax = 255;

inline constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

// A line of text in a buffer of its own, always NUL-terminated.
class FitText {
  public:
    FitText() { chars_[0] = '\0'; }

    const char* c_str() const { return chars_.data(); }
    std::string_view view() const { return {chars_.data(), size_}; }
    operator std::string_view() const { return view(); }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    friend bool operator==(const FitText& a, std::string_view b) { return a.view() == b; }

    // Replaces the text with `text`, cut at a character boundary if it does not fit. Returns
    // false when something was cut.
    bool assign(std::string_view text) {
        const std::string_view kept = cut_to(text, kFitMax);
        size_ = kept.size();
        std::memcpy(chars_.data(), kept.data(), size_);
        chars_[size_] = '\0';
        return kept.size() == text.size();
    }

    // Adds `text` at the end while it fits; what does not fit is dropped. Returns false when
    // something was cut.
    bool append(std::string_view text) {
        const std::string_view kept = cut_to(text, kFitMax - size_);
        std::memcpy(chars_.data() + size_, kept.data(), kept.size());
        size_ += kept.size();
        chars_[size_] = '\0';
        return kept.size() == text.size();
    }

    // Makes the line end in an ellipsis, in place of its last characters if the buffer is full.
    void end_with_ellipsis() {
        size_ = cut_to(view(), kFitMax - kEllipsis.size()).size();
        chars_[size_] = '\0';
        append(kEllipsis);
    }

    void clear() {
        size_ = 0;
        chars_[0] = '\0';
    }

    // Replaces the text with printf-style output, cut to the buffer.
    void format(const char* format, ...) __attribute__((format(printf, 2, 3))) {
        va_list args;
        va_start(args, format);
        const int n = std::vsnprintf(chars_.data(), chars_.size(), format, args);
        va_end(args);
        if (n < 0) {
            size_ = 0;
        } else if (static_cast<std::size_t>(n) < chars_.size()) {
            size_ = static_cast<std::size_t>(n);
        } else {
            // The output was cut at the buffer: drop a character the cut split.
            size_ = kFitMax;
            std::size_t lead = size_ - 1;
            while (lead > 0 && (static_cast<unsigned char>(chars_[lead]) & 0xC0) == 0x80) {
                --lead;
            }
            const auto first = static_cast<unsigned char>(chars_[lead]);
            const std::size_t length = first >= 0xF0   ? 4
                                       : first >= 0xE0 ? 3
                                       : first >= 0xC0 ? 2
                                                       : 1;
            if (lead + length > size_) {
                size_ = lead;
            }
        }
        chars_[size_] = '\0';
    }

    // `text` cut to at most `bytes` bytes, not inside a UTF-8 character.
    static std::string_view cut_to(std::string_view text, std::size_t bytes) {
        if (text.size() <= bytes) {
            return text;
        }
        while (bytes > 0 && (static_cast<unsigned char>(text[bytes]) & 0xC0) == 0x80) {
            --bytes;
        }
        return text.substr(0, bytes);
    }

  private:
    std::array<char, kFitMax + 1> chars_;
    std::size_t size_ = 0;
};

// `text` cut at its end to `max_w` with an ellipsis, or whole when it fits; empty when not even the
// ellipsis fits. `measure(const char*)` is the width of a NUL-terminated line.
template <class Measure>
FitText shorten_line(std::string_view text, float max_w, Measure& measure) {
    FitText out;
    if (text.size() <= kFitMax) {
        out.assign(text);
        if (measure(out.c_str()) <= max_w) {
            return out;
        }
    }
    // What is shown with an ellipsis must leave room for it in the buffer.
    std::string_view body = FitText::cut_to(text, kFitMax - kEllipsis.size());

    // Drop one character at a time until the rest and an ellipsis fit. Text longer than the
    // buffer tries its cut to the buffer first.
    bool first = text.size() > kFitMax;
    while (first || !body.empty()) {
        if (!first) {
            std::size_t cut = body.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(body[cut]) & 0xC0) == 0x80) {
                --cut;
            }
            body = body.substr(0, cut);
        }
        first = false;
        out.assign(body);
        out.append(kEllipsis);
        if (measure(out.c_str()) <= max_w) {
            return out;
        }
    }
    out.clear();
    return out;
}

// The cache: sets of a few ways each; a miss takes a free way, else the set's ways in turn. Built
// once; a lookup that hits copies one line and allocates nothing.
class FitCache {
  public:
    FitCache() : entries_(std::make_unique<Entry[]>(kSets * kWays)) {}

    // Forgets every line (the font pointers they are keyed by are about to be dead).
    void clear() {
        entries_ = std::make_unique<Entry[]>(kSets * kWays);
        next_victim_.fill(0);
    }

    // `text` cut to `max_w` with an ellipsis, or whole when it fits. `measure(const char*)` is the
    // width of a NUL-terminated line in `font` at `size`.
    template <class Measure>
    FitText fit(const void* font, float size, std::string_view text, float max_w,
                Measure&& measure) {
        const std::uint64_t hash = hash_of(font, size, max_w, text);
        const std::size_t set_index = hash % kSets;
        Entry* set = &entries_[set_index * kWays];
        for (std::size_t way = 0; way < kWays; ++way) {
            const Entry& e = set[way];
            if (e.used && e.hash == hash && e.font == font && e.size == size && e.max_w == max_w &&
                e.matches(text)) {
                return e.shown;
            }
        }
        std::size_t victim = kWays;
        for (std::size_t way = 0; way < kWays && victim == kWays; ++way) {
            victim = set[way].used ? kWays : way;
        }
        if (victim == kWays) {
            victim = next_victim_[set_index]++ % kWays;
        }
        Entry& e = set[victim];
        e.used = true;
        e.hash = hash;
        e.font = font;
        e.size = size;
        e.max_w = max_w;
        e.remember(text);
        e.shown = shorten(text, max_w, measure);
        return e.shown;
    }

  private:
    static constexpr std::size_t kSets = 128;
    static constexpr std::size_t kWays = 4;

    struct Entry {
        bool used = false;
        std::uint64_t hash = 0;
        const void* font = nullptr;
        float size = 0.0f;
        float max_w = 0.0f;
        std::size_t length = 0;
        FitText key;          // the text, when it fits the buffer
        std::string long_key; // the whole text, when it does not (set on a miss only)
        FitText shown;

        bool matches(std::string_view text) const {
            return length == text.size() &&
                   (length <= kFitMax ? key.view() == text : long_key == text);
        }
        void remember(std::string_view text) {
            length = text.size();
            if (length <= kFitMax) {
                key.assign(text);
                long_key.clear();
            } else {
                long_key.assign(text);
            }
        }
    };

    static std::uint64_t hash_of(const void* font, float size, float max_w, std::string_view text) {
        std::uint64_t h = 1469598103934665603ull; // FNV-1a
        const auto mix = [&h](const void* p, std::size_t n) {
            const auto* b = static_cast<const unsigned char*>(p);
            for (std::size_t i = 0; i < n; ++i) {
                h = (h ^ b[i]) * 1099511628211ull;
            }
        };
        mix(&font, sizeof font);
        mix(&size, sizeof size);
        mix(&max_w, sizeof max_w);
        mix(text.data(), text.size());
        return h;
    }

    template <class Measure>
    static FitText shorten(std::string_view text, float max_w, Measure& measure) {
        return shorten_line(text, max_w, measure);
    }

    std::unique_ptr<Entry[]> entries_;
    std::array<std::uint8_t, kSets> next_victim_{};
};

// One rule for a name (a song's title) wherever it shows: too long for its room, it is cut at its
// end with an ellipsis. Inside a line (`name_begin`, `name_size` bytes of `text`, as in "Opened
// TITLE, hands split by pitch") the name is what is cut and the words round it stay whole, so the
// title ends where it would alone. When the words round it leave no room for one character of the
// name and the ellipsis, the whole line is cut at its end instead.
template <class Measure>
FitText fit_with_name(std::string_view text, std::size_t name_begin, std::size_t name_size,
                      float max_w, Measure&& measure) {
    name_begin = std::min(name_begin, text.size());
    name_size = std::min(name_size, text.size() - name_begin);
    if (text.size() <= kFitMax) {
        FitText whole;
        whole.assign(text);
        if (measure(whole.c_str()) <= max_w) {
            return whole;
        }
    }
    const std::string_view prefix = text.substr(0, name_begin);
    const std::string_view suffix = text.substr(name_begin + name_size);
    std::string_view name = text.substr(name_begin, name_size);
    while (!name.empty() && prefix.size() + suffix.size() + kEllipsis.size() < kFitMax) {
        std::size_t cut = name.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        name = name.substr(0, cut);
        if (name.empty()) {
            break;
        }
        FitText out;
        out.assign(prefix);
        const bool whole = out.append(name) && out.append(kEllipsis) && out.append(suffix);
        if (whole && measure(out.c_str()) <= max_w) {
            return out;
        }
    }
    return shorten_line(text, max_w, measure);
}

} // namespace etude::app::ui
