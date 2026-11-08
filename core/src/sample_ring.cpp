// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sample_ring.hpp"

#include <algorithm>
#include <cstring>

namespace etude {

namespace {
std::size_t round_up_pow2(std::size_t n) {
    std::size_t p = 2;
    while (p < n) {
        p <<= 1;
    }
    return p;
}
} // namespace

SampleRing::SampleRing(std::size_t min_capacity)
    : buffer_(round_up_pow2(min_capacity), 0.0f), mask_(buffer_.size() - 1) {}

bool SampleRing::write(const float* samples, std::size_t count) noexcept {
    if (count == 0) {
        return true;
    }
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    if (samples == nullptr || count > buffer_.size() - static_cast<std::size_t>(tail - head)) {
        dropped_.fetch_add(count, std::memory_order_relaxed);
        dropped_blocks_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const std::size_t start = static_cast<std::size_t>(tail) & mask_;
    const std::size_t first = std::min(count, buffer_.size() - start);
    std::memcpy(buffer_.data() + start, samples, first * sizeof(float));
    std::memcpy(buffer_.data(), samples + first, (count - first) * sizeof(float));
    tail_.store(tail + count, std::memory_order_release);
    return true;
}

std::size_t SampleRing::read(float* out, std::size_t max) noexcept {
    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    const std::uint64_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t count = std::min(max, static_cast<std::size_t>(tail - head));
    if (count == 0 || out == nullptr) {
        return 0;
    }
    const std::size_t start = static_cast<std::size_t>(head) & mask_;
    const std::size_t first = std::min(count, buffer_.size() - start);
    std::memcpy(out, buffer_.data() + start, first * sizeof(float));
    std::memcpy(out + first, buffer_.data(), (count - first) * sizeof(float));
    head_.store(head + count, std::memory_order_release);
    return count;
}

} // namespace etude
