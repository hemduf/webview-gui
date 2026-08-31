#include "webview-gui/clap-webview-gui.h"

#include <clap/clap.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

std::uint32_t hostExtensionQueries = 0;
std::uint32_t missingHostExtensionQueries = 0;
std::uint32_t pluginExtensionQueries = 0;
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

const void *CLAP_ABI pluginGetExtension(const clap_plugin_t *, const char *) {
    ++pluginExtensionQueries;
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
    // immediately. Plug-in WebView discovery must remain deferred.
    if (!expect(hostExtensionQueries == 1,
                "WCLAP init did not resolve the host WebView proxy exactly once"))
        return 1;
    if (!expect(pluginExtensionQueries == 0,
                "WCLAP init queried a plug-in extension before clap_plugin.init completed"))
        return 2;

    if (!expect(gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                "post-init GUI negotiation lost host WebView support"))
        return 3;
    if (!expect(hostExtensionQueries == 1,
                "post-init GUI negotiation repeated the cached host WebView lookup"))
        return 4;
    if (!expect(pluginExtensionQueries == 0,
                "WCLAP host-owned WebView path unexpectedly queried plug-in WebView support"))
        return 5;

    if (!expect(gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false) &&
                    hostExtensionQueries == 1,
                "resolved host WebView extension was not cached"))
        return 6;
    if (!expect(gui.create(CLAP_WINDOW_API_WEBVIEW, false),
                "resolved WCLAP host-owned WebView path could not be created"))
        return 7;

    const std::uint8_t byte = 0x42;
    if (!expect(gui.send(&byte, 1) && hostSendCalls == 1,
                "WCLAP message did not use the init-resolved host WebView extension"))
        return 8;

    gui.destroy();

    // Absence is also an init-time resolved result. Repeated GUI capability probes
    // must not repeatedly cross the host boundary when clap.webview/3 is missing.
    clap_host_t missingHost{};
    missingHost.get_extension = missingHostGetExtension;
    ClapWebviewGui missingGui{&plugin, &missingHost};
    missingGui.init();
    if (!expect(missingHostExtensionQueries == 1,
                "missing host WebView extension was not resolved exactly once during init"))
        return 9;
    if (!expect(!missingGui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                "missing host WebView extension was reported as supported"))
        return 10;
    if (!expect(missingHostExtensionQueries == 1,
                "negative host WebView lookup was repeated after init"))
        return 11;
    if (!expect(!missingGui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false) &&
                    missingHostExtensionQueries == 1,
                "negative host WebView lookup was not cached"))
        return 12;

    return 0;
}
