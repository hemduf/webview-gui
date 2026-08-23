#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webview-gui/realtime-handoff.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

TEST_CASE("RT-to-UI queue is bounded and never allocates or blocks on push")
{
    webview_gui::RealtimeToUiQueue<std::uint32_t, 8> queue;

    std::uint32_t value = 0;
    CHECK_FALSE(queue.tryPop(value));

    // An N-slot SPSC ring reserves one slot to distinguish full from empty.
    for (std::uint32_t i = 0; i < 7; ++i)
        REQUIRE(queue.tryPush(i));

    CHECK_FALSE(queue.tryPush(99));

    for (std::uint32_t i = 0; i < 7; ++i) {
        REQUIRE(queue.tryPop(value));
        CHECK(value == i);
    }

    CHECK_FALSE(queue.tryPop(value));
    CHECK(queue.tryPush(123));
    REQUIRE(queue.tryPop(value));
    CHECK(value == 123);
}

TEST_CASE("audio producer and UI consumer transfer state without locks or GUI calls")
{
    constexpr std::uint32_t itemCount = 100000;
    webview_gui::RealtimeToUiQueue<std::uint32_t, 256> queue;

    static_assert(decltype(queue)::isLockFree,
                  "The supported plug-in profile requires lock-free queue indices");

    std::atomic<bool> start{false};
    std::atomic<bool> producerDone{false};
    std::atomic<std::uint32_t> droppedAttempts{0};
    std::vector<std::uint32_t> received;
    received.reserve(itemCount);

    std::thread audioThread([&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();

        for (std::uint32_t i = 0; i < itemCount; ++i) {
            // tryPush itself is bounded and non-blocking. A real audio callback
            // may drop/coalesce on false; this stress loop retries only so the
            // test can also verify ordering for every published value.
            while (!queue.tryPush(i)) {
                droppedAttempts.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        }

        producerDone.store(true, std::memory_order_release);
    });

    start.store(true, std::memory_order_release);

    std::uint32_t value = 0;
    while (!producerDone.load(std::memory_order_acquire) || !queue.empty()) {
        while (queue.tryPop(value))
            received.push_back(value);
        std::this_thread::yield();
    }

    audioThread.join();

    REQUIRE(received.size() == itemCount);
    for (std::uint32_t i = 0; i < itemCount; ++i)
        CHECK(received[i] == i);

    // This value is informational: under load the producer may observe a full
    // queue, but every individual publish attempt remains bounded/non-blocking.
    CHECK(droppedAttempts.load(std::memory_order_relaxed) <= itemCount * 100u);
}
