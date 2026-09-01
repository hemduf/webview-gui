#include "polysynth_plugin.h"
#include "polysynth_voice_allocator.h"
#include "wclap/polysynth_wclap_proxy.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>
#include <clap/ext/gui.h>
#include <clap/ext/voice-info.h>

#include <cstdint>
#include <cstring>

namespace {

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui PolySynth voice-info/native GUI tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

const char *expectedNativeApi() noexcept {
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
    using namespace webview_gui::examples::polysynth;

    const auto *factory = polysynthFactory();
    if (!factory)
        return 1;

    const auto *inner = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!inner)
        return 2;

    const auto *plugin = wclap::wrapPolySynthWclapPlugin(inner, &kHost);
    if (!plugin) {
        inner->destroy(inner);
        return 3;
    }
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 4;
    }

    const auto *voiceInfo = static_cast<const clap_plugin_voice_info_t *>(
        plugin->get_extension(plugin, CLAP_EXT_VOICE_INFO));
    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto *webview = static_cast<const clap_plugin_webview_t *>(
        plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    if (!voiceInfo || !voiceInfo->get || !gui || !gui->is_api_supported ||
        !gui->get_preferred_api || !gui->create || !gui->destroy || !webview ||
        !webview->get_uri || !webview->get_resource || !webview->receive) {
        plugin->destroy(plugin);
        return 5;
    }

    const char *nativeApi = expectedNativeApi();
    if (!nativeApi || !gui->is_api_supported(plugin, nativeApi, false) ||
        gui->is_api_supported(plugin, nativeApi, true)) {
        plugin->destroy(plugin);
        return 6;
    }

    if (gui->is_api_supported(plugin, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false)) {
        plugin->destroy(plugin);
        return 7;
    }

    const char *preferredApi = nullptr;
    bool preferredFloating = true;
    if (!gui->get_preferred_api(plugin, &preferredApi, &preferredFloating) ||
        !preferredApi || std::strcmp(preferredApi, nativeApi) != 0 || preferredFloating) {
        plugin->destroy(plugin);
        return 8;
    }

    char uri[64]{};
    const auto requiredUriSize = webview->get_uri(plugin, nullptr, 0);
    if (requiredUriSize <= 0 ||
        webview->get_uri(plugin, uri, static_cast<std::uint32_t>(sizeof(uri))) !=
            requiredUriSize ||
        std::strcmp(uri, "/index.html") != 0) {
        plugin->destroy(plugin);
        return 9;
    }

    if (!plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 10;
    }

    clap_voice_info_t info{};
    if (!voiceInfo->get(plugin, &info)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 11;
    }

    // CLAP distinguishes the number of voices the current patch uses from the
    // fixed storage capacity available without reallocating. This reference
    // patch uses 16 voices while the RT allocator preallocates 64 slots.
    if (info.voice_count != kPolySynthDefaultVoiceCount ||
        info.voice_capacity != VoiceAllocator::kMaximumVoices ||
        (info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) == 0) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 12;
    }

    if (voiceInfo->get(plugin, nullptr)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 13;
    }

    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
