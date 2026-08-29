#include "gain_plugin.h"
#include "webview-gui/clap-webview-gui.h"

#include <clap/clap.h>

#include <cstring>
#include <iostream>

namespace {

const void *CLAP_ABI noHostExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI requestRestart(const clap_host_t *) {}
void CLAP_ABI requestProcess(const clap_host_t *) {}
void CLAP_ABI requestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain native GUI tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    noHostExtension,
    requestRestart,
    requestProcess,
    requestCallback,
};

const char *expectedNativeApi() {
#if defined(__APPLE__)
    return CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32) || defined(_WIN64)
    return CLAP_WINDOW_API_WIN32;
#elif defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__wasm__) && \
      !defined(__wasm32__) && !defined(__wasm64__)
    return CLAP_WINDOW_API_X11;
#else
    return nullptr;
#endif
}

} // namespace

int main() {
    using namespace webview_gui::examples::gain;

    const char *nativeApi = expectedNativeApi();
    if (!nativeApi) {
        std::cerr << "native Gain GUI contract was built for an unsupported platform\n";
        return 1;
    }

    const auto *factory = gainFactory();
    const auto *plugin = factory ? factory->create_plugin(factory, &kHost, kGainPluginId) : nullptr;
    if (!plugin)
        return 2;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 3;
    }

    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    if (!gui) {
        std::cerr << "Gain does not expose clap.gui\n";
        plugin->destroy(plugin);
        return 4;
    }

    if (gui->is_api_supported(plugin, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false)) {
        std::cerr << "Gain advertised host-owned WebView without host clap.webview/3 support\n";
        plugin->destroy(plugin);
        return 5;
    }

    if (!gui->is_api_supported(plugin, nativeApi, false) ||
        gui->is_api_supported(plugin, nativeApi, true)) {
        std::cerr << "Gain did not advertise the embedded native CLAP GUI API\n";
        plugin->destroy(plugin);
        return 6;
    }

    const char *preferred = nullptr;
    bool floating = true;
    if (!gui->get_preferred_api(plugin, &preferred, &floating) ||
        !preferred || std::strcmp(preferred, nativeApi) != 0 || floating) {
        std::cerr << "Gain did not prefer the native CLAP API when host WebView is unavailable\n";
        plugin->destroy(plugin);
        return 7;
    }

    plugin->destroy(plugin);
    return 0;
}
