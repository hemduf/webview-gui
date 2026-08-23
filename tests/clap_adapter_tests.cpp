#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webview-gui/clap-webview-gui.h"

#include <atomic>
#include <cstring>
#include <thread>

namespace {

const void* CLAP_ABI noPluginExtension(const clap_plugin_t*, const char*)
{
    return nullptr;
}

const void* CLAP_ABI noHostExtension(const clap_host_t*, const char*)
{
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
    auto host = makeHost();
    auto plugin = makePlugin();

    webview_gui::ClapWebviewGui gui{&plugin, &host};
    gui.init();
    REQUIRE(gui.setSize(640, 480));

    std::atomic<bool> wrongThreadGetSize{true};
    std::atomic<bool> wrongThreadSetSize{true};

    std::thread worker([&] {
        uint32_t width = 0;
        uint32_t height = 0;
        wrongThreadGetSize.store(
            gui.extPluginGui->get_size(&plugin, &width, &height),
            std::memory_order_relaxed);
        wrongThreadSetSize.store(
            gui.extPluginGui->set_size(&plugin, 1, 1),
            std::memory_order_relaxed);
    });
    worker.join();

    CHECK_FALSE(wrongThreadGetSize.load(std::memory_order_relaxed));
    CHECK_FALSE(wrongThreadSetSize.load(std::memory_order_relaxed));

    uint32_t width = 0;
    uint32_t height = 0;
    CHECK(gui.extPluginGui->get_size(&plugin, &width, &height));
    CHECK(width == 640);
    CHECK(height == 480);
}
