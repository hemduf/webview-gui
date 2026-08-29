#include "gain_plugin.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

bool CLAP_ABI hostWebviewSend(const clap_host_t *, const void *, uint32_t) {
    return true;
}

const clap_host_webview_t kHostWebview{
    hostWebviewSend,
};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *id) {
    if (id && std::strcmp(id, CLAP_EXT_WEBVIEW) == 0)
        return &kHostWebview;
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain GUI tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

} // namespace

int main() {
    using namespace webview_gui::examples::gain;

    const auto *factory = gainFactory();
    if (!factory)
        return 1;

    const auto *plugin = factory->create_plugin(factory, &kHost, kGainPluginId);
    if (!plugin)
        return 2;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 3;
    }

    const auto *webview = static_cast<const clap_plugin_webview_t *>(
        plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));

    if (!webview || !gui) {
        std::cerr << "Gain must expose both clap.webview/3 and clap.gui\n";
        plugin->destroy(plugin);
        return 4;
    }

    if (webview->get_uri(plugin, nullptr, 0) <= 0) {
        std::cerr << "Gain WebView URI negotiation failed before GUI creation\n";
        plugin->destroy(plugin);
        return 5;
    }

    if (!gui->is_api_supported(plugin, CLAP_WINDOW_API_WEBVIEW, false) ||
        gui->is_api_supported(plugin, CLAP_WINDOW_API_WEBVIEW, true)) {
        std::cerr << "Gain clap.gui did not negotiate embedded WebView correctly\n";
        plugin->destroy(plugin);
        return 6;
    }

    const char *preferredApi = nullptr;
    bool isFloating = true;
    if (!gui->get_preferred_api(plugin, &preferredApi, &isFloating) ||
        !preferredApi || std::strcmp(preferredApi, CLAP_WINDOW_API_WEBVIEW) != 0 ||
        isFloating) {
        std::cerr << "Gain clap.gui did not prefer the embedded WebView API\n";
        plugin->destroy(plugin);
        return 7;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!gui->get_size(plugin, &width, &height) || width != 480 || height != 320) {
        std::cerr << "Gain clap.gui did not expose the expected initial logical size\n";
        plugin->destroy(plugin);
        return 8;
    }

    if (gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, true) ||
        !gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false)) {
        std::cerr << "Gain clap.gui create() did not enforce embedded WebView semantics\n";
        plugin->destroy(plugin);
        return 9;
    }

    if (!gui->can_resize(plugin) || !gui->set_size(plugin, 640, 360) ||
        !gui->get_size(plugin, &width, &height) || width != 640 || height != 360) {
        std::cerr << "Gain clap.gui logical resize contract failed\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 10;
    }

    gui->destroy(plugin);

    if (!gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false)) {
        std::cerr << "Gain clap.gui could not be recreated after destroy()\n";
        plugin->destroy(plugin);
        return 11;
    }
    gui->destroy(plugin);

    plugin->destroy(plugin);
    return 0;
}
