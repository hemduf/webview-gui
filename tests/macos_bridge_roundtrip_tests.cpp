#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__APPLE__)
#error macOS-only test
#endif

#include "webview-gui/webview-gui.h"

#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

void pumpMainRunLoop(double seconds = 0.01)
{
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, true);
}

template <typename Predicate>
bool pumpMainRunLoopUntil(Predicate&& predicate,
                          std::chrono::steady_clock::duration timeout = std::chrono::seconds{10})
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        pumpMainRunLoop(0.01);
    return predicate();
}

void pumpMainRunLoopFor(std::chrono::steady_clock::duration duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline)
        pumpMainRunLoop(0.01);
}

struct SelfDeletingReceiveState {
    WebviewGui::UniquePtr* owner = nullptr;
    std::size_t liveTargets = 0;
    std::size_t liveTargetsAfterOwnerReset = 0;
    bool invoked = false;
};

struct SelfDeletingReceive {
    explicit SelfDeletingReceive(std::shared_ptr<SelfDeletingReceiveState> stateIn)
        : state(std::move(stateIn))
    {
        ++state->liveTargets;
    }

    SelfDeletingReceive(const SelfDeletingReceive& other)
        : state(other.state)
    {
        ++state->liveTargets;
    }

    SelfDeletingReceive(SelfDeletingReceive&& other) noexcept
        : state(std::move(other.state))
    {
    }

    ~SelfDeletingReceive()
    {
        if (state)
            --state->liveTargets;
    }

    void operator()(const unsigned char*, std::size_t) const
    {
        auto keepStateAlive = state;
        keepStateAlive->invoked = true;
        keepStateAlive->owner->reset();
        keepStateAlive->liveTargetsAfterOwnerReset = keepStateAlive->liveTargets;
    }

    std::shared_ptr<SelfDeletingReceiveState> state;
};

struct SelfDeletingResourceState {
    WebviewGui::UniquePtr* owner = nullptr;
    std::size_t liveTargets = 0;
    std::size_t liveTargetsAfterOwnerReset = 0;
    bool destroyRequestSeen = false;
};

struct SelfDeletingResource {
    explicit SelfDeletingResource(std::shared_ptr<SelfDeletingResourceState> stateIn)
        : state(std::move(stateIn))
    {
        ++state->liveTargets;
    }

    SelfDeletingResource(const SelfDeletingResource& other)
        : state(other.state)
    {
        ++state->liveTargets;
    }

    SelfDeletingResource(SelfDeletingResource&& other) noexcept
        : state(std::move(other.state))
    {
    }

    ~SelfDeletingResource()
    {
        if (state)
            --state->liveTargets;
    }

    bool operator()(const char* path, WebviewGui::Resource& resource) const
    {
        auto keepStateAlive = state;
        if (!path)
            return false;

        const std::string requested{path};
        if (requested == "/index.html") {
            static constexpr char html[] =
                "<!doctype html><html><head><title>resource-self-delete</title></head>"
                "<body><img src=\"/destroy.bin\"></body></html>";
            resource.mediaType = "text/html";
            resource.bytes.assign(html, html + sizeof(html) - 1);
            return true;
        }

        if (requested != "/destroy.bin")
            return false;

        keepStateAlive->destroyRequestSeen = true;
        if (keepStateAlive->owner)
            keepStateAlive->owner->reset();

        keepStateAlive->liveTargetsAfterOwnerReset = keepStateAlive->liveTargets;
        resource.mediaType = "application/octet-stream";
        resource.bytes = {0x42};
        return true;
    }

    std::shared_ptr<SelfDeletingResourceState> state;
};

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
    REQUIRE(pumpMainRunLoopUntil([&] {
        return done.load(std::memory_order_acquire);
    }));

    CHECK(received == expected);
}

TEST_CASE("public WebviewGui keeps receive target alive when the callback destroys the GUI")
{
    auto gui = WebviewGui::createUnique(
        WebviewGui::COCOA,
        "/index.html",
        [](const char* path, WebviewGui::Resource& resource)
        {
            if (!path || std::string(path) != "/index.html")
                return false;

            static constexpr const char html[] =
                "<!doctype html><html><head><title>self-delete</title></head><body>ready</body></html>";
            resource.mediaType = "text/html";
            resource.bytes.assign(html, html + sizeof(html) - 1);
            return true;
        });

    REQUIRE(gui != nullptr);

    auto state = std::make_shared<SelfDeletingReceiveState>();
    state->owner = &gui;
    gui->receive = SelfDeletingReceive{state};

    const unsigned char trigger = 0x42;
    gui->send(&trigger, 1);

    REQUIRE(pumpMainRunLoopUntil([&] { return state->invoked; }));
    CHECK(gui == nullptr);

    // CHOC explicitly permits a binding callback to destroy its WebView. The
    // public wrapper must therefore keep its receive callable alive until that
    // invocation returns, rather than destroying the currently-executing target
    // as a side effect of WebviewGui destruction.
    CHECK(state->liveTargetsAfterOwnerReset > 0);
}

TEST_CASE("public ResourceGetter keeps its target alive when the callback destroys the GUI")
{
    auto state = std::make_shared<SelfDeletingResourceState>();

    auto gui = WebviewGui::createUnique(
        WebviewGui::COCOA,
        "/index.html",
        WebviewGui::ResourceGetter{SelfDeletingResource{state}});

    REQUIRE(gui != nullptr);
    state->owner = &gui;

    REQUIRE(pumpMainRunLoopUntil([&] { return state->destroyRequestSeen; }));
    CHECK(gui == nullptr);
    CHECK(state->liveTargetsAfterOwnerReset > 0);
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

    REQUIRE(pumpMainRunLoopUntil([&] { return received.size() >= pendingLimit; }));

    // Keep pumping after the expected retained messages arrive so an unbounded
    // implementation deterministically exposes the extra queued messages.
    pumpMainRunLoopFor(std::chrono::milliseconds{500});

    REQUIRE(received.size() == pendingLimit);
    for (std::size_t i = 0; i < pendingLimit; ++i)
        CHECK(received[i] == static_cast<unsigned char>(i));
}

TEST_CASE("native WKWebView CSP blocks JavaScript eval while bridge remains operational")
{
    auto gui = WebviewGui::createUnique(
        WebviewGui::COCOA,
        "/index.html",
        [](const char* path, WebviewGui::Resource& resource)
        {
            if (!path || std::string(path) != "/index.html")
                return false;

            static constexpr const char html[] =
                "<!doctype html><html><head><title>csp-effective</title></head><body>"
                "<script>setTimeout(()=>{try{eval(`window.postMessage(new Uint8Array([202]).buffer,'*')`)}catch(_){}},50)</script>"
                "</body></html>";
            resource.mediaType = "text/html";
            resource.bytes.assign(html, html + sizeof(html) - 1);
            return true;
        });

    REQUIRE(gui != nullptr);

    std::atomic<bool> probeReceived{false};
    std::atomic<bool> evalExecuted{false};
    gui->receive = [&](const unsigned char* bytes, std::size_t size)
    {
        if (size != 1)
            return;
        if (bytes[0] == 0x42)
            probeReceived.store(true, std::memory_order_release);
        if (bytes[0] == 202)
            evalExecuted.store(true, std::memory_order_release);
    };

    // Prove the document-start bootstrap and native bridge are live. The page's
    // inline script is allowed, but its eval() would post 202 if CSP failed open.
    const unsigned char probe = 0x42;
    gui->send(&probe, 1);
    for (int attempt = 0; attempt < 300 && !probeReceived.load(std::memory_order_acquire); ++attempt)
        pumpMainRunLoop(0.01);

    REQUIRE(probeReceived.load(std::memory_order_acquire));

    for (int attempt = 0; attempt < 50; ++attempt)
        pumpMainRunLoop(0.01);

    CHECK_FALSE(evalExecuted.load(std::memory_order_acquire));
}
