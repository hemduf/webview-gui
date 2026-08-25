#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__APPLE__)
#error macOS-only test
#endif

#include "webview-gui/webview-gui.h"

#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace {

void pumpMainRunLoop(double seconds = 0.01)
{
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, true);
}

} // namespace

TEST_CASE("public WebviewGui bridge queues one early send and round-trips opaque bytes through WKWebView")
{
    auto gui = WebviewGui::createUnique(
        WebviewGui::COCOA,
        "/index.html",
        [](const char* path, WebviewGui::Resource& resource)
        {
            if (!path || std::string(path) != "/index.html")
                return false;

            static constexpr const char html[] =
                "<!doctype html><html><head><title>bridge</title></head><body>ready</body></html>";
            resource.mediaType = "text/html";
            resource.bytes.assign(html, html + sizeof(html) - 1);
            return true;
        });

    REQUIRE(gui != nullptr);

    const std::vector<unsigned char> expected{0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff};
    std::vector<unsigned char> received;
    std::atomic<bool> done{false};

    gui->receive = [&](const unsigned char* bytes, std::size_t size)
    {
        received.assign(bytes, bytes + size);
        done.store(true, std::memory_order_release);
    };

    // Deliberately send exactly once before the asynchronous document load has
    // completed. The native bridge must retain this message until the hardened
    // page signals readiness; callers must not need polling/retry semantics.
    gui->send(expected.data(), expected.size());
    for (int attempt = 0; attempt < 300 && !done.load(std::memory_order_acquire); ++attempt)
        pumpMainRunLoop(0.01);

    REQUIRE(done.load(std::memory_order_acquire));
    CHECK(received == expected);
}

TEST_CASE("public WebviewGui bounds the number of messages queued before bridge readiness")
{
    auto gui = WebviewGui::createUnique(
        WebviewGui::COCOA,
        "/index.html",
        [](const char* path, WebviewGui::Resource& resource)
        {
            if (!path || std::string(path) != "/index.html")
                return false;

            static constexpr const char html[] =
                "<!doctype html><html><head><title>bridge</title></head><body>ready</body></html>";
            resource.mediaType = "text/html";
            resource.bytes.assign(html, html + sizeof(html) - 1);
            return true;
        });

    REQUIRE(gui != nullptr);

    constexpr std::size_t pendingLimit = 64;
    constexpr std::size_t sentCount = pendingLimit + 16;
    std::vector<unsigned char> received;

    gui->receive = [&](const unsigned char* bytes, std::size_t size)
    {
        if (size == 1)
            received.push_back(bytes[0]);
    };

    // No run-loop pumping occurs before these sends, so the page cannot have
    // signalled bridge readiness yet. A plug-in host must not be able to grow
    // native memory without bound by repeatedly publishing UI state during this
    // asynchronous startup window.
    for (std::size_t i = 0; i < sentCount; ++i) {
        const auto value = static_cast<unsigned char>(i);
        gui->send(&value, 1);
    }

    for (int attempt = 0; attempt < 300 && received.size() < pendingLimit; ++attempt)
        pumpMainRunLoop(0.01);

    REQUIRE(received.size() >= pendingLimit);

    // Keep pumping after the expected retained messages arrive so an unbounded
    // implementation deterministically exposes the extra queued messages.
    for (int attempt = 0; attempt < 50; ++attempt)
        pumpMainRunLoop(0.01);

    REQUIRE(received.size() == pendingLimit);
    for (std::size_t i = 0; i < pendingLimit; ++i)
        CHECK(received[i] == static_cast<unsigned char>(i));
}
