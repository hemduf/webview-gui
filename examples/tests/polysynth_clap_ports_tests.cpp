#include "polysynth_plugin.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>

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
    "webview-gui PolySynth tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

bool checkAudioPorts(const clap_plugin_t *plugin) {
    const auto *audioPorts = static_cast<const clap_plugin_audio_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (!audioPorts || !audioPorts->count || !audioPorts->get)
        return false;
    if (audioPorts->count(plugin, true) != 0 || audioPorts->count(plugin, false) != 1)
        return false;

    // Once count() reports zero, asking get() for index 0 is host misuse under
    // CLAP. Probe only valid indices so the test itself obeys the ABI contract.
    clap_audio_port_info_t info{};
    if (!audioPorts->get(plugin, 0, false, &info))
        return false;
    if (info.id != webview_gui::examples::polysynth::kPolySynthAudioOutputPortId ||
        (info.flags & CLAP_AUDIO_PORT_IS_MAIN) == 0 ||
        info.channel_count != 2 || !info.port_type ||
        std::strcmp(info.port_type, CLAP_PORT_STEREO) != 0 ||
        info.in_place_pair != CLAP_INVALID_ID ||
        std::strcmp(info.name, "Stereo Out") != 0)
        return false;
    return true;
}

bool checkNotePorts(const clap_plugin_t *plugin) {
    const auto *notePorts = static_cast<const clap_plugin_note_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    if (!notePorts || !notePorts->count || !notePorts->get)
        return false;

    // The shell exposes the instrument's native CLAP note input topology. A
    // note output is intentionally deferred until the subsequent process/NOTE_END
    // increment can supply a real host output-event contract.
    if (notePorts->count(plugin, true) != 1 || notePorts->count(plugin, false) != 0)
        return false;

    clap_note_port_info_t input{};
    if (!notePorts->get(plugin, 0, true, &input) ||
        input.id != webview_gui::examples::polysynth::kPolySynthNoteInputPortId ||
        input.supported_dialects != CLAP_NOTE_DIALECT_CLAP ||
        input.preferred_dialect != CLAP_NOTE_DIALECT_CLAP ||
        std::strcmp(input.name, "Notes In") != 0)
        return false;
    return true;
}

} // namespace

int main() {
    const auto *factory = webview_gui::examples::polysynth::polysynthFactory();
    if (!factory || factory->get_plugin_count(factory) != 1)
        return 1;

    const auto *descriptor = factory->get_plugin_descriptor(factory, 0);
    if (!descriptor || !descriptor->id ||
        std::strcmp(descriptor->id,
                    webview_gui::examples::polysynth::kPolySynthPluginId) != 0 ||
        factory->get_plugin_descriptor(factory, 1) != nullptr)
        return 2;

    const auto *plugin = factory->create_plugin(
        factory, &kHost, webview_gui::examples::polysynth::kPolySynthPluginId);
    if (!plugin)
        return 3;

    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 4;
    }

    if (!checkAudioPorts(plugin) || !checkNotePorts(plugin) ||
        plugin->get_extension(plugin, "clap.example.unimplemented") != nullptr) {
        plugin->destroy(plugin);
        return 5;
    }

    if (!plugin->activate(plugin, 48000.0, 1, 128)) {
        plugin->destroy(plugin);
        return 6;
    }
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 7;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
