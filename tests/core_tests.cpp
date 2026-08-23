#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webview-gui/helpers.h"
#include "webview-gui/_impl/plugin_support.h"

#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

using namespace webview_gui;

TEST_CASE("Base64 helpers preserve arbitrary binary payloads")
{
    for (size_t length = 0; length <= 4096; ++length) {
        CAPTURE(length);

        std::vector<unsigned char> input(length);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<unsigned char>((i * 131u + length * 17u) & 0xffu);

        const auto encoded = helpers::encodeBase64(input.data(), input.size());
        const auto decoded = helpers::decodeBase64(encoded.c_str());
        CHECK(decoded == input);
    }
}

TEST_CASE("ThreadAffinity is unbound until explicitly attached to the GUI thread")
{
    detail::ThreadAffinity affinity;
    CHECK_FALSE(affinity.isBound());
    CHECK(affinity.isCurrentThread());

    affinity.bindToCurrentThread();
    CHECK(affinity.isBound());
    CHECK(affinity.isCurrentThread());
}

TEST_CASE("ThreadAffinity rejects a different worker/audio thread")
{
    detail::ThreadAffinity affinity;
    affinity.bindToCurrentThread();

    std::atomic<bool> workerAccepted{true};
    std::thread worker([&] {
        workerAccepted.store(affinity.isCurrentThread(), std::memory_order_relaxed);
    });
    worker.join();

    CHECK_FALSE(workerAccepted.load(std::memory_order_relaxed));
    CHECK(affinity.isCurrentThread());
}

TEST_CASE("PointerRegistry lookup misses never mutate the registry")
{
    detail::PointerRegistry<int> registry;
    int key = 0;

    CHECK(registry.size() == 0);
    CHECK(registry.find(&key) == nullptr);
    CHECK(registry.size() == 0);
}

TEST_CASE("PointerRegistry keeps instances isolated and removes only the matching owner")
{
    detail::PointerRegistry<int> registry;
    int keyA = 0;
    int keyB = 0;
    int valueA = 11;
    int valueB = 22;

    registry.set(&keyA, &valueA);
    registry.set(&keyB, &valueB);

    CHECK(registry.find(&keyA) == &valueA);
    CHECK(registry.find(&keyB) == &valueB);
    CHECK(registry.size() == 2);

    registry.set(&keyA, &valueB);
    CHECK_FALSE(registry.eraseIfMatches(&keyA, &valueA));
    CHECK(registry.find(&keyA) == &valueB);
    CHECK(registry.eraseIfMatches(&keyA, &valueB));
    CHECK(registry.find(&keyA) == nullptr);
}

TEST_CASE("PointerRegistry tolerates concurrent readers while registrations change")
{
    detail::PointerRegistry<int> registry;
    int key = 0;
    int value = 22;
    std::atomic<bool> stop{false};

    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            (void)registry.find(&key);
            (void)registry.size();
        }
    });

    for (int i = 0; i < 10000; ++i) {
        registry.set(&key, &value);
        CHECK(registry.find(&key) == &value);
        registry.eraseIfMatches(&key, &value);
    }

    stop.store(true, std::memory_order_relaxed);
    reader.join();
}

TEST_CASE("Resource paths remain inside the configured root")
{
    const auto root = std::filesystem::path("/tmp/webview-gui-root");
    std::filesystem::path resolved;

    CHECK(detail::resolveContainedPath(root, "/assets/ui.js", resolved));
    CHECK(resolved == (root / "assets/ui.js").lexically_normal());

    CHECK(detail::resolveContainedPath(root, "images/../images/knob.png", resolved));
    CHECK(resolved == (root / "images/knob.png").lexically_normal());
}

TEST_CASE("Resource path traversal is rejected")
{
    const auto root = std::filesystem::path("/tmp/webview-gui-root");
    std::filesystem::path resolved;

    CHECK_FALSE(detail::resolveContainedPath(root, "../secret.txt", resolved));
    CHECK_FALSE(detail::resolveContainedPath(root, "/../../secret.txt", resolved));
    CHECK_FALSE(detail::resolveContainedPath(root, "assets/../../../secret.txt", resolved));
}
