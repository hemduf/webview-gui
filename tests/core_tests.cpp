#include "webview-gui/helpers.h"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace webview_gui;

static void testBase64RoundTrip()
{
    for (size_t length = 0; length <= 4096; ++length) {
        std::vector<unsigned char> input(length);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<unsigned char>((i * 131u + length * 17u) & 0xffu);

        const auto encoded = helpers::encodeBase64(input.data(), input.size());
        const auto decoded = helpers::decodeBase64(encoded.c_str());
        assert(decoded == input);
    }
}

static void testPointerRegistry()
{
    helpers::PointerRegistry<int> registry;
    int keyA = 0;
    int keyB = 0;
    int valueA = 11;
    int valueB = 22;

    assert(registry.size() == 0);
    assert(registry.find(&keyA) == nullptr);
    assert(registry.size() == 0); // a miss must never mutate the registry

    registry.set(&keyA, &valueA);
    registry.set(&keyB, &valueB);
    assert(registry.find(&keyA) == &valueA);
    assert(registry.find(&keyB) == &valueB);
    assert(registry.size() == 2);

    // A stale owner must not be able to erase a replacement mapping.
    registry.set(&keyA, &valueB);
    assert(!registry.eraseIfMatches(&keyA, &valueA));
    assert(registry.find(&keyA) == &valueB);
    assert(registry.eraseIfMatches(&keyA, &valueB));
    assert(registry.find(&keyA) == nullptr);

    std::atomic<bool> stop{false};
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            (void)registry.find(&keyB);
            (void)registry.size();
        }
    });
    for (int i = 0; i < 10000; ++i) {
        registry.set(&keyB, &valueB);
        assert(registry.find(&keyB) == &valueB);
        registry.eraseIfMatches(&keyB, &valueB);
    }
    stop.store(true, std::memory_order_relaxed);
    reader.join();
}

static void testSafeResourcePath()
{
    const auto root = std::filesystem::path("/tmp/webview-gui-root");
    std::filesystem::path resolved;

    assert(helpers::resolveContainedPath(root, "/assets/ui.js", resolved));
    assert(resolved == (root / "assets/ui.js").lexically_normal());

    assert(helpers::resolveContainedPath(root, "images/../images/knob.png", resolved));
    assert(resolved == (root / "images/knob.png").lexically_normal());

    assert(!helpers::resolveContainedPath(root, "../secret.txt", resolved));
    assert(!helpers::resolveContainedPath(root, "/../../secret.txt", resolved));
    assert(!helpers::resolveContainedPath(root, "assets/../../../secret.txt", resolved));
}

static void testJavascriptBase64Polyfill()
{
    const std::string script = helpers::base64PolyfillScript();
    assert(script.find("new Uint8Array(binaryString.length)") != std::string::npos);
    assert(script.find("new Uint8Array(b64.length)") == std::string::npos);
}

int main()
{
    testBase64RoundTrip();
    testPointerRegistry();
    testSafeResourcePath();
    testJavascriptBase64Polyfill();
    std::cout << "webview-gui core tests passed\n";
    return 0;
}
