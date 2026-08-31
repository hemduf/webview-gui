#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id parameterId(ParameterSlot slot) noexcept {
    return webview_gui::examples::polysynth::kFirstParameterId +
           static_cast<clap_id>(slot);
}

constexpr std::array<clap_id, 13> kPublishedIds{{
    parameterId(ParameterSlot::FineTuning),
    parameterId(ParameterSlot::MasterGain),
    parameterId(ParameterSlot::Waveform),
    parameterId(ParameterSlot::CoarseTuning),
    parameterId(ParameterSlot::Pan),
    parameterId(ParameterSlot::FilterCutoff),
    parameterId(ParameterSlot::FilterResonance),
    parameterId(ParameterSlot::FilterEnvelopeAmount),
    parameterId(ParameterSlot::AmpLevel),
    parameterId(ParameterSlot::AmpAttack),
    parameterId(ParameterSlot::AmpDecay),
    parameterId(ParameterSlot::AmpSustain),
    parameterId(ParameterSlot::AmpRelease),
}};

struct ParameterValues {
    double fineTune = 0.0;
    float masterGain = 0.0f;
    std::uint32_t waveform = 0u;
    std::int32_t coarseTune = 0;
    float pan = 0.0f;
    float cutoff = 6000.0f;
    float resonance = 0.0f;
    float filterEnvelope = 0.0f;
    float ampLevel = 1.0f;
    float attack = 0.01f;
    float decay = 0.1f;
    float sustain = 0.8f;
    float release = 0.25f;
};

constexpr ParameterValues kStateA{
    -50.0,
    -12.0f,
    0u,
    -12,
    -0.75f,
    500.0f,
    0.1f,
    -0.5f,
    0.25f,
    0.05f,
    0.2f,
    0.3f,
    0.4f,
};

constexpr ParameterValues kStateB{
    75.0,
    6.0f,
    2u,
    24,
    0.5f,
    15000.0f,
    0.8f,
    0.75f,
    0.9f,
    0.3f,
    0.4f,
    0.85f,
    0.9f,
};

void storeU32(std::uint8_t *destination, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        destination[i] = static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu);
}

void storeU64(std::uint8_t *destination, std::uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        destination[i] = static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu);
}

void storeFloat(std::uint8_t *destination, float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    storeU32(destination, bits);
}

std::array<std::uint8_t, 68> makeState(const ParameterValues &values) noexcept {
    std::array<std::uint8_t, 68> bytes{};
    constexpr std::array<std::uint8_t, 8> magic{{'W', 'V', 'P', 'S', 'Y', 'N', 'T', 'H'}};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    storeU32(bytes.data() + 8, 10u);

    std::uint64_t fineBits = 0;
    std::memcpy(&fineBits, &values.fineTune, sizeof(fineBits));
    storeU64(bytes.data() + 12, fineBits);
    storeFloat(bytes.data() + 20, values.masterGain);
    storeU32(bytes.data() + 24, values.waveform);

    std::uint32_t coarseBits = 0;
    std::memcpy(&coarseBits, &values.coarseTune, sizeof(coarseBits));
    storeU32(bytes.data() + 28, coarseBits);
    storeFloat(bytes.data() + 32, values.pan);
    storeFloat(bytes.data() + 36, values.cutoff);
    storeFloat(bytes.data() + 40, values.resonance);
    storeFloat(bytes.data() + 44, values.filterEnvelope);
    storeFloat(bytes.data() + 48, values.ampLevel);
    storeFloat(bytes.data() + 52, values.attack);
    storeFloat(bytes.data() + 56, values.decay);
    storeFloat(bytes.data() + 60, values.sustain);
    storeFloat(bytes.data() + 64, values.release);
    return bytes;
}

const std::array<std::uint8_t, 68> kStateABytes = makeState(kStateA);
const std::array<std::uint8_t, 68> kStateBBytes = makeState(kStateB);

struct MemoryInputStream {
    explicit MemoryInputStream(const std::array<std::uint8_t, 68> &source) noexcept
        : bytes(source.data()), size(source.size()) {
        stream.ctx = this;
        stream.read = read;
    }

    static std::int64_t CLAP_ABI read(const clap_istream_t *stream,
                                      void *destination,
                                      std::uint64_t requested) noexcept {
        if (!stream || !stream->ctx || !destination)
            return -1;
        auto &self = *static_cast<MemoryInputStream *>(stream->ctx);
        if (self.offset >= self.size)
            return 0;
        const auto remaining = self.size - self.offset;
        const auto count = std::min<std::size_t>(remaining,
                                                 static_cast<std::size_t>(requested));
        std::memcpy(destination, self.bytes + self.offset, count);
        self.offset += count;
        return static_cast<std::int64_t>(count);
    }

    const std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
    clap_istream_t stream{};
};

struct MemoryOutputStream {
    MemoryOutputStream() noexcept {
        stream.ctx = this;
        stream.write = write;
    }

    static std::int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                       const void *source,
                                       std::uint64_t requested) noexcept {
        if (!stream || !stream->ctx || !source)
            return -1;
        auto &self = *static_cast<MemoryOutputStream *>(stream->ctx);
        const auto remaining = self.bytes.size() - self.used;
        const auto count = std::min<std::size_t>(remaining,
                                                 static_cast<std::size_t>(requested));
        if (count == 0u)
            return -1;
        std::memcpy(self.bytes.data() + self.used, source, count);
        self.used += count;
        return static_cast<std::int64_t>(count);
    }

    std::array<std::uint8_t, 68> bytes{};
    std::size_t used = 0;
    clap_ostream_t stream{};
};

struct EmptyInputEvents {
    EmptyInputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }
    static std::uint32_t CLAP_ABI size(const clap_input_events_t *) noexcept { return 0u; }
    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *,
                                                    std::uint32_t) noexcept {
        return nullptr;
    }
    clap_input_events_t input{};
};

struct AcceptingOutputEvents {
    AcceptingOutputEvents() noexcept {
        output.ctx = this;
        output.try_push = tryPush;
    }
    static bool CLAP_ABI tryPush(const clap_output_events_t *,
                                 const clap_event_header_t *) noexcept {
        return true;
    }
    clap_output_events_t output{};
};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) { return nullptr; }
void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui state snapshot concurrency tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

bool parametersMatch(const clap_plugin_t *plugin,
                     const clap_plugin_params_t *params,
                     const ParameterValues &expected) noexcept {
    const std::array<double, 13> values{{
        expected.fineTune,
        expected.masterGain,
        static_cast<double>(expected.waveform),
        static_cast<double>(expected.coarseTune),
        expected.pan,
        expected.cutoff,
        expected.resonance,
        expected.filterEnvelope,
        expected.ampLevel,
        expected.attack,
        expected.decay,
        expected.sustain,
        expected.release,
    }};
    for (std::size_t i = 0; i < kPublishedIds.size(); ++i) {
        double observed = 0.0;
        if (!params->get_value(plugin, kPublishedIds[i], &observed) ||
            std::fabs(observed - values[i]) > 1.0e-5)
            return false;
    }
    return true;
}

bool saveMatches(const clap_plugin_t *plugin,
                 const clap_plugin_state_t *state,
                 const std::array<std::uint8_t, 68> &expected) noexcept {
    MemoryOutputStream output;
    return state->save(plugin, &output.stream) && output.used == expected.size() &&
           output.bytes == expected;
}

} // namespace

int main() {
    const auto *factory = webview_gui::examples::polysynth::polysynthFactory();
    if (!factory)
        return 1;
    const auto *plugin = factory->create_plugin(
        factory, &kHost, webview_gui::examples::polysynth::kPolySynthPluginId);
    if (!plugin || !plugin->init(plugin)) {
        if (plugin)
            plugin->destroy(plugin);
        return 2;
    }

    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto *tail = static_cast<const clap_plugin_tail_t *>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    if (!params || !params->get_value || !state || !state->load || !state->save ||
        !tail || !tail->get || !plugin->activate(plugin, 48000.0, 1, 32)) {
        plugin->destroy(plugin);
        return 3;
    }

    // state.load() is a main-thread operation and is legal while active. The
    // host-visible state becomes the loaded snapshot immediately, so tail.get()
    // must not keep advertising the previous shorter Release until process().
    // State A has Release=0.4s, which is 19,200 samples at 48 kHz.
    MemoryInputStream initialInput(kStateABytes);
    if (!state->load(plugin, &initialInput.stream) ||
        !parametersMatch(plugin, params, kStateA) ||
        !saveMatches(plugin, state, kStateABytes) || tail->get(plugin) != 19200u) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 4;
    }

    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 5;
    }

    std::atomic<bool> keepProcessing{true};
    std::atomic<bool> audioOk{true};
    std::thread audioThread([&]() noexcept {
        EmptyInputEvents inputEvents;
        AcceptingOutputEvents outputEvents;
        std::array<float, 32> left{};
        std::array<float, 32> right{};
        std::array<float *, 2> channels{{left.data(), right.data()}};
        clap_audio_buffer_t output{};
        output.data32 = channels.data();
        output.channel_count = 2u;
        clap_process_t process{};
        process.frames_count = 32u;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1u;
        process.in_events = &inputEvents.input;
        process.out_events = &outputEvents.output;
        while (keepProcessing.load(std::memory_order_acquire)) {
            if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR) {
                audioOk.store(false, std::memory_order_release);
                break;
            }
        }
    });

    bool mainOk = true;
    constexpr std::size_t kIterations = 512u;
    for (std::size_t iteration = 0; iteration < kIterations && mainOk; ++iteration) {
        const bool useA = (iteration & 1u) == 0u;
        const auto &expectedBytes = useA ? kStateABytes : kStateBBytes;
        const auto &expectedValues = useA ? kStateA : kStateB;
        MemoryInputStream input(expectedBytes);
        if (!state->load(plugin, &input.stream) ||
            !parametersMatch(plugin, params, expectedValues) ||
            !saveMatches(plugin, state, expectedBytes)) {
            mainOk = false;
        }
    }

    keepProcessing.store(false, std::memory_order_release);
    audioThread.join();
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);

    return mainOk && audioOk.load(std::memory_order_acquire) ? 0 : 6;
}