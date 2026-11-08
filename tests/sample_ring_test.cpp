// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sample_ring.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace etude;

TEST_CASE("sample ring: the capacity is a power of two and samples come out in order") {
    SampleRing ring(100);
    CHECK(ring.capacity() == 128);
    std::vector<float> in(60);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<float>(i);
    }
    CHECK(ring.write(in.data(), in.size()));
    CHECK(ring.available() == 60);
    std::vector<float> out(100, -1.0f);
    CHECK(ring.read(out.data(), 40) == 40);
    for (std::size_t i = 0; i < 40; ++i) {
        CHECK(out[i] == static_cast<float>(i));
    }
    CHECK(ring.read(out.data(), 100) == 20);
    CHECK(out[0] == 40.0f);
    CHECK(ring.read(out.data(), 100) == 0);
}

TEST_CASE("sample ring: a block that wraps round the end is intact") {
    SampleRing ring(64);
    std::vector<float> a(50, 1.0f);
    std::vector<float> b(40);
    for (std::size_t i = 0; i < b.size(); ++i) {
        b[i] = 100.0f + static_cast<float>(i);
    }
    std::vector<float> out(64);
    REQUIRE(ring.write(a.data(), a.size()));
    REQUIRE(ring.read(out.data(), 50) == 50);
    REQUIRE(ring.write(b.data(), b.size())); // starts at 50, wraps at 64
    REQUIRE(ring.read(out.data(), 64) == 40);
    for (std::size_t i = 0; i < 40; ++i) {
        CHECK(out[i] == 100.0f + static_cast<float>(i));
    }
}

TEST_CASE("sample ring: a block that does not fit is dropped whole and counted") {
    SampleRing ring(64);
    std::vector<float> a(50, 1.0f);
    std::vector<float> b(20, 2.0f);
    CHECK(ring.write(a.data(), a.size()));
    CHECK_FALSE(ring.write(b.data(), b.size())); // 14 free
    CHECK(ring.dropped_samples() == 20);
    CHECK(ring.dropped_blocks() == 1);
    CHECK(ring.available() == 50); // nothing of b went in
    std::vector<float> out(64);
    CHECK(ring.read(out.data(), 64) == 50);
    CHECK(ring.write(b.data(), b.size())); // room again
}

TEST_CASE("sample ring: a producer thread and a consumer thread lose nothing and keep order") {
    // The producer writes 1,2,3,... in blocks of varying size and waits when the ring is full;
    // the consumer checks the sequence. Run under ThreadSanitizer this also checks the hand-off.
    constexpr std::size_t kTotal = 400'000;
    SampleRing ring(1024);
    std::atomic<bool> bad{false};
    std::thread producer([&] {
        std::vector<float> block;
        std::size_t next = 0;
        std::size_t size = 1;
        while (next < kTotal) {
            const std::size_t n = std::min(size, kTotal - next);
            block.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                block[i] = static_cast<float>(next + i);
            }
            while (!ring.write(block.data(), n)) {
                std::this_thread::yield();
            }
            next += n;
            size = size % 700 + 3;
        }
    });
    std::thread consumer([&] {
        std::vector<float> out(300);
        std::size_t expect = 0;
        while (expect < kTotal) {
            const std::size_t n = ring.read(out.data(), out.size());
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (out[i] != static_cast<float>(expect + i)) {
                    bad = true;
                }
            }
            expect += n;
        }
    });
    producer.join();
    consumer.join();
    CHECK_FALSE(bad.load());
}
