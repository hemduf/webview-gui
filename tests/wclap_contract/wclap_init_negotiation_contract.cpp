#include "webview-gui/clap-webview-gui.h"

#include <clap/clap.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

std::uint32_t hostExtensionQueries = 0;
std::uint32_t missingHostExtensionQueries = 0;
std::uint32_t pluginExtensionQueries = 0;
std::uint32_t missingPluginExtensionQueries = 0;
std::uint32_t hostSendCalls = 0;

bool CLAP_ABI hostWebviewSend(const clap_host_t *, const void *, std::uint32_t) {
    ++hostSendCalls;
    return true;
}

webview_gui::clap_host_webview hostWebview{hostWebviewSend};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *id) {
    ++hostExtensionQueries;
    if (id && std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0)
        return &hostWebview;
    return nullptr;
}

const void *CLAP_ABI missingHostGetExtension(const clap_host_t *, const char *) {
    ++missingHostExtensionQueries;
    return nullptr;
}

std::int32_t CLAP_ABI pluginGetUri(const clap_plugin_t *, char *uri, std::uint32_t capacity) {
    static constexpr char kUri[] = "/index.html";
    if (capacity == 0u)
        return static_cast<std::int32_t>(sizeof(kUri));
    if (!uri || capacity < sizeof(kUri))
        return -1;
    std::memcpy(uri, kUri, sizeof(kUri));
    return static_cast<std::int32_t>(sizeof(kUri));
}

bool CLAP_ABI pluginGetResource(const clap_plugin_t *,
                                const char *,
                                char *,
                                std::uint32_t,
                                const clap_ostream_t *) {
    return true;
}

bool CLAP_ABI pluginReceive(const clap_plugin_t *, const void *, std::uint32_t) {
    return true;
}

webview_gui::clap_plugin_webview pluginWebview{
    pluginGetUri,
    pluginGetResource,
    pluginReceive,
};

const void *CLAP_ABI pluginGetExtension(const clap_plugin_t *, const char *id) {
    ++pluginExtensionQueries;
    if (id && std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0)
        return &pluginWebview;
    return nullptr;
}

const void *CLAP_ABI missingPluginGetExtension(const clap_plugin_t *, const char *) {
    ++missingPluginExtensionQueries;
    return nullptr;
}

bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    using webview_gui::CLAP_WINDOW_API_WEBVIEW;
    using webview_gui::ClapWebviewGui;

    clap_host_t host{};
    host.get_extension = hostGetExtension;
    clap_plugin_t plugin{};
    plugin.get_extension = pluginGetExtension;

    ClapWebviewGui gui{&plugin, &host};
    gui.init();

    // The WCLAP module's host.get_extension(CLAP_EXT_WEBVIEW) is a bridge-owned
    // special case which returns a pre-registered proxy without consulting the
    // native host or re-entering plug-in extension discovery. Resolve that host
    // proxy during init because the pinned native bridge copies extHostWebview
    // immediately. Plug-in WebView discovery must remain deferred until normal
    // post-init GUI negotiation.
    if (!expect(hostExtensionQueries == 1,
                "WCLAP init did not resolve the host WebView proxy exactly once"))
        return 1;
    if (!expect(pluginExtensionQueries == 0,
                "WCLAP init queried a plug-in extension before clap_plugin.init completed"))
        return 2;

    if (!expect(gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                "post-init GUI negotiation lost the complete WebView path"))
        return 3;
    if (!expect(hostExtensionQueries == 1,
                "post-init GUI negotiation repeated the cached host WebView lookup"))
        return 4;
    if (!expect(pluginExtensionQueries == 1,
                "post-init GUI negotiation did not verify plug-in WebView support exactly once"))
        return 5;

    if (!expect(gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false) &&
                    hostExtensionQueries == 1 && pluginExtensionQueries == 1,
                "resolved WebView extensions were not cached"))
        return 6;
    if (!expect(gui.create(CLAP_WINDOW_API_WEBVIEW, false),
                "resolved WCLAP host-owned WebView path could not be created"))
        return 7;

    const std::uint8_t byte = 0x42;
    if (!expect(gui.send(&byte, 1) && hostSendCalls == 1,
                "WCLAP message did not use the init-resolved host WebView extension"))
        return 8;

    gui.destroy();

    // A host extension alone is insufficient: the plug-in must also expose all
    // clap.webview/3 callbacks before CLAP_WINDOW_API_WEBVIEW is advertised.
    clap_plugin_t missingPlugin{};
    missingPlugin.get_extension = missingPluginGetExtension;
    ClapWebviewGui missingPluginGui{&missingPlugin, &host};
    missingPluginGui.init();
    if (!expect(hostExtensionQueries == 2,
                "second WCLAP instance did not resolve the host WebView proxy during init"))
        return 9;
    if (!expect(missingPluginExtensionQueries == 0,
                "missing plug-in WebView was queried during init"))
        return 10;
    if (!expect(!missingPluginGui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                "host-only WebView path was incorrectly advertised in WCLAP mode"))
        return 11;
    if (!expect(missingPluginExtensionQueries == 1,
                "missing plug-in WebView result was not resolved exactly once after init"))
        return 12;
    if (!expect(!missingPluginGui.create(CLAP_WINDOW_API_WEBVIEW, false) &&
                    missingPluginExtensionQueries == 1,
                "negative plug-in WebView lookup was not cached"))
        return 13;

    // Absence on the host side is also an init-time resolved result. Repeated GUI
    // capability probes must not repeatedly cross the host boundary.
    clap_host_t missingHost{};
    missingHost.get_extension = missingHostGetExtension;
    ClapWebviewGui missingHostGui{&plugin, &missingHost};
    missingHostGui.init();
    if (!expect(missingHostExtensionQueries == 1,
                "missing host WebView extension was not resolved exactly once during init"))
        return 14;
    if (!expect(!missingHostGui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                "missing host WebView extension was reported as supported"))
        return 15;
    if (!expect(missingHostExtensionQueries == 1,
                "negative host WebView lookup was repeated after init"))
        return 16;
    if (!expect(!missingHostGui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false) &&
                    missingHostExtensionQueries == 1,
                "negative host WebView lookup was not cached"))
        return 17;

    return 0;
}
