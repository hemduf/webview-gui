#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webview-gui/clap-webview-gui.h"
#include "webview-gui/realtime-handoff.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <type_traits>

namespace {

const void* CLAP_ABI noPluginExtension(const clap_plugin_t*, const char*)
{
    return nullptr;
}

const void* CLAP_ABI noHostExtension(const clap_host_t*, const char*)
{
    return nullptr;
}

std::atomic<int> hostSendCalls{0};

bool CLAP_ABI hostWebviewSend(const clap_host_t*, const void*, uint32_t)
{
    hostSendCalls.fetch_add(1, std::memory_order_relaxed);
    return true;
}

webview_gui::clap_host_webview hostWebviewExtension{hostWebviewSend};

const void* CLAP_ABI hostExtensionWithWebview(const clap_host_t*, const char* id)
{
    if (id && std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0)
        return &hostWebviewExtension;
    return nullptr;
}

clap_plugin_t makePlugin()
{
    clap_plugin_t plugin{};
    plugin.get_extension = noPluginExtension;
    return plugin;
}

clap_host_t makeHost()
{
    clap_host_t host{};
    host.get_extension = noHostExtension;
    return host;
}

clap_host_t makeHostWithWebview()
{
    clap_host_t host{};
    host.get_extension = hostExtensionWithWebview;
    return host;
}

template <typename T, typename = void>
struct HasProcessMember : std::false_type {};

template <typename T>
struct HasProcessMember<T, std::void_t<decltype(&T::process)>> : std::true_type {};

} // namespace

TEST_CASE("CLAP GUI callbacks are routed by plugin instance, not host pointer")
{
    auto host = makeHost();
    auto pluginA = makePlugin();
    auto pluginB = makePlugin();

    webview_gui::ClapWebviewGui guiA{&pluginA, &host};
    webview_gui::ClapWebviewGui guiB{&pluginB, &host};
    guiA.init();
    guiB.init();

    REQUIRE(guiA.extPluginGui != nullptr);
    REQUIRE(guiB.extPluginGui != nullptr);

    CHECK(guiA.setSize(320, 240));
    CHECK(guiB.setSize(900, 700));

    uint32_t width = 0;
    uint32_t height = 0;

    CHECK(guiA.extPluginGui->get_size(&pluginA, &width, &height));
    CHECK(width == 320);
    CHECK(height == 240);

    width = height = 0;
    CHECK(guiB.extPluginGui->get_size(&pluginB, &width, &height));
    CHECK(width == 900);
    CHECK(height == 700);
}

TEST_CASE("CLAP callback lookup fails safely after an instance is unregistered")
{
    auto host = makeHost();
    auto pluginA = makePlugin();
    auto pluginB = makePlugin();

    webview_gui::ClapWebviewGui guiB{&pluginB, &host};
    guiB.init();

    {
        webview_gui::ClapWebviewGui guiA{&pluginA, &host};
        guiA.init();
        CHECK(guiA.setSize(123, 456));
    }

    uint32_t width = 0;
    uint32_t height = 0;

    CHECK_FALSE(guiB.extPluginGui->get_size(&pluginA, &width, &height));
}

TEST_CASE("CLAP reinitialisation unregisters the previous plugin key")
{
    auto host = makeHost();
    auto pluginA = makePlugin();
    auto pluginB = makePlugin();
    auto probe = makePlugin();

    webview_gui::ClapWebviewGui gui{&pluginA, &host};
    webview_gui::ClapWebviewGui probeGui{&probe, &host};
    gui.init();
    probeGui.init();

    gui.init(&pluginB, &host);
    CHECK(gui.setSize(777, 333));

    uint32_t width = 0;
    uint32_t height = 0;

    CHECK_FALSE(probeGui.extPluginGui->get_size(&pluginA, &width, &height));
    CHECK(probeGui.extPluginGui->get_size(&pluginB, &width, &height));
    CHECK(width == 777);
    CHECK(height == 333);
}

TEST_CASE("Synthetic host webview proxy is not exposed")
{
    auto host = makeHost();
    auto plugin = makePlugin();

    webview_gui::ClapWebviewGui gui{&plugin, &host};
    gui.init();

    CHECK(gui.extHostWebview == nullptr);
}

TEST_CASE("CLAP GUI callbacks reject a worker or audio thread")
{
    auto host = makeHostWithWebview();
    auto plugin = makePlugin();

    hostSendCalls.store(0, std::memory_order_relaxed);

    webview_gui::ClapWebviewGui gui{&plugin, &host};
    gui.init();
    REQUIRE(gui.setSize(640, 480));

    std::atomic<bool> wrongThreadGetSize{true};
    std::atomic<bool> wrongThreadSetSize{true};
    std::atomic<bool> wrongThreadSend{true};

    std::thread worker([&] {
        uint32_t width = 0;
        uint32_t height = 0;
        const unsigned char byte = 0x7f;
        wrongThreadGetSize.store(
            gui.extPluginGui->get_size(&plugin, &width, &height),
            std::memory_order_relaxed);
        wrongThreadSetSize.store(
            gui.extPluginGui->set_size(&plugin, 1, 1),
            std::memory_order_relaxed);
        wrongThreadSend.store(gui.send(&byte, 1), std::memory_order_relaxed);
    });
    worker.join();

    CHECK_FALSE(wrongThreadGetSize.load(std::memory_order_relaxed));
    CHECK_FALSE(wrongThreadSetSize.load(std::memory_order_relaxed));
    CHECK_FALSE(wrongThreadSend.load(std::memory_order_relaxed));
    CHECK(hostSendCalls.load(std::memory_order_relaxed) == 0);

    uint32_t width = 0;
    uint32_t height = 0;
    CHECK(gui.extPluginGui->get_size(&plugin, &width, &height));
    CHECK(width == 640);
    CHECK(height == 480);
}

TEST_CASE("reference CLAP adapter has no process entry and audio handoff never enters GUI code")
{
    static_assert(!HasProcessMember<webview_gui::ClapWebviewGui>::value,
                  "ClapWebviewGui must remain a GUI-only adapter");

    auto host = makeHostWithWebview();
    auto plugin = makePlugin();
    webview_gui::ClapWebviewGui gui{&plugin, &host};
    gui.init();

    hostSendCalls.store(0, std::memory_order_relaxed);
    webview_gui::RealtimeToUiQueue<std::uint32_t, 8> queue;
    std::atomic<bool> published{false};

    std::thread audioThread([&] {
        // This is the reference process()/RT pattern: publish POD state only.
        // It deliberately has no reference to gui, CHOC, CLAP host GUI, file IO,
        // locks, or allocation-heavy bridge code.
        published.store(queue.tryPush(42), std::memory_order_release);
    });
    audioThread.join();

    CHECK(published.load(std::memory_order_acquire));
    CHECK(hostSendCalls.load(std::memory_order_relaxed) == 0);

    std::uint32_t value = 0;
    REQUIRE(queue.tryPop(value));
    CHECK(value == 42);

    // The UI thread may decide how/when to publish the drained state.
    CHECK(gui.send(&value, sizeof(value)));
    CHECK(hostSendCalls.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("main-thread CLAP callbacks survive repeated create destroy cycles")
{
    auto host = makeHost();
    auto plugin = makePlugin();

    for (uint32_t iteration = 0; iteration < 32; ++iteration) {
        webview_gui::ClapWebviewGui gui{&plugin, &host};
        gui.init();
        REQUIRE(gui.extPluginGui != nullptr);

        CHECK(gui.extPluginGui->create(&plugin, webview_gui::CLAP_WINDOW_API_WEBVIEW, false));
        CHECK(gui.setSize(300 + iteration, 200 + iteration));

        uint32_t width = 0;
        uint32_t height = 0;
        CHECK(gui.extPluginGui->get_size(&plugin, &width, &height));
        CHECK(width == 300 + iteration);
        CHECK(height == 200 + iteration);

        gui.extPluginGui->destroy(&plugin);
        CHECK(gui.extPluginGui->get_size(&plugin, &width, &height));
    }
}

TEST_CASE("CLAP send rejects oversized payloads before calling the host extension")
{
    auto host = makeHostWithWebview();
    auto plugin = makePlugin();

    hostSendCalls.store(0, std::memory_order_relaxed);

    webview_gui::ClapWebviewGui gui{&plugin, &host};
    gui.init();

    const unsigned char byte = 0x7f;
    CHECK(gui.send(&byte, 1));
    CHECK(hostSendCalls.load(std::memory_order_relaxed) == 1);

    CHECK_FALSE(gui.send(&byte, webview_gui::detail::maxMessageBytes + 1));
    CHECK(hostSendCalls.load(std::memory_order_relaxed) == 1);
}
