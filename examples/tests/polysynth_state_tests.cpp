#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id kFineTuneId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FineTuning);

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui PolySynth state tests",
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
        event.param_id = kFineTuneId;
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

struct ChunkedOutputStream {
    explicit ChunkedOutputStream(std::uint64_t chunk) noexcept : maxChunk(chunk) {
        stream.ctx = this;
        stream.write = write;
    }

    static std::int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                       const void *source,
                                       std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!source && size != 0))
            return -1;
        auto &self = *static_cast<ChunkedOutputStream *>(stream->ctx);
        if (self.used >= self.bytes.size())
            return -1;
        const auto remaining = static_cast<std::uint64_t>(self.bytes.size() - self.used);
        const auto count = std::min({size, self.maxChunk, remaining});
        if (count == 0)
            return -1;
        std::memcpy(self.bytes.data() + self.used, source, static_cast<std::size_t>(count));
        self.used += static_cast<std::size_t>(count);
        ++self.calls;
        return static_cast<std::int64_t>(count);
    }

    std::array<std::uint8_t, 128> bytes{};
    std::size_t used = 0;
    std::uint64_t maxChunk = 1;
    std::uint32_t calls = 0;
    clap_ostream_t stream{};
};

struct ChunkedInputStream {
    ChunkedInputStream(const std::uint8_t *data,
                       std::size_t dataSize,
                       std::uint64_t chunk) noexcept
        : bytes(data), sizeBytes(dataSize), maxChunk(chunk) {
        stream.ctx = this;
        stream.read = read;
    }

    static std::int64_t CLAP_ABI read(const clap_istream_t *stream,
                                      void *destination,
                                      std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!destination && size != 0))
            return -1;
        auto &self = *static_cast<ChunkedInputStream *>(stream->ctx);
        if (self.offset >= self.sizeBytes)
            return 0;
        const auto remaining = static_cast<std::uint64_t>(self.sizeBytes - self.offset);
        const auto count = std::min({size, self.maxChunk, remaining});
        if (count == 0)
            return 0;
        std::memcpy(destination, self.bytes + self.offset, static_cast<std::size_t>(count));
        self.offset += static_cast<std::size_t>(count);
        ++self.calls;
        return static_cast<std::int64_t>(count);
    }

    const std::uint8_t *bytes = nullptr;
    std::size_t sizeBytes = 0;
    std::size_t offset = 0;
    std::uint64_t maxChunk = 1;
    std::uint32_t calls = 0;
    clap_istream_t stream{};
};

bool setFineTune(const clap_plugin_t *plugin,
                 const clap_plugin_params_t *params,
                 double value) noexcept {
    if (!plugin || !params || !params->flush)
        return false;
    FlushInputEvents input(value);
    params->flush(plugin, &input.input, nullptr);
    double observed = 0.0;
    return params->get_value(plugin, kFineTuneId, &observed) &&
           std::fabs(observed - value) <= 1.0e-6;
}

bool getFineTune(const clap_plugin_t *plugin,
                 const clap_plugin_params_t *params,
                 double expected) noexcept {
    double value = 0.0;
    return params->get_value(plugin, kFineTuneId, &value) &&
           std::fabs(value - expected) <= 1.0e-6;
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

    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!params || !params->get_value || !params->flush ||
        !state || !state->save || !state->load) {
        plugin->destroy(plugin);
        return 4;
    }

    if (!setFineTune(plugin, params, 37.5)) {
        plugin->destroy(plugin);
        return 5;
    }

    ChunkedOutputStream saved(3);
    if (!state->save(plugin, &saved.stream) || saved.used == 0 || saved.calls < 2) {
        plugin->destroy(plugin);
        return 6;
    }

    if (!setFineTune(plugin, params, -25.0)) {
        plugin->destroy(plugin);
        return 7;
    }

    ChunkedInputStream restore(saved.bytes.data(), saved.used, 2);
    if (!state->load(plugin, &restore.stream) || restore.calls < 2 ||
        !getFineTune(plugin, params, 37.5)) {
        plugin->destroy(plugin);
        return 8;
    }

    if (saved.used < 2) {
        plugin->destroy(plugin);
        return 9;
    }
    ChunkedInputStream truncated(saved.bytes.data(), saved.used - 1, 2);
    if (state->load(plugin, &truncated.stream) || !getFineTune(plugin, params, 37.5)) {
        plugin->destroy(plugin);
        return 10;
    }

    auto corruptedBytes = saved.bytes;
    corruptedBytes[0] ^= 0x7f;
    ChunkedInputStream corrupted(corruptedBytes.data(), saved.used, 2);
    if (state->load(plugin, &corrupted.stream) || !getFineTune(plugin, params, 37.5)) {
        plugin->destroy(plugin);
        return 11;
    }

    if (state->save(plugin, nullptr) || state->load(plugin, nullptr)) {
        plugin->destroy(plugin);
        return 12;
    }

    const auto *clone = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!clone || !clone->init(clone)) {
        if (clone)
            clone->destroy(clone);
        plugin->destroy(plugin);
        return 13;
    }
    const auto *cloneParams = static_cast<const clap_plugin_params_t *>(
        clone->get_extension(clone, CLAP_EXT_PARAMS));
    const auto *cloneState = static_cast<const clap_plugin_state_t *>(
        clone->get_extension(clone, CLAP_EXT_STATE));
    ChunkedInputStream cloneRestore(saved.bytes.data(), saved.used, 1);
    if (!cloneParams || !cloneState ||
        !cloneState->load(clone, &cloneRestore.stream) ||
        !getFineTune(clone, cloneParams, 37.5) ||
        !clone->activate(clone, 48000.0, 1, 64)) {
        clone->destroy(clone);
        plugin->destroy(plugin);
        return 14;
    }

    clone->deactivate(clone);
    clone->destroy(clone);
    plugin->destroy(plugin);
    return 0;
}
