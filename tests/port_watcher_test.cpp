// SPDX-License-Identifier: GPL-3.0-or-later
// The MIDI port watcher: counts list changes, reports its first listing, and never makes its owner
// wait for a listing that hangs.
#include "port_watcher.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

using namespace etude::app;
using namespace std::chrono_literals;

namespace {

using Entries = std::vector<PortWatcher::Entry>;

bool wait_until(const std::function<bool()>& done, std::chrono::milliseconds limit = 5s) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

} // namespace

TEST_CASE("port watcher: lists once, then counts only the changes") {
    auto ports = std::make_shared<std::atomic<int>>(1);
    PortWatcher w(
        [ports](std::function<void()>) -> PortWatcher::ListFn {
            return [ports] {
                Entries e;
                for (int i = 0; i < ports->load(); ++i) {
                    e.emplace_back(static_cast<std::uint64_t>(i), "port " + std::to_string(i), "");
                }
                return e;
            };
        },
        5ms, 500ms);
    CHECK(wait_until([&] { return w.listed_once(); }));
    CHECK(wait_until([&] { return w.changes() == 1; })); // the first list differs from "nothing"
    std::this_thread::sleep_for(40ms);
    CHECK(w.changes() == 1); // the same list again is not a change
    ports->store(2);
    CHECK(wait_until([&] { return w.changes() == 2; }));
    ports->store(1);
    CHECK(wait_until([&] { return w.changes() == 3; }));
}

TEST_CASE("port watcher: a change reported by the platform counts, from any thread") {
    auto changed = std::make_shared<std::function<void()>>();
    PortWatcher w(
        [changed](std::function<void()> report) -> PortWatcher::ListFn {
            *changed = std::move(report);
            return [] { return Entries{}; };
        },
        5ms, 500ms);
    CHECK(wait_until([&] { return w.listed_once(); }));
    CHECK(w.changes() == 0);
    std::thread other([&] { (*changed)(); });
    other.join();
    CHECK(w.changes() == 1);
}

TEST_CASE("port watcher: the owner does not wait for a listing that is slow to start") {
    // The first listing is held until the test lets it go (the first CoreMIDI listing takes 150 ms
    // or more); the constructor must return while it is held, and ready() turns true afterwards.
    // A held listing, not a timed one, so a loaded machine cannot finish it before the check.
    struct Hold {
        std::mutex m;
        std::condition_variable cv;
        bool open = false;
    };
    auto hold = std::make_shared<Hold>();
    PortWatcher w(
        [hold](std::function<void()>) -> PortWatcher::ListFn {
            return [hold] {
                std::unique_lock lock(hold->m);
                hold->cv.wait(lock, [&] { return hold->open; });
                return Entries{};
            };
        },
        5ms, 500ms);
    CHECK_FALSE(w.listed_once());
    {
        const std::lock_guard lock(hold->m);
        hold->open = true;
    }
    hold->cv.notify_all();
    CHECK(wait_until([&] { return w.listed_once(); }));
}

TEST_CASE("port watcher: destroying it joins a thread that is idle") {
    const auto start = std::chrono::steady_clock::now();
    {
        PortWatcher w(
            [](std::function<void()>) -> PortWatcher::ListFn { return [] { return Entries{}; }; },
            1h, 500ms); // asleep between listings for an hour
        CHECK(wait_until([&] { return w.listed_once(); }));
        CHECK_FALSE(w.detached());
    }
    CHECK(std::chrono::steady_clock::now() - start < 10s); // woken, not waited out (the hour)
}

TEST_CASE("port watcher: a hung listing is left behind after the timeout, and finishes safely") {
    // The lister blocks until released, as a hung CoreMIDI call would. The owner is destroyed while
    // it is blocked; afterwards the thread is released and runs on with only what it owns. Under
    // ASan and TSan this checks nothing it touches was freed with the owner.
    struct Gate {
        std::mutex m;
        std::condition_variable cv;
        bool open = false;
        std::atomic<bool> inside{false};
        std::atomic<bool> returned{false};
        std::atomic<bool> lister_destroyed{false};
    };
    auto gate = std::make_shared<Gate>();
    struct Lister {
        std::shared_ptr<Gate> gate;
        explicit Lister(std::shared_ptr<Gate> g) : gate(std::move(g)) {}
        Lister(const Lister&) = default;
        Entries operator()() const {
            gate->inside = true;
            std::unique_lock lock(gate->m);
            gate->cv.wait(lock, [this] { return gate->open; });
            gate->returned = true;
            return {};
        }
    };
    const auto start = std::chrono::steady_clock::now();
    {
        PortWatcher w(
            [gate](std::function<void()> report) -> PortWatcher::ListFn {
                // `report` is kept by the lister past the owner's life, as an observer's callback
                // is.
                auto keep = std::make_shared<std::function<void()>>(std::move(report));
                return [keep, lister = Lister(gate)]() {
                    (*keep)();
                    return lister();
                };
            },
            5ms, 100ms);
        CHECK(wait_until([&] { return gate->inside.load(); }));
        CHECK_FALSE(w.listed_once());
    } // the destructor waits 100 ms, then detaches
    const auto waited = std::chrono::steady_clock::now() - start;
    CHECK(waited >= 90ms);
    CHECK(waited < 10s);                 // bounded: it did not wait for the hung listing
    CHECK_FALSE(gate->returned.load()); // and the listing was still hung when it came back
    {
        const std::lock_guard lock(gate->m);
        gate->open = true;
    }
    gate->cv.notify_all();
    CHECK(wait_until([&] { return gate->returned.load(); }));
    // Give the detached thread time to wind down before the test ends (it holds only shared state).
    std::this_thread::sleep_for(100ms);
}

TEST_CASE("port watcher: a change the platform reports wakes it to list again at once") {
    auto changed = std::make_shared<std::function<void()>>();
    auto lists = std::make_shared<std::atomic<int>>(0);
    // The interval is ten seconds: only the report can bring the second listing within the test.
    PortWatcher w(
        [changed, lists](std::function<void()> report) -> PortWatcher::ListFn {
            *changed = std::move(report);
            return [lists] {
                lists->fetch_add(1);
                return Entries{};
            };
        },
        10s, 500ms);
    CHECK(wait_until([&] { return w.listed_once(); }));
    CHECK(lists->load() == 1);
    (*changed)();
    CHECK(wait_until([&] { return lists->load() == 2; }));
    (*changed)();
    CHECK(wait_until([&] { return lists->load() == 3; }));
    CHECK(w.changes() == 2);
    // And leaving does not wait out the interval.
    const auto start = std::chrono::steady_clock::now();
    {
        // w is destroyed at the end of the test case; time a second one for the claim
        PortWatcher quick([](std::function<void()>) -> PortWatcher::ListFn { return [] { return Entries{}; }; },
                          10s, 500ms);
        CHECK(wait_until([&] { return quick.listed_once(); }));
    }
    CHECK(std::chrono::steady_clock::now() - start < 8s); // woken, not waited out (the 10 s)
}

TEST_CASE("handover: the reader gets each published value once, the latest of several") {
    Handover<int> h;
    CHECK(h.poll() == nullptr);
    CHECK(h.serial() == 0);
    h.publish(std::make_shared<const int>(1));
    h.publish(std::make_shared<const int>(2));
    const auto got = h.poll();
    REQUIRE(got != nullptr);
    CHECK(*got == 2); // the first was never seen: only the latest counts
    CHECK(h.poll() == nullptr); // and it is delivered once
    CHECK(h.serial() == 2);
    CHECK(h.seen() == 2);
    h.publish(nullptr); // publishing "nothing" is a publication too
    CHECK(h.serial() == 3);
    CHECK(h.poll() == nullptr); // a null value is delivered as null...
    CHECK(h.seen() == 3);       // ...so only seen() shows that it was taken
    h.publish(std::make_shared<const int>(3));
    REQUIRE(h.poll() != nullptr);
}

TEST_CASE("handover: values only move forward under a busy publisher, and the last one arrives") {
    Handover<int> h;
    std::atomic<bool> stop{false};
    std::thread publisher([&] {
        for (int i = 1; i <= 20000; ++i) {
            h.publish(std::make_shared<const int>(i));
        }
        stop = true;
    });
    int last = 0;
    int polls = 0;
    while (!stop.load()) {
        if (const auto v = h.poll()) {
            CHECK(*v > last); // values only move forward
            last = *v;
        }
        ++polls;
    }
    publisher.join();
    // Once the publisher is done the last value is there for the taking (a busy lock only delays).
    for (int i = 0; i < 100 && last != 20000; ++i) {
        if (const auto v = h.poll()) {
            last = *v;
        }
    }
    CHECK(last == 20000);
    CHECK(polls > 0);
}

namespace {
// A value whose release can be held up, to stand in for a port that is slow to close.
struct SlowRelease {
    SlowRelease(std::atomic<bool>* e, std::atomic<bool>* l, int number)
        : entered(e), let_go(l), n(number) {}
    SlowRelease(const SlowRelease&) = delete;
    SlowRelease& operator=(const SlowRelease&) = delete;
    std::atomic<bool>* entered;
    std::atomic<bool>* let_go;
    int n;
    ~SlowRelease() {
        if (entered != nullptr) {
            entered->store(true);
            while (!let_go->load()) {
                std::this_thread::yield();
            }
        }
    }
};
} // namespace

TEST_CASE("handover: a slow release of the replaced value does not hold the reader off") {
    Handover<SlowRelease> h;
    std::atomic<bool> entered{false};
    std::atomic<bool> let_go{false};
    h.publish(std::make_shared<const SlowRelease>(&entered, &let_go, 1));
    REQUIRE(h.poll() != nullptr); // the reader holds no share of it afterwards
    std::thread publisher(
        [&] { h.publish(std::make_shared<const SlowRelease>(nullptr, nullptr, 2)); });
    // The publisher is now stuck releasing value 1 (or about to be). The reader still gets 2.
    while (!entered.load()) {
        std::this_thread::yield();
    }
    const auto got = h.poll();
    REQUIRE(got != nullptr);
    CHECK(got->n == 2);
    let_go = true;
    publisher.join();
}
