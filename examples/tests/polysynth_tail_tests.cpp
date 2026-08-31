#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/tail.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id kAmpReleaseId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::AmpRelease);

struct TailHostState {
    std::uint32_t changedCalls = 0;
};

void CLAP_ABI hostTailChanged(const clap_host_t *host) {
    if (!host || !host->host_data)
        return;
    ++static_cast<TailHostState *>(host->host_data)->changedCalls;
}

const clap_host_tail_t kHostTail{
    hostTailChanged,
};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *extensionId) {
    return extensionId && std::strcmp(extensionId, CLAP_EXT_TAIL) == 0
               ? static_cast<const void *>(&kHostTail)
               : nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

struct FlushInputEvents {
    explicit FlushInputEvents(double value) noexcept {
        event = {};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kAmpReleaseId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx ? 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx || index != 0)
            return nullptr;
        return &static_cast<const FlushInputEvents *>(events->ctx)->event.header;
    }

    clap_event_param_value_t event{};
    clap_input_events_t input{};
};

bool setRelease(const clap_plugin_t *plugin,
                const clap_plugin_params_t *params,
                double seconds) noexcept {
    FlushInputEvents input(seconds);
    params->flush(plugin, &input.input, nullptr);
    double observed = -1.0;
    return params->get_value(plugin, kAmpReleaseId, &observed) &&
           std::fabs(observed - seconds) <= 1.0e-6;
}

} // namespace

int main() {
    using namespace webview_gui::examples::polysynth;

    const auto *factory = polysynthFactory();
    if (!factory)
        return 1;

    TailHostState hostState{};
    const clap_host_t host{
        CLAP_VERSION,
        &hostState,
        "webview-gui PolySynth tail tests",
        "webview-gui",
        "https://github.com/hemduf/webview-gui",
        "0.1.0",
        hostGetExtension,
        hostRequestRestart,
        hostRequestProcess,
        hostRequestCallback,
    };

    const auto *plugin = factory->create_plugin(factory, &host, kPolySynthPluginId);
    if (!plugin)
        return 2;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 3;
    }

    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *tail = static_cast<const clap_plugin_tail_t *>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    if (!params || !params->get_value || !params->flush || !tail || !tail->get) {
        plugin->destroy(plugin);
        return 4;
    }

    // Release becomes a published 0.25-second parameter. At 48 kHz the finite
    // CLAP tail must therefore be 12,000 samples before any user automation.
    double releaseSeconds = -1.0;
    if (!params->get_value(plugin, kAmpReleaseId, &releaseSeconds) ||
        std::fabs(releaseSeconds - 0.25) > 1.0e-6 ||
        !plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 5;
    }

    constexpr std::uint32_t kDefaultTail48k = 12000u;
    const auto initialTail = tail->get(plugin);
    if (initialTail != kDefaultTail48k ||
        initialTail >= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        hostState.changedCalls != 0u) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 6;
    }

    // Active params.flush() executes on the audio thread. A Release change must
    // update the actual VoiceEngine default, publish the new finite tail and call
    // host.tail.changed() exactly once. Merely reading tail must not notify again.
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 7;
    }
    plugin->stop_processing(plugin);
    if (!setRelease(plugin, params, 0.5) || tail->get(plugin) != 24000u ||
        hostState.changedCalls != 1u || tail->get(plugin) != 24000u ||
        hostState.changedCalls != 1u) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 8;
    }

    plugin->deactivate(plugin);

    // Inactive flush runs on the main thread, where CLAP forbids host.tail.changed().
    // Retain the new seconds value silently; activation at a different sample rate
    // must still expose the correct sample-domain tail immediately.
    if (!setRelease(plugin, params, 0.125) || hostState.changedCalls != 1u ||
        !plugin->activate(plugin, 96000.0, 1, 128)) {
        plugin->destroy(plugin);
        return 9;
    }
    if (tail->get(plugin) != 12000u || hostState.changedCalls != 1u) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 10;
    }

    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
