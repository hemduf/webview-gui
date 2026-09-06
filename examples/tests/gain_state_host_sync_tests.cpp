#include "gain_plugin.h"
#include "../common/test_support.h"
#include "webview-gui/clap-webview-gui.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

struct HostState {
    uint32_t rescanFlags = 0;
    uint32_t rescanCount = 0;
    std::vector<std::vector<uint8_t>> webviewMessages;
};

void CLAP_ABI hostParamsRescan(const clap_host_t *host, uint32_t flags) {
    auto *state = static_cast<HostState *>(host->host_data);
    if (!state)
        return;
    state->rescanFlags |= flags;
    ++state->rescanCount;
}

void CLAP_ABI hostParamsClear(const clap_host_t *, clap_id, uint32_t) {}
void CLAP_ABI hostParamsRequestFlush(const clap_host_t *) {}

const clap_host_params_t kHostParams{
    hostParamsRescan,
    hostParamsClear,
    hostParamsRequestFlush,
};

bool CLAP_ABI hostWebviewSend(const clap_host_t *host,
                              const void *buffer,
                              uint32_t size) {
    if (!host || !host->host_data || (!buffer && size != 0u))
        return false;
    auto &state = *static_cast<HostState *>(host->host_data);
    const auto *bytes = static_cast<const uint8_t *>(buffer);
    state.webviewMessages.emplace_back(bytes, bytes + size);
    return true;
}

const ::webview_gui::clap_host_webview kHostWebview{
    hostWebviewSend,
};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *id) {
    if (!id)
        return nullptr;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &kHostParams;
    if (std::strcmp(id, ::webview_gui::CLAP_EXT_WEBVIEW) == 0)
        return &kHostWebview;
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

struct MemoryOutputStream {
    MemoryOutputStream() : stream{this, write} {}

    static int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                  const void *buffer,
                                  uint64_t size) {
        if (!stream || !stream->ctx || (!buffer && size != 0))
            return -1;
        auto &self = *static_cast<MemoryOutputStream *>(stream->ctx);
        const auto amount = static_cast<std::size_t>(std::min<uint64_t>(
            size, static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())));
        const auto *bytes = static_cast<const uint8_t *>(buffer);
        self.bytes.insert(self.bytes.end(), bytes, bytes + amount);
        return static_cast<int64_t>(amount);
    }

    std::vector<uint8_t> bytes;
    clap_ostream_t stream;
};

struct MemoryInputStream {
    explicit MemoryInputStream(const std::vector<uint8_t> &bytes)
        : bytes(bytes), stream{this, read} {}

    static int64_t CLAP_ABI read(const clap_istream_t *stream, void *buffer, uint64_t size) {
        if (!stream || !stream->ctx || (!buffer && size != 0))
            return -1;
        auto &self = *static_cast<MemoryInputStream *>(stream->ctx);
        if (self.position >= self.bytes.size())
            return 0;
        const auto requested = static_cast<std::size_t>(std::min<uint64_t>(
            size, static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())));
        const auto amount = std::min(requested, self.bytes.size() - self.position);
        std::memcpy(buffer, self.bytes.data() + self.position, amount);
        self.position += amount;
        return static_cast<int64_t>(amount);
    }

    const std::vector<uint8_t> &bytes;
    std::size_t position = 0;
    clap_istream_t stream;
};

clap_host_t makeHost(HostState &state) {
    return clap_host_t{
        CLAP_VERSION,
        &state,
        "webview-gui Gain state host-sync tests",
        "webview-gui",
        "https://github.com/hemduf/webview-gui",
        "0.1.0",
        hostGetExtension,
        hostRequestRestart,
        hostRequestProcess,
        hostRequestCallback,
    };
}

uint64_t loadU64Le(const uint8_t *source) noexcept {
    uint64_t value = 0u;
    for (unsigned index = 0; index < 8; ++index)
        value |= static_cast<uint64_t>(source[index]) << (index * 8u);
    return value;
}

bool findParameterSnapshot(const HostState &state,
                           uint8_t parameter,
                           double expected) noexcept {
    for (const auto &message : state.webviewMessages) {
        if (message.size() != 16u || message[0] != 'W' || message[1] != 'V' ||
            message[2] != 'U' || message[3] != '1' || message[4] != parameter ||
            message[5] != 0u || message[6] != 0u || message[7] != 0u)
            continue;
        const auto bits = loadU64Le(message.data() + 8u);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value) && std::fabs(value - expected) <= 1.0e-6;
    }
    return false;
}

bool expectConstantRange(const float *channel,
                         std::size_t begin,
                         std::size_t end,
                         float expected,
                         const char *label) {
    for (std::size_t frame = begin; frame < end; ++frame) {
        if (std::fabs(channel[frame] - expected) <= 1.0e-6f)
            continue;
        std::cerr << label << " frame " << frame << ": expected " << expected
                  << ", got " << channel[frame] << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace webview_gui::examples::gain;
    using webview_gui::examples::test_support::InputEvents;
    using webview_gui::examples::test_support::StereoFloatBlock;

    const auto *factory = gainFactory();
    if (!factory)
        return 1;

    HostState sourceHostState{};
    auto sourceHost = makeHost(sourceHostState);
    const auto *sourcePlugin = factory->create_plugin(factory, &sourceHost, kGainPluginId);
    if (!sourcePlugin || !sourcePlugin->init(sourcePlugin))
        return 2;

    const auto *sourceParams = static_cast<const clap_plugin_params_t *>(
        sourcePlugin->get_extension(sourcePlugin, CLAP_EXT_PARAMS));
    const auto *sourceState = static_cast<const clap_plugin_state_t *>(
        sourcePlugin->get_extension(sourcePlugin, CLAP_EXT_STATE));
    if (!sourceParams || !sourceState) {
        sourcePlugin->destroy(sourcePlugin);
        return 3;
    }

    InputEvents edits;
    if (!edits.pushParamValue(0, kGainParamId, -12.0) ||
        !edits.pushParamValue(0, kBypassParamId, 1.0)) {
        sourcePlugin->destroy(sourcePlugin);
        return 4;
    }
    sourceParams->flush(sourcePlugin, edits.clapInputEvents(), nullptr);

    MemoryOutputStream saved;
    if (!sourceState->save(sourcePlugin, &saved.stream) || saved.bytes.empty()) {
        sourcePlugin->destroy(sourcePlugin);
        return 5;
    }
    sourcePlugin->destroy(sourcePlugin);

    HostState destinationHostState{};
    auto destinationHost = makeHost(destinationHostState);
    const auto *destinationPlugin =
        factory->create_plugin(factory, &destinationHost, kGainPluginId);
    if (!destinationPlugin || !destinationPlugin->init(destinationPlugin))
        return 6;

    const auto *destinationParams = static_cast<const clap_plugin_params_t *>(
        destinationPlugin->get_extension(destinationPlugin, CLAP_EXT_PARAMS));
    const auto *destinationState = static_cast<const clap_plugin_state_t *>(
        destinationPlugin->get_extension(destinationPlugin, CLAP_EXT_STATE));
    if (!destinationParams || !destinationState) {
        destinationPlugin->destroy(destinationPlugin);
        return 7;
    }

    MemoryInputStream loaded(saved.bytes);
    if (!destinationState->load(destinationPlugin, &loaded.stream)) {
        destinationPlugin->destroy(destinationPlugin);
        return 8;
    }

    double gain = 0.0;
    double bypass = 0.0;
    if (!destinationParams->get_value(destinationPlugin, kGainParamId, &gain) ||
        !destinationParams->get_value(destinationPlugin, kBypassParamId, &bypass) ||
        std::fabs(gain + 12.0) > 1.0e-6 || bypass != 1.0) {
        std::cerr << "loaded state was not immediately visible through clap.params\n";
        destinationPlugin->destroy(destinationPlugin);
        return 9;
    }

    if (destinationHostState.rescanCount != 1 ||
        (destinationHostState.rescanFlags & CLAP_PARAM_RESCAN_VALUES) == 0) {
        std::cerr << "successful state load changed parameter values without CLAP_PARAM_RESCAN_VALUES\n";
        destinationPlugin->destroy(destinationPlugin);
        return 10;
    }

    destinationHostState.rescanFlags = 0u;
    destinationHostState.rescanCount = 0u;
    auto corrupt = saved.bytes;
    corrupt.front() ^= 0x7fu;
    MemoryInputStream rejected(corrupt);
    if (destinationState->load(destinationPlugin, &rejected.stream) ||
        destinationHostState.rescanCount != 0) {
        std::cerr << "rejected state load incorrectly notified the host\n";
        destinationPlugin->destroy(destinationPlugin);
        return 11;
    }

    // Reset to a known base before qualifying DAW automation. params.flush()
    // must update the same host/UI snapshots used by process() without needing
    // a WebView-originated edit.
    InputEvents reset;
    if (!reset.pushParamValue(0, kGainParamId, 0.0) ||
        !reset.pushParamValue(0, kBypassParamId, 0.0)) {
        destinationPlugin->destroy(destinationPlugin);
        return 12;
    }
    destinationParams->flush(destinationPlugin, reset.clapInputEvents(), nullptr);

    const auto *destinationGui = static_cast<const clap_plugin_gui_t *>(
        destinationPlugin->get_extension(destinationPlugin, CLAP_EXT_GUI));
    const auto *destinationWebview = static_cast<const ::webview_gui::clap_plugin_webview *>(
        destinationPlugin->get_extension(destinationPlugin, ::webview_gui::CLAP_EXT_WEBVIEW));
    if (!destinationGui || !destinationWebview || !destinationWebview->receive ||
        !destinationGui->create(destinationPlugin, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false)) {
        std::cerr << "host-owned WebView path is unavailable\n";
        destinationPlugin->destroy(destinationPlugin);
        return 13;
    }

    clap_window_t webviewParent{};
    webviewParent.api = ::webview_gui::CLAP_WINDOW_API_WEBVIEW;
    webviewParent.ptr = nullptr;
    if (!destinationGui->set_parent(destinationPlugin, &webviewParent)) {
        std::cerr << "host-owned WebView parent contract was rejected\n";
        destinationGui->destroy(destinationPlugin);
        destinationPlugin->destroy(destinationPlugin);
        return 14;
    }

    if (!destinationPlugin->activate(destinationPlugin, 48000.0, 1u, 64u)) {
        destinationGui->destroy(destinationPlugin);
        destinationPlugin->destroy(destinationPlugin);
        return 15;
    }

    constexpr std::array<uint8_t, 4> kSyncRequest{{'W', 'V', 'Q', '1'}};
    const auto minusSix = static_cast<float>(std::pow(10.0, -6.0 / 20.0));

    // Host automation must change DSP at the exact sample and then be visible
    // through both clap.params and the subsequent WebView WVU1 snapshot.
    {
        StereoFloatBlock block(8);
        block.fillInput(1.0f, 1.0f);
        InputEvents automation;
        if (!automation.pushParamValue(3, kGainParamId, -6.0))
            return 16;

        clap_process_t process{};
        process.frames_count = block.frames();
        process.audio_inputs = block.input();
        process.audio_outputs = block.output();
        process.audio_inputs_count = 1u;
        process.audio_outputs_count = 1u;
        process.in_events = automation.clapInputEvents();

        if (destinationPlugin->process(destinationPlugin, &process) != CLAP_PROCESS_CONTINUE ||
            !expectConstantRange(block.outputChannel(0), 0, 3, 1.0f, "gain automation pre") ||
            !expectConstantRange(block.outputChannel(0), 3, 8, minusSix, "gain automation post")) {
            std::cerr << "host gain automation did not reach DSP sample-accurately\n";
            destinationPlugin->deactivate(destinationPlugin);
            destinationGui->destroy(destinationPlugin);
            destinationPlugin->destroy(destinationPlugin);
            return 17;
        }

        gain = 0.0;
        if (!destinationParams->get_value(destinationPlugin, kGainParamId, &gain) ||
            std::fabs(gain + 6.0) > 1.0e-6) {
            std::cerr << "processed gain automation did not reach clap.params snapshot\n";
            return 18;
        }

        destinationHostState.webviewMessages.clear();
        if (!destinationWebview->receive(destinationPlugin,
                                         kSyncRequest.data(),
                                         static_cast<uint32_t>(kSyncRequest.size())) ||
            !findParameterSnapshot(destinationHostState, 1u, -6.0) ||
            !findParameterSnapshot(destinationHostState, 2u, 0.0)) {
            std::cerr << "processed gain automation did not reach WebView snapshot\n";
            return 19;
        }
    }

    // Bypass follows the same path. The stored -6 dB gain remains visible while
    // audio becomes unity from the bypass event's exact sample onward.
    {
        StereoFloatBlock block(8);
        block.fillInput(1.0f, 1.0f);
        InputEvents automation;
        if (!automation.pushParamValue(4, kBypassParamId, 1.0))
            return 20;

        clap_process_t process{};
        process.frames_count = block.frames();
        process.audio_inputs = block.input();
        process.audio_outputs = block.output();
        process.audio_inputs_count = 1u;
        process.audio_outputs_count = 1u;
        process.in_events = automation.clapInputEvents();

        if (destinationPlugin->process(destinationPlugin, &process) != CLAP_PROCESS_CONTINUE ||
            !expectConstantRange(block.outputChannel(0), 0, 4, minusSix, "bypass automation pre") ||
            !expectConstantRange(block.outputChannel(0), 4, 8, 1.0f, "bypass automation post")) {
            std::cerr << "host bypass automation did not reach DSP sample-accurately\n";
            return 21;
        }

        destinationHostState.webviewMessages.clear();
        if (!destinationWebview->receive(destinationPlugin,
                                         kSyncRequest.data(),
                                         static_cast<uint32_t>(kSyncRequest.size())) ||
            !findParameterSnapshot(destinationHostState, 1u, -6.0) ||
            !findParameterSnapshot(destinationHostState, 2u, 1.0)) {
            std::cerr << "processed bypass automation did not reach WebView snapshot\n";
            return 22;
        }
    }

    destinationPlugin->deactivate(destinationPlugin);
    destinationGui->destroy(destinationPlugin);
    destinationPlugin->destroy(destinationPlugin);
    return 0;
}
