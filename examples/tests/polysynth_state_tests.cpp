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
#include <limits>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id parameterId(ParameterSlot slot) noexcept {
    return webview_gui::examples::polysynth::kFirstParameterId +
           static_cast<clap_id>(slot);
}

constexpr clap_id kMasterGainId = parameterId(ParameterSlot::MasterGain);
constexpr clap_id kWaveformId = parameterId(ParameterSlot::Waveform);
constexpr clap_id kCoarseTuneId = parameterId(ParameterSlot::CoarseTuning);
constexpr clap_id kFineTuneId = parameterId(ParameterSlot::FineTuning);
constexpr clap_id kCutoffId = parameterId(ParameterSlot::FilterCutoff);
constexpr clap_id kResonanceId = parameterId(ParameterSlot::FilterResonance);
constexpr clap_id kAmpAttackId = parameterId(ParameterSlot::AmpAttack);
constexpr clap_id kAmpDecayId = parameterId(ParameterSlot::AmpDecay);
constexpr clap_id kAmpSustainId = parameterId(ParameterSlot::AmpSustain);
constexpr clap_id kAmpReleaseId = parameterId(ParameterSlot::AmpRelease);
constexpr clap_id kFilterEnvelopeId = parameterId(ParameterSlot::FilterEnvelopeAmount);
constexpr clap_id kPanId = parameterId(ParameterSlot::Pan);
constexpr clap_id kAmpLevelId = parameterId(ParameterSlot::AmpLevel);

constexpr std::array<std::size_t, 11> kVersionSizes{{
    0u, 24u, 24u, 28u, 32u, 36u, 40u, 44u, 48u, 52u, 68u,
}};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) { return nullptr; }
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

void storeU32(std::uint8_t *destination, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        destination[i] = static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu);
}

void storeU64(std::uint8_t *destination, std::uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        destination[i] = static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu);
}

std::uint32_t loadU32(const std::uint8_t *source) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(source[i]) << (8u * i);
    return value;
}

void storeFloat(std::array<std::uint8_t, 128> &bytes,
                std::size_t offset,
                float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    storeU32(bytes.data() + offset, bits);
}

void storeDouble(std::array<std::uint8_t, 128> &bytes,
                 std::size_t offset,
                 double value) noexcept {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    storeU64(bytes.data() + offset, bits);
}

struct MemoryOutputStream {
    explicit MemoryOutputStream(std::size_t chunk = 3u) noexcept : chunkSize(chunk) {
        stream.ctx = this;
        stream.write = write;
    }

    static std::int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                       const void *source,
                                       std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!source && size != 0u))
            return -1;
        auto &self = *static_cast<MemoryOutputStream *>(stream->ctx);
        if (self.used >= self.bytes.size())
            return -1;
        const auto remaining = self.bytes.size() - self.used;
        const auto count = std::min<std::size_t>(
            {remaining, static_cast<std::size_t>(size), self.chunkSize});
        if (count == 0u)
            return 0;
        std::memcpy(self.bytes.data() + self.used, source, count);
        self.used += count;
        ++self.calls;
        return static_cast<std::int64_t>(count);
    }

    std::array<std::uint8_t, 128> bytes{};
    std::size_t used = 0;
    std::size_t calls = 0;
    std::size_t chunkSize = 3;
    clap_ostream_t stream{};
};

struct MemoryInputStream {
    MemoryInputStream(const std::uint8_t *source,
                      std::size_t sourceSize,
                      std::size_t chunk = 2u) noexcept
        : bytes(source), sizeBytes(sourceSize), chunkSize(chunk) {
        stream.ctx = this;
        stream.read = read;
    }

    static std::int64_t CLAP_ABI read(const clap_istream_t *stream,
                                      void *destination,
                                      std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!destination && size != 0u))
            return -1;
        auto &self = *static_cast<MemoryInputStream *>(stream->ctx);
        if (self.offset >= self.sizeBytes)
            return 0;
        const auto count = std::min<std::size_t>(
            {self.sizeBytes - self.offset,
             static_cast<std::size_t>(size),
             self.chunkSize});
        if (count == 0u)
            return 0;
        std::memcpy(destination, self.bytes + self.offset, count);
        self.offset += count;
        ++self.calls;
        return static_cast<std::int64_t>(count);
    }

    const std::uint8_t *bytes = nullptr;
    std::size_t sizeBytes = 0;
    std::size_t offset = 0;
    std::size_t calls = 0;
    std::size_t chunkSize = 2;
    clap_istream_t stream{};
};

struct FlushInputEvents {
    FlushInputEvents(clap_id id, double value) noexcept {
        event = {};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
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

bool approximately(double actual, double expected, double epsilon = 1.0e-5) noexcept {
    return std::fabs(actual - expected) <= epsilon;
}

bool setParameter(const clap_plugin_t *plugin,
                  const clap_plugin_params_t *params,
                  clap_id id,
                  double value) noexcept {
    FlushInputEvents event(id, value);
    params->flush(plugin, &event.input, nullptr);
    double observed = 0.0;
    return params->get_value(plugin, id, &observed) && approximately(observed, value);
}

bool getParameter(const clap_plugin_t *plugin,
                  const clap_plugin_params_t *params,
                  clap_id id,
                  double expected) noexcept {
    double value = 0.0;
    return params->get_value(plugin, id, &value) && approximately(value, expected);
}

struct ParameterValues {
    double fine = 11.25;
    double gain = -3.5;
    double waveform = 2.0;
    double coarse = -7.0;
    double pan = 0.375;
    double cutoff = 6543.0;
    double resonance = 0.625;
    double filterEnvelope = -0.75;
    double ampLevel = 0.625;
    double attack = 0.125;
    double decay = 0.25;
    double sustain = 0.6;
    double release = 0.75;
};

bool setAll(const clap_plugin_t *plugin,
            const clap_plugin_params_t *params,
            const ParameterValues &values) noexcept {
    return setParameter(plugin, params, kFineTuneId, values.fine) &&
           setParameter(plugin, params, kMasterGainId, values.gain) &&
           setParameter(plugin, params, kWaveformId, values.waveform) &&
           setParameter(plugin, params, kCoarseTuneId, values.coarse) &&
           setParameter(plugin, params, kPanId, values.pan) &&
           setParameter(plugin, params, kCutoffId, values.cutoff) &&
           setParameter(plugin, params, kResonanceId, values.resonance) &&
           setParameter(plugin, params, kFilterEnvelopeId, values.filterEnvelope) &&
           setParameter(plugin, params, kAmpLevelId, values.ampLevel) &&
           setParameter(plugin, params, kAmpAttackId, values.attack) &&
           setParameter(plugin, params, kAmpDecayId, values.decay) &&
           setParameter(plugin, params, kAmpSustainId, values.sustain) &&
           setParameter(plugin, params, kAmpReleaseId, values.release);
}

bool hasAll(const clap_plugin_t *plugin,
            const clap_plugin_params_t *params,
            const ParameterValues &values) noexcept {
    return getParameter(plugin, params, kFineTuneId, values.fine) &&
           getParameter(plugin, params, kMasterGainId, values.gain) &&
           getParameter(plugin, params, kWaveformId, values.waveform) &&
           getParameter(plugin, params, kCoarseTuneId, values.coarse) &&
           getParameter(plugin, params, kPanId, values.pan) &&
           getParameter(plugin, params, kCutoffId, values.cutoff) &&
           getParameter(plugin, params, kResonanceId, values.resonance) &&
           getParameter(plugin, params, kFilterEnvelopeId, values.filterEnvelope) &&
           getParameter(plugin, params, kAmpLevelId, values.ampLevel) &&
           getParameter(plugin, params, kAmpAttackId, values.attack) &&
           getParameter(plugin, params, kAmpDecayId, values.decay) &&
           getParameter(plugin, params, kAmpSustainId, values.sustain) &&
           getParameter(plugin, params, kAmpReleaseId, values.release);
}

std::array<std::uint8_t, 128> makeVersionState(std::uint32_t version,
                                               const ParameterValues &values) noexcept {
    std::array<std::uint8_t, 128> bytes{};
    constexpr std::array<std::uint8_t, 8> magic{{'W', 'V', 'P', 'S', 'Y', 'N', 'T', 'H'}};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    storeU32(bytes.data() + 8, version);
    storeDouble(bytes, 12, values.fine);

    // Version 1 reserves bytes 20..23 as zero. Every later revision keeps the
    // exact prefix and appends one field, so this builder also verifies that the
    // v10 loader does not reinterpret any historical offset.
    if (version >= 2u)
        storeFloat(bytes, 20, static_cast<float>(values.gain));
    if (version >= 3u)
        storeU32(bytes.data() + 24, static_cast<std::uint32_t>(values.waveform));
    if (version >= 4u) {
        const auto coarse = static_cast<std::int32_t>(values.coarse);
        std::uint32_t coarseBits = 0;
        std::memcpy(&coarseBits, &coarse, sizeof(coarseBits));
        storeU32(bytes.data() + 28, coarseBits);
    }
    if (version >= 5u)
        storeFloat(bytes, 32, static_cast<float>(values.pan));
    if (version >= 6u)
        storeFloat(bytes, 36, static_cast<float>(values.cutoff));
    if (version >= 7u)
        storeFloat(bytes, 40, static_cast<float>(values.resonance));
    if (version >= 8u)
        storeFloat(bytes, 44, static_cast<float>(values.filterEnvelope));
    if (version >= 9u)
        storeFloat(bytes, 48, static_cast<float>(values.ampLevel));
    if (version >= 10u) {
        storeFloat(bytes, 52, static_cast<float>(values.attack));
        storeFloat(bytes, 56, static_cast<float>(values.decay));
        storeFloat(bytes, 60, static_cast<float>(values.sustain));
        storeFloat(bytes, 64, static_cast<float>(values.release));
    }
    return bytes;
}

ParameterValues expectedForVersion(std::uint32_t version,
                                   const ParameterValues &encoded) noexcept {
    ParameterValues expected{};
    expected.fine = encoded.fine;
    expected.gain = version >= 2u ? encoded.gain : 0.0;
    expected.waveform = version >= 3u ? encoded.waveform : 0.0;
    expected.coarse = version >= 4u ? encoded.coarse : 0.0;
    expected.pan = version >= 5u ? encoded.pan : 0.0;
    expected.cutoff = version >= 6u ? encoded.cutoff : 6000.0;
    expected.resonance = version >= 7u ? encoded.resonance : 0.0;
    expected.filterEnvelope = version >= 8u ? encoded.filterEnvelope : 0.0;
    expected.ampLevel = version >= 9u ? encoded.ampLevel : 1.0;
    expected.attack = version >= 10u ? encoded.attack : 0.01;
    expected.decay = version >= 10u ? encoded.decay : 0.1;
    expected.sustain = version >= 10u ? encoded.sustain : 0.8;
    expected.release = version >= 10u ? encoded.release : 0.25;
    return expected;
}

bool verifyLegacyVersion(const clap_plugin_factory_t *factory,
                         std::uint32_t version,
                         const ParameterValues &encoded) noexcept {
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
    const auto bytes = makeVersionState(version, encoded);
    MemoryInputStream input(bytes.data(), kVersionSizes[version], 2u);
    const auto expected = expectedForVersion(version, encoded);
    const bool ok = params && state && state->load(plugin, &input.stream) &&
                    hasAll(plugin, params, expected);
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
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!params || !params->count || !params->get_value || !params->flush ||
        params->count(plugin) != 13u || !state || !state->save || !state->load) {
        plugin->destroy(plugin);
        return 4;
    }

    const ParameterValues savedValues{};
    if (!setAll(plugin, params, savedValues)) {
        plugin->destroy(plugin);
        return 5;
    }

    MemoryOutputStream saved(3u);
    if (!state->save(plugin, &saved.stream) || saved.used != 68u || saved.calls < 2u ||
        loadU32(saved.bytes.data() + 8) != 10u) {
        plugin->destroy(plugin);
        return 6;
    }

    ParameterValues mutated{};
    mutated.fine = -31.0;
    mutated.gain = -9.0;
    mutated.waveform = 1.0;
    mutated.coarse = 12.0;
    mutated.pan = -0.5;
    mutated.cutoff = 9876.0;
    mutated.resonance = 0.125;
    mutated.filterEnvelope = 0.875;
    mutated.ampLevel = 0.25;
    mutated.attack = 0.5;
    mutated.decay = 0.75;
    mutated.sustain = 0.25;
    mutated.release = 0.125;
    if (!setAll(plugin, params, mutated)) {
        plugin->destroy(plugin);
        return 7;
    }

    MemoryInputStream restore(saved.bytes.data(), saved.used, 2u);
    if (!state->load(plugin, &restore.stream) || restore.calls < 2u ||
        !hasAll(plugin, params, savedValues)) {
        plugin->destroy(plugin);
        return 8;
    }

    // Loading a valid state into a clone and then activating it verifies that the
    // lock-free pending-state replay carries all four new defaults into the DSP.
    const auto *clone = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!clone || !clone->init(clone)) {
        if (clone)
            clone->destroy(clone);
        plugin->destroy(plugin);
        return 9;
    }
    const auto *cloneParams = static_cast<const clap_plugin_params_t *>(
        clone->get_extension(clone, CLAP_EXT_PARAMS));
    const auto *cloneState = static_cast<const clap_plugin_state_t *>(
        clone->get_extension(clone, CLAP_EXT_STATE));
    MemoryInputStream cloneRestore(saved.bytes.data(), saved.used, 1u);
    if (!cloneParams || !cloneState || !cloneState->load(clone, &cloneRestore.stream) ||
        !hasAll(clone, cloneParams, savedValues) ||
        !clone->activate(clone, 48000.0, 1, 64)) {
        clone->destroy(clone);
        plugin->destroy(plugin);
        return 10;
    }
    clone->deactivate(clone);
    clone->destroy(clone);

    // Malformed v10 payloads must fail atomically without mutating the current
    // host-visible Release value or any other retained base parameter.
    MemoryInputStream truncated(saved.bytes.data(), saved.used - 1u, 2u);
    if (state->load(plugin, &truncated.stream) || !hasAll(plugin, params, savedValues)) {
        plugin->destroy(plugin);
        return 11;
    }

    auto trailingBytes = saved.bytes;
    trailingBytes[saved.used] = 0x7fu;
    MemoryInputStream trailing(trailingBytes.data(), saved.used + 1u, 3u);
    if (state->load(plugin, &trailing.stream) || !hasAll(plugin, params, savedValues)) {
        plugin->destroy(plugin);
        return 12;
    }

    auto invalidReleaseBytes = saved.bytes;
    storeFloat(invalidReleaseBytes, 64, 10.5f);
    MemoryInputStream invalidRelease(invalidReleaseBytes.data(), saved.used, 2u);
    if (state->load(plugin, &invalidRelease.stream) || !hasAll(plugin, params, savedValues)) {
        plugin->destroy(plugin);
        return 13;
    }

    auto invalidAttackBytes = saved.bytes;
    storeFloat(invalidAttackBytes,
               52,
               std::numeric_limits<float>::quiet_NaN());
    MemoryInputStream invalidAttack(invalidAttackBytes.data(), saved.used, 2u);
    if (state->load(plugin, &invalidAttack.stream) || !hasAll(plugin, params, savedValues)) {
        plugin->destroy(plugin);
        return 14;
    }

    if (state->save(plugin, nullptr) || state->load(plugin, nullptr)) {
        plugin->destroy(plugin);
        return 15;
    }

    // Every historical prefix remains loadable. v1..v9 migrate the newly
    // published envelope to metadata defaults; v9 still restores Amp Level.
    ParameterValues legacy{};
    legacy.fine = -12.5;
    legacy.gain = -6.0;
    legacy.waveform = 2.0;
    legacy.coarse = -5.0;
    legacy.pan = -0.375;
    legacy.cutoff = 7654.0;
    legacy.resonance = 0.375;
    legacy.filterEnvelope = -0.5;
    legacy.ampLevel = 0.75;
    for (std::uint32_t version = 1u; version <= 9u; ++version) {
        if (!verifyLegacyVersion(factory, version, legacy)) {
            plugin->destroy(plugin);
            return static_cast<int>(15u + version);
        }
    }

    // The current v10 builder is independently loadable as a final offset/layout
    // guard rather than relying only on stateSave() output.
    const auto explicitV10 = makeVersionState(10u, legacy);
    MemoryInputStream explicitCurrent(explicitV10.data(), kVersionSizes[10], 4u);
    if (!state->load(plugin, &explicitCurrent.stream) ||
        !hasAll(plugin, params, legacy)) {
        plugin->destroy(plugin);
        return 25;
    }

    plugin->destroy(plugin);
    return 0;
}
