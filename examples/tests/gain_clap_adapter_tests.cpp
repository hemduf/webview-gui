#include "gain_plugin.h"
#include "../common/test_support.h"

#include <clap/clap.h>

#include <cmath>
#include <cstring>
#include <iostream>

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
    if (!ports || !params || plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS) != nullptr) {
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

    if (!plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 13;
    }
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 14;
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

    const float minusSix = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
    for (uint32_t frame = 0; frame < 4; ++frame) {
        if (std::fabs(block.outputChannel(0)[frame] - minusSix) > 1.0e-6f) {
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
