#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__APPLE__)
#error macOS-only test
#endif

#include "webview-gui/webview-gui.h"

#include <CoreFoundation/CoreFoundation.h>

#include <cstddef>
#include <memory>
#include <string>

namespace {

void pumpMainRunLoop(double seconds = 0.01)
{
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, true);
}

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

        // This mirrors the receive self-delete contract: the currently executing
        // callable target must remain alive until operator() returns. A zero value
        // means owner destruction tore down the ResourceGetter target underneath
        // its own invocation.
        keepStateAlive->liveTargetsAfterOwnerReset = keepStateAlive->liveTargets;

        resource.mediaType = "application/octet-stream";
        resource.bytes = {0x42};
        return true;
    }

    std::shared_ptr<SelfDeletingResourceState> state;
};

} // namespace

TEST_CASE("public ResourceGetter survives destroying its owning WebviewGui from a resource callback")
{
    auto state = std::make_shared<SelfDeletingResourceState>();

    auto gui = WebviewGui::createUnique(
        WebviewGui::COCOA,
        "/index.html",
        WebviewGui::ResourceGetter{SelfDeletingResource{state}});

    REQUIRE(gui != nullptr);
    state->owner = &gui;

    for (int attempt = 0; attempt < 300 && !state->destroyRequestSeen; ++attempt)
        pumpMainRunLoop(0.01);

    REQUIRE(state->destroyRequestSeen);
    CHECK(gui == nullptr);
    CHECK(state->liveTargetsAfterOwnerReset > 0);
}
