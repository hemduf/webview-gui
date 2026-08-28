#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webview-gui/_impl/callback_registry.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

constexpr bool isThreadSanitizerBuild() noexcept
{
#if defined(__has_feature)
#  if __has_feature(thread_sanitizer)
    return true;
#  endif
#endif
#if defined(__SANITIZE_THREAD__)
    return true;
#else
    return false;
#endif
}

struct Payload {
    std::atomic<std::uint64_t> visits{0};
};

} // namespace

TEST_CASE("callback registry stress target is ThreadSanitizer instrumented")
{
    CHECK(isThreadSanitizerBuild());
}

TEST_CASE("callback registry is race-free under concurrent visit set and erase stress")
{
    constexpr std::size_t slotCount = 8;
    constexpr std::size_t iterations = 20000;

    webview_gui::detail::CallbackRegistry<Payload> registry;
    std::array<int, slotCount> keys{};
    std::array<Payload, slotCount> payloads{};

    for (std::size_t i = 0; i < slotCount; ++i)
        registry.set(&keys[i], &payloads[i]);

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    const auto reader = [&](std::size_t seed) {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();

        for (std::size_t i = 0; i < iterations; ++i) {
            const auto index = (i * 5u + seed) % slotCount;
            registry.visit(&keys[index], [&](Payload& payload) {
                payload.visits.fetch_add(1, std::memory_order_relaxed);

                const auto nestedIndex = (index + 1u) % slotCount;
                registry.visit(&keys[nestedIndex], [](Payload& nested) {
                    nested.visits.fetch_add(1, std::memory_order_relaxed);
                });
            });
        }
    };

    const auto writer = [&](std::size_t seed) {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();

        for (std::size_t i = 0; i < iterations; ++i) {
            const auto index = (i * 3u + seed) % slotCount;
            if ((i + seed) % 3u == 0u)
                registry.eraseIfMatches(&keys[index], &payloads[index]);
            else
                registry.set(&keys[index], &payloads[index]);
        }
    };

    threads.emplace_back(reader, 0u);
    threads.emplace_back(reader, 1u);
    threads.emplace_back(reader, 2u);
    threads.emplace_back(reader, 3u);
    threads.emplace_back(writer, 0u);
    threads.emplace_back(writer, 1u);

    start.store(true, std::memory_order_release);

    for (auto& thread : threads)
        thread.join();

    for (std::size_t i = 0; i < slotCount; ++i) {
        registry.set(&keys[i], &payloads[i]);
        REQUIRE(registry.eraseIfMatches(&keys[i], &payloads[i]));
    }

    CHECK(registry.size() == 0u);

    std::uint64_t totalVisits = 0;
    for (const auto& payload : payloads)
        totalVisits += payload.visits.load(std::memory_order_relaxed);
    CHECK(totalVisits > 0u);
}

TEST_CASE("callback registry reentrant teardown drains foreign visitors without waiting for itself")
{
    webview_gui::detail::CallbackRegistry<Payload> registry;
    int key = 0;
    Payload initial;
    Payload replacement;
    registry.set(&key, &initial);

    std::atomic<bool> foreignEntered{false};
    std::atomic<bool> releaseForeign{false};
    std::atomic<bool> foreignExited{false};
    std::atomic<bool> foreignVisitReturned{false};
    std::atomic<bool> reentrantEraseStarted{false};

    std::thread foreignVisitor([&] {
        const auto visited = registry.visit(&key, [&](Payload& payload) {
            payload.visits.fetch_add(1, std::memory_order_relaxed);
            foreignEntered.store(true, std::memory_order_release);
            while (!releaseForeign.load(std::memory_order_acquire))
                std::this_thread::yield();
            foreignExited.store(true, std::memory_order_release);
        });
        foreignVisitReturned.store(visited, std::memory_order_release);
    });

    while (!foreignEntered.load(std::memory_order_acquire))
        std::this_thread::yield();

    std::thread releaser([&] {
        while (!reentrantEraseStarted.load(std::memory_order_acquire))
            std::this_thread::yield();
        releaseForeign.store(true, std::memory_order_release);
    });

    bool erased = false;
    const auto selfVisited = registry.visit(&key, [&](Payload& payload) {
        reentrantEraseStarted.store(true, std::memory_order_release);
        erased = registry.eraseIfMatches(&key, &payload);
        CHECK(foreignExited.load(std::memory_order_acquire));
    });

    releaser.join();
    foreignVisitor.join();

    CHECK(selfVisited);
    CHECK(erased);
    CHECK(foreignVisitReturned.load(std::memory_order_acquire));
    CHECK_FALSE(registry.visit(&key, [](Payload&) {}));

    registry.set(&key, &initial);
    bool replaced = false;
    CHECK(registry.visit(&key, [&](Payload& payload) {
        CHECK(&payload == &initial);
        registry.set(&key, &replacement);
        replaced = true;
    }));
    CHECK(replaced);

    bool sawReplacement = false;
    CHECK(registry.visit(&key, [&](Payload& payload) {
        sawReplacement = &payload == &replacement;
    }));
    CHECK(sawReplacement);
}
