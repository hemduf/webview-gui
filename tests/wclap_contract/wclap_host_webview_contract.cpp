#include "webview-gui/clap-webview-gui.h"

#include <clap/clap.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

int hostSendCalls = 0;

bool CLAP_ABI hostWebviewSend(const clap_host_t *, const void *, uint32_t)
{
    ++hostSendCalls;
    return true;
}

webview_gui::clap_host_webview hostWebview{hostWebviewSend};

const void *CLAP_ABI hostNoExtensions(const clap_host_t *, const char *)
{
    return nullptr;
}

const void *CLAP_ABI hostWithWebview(const clap_host_t *, const char *id)
{
    if (id && std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0)
        return &hostWebview;
    return nullptr;
}

int32_t CLAP_ABI pluginGetUri(const clap_plugin_t *, char *uri, uint32_t capacity)
{
    static constexpr char kUri[] = "/index.html";
    if (!uri || capacity < sizeof(kUri))
        return static_cast<int32_t>(sizeof(kUri));
    std::memcpy(uri, kUri, sizeof(kUri));
    return static_cast<int32_t>(sizeof(kUri) - 1);
}

bool CLAP_ABI pluginGetResource(const clap_plugin_t *, const char *, char *, uint32_t,
                                const clap_ostream_t *)
{
    return true;
}

bool CLAP_ABI pluginReceive(const clap_plugin_t *, const void *, uint32_t)
{
    return true;
}

webview_gui::clap_plugin_webview pluginWebview{
    pluginGetUri,
    pluginGetResource,
    pluginReceive,
};

const void *CLAP_ABI pluginNoExtensions(const clap_plugin_t *, const char *)
{
    return nullptr;
}

const void *CLAP_ABI pluginWithWebview(const clap_plugin_t *, const char *id)
{
    if (id && std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0)
        return &pluginWebview;
    return nullptr;
}

clap_host_t makeHost(bool withWebview)
{
    clap_host_t host{};
    host.get_extension = withWebview ? hostWithWebview : hostNoExtensions;
    return host;
}

clap_plugin_t makePlugin(bool withWebview)
{
    clap_plugin_t plugin{};
    plugin.get_extension = withWebview ? pluginWithWebview : pluginNoExtensions;
    return plugin;
}

bool expect(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

} // namespace

int main()
{
    using webview_gui::CLAP_WINDOW_API_WEBVIEW;
    using webview_gui::ClapWebviewGui;

    {
        auto host = makeHost(true);
        auto plugin = makePlugin(false);
        ClapWebviewGui gui{&plugin, &host};
        gui.init();

        if (!expect(!gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                    "host WebView must not be advertised when the plug-in WebView extension is missing"))
            return 1;
        if (!expect(!gui.create(CLAP_WINDOW_API_WEBVIEW, false),
                    "host WebView create must fail when the plug-in WebView extension is missing"))
            return 2;
    }

    {
        auto host = makeHost(false);
        auto plugin = makePlugin(true);
        ClapWebviewGui gui{&plugin, &host};
        gui.init();

        if (!expect(!gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                    "host WebView must not be advertised when the host WebView extension is missing"))
            return 3;
        if (!expect(!gui.create(CLAP_WINDOW_API_WEBVIEW, false),
                    "host WebView create must fail when the host WebView extension is missing"))
            return 4;
    }

    {
        auto host = makeHost(true);
        auto plugin = makePlugin(true);
        ClapWebviewGui gui{&plugin, &host};
        gui.init();

        if (!expect(gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, false),
                    "complete clap.webview/3 path must advertise the host-owned WebView API"))
            return 5;
        if (!expect(!gui.isApiSupported(CLAP_WINDOW_API_WEBVIEW, true),
                    "host-owned WebView API must never be floating"))
            return 6;

        const char *preferred = nullptr;
        bool floating = true;
        if (!expect(gui.getPreferredApi(&preferred, &floating) && preferred &&
                        std::strcmp(preferred, CLAP_WINDOW_API_WEBVIEW) == 0 && !floating,
                    "complete clap.webview/3 path must be the preferred embedded API"))
            return 7;

        if (!expect(gui.create(CLAP_WINDOW_API_WEBVIEW, false),
                    "complete host-owned WebView path must create logically without a native OS view"))
            return 8;

        clap_window_t window{};
        window.api = CLAP_WINDOW_API_WEBVIEW;
        window.ptr = nullptr;
        if (!expect(gui.setParent(&window),
                    "host-owned WebView must accept the spec-defined null opaque parent"))
            return 9;
        if (!expect(!gui.setScale(2.0),
                    "host-owned WebView uses logical pixels and must reject set_scale"))
            return 10;
        if (!expect(gui.show() && gui.hide(),
                    "host-owned WebView show/hide must not require a native WebviewGui instance"))
            return 11;

        const uint8_t byte = 0x42;
        hostSendCalls = 0;
        if (!expect(gui.send(&byte, 1) && hostSendCalls == 1,
                    "host-owned WebView messages must route through clap_host_webview.send"))
            return 12;

        gui.destroy();
        if (!expect(!gui.send(&byte, 1),
                    "destroyed host-owned WebView must stop routing messages"))
            return 13;
    }

    return 0;
}
