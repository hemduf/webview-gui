#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/remote-controls.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id paramId(ParameterSlot slot) noexcept {
    return webview_gui::examples::polysynth::kFirstParameterId +
           static_cast<clap_id>(slot);
}
constexpr clap_id kMasterGainId = paramId(ParameterSlot::MasterGain);
constexpr clap_id kWaveformId = paramId(ParameterSlot::Waveform);
constexpr clap_id kCoarseTuneId = paramId(ParameterSlot::CoarseTuning);
constexpr clap_id kFineTuneId = paramId(ParameterSlot::FineTuning);
constexpr clap_id kCutoffId = paramId(ParameterSlot::FilterCutoff);
constexpr clap_id kResonanceId = paramId(ParameterSlot::FilterResonance);
constexpr clap_id kFilterEnvelopeAmountId = paramId(ParameterSlot::FilterEnvelopeAmount);
constexpr clap_id kPanId = paramId(ParameterSlot::Pan);
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

template <std::size_t Size>
void storeFloat(std::array<std::uint8_t, Size> &bytes,
                std::size_t offset,
                float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    storeU32Le(bytes.data() + offset, bits);
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
    storeFloat(bytes, 20, masterGainDb);
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

std::array<std::uint8_t, 32> version4State(double fineTune,
                                           float masterGainDb,
                                           std::uint32_t waveform,
                                           std::int32_t coarseTune) noexcept {
    std::array<std::uint8_t, 32> bytes{};
    const auto v3 = version3State(fineTune, masterGainDb, waveform);
    std::copy(v3.begin(), v3.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, 4u);
    std::uint32_t coarseBits = 0;
    std::memcpy(&coarseBits, &coarseTune, sizeof(coarseBits));
    storeU32Le(bytes.data() + 28, coarseBits);
    return bytes;
}

std::array<std::uint8_t, 36> version5State(double fineTune,
                                           float masterGainDb,
                                           std::uint32_t waveform,
                                           std::int32_t coarseTune,
                                           float pan) noexcept {
    std::array<std::uint8_t, 36> bytes{};
    const auto v4 = version4State(fineTune, masterGainDb, waveform, coarseTune);
    std::copy(v4.begin(), v4.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, 5u);
    storeFloat(bytes, 32, pan);
    return bytes;
}

std::array<std::uint8_t, 40> version6State(double fineTune,
                                           float masterGainDb,
                                           std::uint32_t waveform,
                                           std::int32_t coarseTune,
                                           float pan,
                                           float cutoff) noexcept {
    std::array<std::uint8_t, 40> bytes{};
    const auto v5 = version5State(fineTune, masterGainDb, waveform, coarseTune, pan);
    std::copy(v5.begin(), v5.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, 6u);
    storeFloat(bytes, 36, cutoff);
    return bytes;
}

std::array<std::uint8_t, 44> version7State(double fineTune,
                                           float masterGainDb,
                                           std::uint32_t waveform,
                                           std::int32_t coarseTune,
                                           float pan,
                                           float cutoff,
                                           float resonance) noexcept {
    std::array<std::uint8_t, 44> bytes{};
    const auto v6 = version6State(fineTune, masterGainDb, waveform, coarseTune, pan, cutoff);
    std::copy(v6.begin(), v6.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, 7u);
    storeFloat(bytes, 40, resonance);
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

bool verifyLegacyState(const clap_plugin_factory_t *factory,
                       const std::uint8_t *bytes,
                       std::size_t size,
                       double fine,
                       double gain,
                       double waveform,
                       double coarse,
                       double pan,
                       double cutoff,
                       double resonance,
                       double filterEnvelopeAmount) noexcept {
    const auto *plugin = factory->create_plugin(
        factory, &kHost, webview_gui::examples::polysynth::kPolySynthPluginId);
    if (!plugin || !plugin->init(plugin)) {
        if (plugin)
            plugin->destroy(plugin);
        return false;
    }
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    ChunkedInputStream input(bytes, size, 2);
    const bool ok = params && state && state->load(plugin, &input.stream) &&
                    getParameter(plugin, params, kFineTuneId, fine) &&
                    getParameter(plugin, params, kMasterGainId, gain) &&
                    getParameter(plugin, params, kWaveformId, waveform) &&
                    getParameter(plugin, params, kCoarseTuneId, coarse) &&
                    getParameter(plugin, params, kPanId, pan) &&
                    getParameter(plugin, params, kCutoffId, cutoff) &&
                    getParameter(plugin, params, kResonanceId, resonance) &&
                    getParameter(plugin, params, kFilterEnvelopeAmountId,
                                 filterEnvelopeAmount);
    plugin->destroy(plugin);
    return ok;
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
    const auto *remote = static_cast<const clap_plugin_remote_controls_t *>(
        plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS));
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!params || !params->get_value || !params->flush || !params->get_info ||
        !params->value_to_text || !params->text_to_value || params->count(plugin) != 8u ||
        !remote || !remote->count || !remote->get || remote->count(plugin) != 3u ||
        !state || !state->save || !state->load) {
        plugin->destroy(plugin);
        return 4;
    }

    clap_param_info_t filterEnvelopeInfo{};
    if (!params->get_info(plugin, 7u, &filterEnvelopeInfo) ||
        filterEnvelopeInfo.id != kFilterEnvelopeAmountId ||
        std::strcmp(filterEnvelopeInfo.name, "Filter Env") != 0 ||
        std::strcmp(filterEnvelopeInfo.module, "Filter") != 0 ||
        filterEnvelopeInfo.min_value != -1.0 || filterEnvelopeInfo.max_value != 1.0 ||
        filterEnvelopeInfo.default_value != 0.0 ||
        (filterEnvelopeInfo.flags & CLAP_PARAM_IS_MODULATABLE) == 0u ||
        (filterEnvelopeInfo.flags & CLAP_PARAM_REQUIRES_PROCESS) == 0u) {
        plugin->destroy(plugin);
        return 5;
    }

    std::array<char, CLAP_NAME_SIZE> filterEnvelopeText{};
    double parsedFilterEnvelope = -2.0;
    if (!params->value_to_text(plugin,
                               kFilterEnvelopeAmountId,
                               0.75,
                               filterEnvelopeText.data(),
                               static_cast<std::uint32_t>(filterEnvelopeText.size())) ||
        !params->text_to_value(plugin,
                               kFilterEnvelopeAmountId,
                               filterEnvelopeText.data(),
                               &parsedFilterEnvelope) ||
        std::fabs(parsedFilterEnvelope - 0.75) > 1.0e-12) {
        plugin->destroy(plugin);
        return 6;
    }

    clap_remote_controls_page filterPage{};
    if (!remote->get(plugin, 2u, &filterPage) || filterPage.param_ids[0] != kCutoffId ||
        filterPage.param_ids[1] != kResonanceId ||
        filterPage.param_ids[2] != kFilterEnvelopeAmountId) {
        plugin->destroy(plugin);
        return 7;
    }

    if (!setParameter(plugin, params, kFilterEnvelopeAmountId, -0.25) ||
        !plugin->activate(plugin, 48000.0, 1, 64) ||
        !setParameter(plugin, params, kFilterEnvelopeAmountId, 0.5)) {
        plugin->destroy(plugin);
        return 8;
    }
    plugin->deactivate(plugin);

    if (!setParameter(plugin, params, kFineTuneId, 37.5) ||
        !setParameter(plugin, params, kMasterGainId, kQuarterGainDb) ||
        !setParameter(plugin, params, kWaveformId, 2.0) ||
        !setParameter(plugin, params, kCoarseTuneId, -17.0) ||
        !setParameter(plugin, params, kPanId, 0.625) ||
        !setParameter(plugin, params, kCutoffId, 4321.5) ||
        !setParameter(plugin, params, kResonanceId, 0.875) ||
        !setParameter(plugin, params, kFilterEnvelopeAmountId, 0.625)) {
        plugin->destroy(plugin);
        return 9;
    }

    ChunkedOutputStream saved(3);
    if (!state->save(plugin, &saved.stream) || saved.used != 48u || saved.calls < 2 ||
        loadU32Le(saved.bytes.data() + 8) != 8u) {
        plugin->destroy(plugin);
        return 10;
    }

    if (!setParameter(plugin, params, kFineTuneId, -25.0) ||
        !setParameter(plugin, params, kMasterGainId, -3.0) ||
        !setParameter(plugin, params, kWaveformId, 1.0) ||
        !setParameter(plugin, params, kCoarseTuneId, 12.0) ||
        !setParameter(plugin, params, kPanId, -0.25) ||
        !setParameter(plugin, params, kCutoffId, 9876.0) ||
        !setParameter(plugin, params, kResonanceId, 0.125) ||
        !setParameter(plugin, params, kFilterEnvelopeAmountId, -0.875)) {
        plugin->destroy(plugin);
        return 11;
    }

    ChunkedInputStream restore(saved.bytes.data(), saved.used, 2);
    if (!state->load(plugin, &restore.stream) || restore.calls < 2 ||
        !getParameter(plugin, params, kFineTuneId, 37.5) ||
        !getParameter(plugin, params, kMasterGainId, kQuarterGainDb) ||
        !getParameter(plugin, params, kWaveformId, 2.0) ||
        !getParameter(plugin, params, kCoarseTuneId, -17.0) ||
        !getParameter(plugin, params, kPanId, 0.625) ||
        !getParameter(plugin, params, kCutoffId, 4321.5) ||
        !getParameter(plugin, params, kResonanceId, 0.875) ||
        !getParameter(plugin, params, kFilterEnvelopeAmountId, 0.625)) {
        plugin->destroy(plugin);
        return 12;
    }

    ChunkedInputStream truncated(saved.bytes.data(), saved.used - 1, 2);
    if (state->load(plugin, &truncated.stream) ||
        !getParameter(plugin, params, kFilterEnvelopeAmountId, 0.625)) {
        plugin->destroy(plugin);
        return 13;
    }

    auto corruptedBytes = saved.bytes;
    corruptedBytes[0] ^= 0x7f;
    ChunkedInputStream corrupted(corruptedBytes.data(), saved.used, 2);
    if (state->load(plugin, &corrupted.stream) ||
        !getParameter(plugin, params, kFilterEnvelopeAmountId, 0.625)) {
        plugin->destroy(plugin);
        return 14;
    }

    if (state->save(plugin, nullptr) || state->load(plugin, nullptr)) {
        plugin->destroy(plugin);
        return 15;
    }

    const auto *clone = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!clone || !clone->init(clone)) {
        if (clone)
            clone->destroy(clone);
        plugin->destroy(plugin);
        return 16;
    }
    const auto *cloneParams = static_cast<const clap_plugin_params_t *>(
        clone->get_extension(clone, CLAP_EXT_PARAMS));
    const auto *cloneState = static_cast<const clap_plugin_state_t *>(
        clone->get_extension(clone, CLAP_EXT_STATE));
    ChunkedInputStream cloneRestore(saved.bytes.data(), saved.used, 1);
    if (!cloneParams || !cloneState || !cloneState->load(clone, &cloneRestore.stream) ||
        !getParameter(clone, cloneParams, kPanId, 0.625) ||
        !getParameter(clone, cloneParams, kCutoffId, 4321.5) ||
        !getParameter(clone, cloneParams, kResonanceId, 0.875) ||
        !getParameter(clone, cloneParams, kFilterEnvelopeAmountId, 0.625) ||
        !clone->activate(clone, 48000.0, 1, 64)) {
        clone->destroy(clone);
        plugin->destroy(plugin);
        return 17;
    }
    clone->deactivate(clone);
    clone->destroy(clone);

    const auto v1 = legacyState(-12.5);
    const auto v2 = version2State(21.0, -6.0f);
    const auto v3 = version3State(-9.0, -4.0f, 1u);
    const auto v4 = version4State(11.0, -2.0f, 2u, -7);
    const auto v5 = version5State(13.0, -1.0f, 1u, 5, -0.375f);
    const auto v6 = version6State(-15.0, -5.0f, 2u, -11, 0.25f, 8765.0f);
    const auto v7 = version7State(7.5, -7.0f, 1u, 9, -0.125f, 7654.0f, 0.375f);
    constexpr double kLegacyCutoffDefault = 6000.0;
    constexpr double kLegacyResonanceDefault = 0.0;
    constexpr double kLegacyFilterEnvelopeDefault = 0.0;
    if (!verifyLegacyState(factory, v1.data(), v1.size(), -12.5, 0.0, 0.0, 0.0, 0.0,
                           kLegacyCutoffDefault, kLegacyResonanceDefault,
                           kLegacyFilterEnvelopeDefault) ||
        !verifyLegacyState(factory, v2.data(), v2.size(), 21.0, -6.0, 0.0, 0.0, 0.0,
                           kLegacyCutoffDefault, kLegacyResonanceDefault,
                           kLegacyFilterEnvelopeDefault) ||
        !verifyLegacyState(factory, v3.data(), v3.size(), -9.0, -4.0, 1.0, 0.0, 0.0,
                           kLegacyCutoffDefault, kLegacyResonanceDefault,
                           kLegacyFilterEnvelopeDefault) ||
        !verifyLegacyState(factory, v4.data(), v4.size(), 11.0, -2.0, 2.0, -7.0, 0.0,
                           kLegacyCutoffDefault, kLegacyResonanceDefault,
                           kLegacyFilterEnvelopeDefault) ||
        !verifyLegacyState(factory, v5.data(), v5.size(), 13.0, -1.0, 1.0, 5.0, -0.375,
                           kLegacyCutoffDefault, kLegacyResonanceDefault,
                           kLegacyFilterEnvelopeDefault) ||
        !verifyLegacyState(factory, v6.data(), v6.size(), -15.0, -5.0, 2.0, -11.0, 0.25,
                           8765.0, kLegacyResonanceDefault,
                           kLegacyFilterEnvelopeDefault) ||
        !verifyLegacyState(factory, v7.data(), v7.size(), 7.5, -7.0, 1.0, 9.0, -0.125,
                           7654.0, 0.375, kLegacyFilterEnvelopeDefault)) {
        plugin->destroy(plugin);
        return 18;
    }

    plugin->destroy(plugin);
    return 0;
}
