#include "polysynth_plugin.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>

#include <array>
#include <cmath>
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
    "webview-gui PolySynth tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(std::uint16_t type,
                  std::uint32_t time,
                  std::int32_t noteId,
                  std::int16_t key) noexcept {
        if (count >= notes.size())
            return false;
        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = 1.0;
        headers[count] = &event.header;
        ++count;
        return true;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx
                   ? static_cast<const InputEvents *>(events->ctx)->count
                   : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(events->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, 4> notes{};
    std::array<const clap_event_header_t *, 4> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct OutputEvents {
    OutputEvents() noexcept {
        output.ctx = this;
        output.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t *events,
                                 const clap_event_header_t *header) noexcept {
        if (!events || !events->ctx || !header ||
            header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
            header->type != CLAP_EVENT_NOTE_END ||
            header->size != sizeof(clap_event_note_t))
            return false;

        auto &self = *static_cast<OutputEvents *>(events->ctx);
        if (self.count >= self.notes.size())
            return false;
        self.notes[self.count++] = *reinterpret_cast<const clap_event_note_t *>(header);
        return true;
    }

    std::array<clap_event_note_t, 4> notes{};
    std::uint32_t count = 0;
    clap_output_events_t output{};
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

    if (notePorts->count(plugin, true) != 1 || notePorts->count(plugin, false) != 1)
        return false;

    clap_note_port_info_t input{};
    if (!notePorts->get(plugin, 0, true, &input) ||
        input.id != webview_gui::examples::polysynth::kPolySynthNoteInputPortId ||
        input.supported_dialects != CLAP_NOTE_DIALECT_CLAP ||
        input.preferred_dialect != CLAP_NOTE_DIALECT_CLAP ||
        std::strcmp(input.name, "Notes In") != 0)
        return false;

    clap_note_port_info_t output{};
    if (!notePorts->get(plugin, 0, false, &output) ||
        output.supported_dialects != CLAP_NOTE_DIALECT_CLAP ||
        output.preferred_dialect != CLAP_NOTE_DIALECT_CLAP ||
        std::strcmp(output.name, "Notes Out") != 0)
        return false;
    return true;
}

bool checkProcessBridge(const clap_plugin_t *plugin) {
    constexpr std::uint32_t kFrames = 16;
    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    std::array<float *, 2> channels{left.data(), right.data()};

    clap_audio_buffer_t outputBuffer{};
    outputBuffer.data32 = channels.data();
    outputBuffer.channel_count = static_cast<std::uint32_t>(channels.size());

    InputEvents inputEvents;
    if (!inputEvents.pushNote(CLAP_EVENT_NOTE_ON, 4, 701, 69) ||
        !inputEvents.pushNote(CLAP_EVENT_NOTE_CHOKE, 8, 701, 69))
        return false;

    OutputEvents outputEvents;
    clap_process_t process{};
    process.steady_time = 0;
    process.frames_count = kFrames;
    process.transport = nullptr;
    process.audio_inputs = nullptr;
    process.audio_outputs = &outputBuffer;
    process.audio_inputs_count = 0;
    process.audio_outputs_count = 1;
    process.in_events = &inputEvents.input;
    process.out_events = &outputEvents.output;

    if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR)
        return false;

    // NOTE_ON at sample 4 must not leak audio into earlier samples. NOTE_CHOKE
    // at sample 8 must stop rendering at that exact boundary.
    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        if (left[frame] != 0.0f || right[frame] != 0.0f)
            return false;
    }
    if (!std::isfinite(left[4]) || !std::isfinite(right[4]) ||
        std::fabs(left[4]) < 0.5f || std::fabs(left[4] - right[4]) > 1.0e-6f)
        return false;
    for (std::uint32_t frame = 8; frame < kFrames; ++frame) {
        if (left[frame] != 0.0f || right[frame] != 0.0f)
            return false;
    }

    if (outputEvents.count != 1)
        return false;
    const auto &noteEnd = outputEvents.notes[0];
    return noteEnd.header.time == 8 &&
           noteEnd.header.type == CLAP_EVENT_NOTE_END &&
           noteEnd.note_id == 701 && noteEnd.port_index == 0 &&
           noteEnd.channel == 0 && noteEnd.key == 69;
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

    const bool processOk = checkProcessBridge(plugin);
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return processOk ? 0 : 8;
}
