#include "polysynth_plugin.h"

#include <clap/clap.h>
#include <clap/ext/voice-info.h>

#include <cstdint>

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
    "webview-gui PolySynth voice-info tests",
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
    using namespace webview_gui::examples::polysynth;

    const auto *factory = polysynthFactory();
    if (!factory)
        return 1;

    const auto *plugin = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!plugin)
        return 2;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 3;
    }

    const auto *voiceInfo = static_cast<const clap_plugin_voice_info_t *>(
        plugin->get_extension(plugin, CLAP_EXT_VOICE_INFO));
    if (!voiceInfo || !voiceInfo->get) {
        plugin->destroy(plugin);
        return 4;
    }

    if (!plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 5;
    }

    clap_voice_info_t info{};
    if (!voiceInfo->get(plugin, &info)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 6;
    }

    if (info.voice_count != kPolySynthDefaultVoiceCount ||
        info.voice_capacity != kPolySynthDefaultVoiceCount ||
        (info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) == 0) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 7;
    }

    if (voiceInfo->get(plugin, nullptr)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 8;
    }

    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
