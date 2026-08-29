#include "gain_plugin.h"
#include "../common/test_support.h"

#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) { return nullptr; }
void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

struct MemoryOutputStream {
    explicit MemoryOutputStream(std::size_t maxWrite = std::numeric_limits<std::size_t>::max(),
                                std::size_t failAfterBytes = std::numeric_limits<std::size_t>::max())
        : maxWrite(maxWrite), failAfterBytes(failAfterBytes), stream{this, write} {}

    static int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                  const void *buffer,
                                  uint64_t size) {
        auto &self = *static_cast<MemoryOutputStream *>(stream->ctx);
        if (!buffer || size == 0)
            return size == 0 ? 0 : -1;
        if (self.bytes.size() >= self.failAfterBytes)
            return -1;

        const auto remainingBeforeFailure = self.failAfterBytes - self.bytes.size();
        const auto requested = static_cast<std::size_t>(std::min<uint64_t>(
            size, static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())));
        const auto amount = std::min({requested, self.maxWrite, remainingBeforeFailure});
        if (amount == 0)
            return -1;

        const auto *begin = static_cast<const uint8_t *>(buffer);
        self.bytes.insert(self.bytes.end(), begin, begin + amount);
        return static_cast<int64_t>(amount);
    }

    std::size_t maxWrite;
    std::size_t failAfterBytes;
    std::vector<uint8_t> bytes;
    clap_ostream_t stream;
};

struct MemoryInputStream {
    explicit MemoryInputStream(const std::vector<uint8_t> &bytes,
                               std::size_t maxRead = std::numeric_limits<std::size_t>::max(),
                               std::size_t failAfterBytes = std::numeric_limits<std::size_t>::max())
        : bytes(bytes), maxRead(maxRead), failAfterBytes(failAfterBytes), stream{this, read} {}

    static int64_t CLAP_ABI read(const clap_istream_t *stream, void *buffer, uint64_t size) {
        auto &self = *static_cast<MemoryInputStream *>(stream->ctx);
        if (!buffer || size == 0)
            return size == 0 ? 0 : -1;
        if (self.position >= self.failAfterBytes)
            return -1;
        if (self.position >= self.bytes.size())
            return 0;

        const auto requested = static_cast<std::size_t>(std::min<uint64_t>(
            size, static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())));
        const auto available = self.bytes.size() - self.position;
        const auto beforeFailure = self.failAfterBytes - self.position;
        const auto amount = std::min({requested, self.maxRead, available, beforeFailure});
        if (amount == 0)
            return -1;

        std::memcpy(buffer, self.bytes.data() + self.position, amount);
        self.position += amount;
        return static_cast<int64_t>(amount);
    }

    const std::vector<uint8_t> &bytes;
    std::size_t maxRead;
    std::size_t failAfterBytes;
    std::size_t position = 0;
    clap_istream_t stream;
};

bool checkPort(const clap_plugin_audio_ports_t *ports,
               const clap_plugin_t *plugin,
               bool isInput,
               clap_id expectedId,
               clap_id expectedPair,
               const char *expectedName) {
    if (!ports || ports->count(plugin, isInput) != 1)
        return false;

    clap_audio_port_info_t info{};
    if (!ports->get(plugin, 0, isInput, &info))
        return false;
    return info.id == expectedId &&
           info.flags == CLAP_AUDIO_PORT_IS_MAIN &&
           info.channel_count == 2 &&
           info.port_type && std::strcmp(info.port_type, CLAP_PORT_STEREO) == 0 &&
           info.in_place_pair == expectedPair &&
           std::strcmp(info.name, expectedName) == 0;
}

bool checkParam(const clap_plugin_params_t *params,
                const clap_plugin_t *plugin,
                uint32_t index,
                clap_id expectedId,
                uint32_t requiredFlags,
                double minimum,
                double maximum,
                double defaultValue,
                const char *name) {
    clap_param_info_t info{};
    if (!params->get_info(plugin, index, &info))
        return false;
    if (info.id != expectedId || (info.flags & requiredFlags) != requiredFlags ||
        info.min_value != minimum || info.max_value != maximum ||
        info.default_value != defaultValue || std::strcmp(info.name, name) != 0)
        return false;

    const uint32_t forbiddenPolyFlags =
        CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID |
        CLAP_PARAM_IS_AUTOMATABLE_PER_KEY |
        CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL |
        CLAP_PARAM_IS_AUTOMATABLE_PER_PORT |
        CLAP_PARAM_IS_MODULATABLE |
        CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID |
        CLAP_PARAM_IS_MODULATABLE_PER_KEY |
        CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL |
        CLAP_PARAM_IS_MODULATABLE_PER_PORT;
    return (info.flags & forbiddenPolyFlags) == 0;
}

} // namespace

int main() {
    using namespace webview_gui::examples::gain;
    using webview_gui::examples::test_support::InputEvents;
    using webview_gui::examples::test_support::StereoFloatBlock;

    const auto *factory = gainFactory();
    if (!factory || factory->get_plugin_count(factory) != 1)
        return 1;

    const auto *descriptor = factory->get_plugin_descriptor(factory, 0);
    if (!descriptor || !descriptor->id || std::strcmp(descriptor->id, kGainPluginId) != 0)
        return 2;
    if (factory->get_plugin_descriptor(factory, 1) != nullptr)
        return 3;

    const auto *plugin = factory->create_plugin(factory, &kHost, kGainPluginId);
    if (!plugin)
        return 4;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 5;
    }

    const auto *ports = static_cast<const clap_plugin_audio_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!ports || !params || !state ||
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS) != nullptr) {
        std::cerr << "required Gain CLAP extensions are missing or a note extension is falsely exposed\n";
        plugin->destroy(plugin);
        return 6;
    }

    if (!checkPort(ports, plugin, true, kGainInputPortId, kGainOutputPortId, "Stereo In") ||
        !checkPort(ports, plugin, false, kGainOutputPortId, kGainInputPortId, "Stereo Out")) {
        plugin->destroy(plugin);
        return 7;
    }

    if (params->count(plugin) != 2 ||
        !checkParam(params, plugin, 0, kGainParamId,
                    CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS,
                    -60.0, 12.0, 0.0, "Gain") ||
        !checkParam(params, plugin, 1, kBypassParamId,
                    CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
                        CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS,
                    0.0, 1.0, 0.0, "Bypass")) {
        plugin->destroy(plugin);
        return 8;
    }

    double value = 123.0;
    if (!params->get_value(plugin, kGainParamId, &value) || value != 0.0 ||
        !params->get_value(plugin, kBypassParamId, &value) || value != 0.0) {
        plugin->destroy(plugin);
        return 10;
    }

    // flush() is the non-processing parameter path and must update the same
    // base values that process() consumes.
    InputEvents flushEvents;
    if (!flushEvents.pushParamValue(0, kGainParamId, -6.0) ||
        !flushEvents.pushParamValue(0, kBypassParamId, 1.0)) {
        plugin->destroy(plugin);
        return 11;
    }
    params->flush(plugin, flushEvents.clapInputEvents(), nullptr);
    if (!params->get_value(plugin, kGainParamId, &value) || std::fabs(value + 6.0) > 1.0e-6 ||
        !params->get_value(plugin, kBypassParamId, &value) || value != 1.0) {
        plugin->destroy(plugin);
        return 12;
    }

    // State streams are allowed to return short reads/writes. A valid state
    // round-trip must therefore loop until the entire payload is transferred.
    MemoryOutputStream savedState(3);
    if (!state->save(plugin, &savedState.stream) || savedState.bytes.empty()) {
        std::cerr << "CLAP state save did not handle partial stream writes\n";
        plugin->destroy(plugin);
        return 22;
    }

    InputEvents changedState;
    if (!changedState.pushParamValue(0, kGainParamId, 3.0) ||
        !changedState.pushParamValue(0, kBypassParamId, 0.0)) {
        plugin->destroy(plugin);
        return 23;
    }
    params->flush(plugin, changedState.clapInputEvents(), nullptr);

    MemoryInputStream savedStateInput(savedState.bytes, 2);
    if (!state->load(plugin, &savedStateInput.stream) ||
        !params->get_value(plugin, kGainParamId, &value) || std::fabs(value + 6.0) > 1.0e-6 ||
        !params->get_value(plugin, kBypassParamId, &value) || value != 1.0) {
        std::cerr << "CLAP state round-trip did not restore Gain/Bypass\n";
        plugin->destroy(plugin);
        return 24;
    }

    // Malformed/truncated state must fail atomically: a rejected load must not
    // partially replace the last valid parameter state.
    InputEvents atomicBaseline;
    if (!atomicBaseline.pushParamValue(0, kGainParamId, -3.0) ||
        !atomicBaseline.pushParamValue(0, kBypassParamId, 0.0)) {
        plugin->destroy(plugin);
        return 25;
    }
    params->flush(plugin, atomicBaseline.clapInputEvents(), nullptr);

    auto truncatedState = savedState.bytes;
    truncatedState.pop_back();
    MemoryInputStream truncatedInput(truncatedState, 2);
    if (state->load(plugin, &truncatedInput.stream) ||
        !params->get_value(plugin, kGainParamId, &value) || std::fabs(value + 3.0) > 1.0e-6 ||
        !params->get_value(plugin, kBypassParamId, &value) || value != 0.0) {
        std::cerr << "truncated CLAP state was accepted or mutated live state\n";
        plugin->destroy(plugin);
        return 26;
    }

    auto corruptState = savedState.bytes;
    corruptState.front() ^= 0x7fu;
    MemoryInputStream corruptInput(corruptState, 2);
    if (state->load(plugin, &corruptInput.stream) ||
        !params->get_value(plugin, kGainParamId, &value) || std::fabs(value + 3.0) > 1.0e-6 ||
        !params->get_value(plugin, kBypassParamId, &value) || value != 0.0) {
        std::cerr << "corrupt CLAP state was accepted or mutated live state\n";
        plugin->destroy(plugin);
        return 27;
    }

    auto stateWithTrailingData = savedState.bytes;
    stateWithTrailingData.push_back(0x42u);
    MemoryInputStream trailingInput(stateWithTrailingData, 2);
    if (state->load(plugin, &trailingInput.stream) ||
        !params->get_value(plugin, kGainParamId, &value) || std::fabs(value + 3.0) > 1.0e-6) {
        std::cerr << "non-canonical CLAP state with trailing data was accepted\n";
        plugin->destroy(plugin);
        return 28;
    }

    MemoryOutputStream failingOutput(3, 4);
    if (state->save(plugin, &failingOutput.stream)) {
        std::cerr << "CLAP state save ignored a stream write error\n";
        plugin->destroy(plugin);
        return 29;
    }

    MemoryInputStream failingInput(savedState.bytes, 2, 4);
    if (state->load(plugin, &failingInput.stream) ||
        !params->get_value(plugin, kGainParamId, &value) || std::fabs(value + 3.0) > 1.0e-6) {
        std::cerr << "CLAP state load ignored a stream read error or mutated live state\n";
        plugin->destroy(plugin);
        return 30;
    }

    // CLAP stepped parameters use integer cast/truncation semantics. A fractional
    // bypass value in [0,1) therefore maps to 0, not a thresholded true value.
    InputEvents fractionalBypass;
    if (!fractionalBypass.pushParamValue(0, kBypassParamId, 0.9)) {
        plugin->destroy(plugin);
        return 18;
    }
    params->flush(plugin, fractionalBypass.clapInputEvents(), nullptr);
    if (!params->get_value(plugin, kBypassParamId, &value) || value != 0.0) {
        std::cerr << "fractional stepped bypass did not use CLAP truncation semantics\n";
        plugin->destroy(plugin);
        return 19;
    }

    // Preserve a state that will be loaded after start_processing(). CLAP state
    // callbacks are main-thread operations and may race an active audio thread;
    // the next process block must observe the loaded values without a direct
    // main-thread mutation of the DSP object.
    MemoryOutputStream activeState;
    if (!state->save(plugin, &activeState.stream)) {
        plugin->destroy(plugin);
        return 31;
    }
    InputEvents activePreloadChange;
    if (!activePreloadChange.pushParamValue(0, kGainParamId, 6.0)) {
        plugin->destroy(plugin);
        return 32;
    }
    params->flush(plugin, activePreloadChange.clapInputEvents(), nullptr);

    if (!plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 13;
    }
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 14;
    }

    MemoryInputStream activeStateInput(activeState.bytes, 2);
    if (!state->load(plugin, &activeStateInput.stream)) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 33;
    }

    StereoFloatBlock loadedBlock(2);
    loadedBlock.fillInput(1.0f, 1.0f);
    clap_process_t loadedProcess{};
    loadedProcess.frames_count = loadedBlock.frames();
    loadedProcess.audio_inputs = loadedBlock.input();
    loadedProcess.audio_outputs = loadedBlock.output();
    loadedProcess.audio_inputs_count = 1;
    loadedProcess.audio_outputs_count = 1;
    if (plugin->process(plugin, &loadedProcess) != CLAP_PROCESS_CONTINUE) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 34;
    }
    const float minusThree = static_cast<float>(std::pow(10.0, -3.0 / 20.0));
    for (uint32_t frame = 0; frame < loadedBlock.frames(); ++frame) {
        if (std::fabs(loadedBlock.outputChannel(0)[frame] - minusThree) > 1.0e-6f) {
            std::cerr << "active CLAP state load was not applied before the next process block\n";
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            return 35;
        }
    }

    StereoFloatBlock block(8);
    block.fillInput(1.0f, 1.0f);
    InputEvents processEvents;
    if (!processEvents.pushParamValue(0, kBypassParamId, 0.0) ||
        !processEvents.pushParamValue(4, kGainParamId, 0.0)) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 15;
    }

    clap_process_t process{};
    process.frames_count = block.frames();
    process.audio_inputs = block.input();
    process.audio_outputs = block.output();
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;
    process.in_events = processEvents.clapInputEvents();

    if (plugin->process(plugin, &process) != CLAP_PROCESS_CONTINUE) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 16;
    }

    for (uint32_t frame = 0; frame < 4; ++frame) {
        if (std::fabs(block.outputChannel(0)[frame] - minusThree) > 1.0e-6f) {
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            return 20;
        }
    }
    for (uint32_t frame = 4; frame < 8; ++frame) {
        if (std::fabs(block.outputChannel(0)[frame] - 1.0f) > 1.0e-6f) {
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            return 21;
        }
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
