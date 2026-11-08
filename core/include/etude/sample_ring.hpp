// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace etude {

// Ring buffer of mono samples from exactly one producer thread (an audio capture callback) to
// exactly one consumer thread (the analysis loop). write() and read() never block, lock or
// allocate; the storage is allocated once by the constructor.
//
// A block that does not fit is dropped whole, never partly written, so the consumer never sees a
// glitch inside a block, and its samples are counted in dropped_samples().
//
// Memory ordering: the producer copies a block into the free part, then publishes it with a
// release store of tail_; the consumer's acquire load of tail_ makes the samples visible before it
// reads them. The consumer copies samples out, then frees them with a release store of head_; the
// producer's acquire load of head_ guarantees it never overwrites samples still being read. Each
// index has one writer, so plain load and store are enough.
class SampleRing {
  public:
    // The capacity is `min_capacity` rounded up to a power of two (at least 2).
    explicit SampleRing(std::size_t min_capacity);

    std::size_t capacity() const { return buffer_.size(); }

    // Producer thread only. All or nothing: false (and the samples counted as dropped) when
    // fewer than `count` samples are free.
    bool write(const float* samples, std::size_t count) noexcept;

    // Consumer thread only. Copies up to `max` samples out, oldest first; returns how many.
    std::size_t read(float* out, std::size_t max) noexcept;

    // Samples waiting. Exact for the consumer, a lower bound for anyone else.
    std::size_t available() const noexcept {
        return static_cast<std::size_t>(tail_.load(std::memory_order_acquire) -
                                        head_.load(std::memory_order_acquire));
    }

    // Any thread; the values may lag slightly.
    std::uint64_t dropped_samples() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    std::uint64_t dropped_blocks() const noexcept {
        return dropped_blocks_.load(std::memory_order_relaxed);
    }

  private:
    static constexpr std::size_t kCacheLine = 64;

    std::vector<float> buffer_;
    std::size_t mask_;
    // Consumer side.
    alignas(kCacheLine) std::atomic<std::uint64_t> head_{0};
    // Producer side.
    alignas(kCacheLine) std::atomic<std::uint64_t> tail_{0};
    alignas(kCacheLine) std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> dropped_blocks_{0};
};

} // namespace etude
