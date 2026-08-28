#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(_WIN32) && !defined(__linux__)
#error Windows/Linux-only test
#endif

#include "webview-gui/webview-gui.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#elif defined(__linux__)
#include <gtk/gtk.h>
#endif

namespace {

void pumpNativeEvents()
{
#if defined(_WIN32)
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
#elif defined(__linux__)
    while (g_main_context_pending(nullptr))
        g_main_context_iteration(nullptr, FALSE);
#endif
}

template <typename Predicate>
bool pumpUntil(Predicate&& predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        pumpNativeEvents();
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);

    pumpNativeEvents();
    return predicate();
}

WebviewGui::Platform nativePlatform()
{
#if defined(_WIN32)
    return WebviewGui::HWND;
#else
    return WebviewGui::X11EMBED;
#endif
}

struct SelfDeletingResourceState {
    WebviewGui::UniquePtr* owner = nullptr;
    std::size_t liveTargets = 0;
    std::size_t liveTargetsAfterOwnerReset = 0;
    bool destroyRequestSeen = false;
#if defined(_WIN32)
    bool comApartmentStayedSta = false;
#endif
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

#if defined(_WIN32)
        // WebView2 still needs COM after ResourceGetter returns to construct and
        // attach the response. Destroying the public owner must therefore not
        // drop the last STA reference while this native callback is in flight.
        const auto mtaProbe = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        keepStateAlive->comApartmentStayedSta = mtaProbe == RPC_E_CHANGED_MODE;
        if (mtaProbe == S_OK || mtaProbe == S_FALSE)
            CoUninitialize();
#endif

        keepStateAlive->liveTargetsAfterOwnerReset = keepStateAlive->liveTargets;
        resource.mediaType = "application/octet-stream";
        resource.bytes = {0x42};
        return true;
    }

    std::shared_ptr<SelfDeletingResourceState> state;
};

} // namespace

TEST_CASE("native ResourceGetter survives destroying its owning WebviewGui")
{
#if defined(__linux__)
    REQUIRE(gtk_init_check(nullptr, nullptr));
#endif

    auto state = std::make_shared<SelfDeletingResourceState>();
    auto gui = WebviewGui::createUnique(
        nativePlatform(),
        "/index.html",
        WebviewGui::ResourceGetter{SelfDeletingResource{state}});

    REQUIRE(gui != nullptr);
    state->owner = &gui;

    REQUIRE(pumpUntil([&] { return state->destroyRequestSeen; }, std::chrono::seconds(20)));
    CHECK(gui == nullptr);
    CHECK(state->liveTargetsAfterOwnerReset > 0);
#if defined(_WIN32)
    CHECK(state->comApartmentStayedSta);
#endif

    // Drain native teardown work while all test/module code remains loaded.
    for (int i = 0; i < 16; ++i) {
        pumpNativeEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
