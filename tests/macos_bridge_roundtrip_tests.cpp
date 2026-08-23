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

TEST_CASE("public WebviewGui bridge round-trips opaque bytes through WKWebView")
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

    // The page load is asynchronous. send() is intentionally safe before the
    // capability-scoped JS function exists, so retry until the local UI is ready.
    for (int attempt = 0; attempt < 150 && !done.load(std::memory_order_acquire); ++attempt) {
        gui->send(expected.data(), expected.size());
        pumpMainRunLoop(0.01);
    }

    REQUIRE(done.load(std::memory_order_acquire));
    CHECK(received == expected);
}
