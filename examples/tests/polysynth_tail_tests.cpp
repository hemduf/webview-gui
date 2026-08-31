#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/tail.h>

#include <array>
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
        if (!events || !events->ctx || index != 0u)
            return nullptr;
        return &static_cast<const FlushInputEvents *>(events->ctx)->event.header;
    }

    clap_event_param_value_t event{};
    clap_input_events_t input{};
};

struct NoteInputEvents {
    NoteInputEvents(std::uint16_t type, std::int32_t noteId) noexcept {
        event = {};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = 69;
        event.velocity = 1.0;
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx ? 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx || index != 0u)
            return nullptr;
        return &static_cast<const NoteInputEvents *>(events->ctx)->event.header;
    }

    clap_event_note_t event{};
    clap_input_events_t input{};
};

struct AcceptingOutputEvents {
    AcceptingOutputEvents() noexcept {
        output.ctx = this;
        output.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t *events,
                                 const clap_event_header_t *header) noexcept {
        if (!events || !events->ctx || !header)
            return false;
        ++static_cast<AcceptingOutputEvents *>(events->ctx)->count;
        return true;
    }

    std::uint32_t count = 0;
    clap_output_events_t output{};
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

bool processNoteEvent(const clap_plugin_t *plugin,
                      const clap_input_events_t *events,
                      AcceptingOutputEvents &outputEvents) noexcept {
    constexpr std::uint32_t kFrames = 4u;
    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    std::array<float *, 2> channels{{left.data(), right.data()}};
    clap_audio_buffer_t output{};
    output.data32 = channels.data();
    output.channel_count = 2u;

    clap_process_t process{};
    process.frames_count = kFrames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    process.out_events = &outputEvents.output;
    return plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
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

    // Release is a published 0.25-second parameter. At 48 kHz the finite CLAP
    // tail is therefore 12,000 samples before any user automation.
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

    // Active params.flush() is an audio-thread path. Increasing Release while no
    // voice is active must publish the new tail and notify exactly once.
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

    // Start one generation while the 0.5-second Release default is active. It
    // snapshots 24,000 samples and must keep that lifecycle after the host changes
    // the default for future voices.
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 9;
    }
    NoteInputEvents longNoteOn(CLAP_EVENT_NOTE_ON, 700);
    AcceptingOutputEvents longNoteOnOutput;
    if (!processNoteEvent(plugin, &longNoteOn.input, longNoteOnOutput)) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 10;
    }
    plugin->stop_processing(plugin);

    // A shorter default cannot lower the advertised tail while that older voice
    // is alive. No host notification is required because the published tail did
    // not change, even though get_value() immediately reflects the new base value.
    if (!setRelease(plugin, params, 0.125) || tail->get(plugin) != 24000u ||
        hostState.changedCalls != 1u) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 11;
    }

    // A new generation now snapshots the shorter 6,000-sample Release. Keeping
    // it active after the long generation is retired distinguishes the exact
    // max-active-snapshot contract from a coarse "any voice active" hold.
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 12;
    }
    NoteInputEvents shortNoteOn(CLAP_EVENT_NOTE_ON, 701);
    AcceptingOutputEvents shortNoteOnOutput;
    if (!processNoteEvent(plugin, &shortNoteOn.input, shortNoteOnOutput) ||
        tail->get(plugin) != 24000u || hostState.changedCalls != 1u) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 13;
    }
    plugin->stop_processing(plugin);

    // Retiring only the old long generation must immediately lower the published
    // tail to max(default=6000, remaining active snapshot=6000), even though the
    // shorter generation is still active. This is the exact CLAP tail contract.
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 14;
    }
    NoteInputEvents chokeLong(CLAP_EVENT_NOTE_CHOKE, 700);
    AcceptingOutputEvents chokeLongOutput;
    if (!processNoteEvent(plugin, &chokeLong.input, chokeLongOutput) ||
        tail->get(plugin) != 6000u || hostState.changedCalls != 2u) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 15;
    }
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);

    // Inactive flush runs on the main thread, where CLAP forbids host.tail.changed().
    // Retain the seconds value silently; activation at a different sample rate
    // exposes the correct sample-domain tail immediately without a callback.
    if (!setRelease(plugin, params, 0.125) || hostState.changedCalls != 2u ||
        !plugin->activate(plugin, 96000.0, 1, 128)) {
        plugin->destroy(plugin);
        return 16;
    }
    if (tail->get(plugin) != 12000u || hostState.changedCalls != 2u) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 17;
    }

    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
