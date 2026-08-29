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

struct HostState {
    uint32_t rescanFlags = 0;
    uint32_t rescanCount = 0;
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

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *id) {
    return id && std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &kHostParams : nullptr;
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

} // namespace

int main() {
    using namespace webview_gui::examples::gain;
    using webview_gui::examples::test_support::InputEvents;

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

    destinationHostState = {};
    auto corrupt = saved.bytes;
    corrupt.front() ^= 0x7fu;
    MemoryInputStream rejected(corrupt);
    if (destinationState->load(destinationPlugin, &rejected.stream) ||
        destinationHostState.rescanCount != 0) {
        std::cerr << "rejected state load incorrectly notified the host\n";
        destinationPlugin->destroy(destinationPlugin);
        return 11;
    }

    destinationPlugin->destroy(destinationPlugin);
    return 0;
}
