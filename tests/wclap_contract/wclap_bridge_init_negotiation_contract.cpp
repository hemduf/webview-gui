#include "webview-gui/clap-webview-gui.h"

#include <clap/clap.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

std::uint32_t pluginExtensionQueries = 0;
std::uint32_t hostExtensionQueries = 0;

int32_t CLAP_ABI pluginGetUri(const clap_plugin_t *, char *, std::uint32_t) {
    return 1;
}

bool CLAP_ABI pluginGetResource(const clap_plugin_t *,
                                const char *,
                                char *,
                                std::uint32_t,
                                const clap_ostream_t *) {
    return false;
}

bool CLAP_ABI pluginReceive(const clap_plugin_t *, const void *, std::uint32_t) {
    return true;
}

const webview_gui::clap_plugin_webview pluginWebview{
    pluginGetUri,
    pluginGetResource,
    pluginReceive,
};

bool CLAP_ABI hostSend(const clap_host_t *, const void *, std::uint32_t) {
    return true;
}

const webview_gui::clap_host_webview hostWebview{hostSend};

const void *CLAP_ABI pluginGetExtension(const clap_plugin_t *, const char *id) {
    ++pluginExtensionQueries;
    return id && std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0
               ? static_cast<const void *>(&pluginWebview)
               : nullptr;
}

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *id) {
    ++hostExtensionQueries;
    return id && std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0
               ? static_cast<const void *>(&hostWebview)
               : nullptr;
}

bool expect(bool condition, const char *message) {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    clap_plugin_t plugin{};
    plugin.get_extension = pluginGetExtension;
    clap_host_t host{};
    host.get_extension = hostGetExtension;

    webview_gui::ClapWebviewGui gui{&plugin, &host};
    gui.init();

    // The pinned WCLAP bridge constructs this helper around its native wrapper
    // before it has called the module's clap_plugin.init(). If helper init queries
    // wrapper plug-in extensions, the bridge forwards that query into the still
    // uninitialized module and strict clap-helpers terminates. Identity/thread
    // binding must therefore be extension-free on the native bridge side too.
    if (!expect(pluginExtensionQueries == 0,
                "bridge-side GUI init queried plug-in extensions before module init"))
        return 1;
    if (!expect(hostExtensionQueries == 0,
                "bridge-side GUI init queried host extensions before module init"))
        return 2;

    if (!expect(gui.isApiSupported(webview_gui::CLAP_WINDOW_API_WEBVIEW, false),
                "post-init native WebView negotiation did not resolve both sides"))
        return 3;
    if (!expect(pluginExtensionQueries == 1 && hostExtensionQueries == 1,
                "post-init native WebView negotiation did not resolve exactly once"))
        return 4;

    if (!expect(gui.isApiSupported(webview_gui::CLAP_WINDOW_API_WEBVIEW, false),
                "cached native WebView negotiation changed result"))
        return 5;
    if (!expect(pluginExtensionQueries == 1 && hostExtensionQueries == 1,
                "native WebView negotiation was not cached"))
        return 6;

    return 0;
}
