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

constexpr clap_id kMasterGainId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::MasterGain);
constexpr clap_id kFineTuneId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FineTuning);
constexpr double kQuarterGainDb = -12.041199826559248;

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
    FlushInputEvents(clap_id paramId, double value) noexcept {
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = paramId;
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

void storeU32Le(std::uint8_t *destination, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        destination[i] = static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu);
}

void storeU64Le(std::uint8_t *destination, std::uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        destination[i] = static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu);
}

std::uint32_t loadU32Le(const std::uint8_t *source) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(source[i]) << (i * 8u);
    return value;
}

std::array<std::uint8_t, 24> legacyState(double fineTune) noexcept {
    std::array<std::uint8_t, 24> bytes{};
    constexpr std::array<std::uint8_t, 8> magic{{'W', 'V', 'P', 'S', 'Y', 'N', 'T', 'H'}};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, 1u);
    std::uint64_t fineTuneBits = 0;
    std::memcpy(&fineTuneBits, &fineTune, sizeof(fineTuneBits));
    storeU64Le(bytes.data() + 12, fineTuneBits);
    return bytes;
}

bool setParameter(const clap_plugin_t *plugin,
                  const clap_plugin_params_t *params,
                  clap_id paramId,
                  double value) noexcept {
    if (!plugin || !params || !params->flush)
        return false;
    FlushInputEvents input(paramId, value);
    params->flush(plugin, &input.input, nullptr);
    double observed = 0.0;
    return params->get_value(plugin, paramId, &observed) &&
           std::fabs(observed - value) <= 1.0e-5;
}

bool getParameter(const clap_plugin_t *plugin,
                  const clap_plugin_params_t *params,
                  clap_id paramId,
                  double expected) noexcept {
    double value = 0.0;
    return params->get_value(plugin, paramId, &value) &&
           std::fabs(value - expected) <= 1.0e-5;
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
    if (!params || !params->get_value || !params->flush || params->count(plugin) != 2u ||
        !state || !state->save || !state->load) {
        plugin->destroy(plugin);
        return 4;
    }

    if (!setParameter(plugin, params, kFineTuneId, 37.5) ||
        !setParameter(plugin, params, kMasterGainId, kQuarterGainDb)) {
        plugin->destroy(plugin);
        return 5;
    }

    ChunkedOutputStream saved(3);
    if (!state->save(plugin, &saved.stream) || saved.used != 24u || saved.calls < 2 ||
        loadU32Le(saved.bytes.data() + 8) != 2u) {
        plugin->destroy(plugin);
        return 6;
    }

    if (!setParameter(plugin, params, kFineTuneId, -25.0) ||
        !setParameter(plugin, params, kMasterGainId, -3.0)) {
        plugin->destroy(plugin);
        return 7;
    }

    ChunkedInputStream restore(saved.bytes.data(), saved.used, 2);
    if (!state->load(plugin, &restore.stream) || restore.calls < 2 ||
        !getParameter(plugin, params, kFineTuneId, 37.5) ||
        !getParameter(plugin, params, kMasterGainId, kQuarterGainDb)) {
        plugin->destroy(plugin);
        return 8;
    }

    if (saved.used < 2) {
        plugin->destroy(plugin);
        return 9;
    }
    ChunkedInputStream truncated(saved.bytes.data(), saved.used - 1, 2);
    if (state->load(plugin, &truncated.stream) ||
        !getParameter(plugin, params, kFineTuneId, 37.5) ||
        !getParameter(plugin, params, kMasterGainId, kQuarterGainDb)) {
        plugin->destroy(plugin);
        return 10;
    }

    auto corruptedBytes = saved.bytes;
    corruptedBytes[0] ^= 0x7f;
    ChunkedInputStream corrupted(corruptedBytes.data(), saved.used, 2);
    if (state->load(plugin, &corrupted.stream) ||
        !getParameter(plugin, params, kFineTuneId, 37.5) ||
        !getParameter(plugin, params, kMasterGainId, kQuarterGainDb)) {
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
        !getParameter(clone, cloneParams, kFineTuneId, 37.5) ||
        !getParameter(clone, cloneParams, kMasterGainId, kQuarterGainDb) ||
        !clone->activate(clone, 48000.0, 1, 64)) {
        clone->destroy(clone);
        plugin->destroy(plugin);
        return 14;
    }

    clone->deactivate(clone);
    clone->destroy(clone);

    const auto legacyBytes = legacyState(-12.5);
    const auto *legacy = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!legacy || !legacy->init(legacy)) {
        if (legacy)
            legacy->destroy(legacy);
        plugin->destroy(plugin);
        return 15;
    }
    const auto *legacyParams = static_cast<const clap_plugin_params_t *>(
        legacy->get_extension(legacy, CLAP_EXT_PARAMS));
    const auto *legacyStateExt = static_cast<const clap_plugin_state_t *>(
        legacy->get_extension(legacy, CLAP_EXT_STATE));
    ChunkedInputStream legacyRestore(legacyBytes.data(), legacyBytes.size(), 2);
    if (!legacyParams || !legacyStateExt ||
        !legacyStateExt->load(legacy, &legacyRestore.stream) ||
        !getParameter(legacy, legacyParams, kFineTuneId, -12.5) ||
        !getParameter(legacy, legacyParams, kMasterGainId, 0.0)) {
        legacy->destroy(legacy);
        plugin->destroy(plugin);
        return 16;
    }

    legacy->destroy(legacy);
    plugin->destroy(plugin);
    return 0;
}
