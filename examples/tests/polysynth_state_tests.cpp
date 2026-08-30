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
constexpr clap_id kWaveformId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::Waveform);
constexpr clap_id kCoarseTuneId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::CoarseTuning);
constexpr clap_id kFineTuneId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FineTuning);
constexpr double kQuarterGainDb = -12.041199826559248;

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) { return nullptr; }
void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION, nullptr, "webview-gui PolySynth state tests", "webview-gui",
    "https://github.com/hemduf/webview-gui", "0.1.0", hostGetExtension,
    hostRequestRestart, hostRequestProcess, hostRequestCallback,
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

std::array<std::uint8_t, 24> version2State(double fineTune, float masterGainDb) noexcept {
    auto bytes = legacyState(fineTune);
    storeU32Le(bytes.data() + 8, 2u);
    std::uint32_t masterGainBits = 0;
    std::memcpy(&masterGainBits, &masterGainDb, sizeof(masterGainBits));
    storeU32Le(bytes.data() + 20, masterGainBits);
    return bytes;
}

std::array<std::uint8_t, 28> version3State(double fineTune,
                                           float masterGainDb,
                                           std::uint32_t waveform) noexcept {
    std::array<std::uint8_t, 28> bytes{};
    const auto v2 = version2State(fineTune, masterGainDb);
    std::copy(v2.begin(), v2.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, 3u);
    storeU32Le(bytes.data() + 24, waveform);
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
    if (!params || !params->get_value || !params->flush || params->count(plugin) != 4u ||
        !state || !state->save || !state->load) {
        plugin->destroy(plugin);
        return 4;
    }

    if (!setParameter(plugin, params, kFineTuneId, 37.5) ||
        !setParameter(plugin, params, kMasterGainId, kQuarterGainDb) ||
        !setParameter(plugin, params, kWaveformId, 2.0) ||
        !setParameter(plugin, params, kCoarseTuneId, -17.0)) {
        plugin->destroy(plugin);
        return 5;
    }

    ChunkedOutputStream saved(3);
    if (!state->save(plugin, &saved.stream) || saved.used != 32u || saved.calls < 2 ||
        loadU32Le(saved.bytes.data() + 8) != 4u) {
        plugin->destroy(plugin);
        return 6;
    }

    if (!setParameter(plugin, params, kFineTuneId, -25.0) ||
        !setParameter(plugin, params, kMasterGainId, -3.0) ||
        !setParameter(plugin, params, kWaveformId, 1.0) ||
        !setParameter(plugin, params, kCoarseTuneId, 12.0)) {
        plugin->destroy(plugin);
        return 7;
    }

    ChunkedInputStream restore(saved.bytes.data(), saved.used, 2);
    if (!state->load(plugin, &restore.stream) || restore.calls < 2 ||
        !getParameter(plugin, params, kFineTuneId, 37.5) ||
        !getParameter(plugin, params, kMasterGainId, kQuarterGainDb) ||
        !getParameter(plugin, params, kWaveformId, 2.0) ||
        !getParameter(plugin, params, kCoarseTuneId, -17.0)) {
        plugin->destroy(plugin);
        return 8;
    }

    ChunkedInputStream truncated(saved.bytes.data(), saved.used - 1, 2);
    if (state->load(plugin, &truncated.stream) ||
        !getParameter(plugin, params, kCoarseTuneId, -17.0)) {
        plugin->destroy(plugin);
        return 9;
    }

    auto corruptedBytes = saved.bytes;
    corruptedBytes[0] ^= 0x7f;
    ChunkedInputStream corrupted(corruptedBytes.data(), saved.used, 2);
    if (state->load(plugin, &corrupted.stream) ||
        !getParameter(plugin, params, kCoarseTuneId, -17.0)) {
        plugin->destroy(plugin);
        return 10;
    }

    if (state->save(plugin, nullptr) || state->load(plugin, nullptr)) {
        plugin->destroy(plugin);
        return 11;
    }

    const auto *clone = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!clone || !clone->init(clone)) {
        if (clone)
            clone->destroy(clone);
        plugin->destroy(plugin);
        return 12;
    }
    const auto *cloneParams = static_cast<const clap_plugin_params_t *>(
        clone->get_extension(clone, CLAP_EXT_PARAMS));
    const auto *cloneState = static_cast<const clap_plugin_state_t *>(
        clone->get_extension(clone, CLAP_EXT_STATE));
    ChunkedInputStream cloneRestore(saved.bytes.data(), saved.used, 1);
    if (!cloneParams || !cloneState || !cloneState->load(clone, &cloneRestore.stream) ||
        !getParameter(clone, cloneParams, kFineTuneId, 37.5) ||
        !getParameter(clone, cloneParams, kMasterGainId, kQuarterGainDb) ||
        !getParameter(clone, cloneParams, kWaveformId, 2.0) ||
        !getParameter(clone, cloneParams, kCoarseTuneId, -17.0) ||
        !clone->activate(clone, 48000.0, 1, 64)) {
        clone->destroy(clone);
        plugin->destroy(plugin);
        return 13;
    }
    clone->deactivate(clone);
    clone->destroy(clone);

    const auto legacyBytes = legacyState(-12.5);
    const auto *legacy = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!legacy || !legacy->init(legacy)) {
        if (legacy)
            legacy->destroy(legacy);
        plugin->destroy(plugin);
        return 14;
    }
    const auto *legacyParams = static_cast<const clap_plugin_params_t *>(
        legacy->get_extension(legacy, CLAP_EXT_PARAMS));
    const auto *legacyStateExt = static_cast<const clap_plugin_state_t *>(
        legacy->get_extension(legacy, CLAP_EXT_STATE));
    ChunkedInputStream legacyRestore(legacyBytes.data(), legacyBytes.size(), 2);
    if (!legacyParams || !legacyStateExt || !legacyStateExt->load(legacy, &legacyRestore.stream) ||
        !getParameter(legacy, legacyParams, kFineTuneId, -12.5) ||
        !getParameter(legacy, legacyParams, kMasterGainId, 0.0) ||
        !getParameter(legacy, legacyParams, kWaveformId, 0.0) ||
        !getParameter(legacy, legacyParams, kCoarseTuneId, 0.0)) {
        legacy->destroy(legacy);
        plugin->destroy(plugin);
        return 15;
    }
    legacy->destroy(legacy);

    const auto v2Bytes = version2State(21.0, -6.0f);
    const auto *v2 = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!v2 || !v2->init(v2)) {
        if (v2)
            v2->destroy(v2);
        plugin->destroy(plugin);
        return 16;
    }
    const auto *v2Params = static_cast<const clap_plugin_params_t *>(
        v2->get_extension(v2, CLAP_EXT_PARAMS));
    const auto *v2StateExt = static_cast<const clap_plugin_state_t *>(
        v2->get_extension(v2, CLAP_EXT_STATE));
    ChunkedInputStream v2Restore(v2Bytes.data(), v2Bytes.size(), 2);
    if (!v2Params || !v2StateExt || !v2StateExt->load(v2, &v2Restore.stream) ||
        !getParameter(v2, v2Params, kFineTuneId, 21.0) ||
        !getParameter(v2, v2Params, kMasterGainId, -6.0) ||
        !getParameter(v2, v2Params, kWaveformId, 0.0) ||
        !getParameter(v2, v2Params, kCoarseTuneId, 0.0)) {
        v2->destroy(v2);
        plugin->destroy(plugin);
        return 17;
    }
    v2->destroy(v2);

    const auto v3Bytes = version3State(-9.0, -4.0f, 1u);
    const auto *v3 = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!v3 || !v3->init(v3)) {
        if (v3)
            v3->destroy(v3);
        plugin->destroy(plugin);
        return 18;
    }
    const auto *v3Params = static_cast<const clap_plugin_params_t *>(
        v3->get_extension(v3, CLAP_EXT_PARAMS));
    const auto *v3StateExt = static_cast<const clap_plugin_state_t *>(
        v3->get_extension(v3, CLAP_EXT_STATE));
    ChunkedInputStream v3Restore(v3Bytes.data(), v3Bytes.size(), 2);
    if (!v3Params || !v3StateExt || !v3StateExt->load(v3, &v3Restore.stream) ||
        !getParameter(v3, v3Params, kFineTuneId, -9.0) ||
        !getParameter(v3, v3Params, kMasterGainId, -4.0) ||
        !getParameter(v3, v3Params, kWaveformId, 1.0) ||
        !getParameter(v3, v3Params, kCoarseTuneId, 0.0)) {
        v3->destroy(v3);
        plugin->destroy(plugin);
        return 19;
    }
    v3->destroy(v3);

    plugin->destroy(plugin);
    return 0;
}
