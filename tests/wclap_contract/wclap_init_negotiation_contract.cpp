#include "webview-gui/clap-webview-gui.h"

#include <clap/clap.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

std::uint32_t hostExtensionQueries = 0;
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

    // WCLAP bridges may implement host.get_extension() by consulting plug-in
    // extension state. During clap_plugin.init() that can re-enter the plug-in
    // before strict CLAP helpers have marked initialization complete. The GUI
    // helper must therefore perform identity/thread binding only here and defer
    // both sides of WebView negotiation until a post-init GUI callback.
    if (!expect(hostExtensionQueries == 0,
                "WCLAP init queried a host extension before clap_plugin.init completed"))
        return 1;
    if (!expect(pluginExtensionQueries == 0,
                "WCLAP init queried a plug-in extension before clap_plugin.init completed"))
        return 2;

    if (!expect(gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                "first post-init GUI negotiation did not resolve host WebView support"))
        return 3;
    if (!expect(hostExtensionQueries == 1,
                "host WebView extension was not resolved exactly once after init"))
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
                "WCLAP message did not use the lazily resolved host WebView extension"))
        return 8;

    gui.destroy();
    return 0;
}
