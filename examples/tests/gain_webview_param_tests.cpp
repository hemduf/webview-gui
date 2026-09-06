#include "gain_plugin.h"
#include "../common/test_support.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

using webview_gui::examples::test_support::CapturedOutputEvents;
using webview_gui::examples::test_support::InputEvents;
using webview_gui::examples::test_support::StereoFloatBlock;

uint32_t gRequestFlushCount = 0;
uint32_t gRequestProcessCount = 0;
uint32_t gGainSnapshotCount = 0;
uint32_t gBypassSnapshotCount = 0;
std::array<uint8_t, 16> gLastGainSnapshot{};
std::array<uint8_t, 16> gLastBypassSnapshot{};

bool CLAP_ABI hostWebviewSend(const clap_host_t *, const void *buffer, uint32_t size) {
    if (!buffer && size != 0u)
        return false;
    if (size != 16u)
        return true;
    const auto *bytes = static_cast<const uint8_t *>(buffer);
    if (bytes[0] != 'W' || bytes[1] != 'V' || bytes[2] != 'U' || bytes[3] != '1')
        return true;
    if (bytes[4] == 1u) {
        std::memcpy(gLastGainSnapshot.data(), bytes, size);
        ++gGainSnapshotCount;
    } else if (bytes[4] == 2u) {
        std::memcpy(gLastBypassSnapshot.data(), bytes, size);
        ++gBypassSnapshotCount;
    }
    return true;
}
void CLAP_ABI hostParamsRescan(const clap_host_t *, clap_param_rescan_flags) {}
void CLAP_ABI hostParamsClear(const clap_host_t *, clap_id, clap_param_clear_flags) {}
void CLAP_ABI hostParamsRequestFlush(const clap_host_t *) { ++gRequestFlushCount; }

const clap_host_webview_t kHostWebview{hostWebviewSend};
const clap_host_params_t kHostParams{hostParamsRescan, hostParamsClear, hostParamsRequestFlush};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *id) {
    if (!id)
        return nullptr;
    if (std::strcmp(id, CLAP_EXT_WEBVIEW) == 0)
        return &kHostWebview;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &kHostParams;
    return nullptr;
}

const void *CLAP_ABI hostGetExtensionWithoutParams(const clap_host_t *, const char *id) {
    if (id && std::strcmp(id, CLAP_EXT_WEBVIEW) == 0)
        return &kHostWebview;
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) { ++gRequestProcessCount; }
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain parameter bridge tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

const clap_host_t kHostWithoutParams{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain minimal parameter bridge host",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtensionWithoutParams,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

constexpr uint8_t kUiGestureBegin = 1;
constexpr uint8_t kUiValue = 2;
constexpr uint8_t kUiGestureEnd = 3;
constexpr uint8_t kUiGain = 1;
constexpr uint8_t kUiBypass = 2;

void storeU64Le(uint8_t *destination, uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        destination[i] = static_cast<uint8_t>((value >> (i * 8u)) & 0xffu);
}

uint64_t loadU64Le(const uint8_t *source) noexcept {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(source[i]) << (i * 8u);
    return value;
}

double snapshotValue(const std::array<uint8_t, 16> &message) noexcept {
    const auto bits = loadU64Le(message.data() + 8u);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::array<uint8_t, 16> makeUiMessage(uint8_t kind, uint8_t parameter, double value = 0.0) {
    std::array<uint8_t, 16> bytes{};
    bytes[0] = 'W';
    bytes[1] = 'V';
    bytes[2] = 'G';
    bytes[3] = '1';
    bytes[4] = kind;
    bytes[5] = parameter;

    uint64_t valueBits = 0;
    std::memcpy(&valueBits, &value, sizeof(valueBits));
    storeU64Le(bytes.data() + 8, valueBits);
    return bytes;
}

template <typename Event>
bool copyEvent(const CapturedOutputEvents &events, std::size_t index, Event &event) {
    if (events.byteSize(index) != sizeof(Event) || !events.bytes(index))
        return false;
    std::memcpy(&event, events.bytes(index), sizeof(Event));
    return true;
}

bool checkGesture(const CapturedOutputEvents &events,
                  std::size_t index,
                  uint16_t expectedType,
                  clap_id expectedParam) {
    clap_event_param_gesture_t event{};
    return copyEvent(events, index, event) &&
           event.header.space_id == CLAP_CORE_EVENT_SPACE_ID &&
           event.header.type == expectedType && event.header.time == 0 &&
           event.header.flags == CLAP_EVENT_IS_LIVE &&
           event.param_id == expectedParam;
}

bool checkValue(const CapturedOutputEvents &events,
                std::size_t index,
                clap_id expectedParam,
                double expectedValue) {
    clap_event_param_value_t event{};
    return copyEvent(events, index, event) &&
           event.header.space_id == CLAP_CORE_EVENT_SPACE_ID &&
           event.header.type == CLAP_EVENT_PARAM_VALUE && event.header.time == 0 &&
           event.header.flags == CLAP_EVENT_IS_LIVE &&
           event.param_id == expectedParam && event.cookie == nullptr &&
           event.note_id == -1 && event.port_index == -1 &&
           event.channel == -1 && event.key == -1 &&
           std::fabs(event.value - expectedValue) < 1.0e-9;
}

} // namespace

int main() {
    using namespace webview_gui::examples::gain;

    const auto *factory = gainFactory();
    if (!factory)
        return 1;

    const auto *plugin = factory->create_plugin(factory, &kHost, kGainPluginId);
    if (!plugin)
        return 2;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 3;
    }

    const auto *webview = static_cast<const clap_plugin_webview_t *>(
        plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!webview || !gui || !params || !webview->receive) {
        plugin->destroy(plugin);
        return 4;
    }

    if (!gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false)) {
        plugin->destroy(plugin);
        return 5;
    }

    // Inactive plug-ins must route editor changes through request_flush() and
    // emit the canonical CLAP begin/value/end sequence from params.flush().
    const auto gainBegin = makeUiMessage(kUiGestureBegin, kUiGain);
    const auto gainValue = makeUiMessage(kUiValue, kUiGain, -6.0);
    const auto gainEnd = makeUiMessage(kUiGestureEnd, kUiGain);
    if (!webview->receive(plugin, gainBegin.data(), gainBegin.size()) ||
        !webview->receive(plugin, gainValue.data(), gainValue.size()) ||
        !webview->receive(plugin, gainEnd.data(), gainEnd.size())) {
        std::cerr << "Gain WebView parameter messages were not accepted\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 6;
    }
    if (gRequestFlushCount != 3 || gRequestProcessCount != 0) {
        std::cerr << "Inactive WebView edits did not request a CLAP parameter flush\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 7;
    }

    CapturedOutputEvents inactiveOutput;
    params->flush(plugin, nullptr, inactiveOutput.clapOutputEvents());
    if (inactiveOutput.size() != 3 ||
        !checkGesture(inactiveOutput, 0, CLAP_EVENT_PARAM_GESTURE_BEGIN, kGainParamId) ||
        !checkValue(inactiveOutput, 1, kGainParamId, -6.0) ||
        !checkGesture(inactiveOutput, 2, CLAP_EVENT_PARAM_GESTURE_END, kGainParamId)) {
        std::cerr << "Inactive WebView edit did not emit canonical CLAP parameter events\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 8;
    }

    double value = 0.0;
    if (!params->get_value(plugin, kGainParamId, &value) || std::fabs(value + 6.0) > 1.0e-6) {
        std::cerr << "Inactive WebView edit did not update the processor-visible base value\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 9;
    }

    // Malformed wire messages must fail atomically and must not schedule host work.
    auto malformed = gainValue;
    malformed[0] = 'X';
    const auto flushBeforeMalformed = gRequestFlushCount;
    if (webview->receive(plugin, malformed.data(), malformed.size()) ||
        gRequestFlushCount != flushBeforeMalformed) {
        std::cerr << "Malformed WebView parameter message was accepted\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 10;
    }

    if (!plugin->activate(plugin, 48000.0, 1, 64) || !plugin->start_processing(plugin)) {
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 11;
    }

    // Host/DAW automation must change audio at the exact sample and then be the
    // same base value reported by clap.params and the WebView WVU1 snapshot.
    {
        StereoFloatBlock automationBlock(4);
        automationBlock.fillInput(1.0f, 1.0f);
        InputEvents hostEvents;
        if (!hostEvents.pushParamValue(2u, kGainParamId, -12.0) ||
            !hostEvents.pushParamValue(3u, kBypassParamId, 1.0)) {
            return 12;
        }
        CapturedOutputEvents automationOutput;
        clap_process_t automationProcess{};
        automationProcess.frames_count = automationBlock.frames();
        automationProcess.audio_inputs = automationBlock.input();
        automationProcess.audio_outputs = automationBlock.output();
        automationProcess.audio_inputs_count = 1;
        automationProcess.audio_outputs_count = 1;
        automationProcess.in_events = hostEvents.clapInputEvents();
        automationProcess.out_events = automationOutput.clapOutputEvents();

        if (plugin->process(plugin, &automationProcess) != CLAP_PROCESS_CONTINUE) {
            std::cerr << "Host automation process block failed\n";
            return 13;
        }

        const float minusSix = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
        const float minusTwelve = static_cast<float>(std::pow(10.0, -12.0 / 20.0));
        if (std::fabs(automationBlock.outputChannel(0)[0] - minusSix) > 1.0e-6f ||
            std::fabs(automationBlock.outputChannel(0)[1] - minusSix) > 1.0e-6f ||
            std::fabs(automationBlock.outputChannel(0)[2] - minusTwelve) > 1.0e-6f ||
            std::fabs(automationBlock.outputChannel(0)[3] - 1.0f) > 1.0e-6f) {
            std::cerr << "Host automation did not reach DSP on exact sample boundaries\n";
            return 14;
        }

        double gain = 0.0;
        double bypass = 0.0;
        if (!params->get_value(plugin, kGainParamId, &gain) ||
            !params->get_value(plugin, kBypassParamId, &bypass) ||
            std::fabs(gain + 12.0) > 1.0e-9 || bypass != 1.0) {
            std::cerr << "Host automation did not update host-visible base snapshots\n";
            return 15;
        }

        gGainSnapshotCount = 0u;
        gBypassSnapshotCount = 0u;
        constexpr std::array<uint8_t, 4> sync{{'W', 'V', 'Q', '1'}};
        if (!webview->receive(plugin, sync.data(), sync.size()) ||
            gGainSnapshotCount != 1u || gBypassSnapshotCount != 1u ||
            std::fabs(snapshotValue(gLastGainSnapshot) + 12.0) > 1.0e-9 ||
            snapshotValue(gLastBypassSnapshot) != 1.0) {
            std::cerr << "WebView snapshot diverged from DAW-automated base values\n";
            return 16;
        }
    }

    // Active plug-ins must request processing and emit/apply the edit from process(),
    // because both Gain parameters are marked CLAP_PARAM_REQUIRES_PROCESS.
    const auto bypassBegin = makeUiMessage(kUiGestureBegin, kUiBypass);
    const auto bypassValue = makeUiMessage(kUiValue, kUiBypass, 1.0);
    const auto bypassEnd = makeUiMessage(kUiGestureEnd, kUiBypass);
    if (!webview->receive(plugin, bypassBegin.data(), bypassBegin.size()) ||
        !webview->receive(plugin, bypassValue.data(), bypassValue.size()) ||
        !webview->receive(plugin, bypassEnd.data(), bypassEnd.size()) ||
        gRequestProcessCount != 3) {
        std::cerr << "Active WebView edit did not request CLAP processing\n";
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 17;
    }

    StereoFloatBlock block(4);
    block.fillInput(1.0f, 1.0f);
    CapturedOutputEvents processOutput;
    clap_process_t process{};
    process.frames_count = block.frames();
    process.audio_inputs = block.input();
    process.audio_outputs = block.output();
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;
    process.out_events = processOutput.clapOutputEvents();

    if (plugin->process(plugin, &process) != CLAP_PROCESS_CONTINUE ||
        processOutput.size() != 3 ||
        !checkGesture(processOutput, 0, CLAP_EVENT_PARAM_GESTURE_BEGIN, kBypassParamId) ||
        !checkValue(processOutput, 1, kBypassParamId, 1.0) ||
        !checkGesture(processOutput, 2, CLAP_EVENT_PARAM_GESTURE_END, kBypassParamId)) {
        std::cerr << "Active WebView edit did not emit canonical CLAP process events\n";
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 18;
    }

    for (uint32_t frame = 0; frame < block.frames(); ++frame) {
        if (std::fabs(block.outputChannel(0)[frame] - 1.0f) > 1.0e-6f ||
            std::fabs(block.outputChannel(1)[frame] - 1.0f) > 1.0e-6f) {
            std::cerr << "Active WebView bypass edit was not applied before audio processing\n";
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            gui->destroy(plugin);
            plugin->destroy(plugin);
            return 19;
        }
    }

    if (!params->get_value(plugin, kBypassParamId, &value) || value != 1.0) {
        std::cerr << "Active WebView edit did not update the host-visible parameter snapshot\n";
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 20;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    gui->destroy(plugin);
    plugin->destroy(plugin);

    // CLAP permits either request_flush() or request_process() for a plug-in UI
    // edit. A minimal/WCLAP-style host may expose WebView without clap.host-params;
    // in that case the bridge must fall back to the thread-safe core request_process().
    gRequestFlushCount = 0;
    gRequestProcessCount = 0;
    const auto *fallbackPlugin =
        factory->create_plugin(factory, &kHostWithoutParams, kGainPluginId);
    if (!fallbackPlugin)
        return 21;
    if (!fallbackPlugin->init(fallbackPlugin)) {
        fallbackPlugin->destroy(fallbackPlugin);
        return 22;
    }

    const auto *fallbackWebview = static_cast<const clap_plugin_webview_t *>(
        fallbackPlugin->get_extension(fallbackPlugin, CLAP_EXT_WEBVIEW));
    const auto *fallbackGui = static_cast<const clap_plugin_gui_t *>(
        fallbackPlugin->get_extension(fallbackPlugin, CLAP_EXT_GUI));
    if (!fallbackWebview || !fallbackGui ||
        !fallbackGui->create(fallbackPlugin, CLAP_WINDOW_API_WEBVIEW, false)) {
        fallbackPlugin->destroy(fallbackPlugin);
        return 23;
    }

    const auto fallbackBegin = makeUiMessage(kUiGestureBegin, kUiGain);
    const auto fallbackValue = makeUiMessage(kUiValue, kUiGain, -3.0);
    const auto fallbackEnd = makeUiMessage(kUiGestureEnd, kUiGain);
    if (!fallbackWebview->receive(fallbackPlugin, fallbackBegin.data(), fallbackBegin.size()) ||
        !fallbackWebview->receive(fallbackPlugin, fallbackValue.data(), fallbackValue.size()) ||
        !fallbackWebview->receive(fallbackPlugin, fallbackEnd.data(), fallbackEnd.size()) ||
        gRequestFlushCount != 0 || gRequestProcessCount != 3) {
        std::cerr << "WebView edit did not fall back to request_process without host params\n";
        fallbackGui->destroy(fallbackPlugin);
        fallbackPlugin->destroy(fallbackPlugin);
        return 24;
    }

    if (!fallbackPlugin->activate(fallbackPlugin, 48000.0, 1, 64) ||
        !fallbackPlugin->start_processing(fallbackPlugin)) {
        fallbackGui->destroy(fallbackPlugin);
        fallbackPlugin->destroy(fallbackPlugin);
        return 25;
    }

    StereoFloatBlock fallbackBlock(2);
    fallbackBlock.fillInput(1.0f, 1.0f);
    CapturedOutputEvents fallbackOutput;
    clap_process_t fallbackProcess{};
    fallbackProcess.frames_count = fallbackBlock.frames();
    fallbackProcess.audio_inputs = fallbackBlock.input();
    fallbackProcess.audio_outputs = fallbackBlock.output();
    fallbackProcess.audio_inputs_count = 1;
    fallbackProcess.audio_outputs_count = 1;
    fallbackProcess.out_events = fallbackOutput.clapOutputEvents();
    if (fallbackPlugin->process(fallbackPlugin, &fallbackProcess) != CLAP_PROCESS_CONTINUE ||
        fallbackOutput.size() != 3 ||
        !checkGesture(fallbackOutput, 0, CLAP_EVENT_PARAM_GESTURE_BEGIN, kGainParamId) ||
        !checkValue(fallbackOutput, 1, kGainParamId, -3.0) ||
        !checkGesture(fallbackOutput, 2, CLAP_EVENT_PARAM_GESTURE_END, kGainParamId)) {
        std::cerr << "request_process fallback did not drain the queued WebView edit\n";
        fallbackPlugin->stop_processing(fallbackPlugin);
        fallbackPlugin->deactivate(fallbackPlugin);
        fallbackGui->destroy(fallbackPlugin);
        fallbackPlugin->destroy(fallbackPlugin);
        return 26;
    }

    const float minusThree = static_cast<float>(std::pow(10.0, -3.0 / 20.0));
    for (uint32_t frame = 0; frame < fallbackBlock.frames(); ++frame) {
        if (std::fabs(fallbackBlock.outputChannel(0)[frame] - minusThree) > 1.0e-6f ||
            std::fabs(fallbackBlock.outputChannel(1)[frame] - minusThree) > 1.0e-6f) {
            std::cerr << "request_process fallback edit was not applied before audio processing\n";
            fallbackPlugin->stop_processing(fallbackPlugin);
            fallbackPlugin->deactivate(plugin);
            fallbackGui->destroy(fallbackPlugin);
            fallbackPlugin->destroy(fallbackPlugin);
            return 27;
        }
    }

    fallbackPlugin->stop_processing(fallbackPlugin);
    fallbackPlugin->deactivate(fallbackPlugin);
    fallbackGui->destroy(fallbackPlugin);
    fallbackPlugin->destroy(fallbackPlugin);
    return 0;
}
