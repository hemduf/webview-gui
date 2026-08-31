#include "polysynth_parameters.h"
#include "polysynth_plugin.h"
#include "polysynth_voice_allocator.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/remote-controls.h>
#include <clap/ext/state.h>
#include <clap/ext/voice-info.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id kCutoffId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FilterCutoff);
constexpr clap_id kResonanceId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FilterResonance);

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

struct FlushInputEvents {
    explicit FlushInputEvents(double value) noexcept {
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kCutoffId;
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

struct MemoryOutputStream {
    MemoryOutputStream() noexcept {
        stream.ctx = this;
        stream.write = write;
    }

    static std::int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                       const void *source,
                                       std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!source && size != 0u))
            return -1;
        auto &self = *static_cast<MemoryOutputStream *>(stream->ctx);
        if (size > self.bytes.size() - self.used)
            return -1;
        std::memcpy(self.bytes.data() + self.used, source, static_cast<std::size_t>(size));
        self.used += static_cast<std::size_t>(size);
        return static_cast<std::int64_t>(size);
    }

    std::array<std::uint8_t, 64> bytes{};
    std::size_t used = 0;
    clap_ostream_t stream{};
};

struct MemoryInputStream {
    MemoryInputStream(const std::uint8_t *bytes,
                      std::size_t size) noexcept
        : bytes(bytes), size(size) {
        stream.ctx = this;
        stream.read = read;
    }

    static std::int64_t CLAP_ABI read(const clap_istream_t *stream,
                                      void *destination,
                                      std::uint64_t requested) noexcept {
        if (!stream || !stream->ctx || (!destination && requested != 0u))
            return -1;
        auto &self = *static_cast<MemoryInputStream *>(stream->ctx);
        if (self.offset >= self.size)
            return 0;
        const auto remaining = self.size - self.offset;
        const auto count = static_cast<std::size_t>(
            requested < remaining ? requested : static_cast<std::uint64_t>(remaining));
        std::memcpy(destination, self.bytes + self.offset, count);
        self.offset += count;
        return static_cast<std::int64_t>(count);
    }

    const std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    clap_istream_t stream{};
};

std::uint32_t loadU32Le(const std::uint8_t *source) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(source[i]) << (i * 8u);
    return value;
}

bool checkFilterHostContract(const clap_plugin_t *plugin) noexcept {
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *remote = static_cast<const clap_plugin_remote_controls_t *>(
        plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS));
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!params || !params->count || !params->get_info || !params->get_value ||
        !params->value_to_text || !params->text_to_value || !params->flush ||
        !remote || !remote->count || !remote->get || !state || !state->save || !state->load)
        return false;

    // Preserve the six existing host indices; publish Resonance as stable index 6.
    if (params->count(plugin) != 7u)
        return false;

    clap_param_info_t cutoffInfo{};
    if (!params->get_info(plugin, 5u, &cutoffInfo) || cutoffInfo.id != kCutoffId ||
        cutoffInfo.flags != webview_gui::examples::polysynth::kPolyphonicParameterFlags ||
        cutoffInfo.min_value != 20.0 || cutoffInfo.max_value != 20000.0 ||
        cutoffInfo.default_value != 6000.0 || std::strcmp(cutoffInfo.name, "Cutoff") != 0 ||
        std::strcmp(cutoffInfo.module, "Filter") != 0)
        return false;

    clap_param_info_t resonanceInfo{};
    if (!params->get_info(plugin, 6u, &resonanceInfo) || resonanceInfo.id != kResonanceId ||
        resonanceInfo.flags != webview_gui::examples::polysynth::kPolyphonicParameterFlags ||
        resonanceInfo.min_value != 0.0 || resonanceInfo.max_value != 0.99 ||
        resonanceInfo.default_value != 0.0 ||
        std::strcmp(resonanceInfo.name, "Resonance") != 0 ||
        std::strcmp(resonanceInfo.module, "Filter") != 0)
        return false;

    double cutoff = 0.0;
    double resonance = -1.0;
    if (!params->get_value(plugin, kCutoffId, &cutoff) || cutoff != 6000.0 ||
        !params->get_value(plugin, kResonanceId, &resonance) || resonance != 0.0)
        return false;

    char display[CLAP_NAME_SIZE]{};
    double parsed = 0.0;
    if (!params->value_to_text(plugin, kCutoffId, 12345.5, display, sizeof(display)) ||
        !params->text_to_value(plugin, kCutoffId, display, &parsed) || parsed != 12345.5)
        return false;

    // Inactive flush must retain only the global base so activation can replay it.
    FlushInputEvents setCutoff(4321.5);
    params->flush(plugin, &setCutoff.input, nullptr);
    if (!params->get_value(plugin, kCutoffId, &cutoff) || cutoff != 4321.5)
        return false;

    if (remote->count(plugin) != 3u)
        return false;
    clap_remote_controls_page_t filterPage{};
    if (!remote->get(plugin, 2u, &filterPage) || filterPage.page_id == CLAP_INVALID_ID ||
        filterPage.is_for_preset || std::strcmp(filterPage.section_name, "Filter") != 0 ||
        std::strcmp(filterPage.page_name, "Tone") != 0 ||
        filterPage.param_ids[0] != kCutoffId || filterPage.param_ids[1] != kResonanceId)
        return false;
    for (std::size_t index = 2; index < CLAP_REMOTE_CONTROLS_COUNT; ++index) {
        if (filterPage.param_ids[index] != CLAP_INVALID_ID)
            return false;
    }

    MemoryOutputStream saved;
    if (!state->save(plugin, &saved.stream) || saved.used != 44u ||
        loadU32Le(saved.bytes.data() + 8) != 7u)
        return false;

    FlushInputEvents mutateCutoff(9876.0);
    params->flush(plugin, &mutateCutoff.input, nullptr);
    if (!params->get_value(plugin, kCutoffId, &cutoff) || cutoff != 9876.0)
        return false;

    MemoryInputStream restore(saved.bytes.data(), saved.used);
    if (!state->load(plugin, &restore.stream) ||
        !params->get_value(plugin, kCutoffId, &cutoff) || cutoff != 4321.5)
        return false;

    return true;
}

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

    if (!checkFilterHostContract(plugin)) {
        plugin->destroy(plugin);
        return 4;
    }

    const auto *voiceInfo = static_cast<const clap_plugin_voice_info_t *>(
        plugin->get_extension(plugin, CLAP_EXT_VOICE_INFO));
    if (!voiceInfo || !voiceInfo->get) {
        plugin->destroy(plugin);
        return 5;
    }

    if (!plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 6;
    }

    clap_voice_info_t info{};
    if (!voiceInfo->get(plugin, &info)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 7;
    }

    // CLAP distinguishes the number of voices the current patch uses from the
    // fixed storage capacity available without reallocating. This reference
    // patch uses 16 voices while the RT allocator preallocates 64 slots.
    if (info.voice_count != kPolySynthDefaultVoiceCount ||
        info.voice_capacity != VoiceAllocator::kMaximumVoices ||
        (info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) == 0) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 8;
    }

    if (voiceInfo->get(plugin, nullptr)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 9;
    }

    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
